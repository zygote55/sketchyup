#pragma once
// R107.b1 (ADR 0181 §2.8, §7.2, §7.3, §9): a bounded GeoTIFF reader for the elevation subset.
// R107.b1.1 adds LZW (ADR 0181 §7.2, parent decision 2026-10-30).
// R107.b1.2 skips reduced-resolution overview images (ADR 0181 §7.2 and §7.3, parent decision
// 2026-10-30), so a 3DEP tile as USGS distributes it is read.
// R107.b2.1 (ADR 0181, R107.b2 design): an 8-bit RGB or RGBA orthophoto in a UTM zone, read beside the
// elevation read (GeoTiffOrthoHeader, below).
// R107.b3 (UX §20.5, §20.7 item 9): every refusal carries a GeoTiffRefusalCode and a plain sentence;
// the message is unchanged for the command line.
//
// Qt-free and offline. The reader walks the full-resolution image's IFD, checks every count and
// offset against the bytes it has, and decodes one strip or tile at a time into a block cache of
// at most 64 MiB of raster (ADR 0181 §9). Anything outside the subset is refused by name with
// GeoTiffRefusal; a malformed file is refused as GeoTiffRefusal too, with the offset named.
//
// The subset: classic TIFF, little or big byte order; one full-resolution image, with any number of
// reduced-resolution overviews after it (at most GeoTiffBounds::furtherImages); one sample per
// pixel; int16 or float32 samples; no compression, LZW or Deflate; predictor 1, 2 (int16) or 3
// (float32); stripped or tiled; a geographic model (EPSG:4326 WGS84, or 4269 NAD83 as a labelled
// approximation) with pixel-is-area raster type, no rotation, and a vertical reference the caller
// can label.
#include <cstddef>
#include <cstdint>
#include <list>
#include <map>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

namespace sketchy {

// The bytes of a file, read in pieces. Every read is checked against size().
class GeoTiffBytes {
  public:
    virtual ~GeoTiffBytes() = default;
    virtual std::uint64_t size() const = 0;
    // Copies `count` bytes at `offset` into `out`; false when the range is not inside the bytes.
    virtual bool read(std::uint64_t offset, std::size_t count, std::uint8_t *out) const = 0;
};

// Bytes held in memory (tests and the fuzz harness).
class GeoTiffMemory final : public GeoTiffBytes {
  public:
    explicit GeoTiffMemory(std::vector<std::uint8_t> bytes) : bytes_(std::move(bytes)) {}
    std::uint64_t size() const override { return bytes_.size(); }
    bool read(std::uint64_t offset, std::size_t count, std::uint8_t *out) const override;

  private:
    std::vector<std::uint8_t> bytes_;
};

// The stable code of a refusal (R107.b3, UX §20.5). The command line prints the message, which
// cites ADR sections; the dialog shows geoTiffRefusalSentence instead, which cites neither.
enum class GeoTiffRefusalCode : std::uint8_t {
    NotTiff,           // not a TIFF file, or its header cannot be read
    BigTiff,           // BigTIFF (magic 43)
    Damaged,           // truncated, malformed or cut short
    Compression,       // a compression other than none, LZW or Deflate
    ImageLayout,       // a second full-resolution image, a mask, an image loop or a link outside the file
    Bands,             // more than one sample per pixel
    SampleType,        // samples other than 16-bit integer or 32-bit float
    Projected,         // projected coordinates
    NationalGrid,      // OSGB36 or ETRS89: a transformation this version does not have
    CoordinateSystem,  // a geographic CRS the subset does not read, or none named
    VerticalUnits,     // heights in units other than metres
    VerticalReference, // a vertical reference the subset does not read
    NotGeoreferenced,  // no map position for the posts
    Unsupported,       // outside the subset for another reason, which the detail names
};
// The stable name of a code, as the refusal's "refusal" field (for example "compression").
const char *geoTiffRefusalName(GeoTiffRefusalCode code);
// The plain sentence of a code. `detail` fills its value (a compression's name, a band count, a
// CRS code); a sentence with no value ignores it. No ADR section or command-line option appears.
std::string geoTiffRefusalSentence(GeoTiffRefusalCode code, const std::string &detail);

// A file or a feature outside the supported subset. The message names it for the command line
// (ADR 0181 §7.3); code() and sentence() are for the dialog.
class GeoTiffRefusal : public std::runtime_error {
  public:
    GeoTiffRefusal(GeoTiffRefusalCode code, const std::string &message, std::string detail = {})
        : std::runtime_error(message), code_(code), detail_(std::move(detail)) {}
    GeoTiffRefusalCode code() const { return code_; }
    // The sentence with the detail, or with the message when the code is given no detail.
    std::string sentence() const {
        return geoTiffRefusalSentence(code_, detail_.empty() ? std::string(what()) : detail_);
    }

  private:
    GeoTiffRefusalCode code_;
    std::string detail_;
};

// The bounds the reader keeps to (ADR 0181 §9, and the fuzzing budget of ADR 0181 §8 V7).
struct GeoTiffBounds {
    static constexpr std::uint64_t maxDimension = 1000000;            // pixels per side
    static constexpr std::size_t ifdEntries = 512;                    // directory entries
    static constexpr std::size_t tagValueBytes = 8u * 1024 * 1024;    // one tag's values
    static constexpr std::size_t geoKeys = 256;                       // GeoKeys in the directory
    static constexpr std::size_t blockSamples = 4u * 1024 * 1024;     // 16 MiB of float32
    static constexpr std::size_t blockCount = 1000000;                // strips or tiles
    static constexpr std::size_t rasterBytes = 64u * 1024 * 1024;     // raster held at once
    static constexpr std::size_t furtherImages = 16;                  // images walked after the first
};

enum class GeoTiffSampleType : std::uint8_t { Int16, Float32 };

// The metadata of a supported file, after every check. Scales are degrees per pixel. The
// tie maps raster position (tieI, tieJ) to (tieX, tieY) in degrees (ADR 0181 §2.8).
struct GeoTiffHeader {
    std::uint32_t width{}, height{};
    GeoTiffSampleType sampleType{GeoTiffSampleType::Float32};
    bool littleEndian{true};
    std::uint16_t compression{1}; // 1 none, 5 LZW, 8 Deflate
    std::uint16_t predictor{1};   // 1 none, 2 horizontal (int16), 3 floating point (float32)
    bool tiled{};
    std::uint32_t blockWidth{}, blockHeight{}; // a tile, or the width and rows of a strip
    std::uint32_t blocksAcross{}, blocksDown{};
    std::vector<std::uint64_t> offsets, byteCounts; // per block, row-major
    double scaleX{}, scaleY{};
    double tieI{}, tieJ{}, tieX{}, tieY{};
    bool pixelIsArea{true};       // GeoKey 1025: 1 pixel-is-area, 2 pixel-is-point (GLO-30 is 2)
    int geographicKey{};          // 4326 or 4269
    std::string horizontalDatum;  // "WGS84" or "NAD83"
    std::uint16_t verticalKey{};  // GeoKey 4096 (VerticalCSTypeGeoKey): 5703, 3855, 5773, or 0 when none
    std::string verticalReference; // "NAVD88", "EGM2008" or "EGM96", or empty when the file has none
    std::optional<double> noData; // GDAL_NODATA
    // The pixel-centre index of a position: column and row in [0, width - 1] and [0, height - 1]
    // when the position lies within the pixel centres (ADR 0181 §2.8). Pixel-is-area puts the
    // centre half a pixel inside the tie; pixel-is-point puts it on the tie.
    void toPixel(double longitude, double latitude, double &column, double &row) const;
    // The longitude and latitude (degrees) of a pixel centre.
    void fromPixel(double column, double row, double &longitude, double &latitude) const;
};

// Parses and checks the file's full-resolution image. Throws GeoTiffRefusal for any file or feature
// outside the subset. Reads the directories (the first image's tags and GeoKeys, and each later
// image's NewSubfileType and link), never the blocks of an overview.
GeoTiffHeader readGeoTiffHeader(const GeoTiffBytes &bytes);

// Samples of a checked file, read in windows. Bilinear sampling at pixel-centre indices; no
// sample is ever read past the file, and at most GeoTiffBounds::rasterBytes of decoded blocks
// are held (least recently used blocks are dropped first).
class GeoTiffRaster {
  public:
    GeoTiffRaster(const GeoTiffBytes &bytes, GeoTiffHeader header);
    const GeoTiffHeader &header() const { return header_; }
    // The bilinear value at (column, row), which must lie within the pixel centres. `isVoid` is
    // set when a neighbour that carries weight is no-data or NaN (the value is then 0).
    double sample(double column, double row, bool &isVoid);
    // Decoded bytes held now and at the peak (for the budget report), and blocks decoded.
    std::size_t heldBytes() const { return held_; }
    std::size_t peakHeldBytes() const { return peakHeld_; }
    std::uint64_t blocksDecoded() const { return decoded_; }

  private:
    struct Block {
        std::size_t index{};
        std::vector<float> samples; // row-major, blockWidth wide
    };
    const std::vector<float> &block(std::size_t index);
    std::vector<float> decode(std::size_t index) const;
    const GeoTiffBytes &bytes_;
    GeoTiffHeader header_;
    std::list<Block> cache_; // most recently used first
    std::map<std::size_t, std::list<Block>::iterator> lookup_;
    std::size_t held_{}, peakHeld_{};
    std::uint64_t decoded_{};
};


// R107.b2.1 (ADR 0181, R107.b2 design "Inputs" and "Limits"): an 8-bit RGB or RGBA GeoTIFF in a WGS84 or
// NAD83 UTM zone, read beside the elevation read above. The elevation refusals are unchanged. A refusal
// is an ImageRefusal (image_refusal.hpp), named by its code and given one plain sentence.
struct GeoTiffOrthoHeader {
    std::uint32_t width{}, height{};
    std::uint16_t samples{3};          // 3 (RGB), or 4 (RGBA with unassociated alpha)
    bool littleEndian{true};
    std::uint16_t compression{1};      // 1 none, 5 LZW, 8 Deflate (32946 is read as 8)
    std::uint16_t predictor{1};        // 1 none, 2 horizontal differencing
    bool tiled{};
    std::uint32_t blockWidth{}, blockHeight{}; // a tile, or the width and rows of a strip
    std::uint32_t blocksAcross{}, blocksDown{};
    std::vector<std::uint64_t> offsets, byteCounts; // per block, row-major
    int zone{};                        // 1 to 60
    bool north{true};                  // the hemisphere of the zone grid
    std::string datum;                 // "WGS84", or "NAD83" as a labelled approximation
    std::uint32_t projectedKey{};      // the EPSG code of the projected CRS
    double easting{}, northing{};      // the top-left corner, in metres of the zone
    double pixelWidth{}, pixelHeight{}; // metres per pixel, east and north, both positive
};

// Reads the first image's directory and georeference, after every check the orthophoto subset needs.
GeoTiffOrthoHeader readGeoTiffOrthophoto(const GeoTiffBytes &bytes);

// Decodes every block to RGBA, row-major, four bytes a pixel (an RGB file has alpha 255). The
// block is decoded in turn, so the peak is the RGBA image and one block.
std::vector<std::uint8_t> decodeGeoTiffOrthophoto(const GeoTiffBytes &bytes,
                                                  const GeoTiffOrthoHeader &image);

} // namespace sketchy
