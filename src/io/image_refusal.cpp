#include "io/image_refusal.hpp"

namespace sketchy {
namespace {

// The sentence for each code, from the R107.b2 design's refusal table. A blank takes the next
// detail; a code with no blank ignores the details.
std::string sentenceFor(const std::string &code, const std::vector<std::string> &d) {
    const auto at = [&d](std::size_t i) { return i < d.size() ? d[i] : std::string("?"); };
    if (code == "image-type")
        return "This is not a PNG, JPEG or GeoTIFF file.";
    if (code == "image-size")
        return "The image is " + at(0) + " pixels. The limit is 4096 × 4096.";
    if (code == "stored-size")
        return "The image is " + at(0) + " as stored. The limit is 16 MiB. Crop it or reduce it in an "
               "image editor, then import it again.";
    if (code == "image-extent")
        return "The image would cover " + at(0) + " in one direction. A placed image can cover at most "
               "1,000 km.";
    if (code == "world-file")
        return "The world file does not hold six numbers, one to a line.";
    if (code == "world-rotated")
        return "The world file rotates or shears the image. Only north-up images are read.";
    if (code == "world-flipped")
        return "The world file flips the image. Only north-up images are read.";
    if (code == "geotiff-untagged")
        return "This GeoTIFF has no map position, so its pixels cannot be placed.";
    if (code == "geotiff-transform")
        return "The file's georeference is not one tie point with a scale, or one north-up "
               "transformation.";
    if (code == "geotiff-crs")
        return "The file's coordinate system is " + at(0) + ". Only WGS84 and NAD83 UTM zones are read.";
    if (code == "geotiff-units")
        return "The coordinates are in " + at(0) + ". Only metres are read.";
    if (code == "geotiff-samples")
        return "The pixels are " + at(0) + ". Only 8-bit RGB and RGBA are read.";
    if (code == "geotiff-compression")
        return "This GeoTIFF is compressed with " + at(0) + ", which this version does not read. Only "
               "uncompressed, LZW and Deflate files are read.";
    if (code == "geotiff-raster")
        return "The raster type is neither pixel-is-area nor pixel-is-point, so its pixels cannot be "
               "placed.";
    if (code == "geotiff-layout")
        return "The file is not one classic TIFF image: it is BigTIFF, or it holds more than one "
               "full-resolution image.";
    if (code == "zone-mismatch")
        return "The image is in zone " + at(0) + ", and the site is in zone " + at(1) + ". An image "
               "cannot cross zones.";
    if (code == "zone-band")
        return "A corner of the image lies outside zone " + at(0) + ". Choose the zone the image is in.";
    if (code == "zone-exception")
        return "This image is in the " + at(0) + " zone exception, which this version does not place.";
    if (code == "latitude")
        return "A corner lies outside 80° S to 84° N, the range of the UTM grid.";
    if (code == "elevation")
        return "The elevation must be a number of metres between −1,000,000 and 1,000,000.";
    if (code == "credit")
        return "The credit line is longer than 256 bytes. Shorten it.";
    if (code == "file-changed")
        return "The file changed while it was read. Choose it again.";
    if (code == "damaged")
        return "The file is damaged: " + at(0) + ".";
    return "The image cannot be placed: " + code + ".";
}

} // namespace

ImageRefusal::ImageRefusal(std::string code, std::vector<std::string> details)
    : std::runtime_error(sentenceFor(code, details)), code_(std::move(code)),
      details_(std::move(details)) {}

std::string ImageRefusal::sentence() const { return what(); }

} // namespace sketchy
