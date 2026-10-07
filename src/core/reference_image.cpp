#include "core/reference_image.hpp"
namespace sketchy {
void ReferenceImage::validate() const {
    if (!asset)
        throw std::runtime_error("Reference image needs a managed asset identity");
    for (const auto size : {width, height})
        if (!std::isfinite(size) || size < 1e-6 || size > coordinateLimit)
            throw std::runtime_error(
                "Reference image dimensions must be between 1e-6 and 1e6 meters");
    if (!std::isfinite(opacity) || opacity < 0 || opacity > 1)
        throw std::runtime_error("Reference image opacity must be between zero and one");
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
