#include "io/reference_image_io.hpp"
namespace sketchy {
QJsonObject encodeReferenceImage(const ReferenceImage &image) {
    image.validate();
    return {{"asset", QString::number(image.asset)},
            {"width", image.width},
            {"height", image.height},
            {"opacity", image.opacity}};
}
ReferenceImage decodeReferenceImage(const QJsonObject &object) {
    if (object.size() != 4 || !object["asset"].isString() || !object["width"].isDouble() ||
        !object["height"].isDouble() || !object["opacity"].isDouble())
        throw std::runtime_error("Reference image requires asset, width, height and opacity");
    const auto token = object["asset"].toString();
    bool valid{};
    const auto asset = token.toULongLong(&valid);
    if (!valid || QString::number(asset) != token)
        throw std::runtime_error("Reference image asset must be a canonical decimal identity");
    ReferenceImage result{asset, object["width"].toDouble(), object["height"].toDouble(),
                          object["opacity"].toDouble()};
    result.validate();
    return result;
}
} // namespace sketchy
