#pragma once
#include "core/site_origin.hpp"
#include "core/terrain_fwd.hpp"
#include "core/terrain_tile_key.hpp"
#include "geometry/surface.hpp"
#include <array>
#include <atomic>
#include <compare>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <initializer_list>
#include <memory>
#include <mutex>
#include <optional>
#include <ostream>
#include <stdexcept>
#include <string>
#include <vector>
// R095.a terrain and site records (ADR 0168 §2, §4). Records only: the operations
// that build, check and convert them are in core/terrain.hpp.
namespace sketchy {
// A source polyline or point set in the terrain's local frame (the terrain body's
// frame, which its tiles share), with the reference that names it in the source
// file: a DXF entity handle, a CSV line number or a model edge ID.
//
// R096.a (ADR 0169 §4.2, §7): every source has a stable `id`, unique within the terrain and
// never zero: 1..n in canonical order at creation (createTerrain assigns them to a record
// whose ids are all zero). Commands and edit entries name sources by id; `reference` stays the
// import provenance.
struct TerrainSource {
    TerrainSourceRole role{TerrainSourceRole::Contour};
    std::string reference;
    std::vector<Vec3> points;
    std::uint32_t id{};
    bool operator==(const TerrainSource &) const = default;
};
// R096.a structural sharing (ADR 0169 §2): the base sources are one immutable, shared block.
// Copying a record copies a pointer; equality compares contents (identity first). Under the
// hybrid edit model the base sources never change after creation, so one block per record is
// the unit of sharing. Mutators copy the block (they are for building records, not editing).
class TerrainSources {
  public:
    using Vector = std::vector<TerrainSource>;
    TerrainSources() : block_(emptyBlock()) {}
    TerrainSources(Vector sources) : block_(std::make_shared<Vector>(std::move(sources))) {}
    TerrainSources(std::initializer_list<TerrainSource> sources) : TerrainSources(Vector(sources)) {}
    const Vector &get() const { return *block_; }
    operator const Vector &() const { return *block_; }
    // The shared block's identity (the unit of structural sharing).
    const void *block() const { return block_.get(); }
    std::size_t size() const { return block_->size(); }
    bool empty() const { return block_->empty(); }
    Vector::const_iterator begin() const { return block_->begin(); }
    Vector::const_iterator end() const { return block_->end(); }
    const TerrainSource &operator[](std::size_t i) const { return (*block_)[i]; }
    const TerrainSource &front() const { return block_->front(); }
    const TerrainSource &back() const { return block_->back(); }
    // Copy on write: a block another record shares is copied first.
    void push_back(TerrainSource source) {
        if (block_.use_count() != 1 || block_ == emptyBlock())
            block_ = std::make_shared<Vector>(*block_);
        block_->push_back(std::move(source));
    }
    void clear() { block_ = emptyBlock(); }
    bool operator==(const TerrainSources &other) const {
        return block_ == other.block_ || *block_ == *other.block_;
    }

  private:
    static const std::shared_ptr<Vector> &emptyBlock() {
        static const auto block = std::make_shared<Vector>();
        return block;
    }
    std::shared_ptr<Vector> block_; // never changed while shared
};
// Triangulation parameters recorded for Regenerate (ADR 0168 §3).
enum class TerrainBoundaryMode { ConvexHull, MaxEdgeLength, Boundary };
struct TerrainParameters {
    TerrainBoundaryMode boundary{TerrainBoundaryMode::MaxEdgeLength};
    double maxEdgeLength{}; // Metres; zero means "not set" (only valid for other modes).
    bool flipFlatTriangles{true}, steinerFill{false};
    bool operator==(const TerrainParameters &) const = default;
};
// R107.b1 (ADR 0181 §3.2, §2.7, §2.8): the source record of a terrain made from a GeoTIFF
// elevation file (USGS 3DEP or Copernicus GLO-30), as the user downloaded it. Each text field is
// at most 256 bytes (ADR 0181 §3.2). The file's SHA-256 is the terrain's provenance digest.
struct TerrainDemSource {
    static constexpr std::size_t textBytes = 256;
    std::string product;        // "USGS 3DEP" or "Copernicus DEM GLO-30"
    std::string sourceName;     // the file's name, not its path
    std::string licence;        // the licence the product is taken under
    std::string credit;         // the notice the product's licence or the agency asks for
    std::string disclaimer;     // the no-liability sentence (GLO-30); empty for 3DEP
    std::string datum;          // "WGS84", or "NAD83" as a labelled approximation
    std::string vertical;       // "NAVD88", "EGM2008" or "EGM96"
    std::string elevation;      // "bare earth" (3DEP) or "surface" (GLO-30, a surface model)
    double verticalOffset{};    // metres added to the file's heights to reach the site's reference
    std::string verticalNote;   // required when the offset is not zero
    int zone{};                 // the UTM zone of the grid, 1 to 60
    bool north{true};           // its hemisphere
    double easting0{}, northing0{}, easting1{}, northing1{}; // the window, metres
    double spacing{}, nominalSpacing{};                     // metres; the post size of the file
    std::uint64_t columns{}, rows{};                        // grid posts per row and column
    bool operator==(const TerrainDemSource &) const = default;
};
// Import provenance: source file digest, its unit name and the unit scale.
struct TerrainProvenance {
    std::string sha256; // Lowercase hex, or empty for terrains built from model edges.
    std::string units{"m"};
    double metresPerUnit{1};
    std::optional<TerrainDemSource> dem; // R107.b1: set for a terrain made from a GeoTIFF file
    bool operator==(const TerrainProvenance &) const = default;
};
// R095.e2 display settings (ADR 0168 "Decisions > Interaction design", Appearance). Derived
// display only: they never change the mesh or its digest. Contour lines are drawn at site
// elevations that are multiples of the interval, every fifth an index line; shading is by the
// tiles' material or by ten elevation bands. Stored on the record (schema 29 addendum, ADR
// 0108) with defaults omitted, so a record at its defaults encodes as before.
enum class TerrainShading { Material, ElevationBands };
struct TerrainDisplay {
    bool contours{false};
    double contourInterval{1}; // Metres.
    TerrainShading shading{TerrainShading::Material};
    bool operator==(const TerrainDisplay &) const = default;
};
inline constexpr double terrainContourIntervalMin = 0.01, terrainContourIntervalMax = 1000;
// R096.a stable tiling (ADR 0169 §3): the tile grid of the creation (or last Regenerate) is
// part of the record. `origin` is the minimum triangle centroid X and Y of that mesh and `cell`
// the cell size after halving; a triangle belongs to the cell of its centroid,
// column floor((cx - originX) / cell) and row floor((cy - originY) / cell). A cell that an
// edit pushes over 2,048 triangles splits into quadrants (the key's path), and a cluster no
// split separates is split in triangle order (the key's part).
struct TerrainTileGrid {
    double originX{}, originY{}, cell{};
    bool operator==(const TerrainTileGrid &) const = default;
};
// R096.a edit list (ADR 0169 §2, §7). An entry is immutable and shared between records (an
// edit copies pointers). Its kind-specific parameters and resolved geometry are a
// TerrainEditData of its kind; kinds are registered (core/terrain_edit.hpp) and encoded by
// io/terrain_io. Entry IDs come from the record's `nextEditId` counter, so an ID is never reused,
// not even after Remove change or Reset edits.
struct TerrainEditData {
    virtual ~TerrainEditData() = default;
    virtual bool equals(const TerrainEditData &other) const = 0;
    // Storage charged to history and to the list's 8 MiB bound.
    virtual std::size_t bytes() const = 0;
    // R096.e: the document materials the entry refers to (a drape's paint regions), so material
    // validation keeps them alive.
    virtual std::vector<std::uint64_t> materials() const { return {}; }
};
struct TerrainEdit {
    std::uint32_t id{};
    std::string kind;
    std::shared_ptr<const TerrainEditData> data;
    bool operator==(const TerrainEdit &other) const {
        return id == other.id && kind == other.kind &&
               (data == other.data || (data && other.data && data->equals(*other.data)));
    }
};
using TerrainEditPtr = std::shared_ptr<const TerrainEdit>;
// R096.f1b: a terrain's mesh digest (the SHA-256 hex text of terrainMeshDigest). Its definition
// is unchanged; only when it is computed differs. A digest is either known (text) or deferred: a
// terrain edit's commit captures the immutable tiles it describes and the digest is computed once,
// on the first read (save, describe, a comparison) or earlier on a worker (prefetchTask),
// whichever comes first. Copies share the deferred value, so an Undo, a Redo or a copied record
// reads the same result, and a reader blocks until it is computed: no reader ever sees a stale or
// missing value. Once computed, the value is read without locking. Thread-safe.
class TerrainDigest {
  public:
    using Hash = std::function<std::string(const std::vector<const void *> &tiles)>;
    TerrainDigest() = default;
    TerrainDigest(std::string text) : text_(std::move(text)) {} // NOLINT: a known digest
    TerrainDigest(const char *text) : text_(text) {}            // NOLINT
    // A digest computed on first read as `hash(tiles)`. `tiles` (in the order `hash` expects)
    // are held until then and released after; they also identify what the digest describes
    // (describes), held weakly.
    static TerrainDigest deferred(Hash hash, std::vector<std::shared_ptr<const void>> tiles);
    // The digest text (computing it now if it is deferred and not yet computed).
    const std::string &value() const;
    operator const std::string &() const { return value(); } // NOLINT
    std::size_t size() const { return deferred_ ? 64 : text_.size(); }
    bool empty() const { return !deferred_ && text_.empty(); }
    bool isDeferred() const { return bool(deferred_); }
    // Whether a deferred digest has not been computed yet (never blocks).
    bool pending() const;
    // Whether this digest was deferred from exactly these live tile records (by identity, any
    // order): then it is also their current digest, without hashing them. Never computes and
    // never waits for a computation.
    bool describes(const std::vector<const void *> &tiles) const;
    // Document::applyEdit stores a copy of every record it commits; `frozen` maps each committed
    // record's original to its stored copy (both alive), and the digest follows its tiles there.
    // The copies' geometry, and so the digest, is the same.
    void rebind(const std::vector<std::pair<const void *, std::shared_ptr<const void>>> &frozen) const;
    // A task that computes a pending digest if anything still holds it (a worker's prefetch); it
    // never throws and keeps nothing alive.
    std::function<void()> prefetchTask() const;
    // The bytes a deferred digest retains besides its text (its identity table), for the history
    // charge; a shared state (sharesState) is charged once per record pair.
    std::size_t retainedBytes() const;
    bool sharesState(const TerrainDigest &other) const {
        return deferred_ && deferred_ == other.deferred_;
    }
    bool operator==(const TerrainDigest &other) const;
    bool operator==(const std::string &other) const { return value() == other; }
    bool operator==(const char *other) const { return value() == other; }
    // Process-wide count of deferred digests computed (tests and the benchmark).
    static std::size_t computations();

  private:
    struct Deferred {
        mutable std::mutex computing;     // held while computing
        mutable std::atomic<bool> done{}; // released after `text` is written
        mutable std::string text;
        Hash hash;
        // Under `guard` (never held while hashing): the tiles to hash (strong, cleared once
        // done), and each tile's address with a weak reference proving the address still names
        // it, by address.
        mutable std::mutex guard;
        mutable std::vector<std::shared_ptr<const void>> tiles;
        mutable std::vector<std::pair<const void *, std::weak_ptr<const void>>> identity;
    };
    std::string text_;
    std::shared_ptr<const Deferred> deferred_;
};
// Text use of a digest (messages, logs) reads its value.
inline std::ostream &operator<<(std::ostream &out, const TerrainDigest &digest) {
    return out << digest.value();
}
inline std::string operator+(const std::string &a, const TerrainDigest &b) { return a + b.value(); }
inline std::string operator+(const char *a, const TerrainDigest &b) { return a + b.value(); }
inline std::string operator+(const TerrainDigest &a, const std::string &b) { return a.value() + b; }
inline std::string operator+(const TerrainDigest &a, const char *b) { return a.value() + b; }
struct TerrainRecord {
    TerrainSources sources; // The base sources (shared, immutable).
    TerrainParameters parameters;
    std::string generator;  // e.g. "cdt-2.0.0/terrain-v1+edit-v1"
    TerrainProvenance provenance;
    TerrainDigest meshDigest; // terrainMeshDigest of the tiles when generated (R096.f1b: deferred).
    TerrainDisplay display;
    TerrainTileGrid tiling;              // R096.a: the stored tile grid.
    std::vector<TerrainEditPtr> edits;   // R096.a: the ordered edit list.
    std::uint32_t nextEditId{1};         // R096.a: the next entry's ID (above every entry's).
    bool operator==(const TerrainRecord &other) const {
        if (!(sources == other.sources && parameters == other.parameters &&
              nextEditId == other.nextEditId &&
              generator == other.generator && provenance == other.provenance &&
              display == other.display && tiling == other.tiling &&
              edits.size() == other.edits.size()))
            return false;
        for (std::size_t i = 0; i < edits.size(); ++i)
            if (edits[i] != other.edits[i] && !(edits[i] && other.edits[i] && *edits[i] == *other.edits[i]))
                return false;
        // R096.f1b: last, since comparing a pending digest computes it.
        return meshDigest == other.meshDigest;
    }
};
// R095.e2: the built-in material new terrains get ("Terrain"; the Materials panel's local
// library offers the same name and colour). An existing material of that name is reused.
inline constexpr const char *terrainMaterialName = "Terrain";
inline constexpr std::array<float, 3> terrainMaterialColor{0.541f, 0.604f, 0.420f}; // #8a9a6b
struct TerrainLimits {
    // Per document, in its own limit class (ADR 0168 decision 3).
    static constexpr std::size_t triangles = 250000;
    static constexpr std::size_t tileTriangles = 2048;
    static constexpr std::size_t sourceVertices = 250000;
    static constexpr std::size_t sources = 100000;
    static constexpr std::size_t referenceBytes = 128;
    static constexpr std::size_t generatorBytes = 64;
    static constexpr std::size_t unitBytes = 16;
    // R096.a edit list bounds (ADR 0169 §2, §7), per terrain.
    static constexpr std::size_t edits = 4096;
    static constexpr std::size_t editBytes = 8u * 1024 * 1024;
    static constexpr std::size_t editKindBytes = 32;
};
// D-043: a terrain edit whose record and tile changes exceed Document::terrainHistoryLimit.
inline constexpr const char *terrainHistoryMessage =
    "The terrain edit is too large for its 512 MiB undo history budget";
inline constexpr const char *terrainProtectedMessage =
    "Edit terrain with the terrain tools, or Explode it to ordinary geometry";
// Refusals that carry one of the shared automation error codes
// (UNSUPPORTED_TARGET, LIMIT_EXCEEDED, NOT_FOUND, INVALID_REQUEST).
class TerrainError : public std::runtime_error {
  public:
    // `refusal` and `sentence` (R107.b3, UX §20.5) are the stable refusal code and the plain
    // sentence the dialog shows. They are empty when the refusal has neither; the message is
    // what the command line prints, and it is unchanged by them.
    TerrainError(std::string code, const std::string &message, std::string refusal = {},
                 std::string sentence = {})
        : std::runtime_error(message), code_(std::move(code)), refusal_(std::move(refusal)),
          sentence_(std::move(sentence)) {}
    const std::string &code() const { return code_; }
    const std::string &refusal() const { return refusal_; }
    const std::string &sentence() const { return sentence_; }

  private:
    std::string code_, refusal_, sentence_;
};
void validateTerrainRecord(const TerrainRecord &record);
void validateTerrainDisplay(const TerrainDisplay &display);
// R107.b1: the GeoTIFF source record's fields, bounds and labels (ADR 0181 §3.2).
void validateTerrainDemSource(const TerrainDemSource &dem);
std::size_t terrainRecordBytes(const TerrainRecord &record);
// R096.a: the history charge of a record change (ADR 0169 §2, structural sharing): both
// records' own fields, plus the shared parts (the source block and the edit entries) that only
// one of them holds. A part both share is charged nothing, so an edit that appends one entry
// is charged that entry, not the whole record. With `before` empty it is the full record.
std::size_t terrainRecordChangeBytes(const TerrainRecord *before, const TerrainRecord *after);
// Sum of the edit entries' storage (the 8 MiB bound), and one entry's share of it.
std::size_t terrainEditBytes(const std::vector<TerrainEditPtr> &edits);
std::size_t terrainEditEntryBytes(const TerrainEdit &edit);
} // namespace sketchy
