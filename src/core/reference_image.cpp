#include "core/reference_image.hpp"

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace sketchy {
namespace {

bool hasControlCharacter(const std::string &value) {
    return std::any_of(value.begin(), value.end(),
                       [](char c) { return static_cast<unsigned char>(c) < 0x20; });
}

// The source of a georeferenced image: its format, its file name and digest, and its world file.
// A PNG or JPEG has a world file and a GeoTIFF has none (R107.b2 design, "The record (schema 35)").
void validateGeoSource(const ImageGeoSource &source) {
    const auto hex = [](const std::string &value) {
        return value.size() == 64 && value.find_first_not_of("0123456789abcdef") == std::string::npos;
    };
    if (source.format != "png" && source.format != "jpeg" && source.format != "geotiff")
        throw std::runtime_error("Reference image source format must be png, jpeg or geotiff");
    if (source.file.empty() || source.file.size() > imageGeoTextBytes ||
        source.file.find_first_of("/\\") != std::string::npos || hasControlCharacter(source.file))
        throw std::runtime_error("Reference image source file must be a file name of at most 256 bytes");
    if (!hex(source.sha256))
        throw std::runtime_error("Reference image source digest must be a SHA-256 in lower-case hex");
    const bool worldFile = source.format != "geotiff";
    if (worldFile == source.worldFile.empty() || source.worldFile.size() > imageGeoTextBytes ||
        hasControlCharacter(source.worldFile))
        throw std::runtime_error("A PNG or JPEG reference image needs its world file, and a GeoTIFF has none");
    if (worldFile && !hex(source.worldSha256))
        throw std::runtime_error("Reference world file digest must be a SHA-256 in lower-case hex");
}

} // namespace

void validateImageGeoreference(const ImageGeoreference &geo, double width, double height) {
    if (geo.zone < 1 || geo.zone > 60)
        throw std::runtime_error("Reference image zone must be between 1 and 60");
    if (geo.datum != "WGS84" && geo.datum != "NAD83")
        throw std::runtime_error("Reference image datum must be WGS84, or NAD83 as an approximation");
    for (const auto value : {geo.easting, geo.northing})
        if (!std::isfinite(value) || std::abs(value) > 1e8)
            throw std::runtime_error("Reference image corner must be finite and within 100,000 km");
    for (const auto size : {geo.pixelWidth, geo.pixelHeight})
        if (!std::isfinite(size) || size <= 0 || size > 1e6)
            throw std::runtime_error("Reference image pixel size must be greater than zero, in metres");
    for (const auto count : {geo.columns, geo.rows})
        if (count < 1 || count > imageGeoPixelLimit)
            throw std::runtime_error("Reference image pixel counts must be between 1 and 4096");
    // The placed size is the pixel count times the pixel size, so the record and the image agree.
    if (std::abs(width - geo.columns * geo.pixelWidth) > 1e-6 ||
        std::abs(height - geo.rows * geo.pixelHeight) > 1e-6)
        throw std::runtime_error("Reference image size must be its pixel counts times its pixel sizes");
    if (geo.credit.size() > imageGeoTextBytes || hasControlCharacter(geo.credit))
        throw std::runtime_error("Reference image credit must be at most 256 bytes without control characters");
    validateGeoSource(geo.source);
}

void ReferenceImage::validate() const {
    if (!asset)
        throw std::runtime_error("Reference image needs a managed asset identity");
    for (const auto size : {width, height})
        if (!std::isfinite(size) || size < 1e-6 || size > coordinateLimit)
            throw std::runtime_error(
                "Reference image dimensions must be between 1e-6 and 1e6 meters");
    if (!std::isfinite(opacity) || opacity < 0 || opacity > 1)
        throw std::runtime_error("Reference image opacity must be between zero and one");
    if (georeference)
        validateImageGeoreference(*georeference, width, height);
}

Vec3 referenceImagePoint(const ReferenceImage &image, ImagePoint point) {
    image.validate();
    for (const auto value : point)
        if (!std::isfinite(value) || value < 0 || value > 1)
            throw std::runtime_error("Reference image coordinates must be between zero and one");
    return {point[0] * image.width, (1 - point[1]) * image.height, 0};
}

std::array<Vec3, 4> referenceImageCorners(const ReferenceImage &image, const Transform &world) {
    image.validate();
    world.validate();
    std::array<Vec3, 4> corners{
        {{0, 0, 0}, {image.width, 0, 0}, {image.width, image.height, 0}, {0, image.height, 0}}};
    for (auto &point : corners) {
        point = world.point(point);
        checkPoint(point);
    }
    return corners;
}

CalibratedReferenceImage calibrateReferenceImage(const ReferenceImage &image,
                                                 const Transform &local, const Transform &parent,
                                                 ImagePoint first, ImagePoint second,
                                                 double knownLength) {
    if (!std::isfinite(knownLength) || knownLength < 1e-6 || knownLength > coordinateLimit)
        throw std::runtime_error(
            "Reference calibration length must be between 1e-6 and 1e6 meters");
    if (image.georeference)
        throw std::runtime_error(
            "A georeferenced image keeps the size its world file gives, so it cannot be calibrated");
    local.validate();
    parent.validate();
    const auto world = parent * local;
    referenceImageCorners(image, world);
    const auto a = referenceImagePoint(image, first), b = referenceImagePoint(image, second);
    // Transform the difference directly: subtracting distant world positions loses precision.
    const auto distance = length(world.vector(b - a));
    if (!std::isfinite(distance) || distance < 1e-6)
        throw std::runtime_error(
            "Reference calibration anchors must be at least 1e-6 meters apart");
    const auto scale = knownLength / distance;
    auto resized = image;
    resized.width *= scale;
    resized.height *= scale;
    resized.validate();
    auto placement = local;
    const auto delta = local.vector(a - referenceImagePoint(resized, first));
    placement.m[12] += delta.x;
    placement.m[13] += delta.y;
    placement.m[14] += delta.z;
    placement.validate();
    referenceImageCorners(resized, parent * placement);
    return {resized, placement};
}

} // namespace sketchy
