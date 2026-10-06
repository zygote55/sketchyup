#include "core/reference_images.hpp"
namespace sketchy {
Id createReferenceImage(Document &doc, ReferenceImage image, Transform placement, Id parent,
                        std::string name) {
    auto body = std::make_shared<Body>();
    body->id = doc.nextId();
    body->kind = BodyKind::ReferenceImage;
    body->referenceImage = image;
    body->transform = placement;
    body->parent = parent;
    body->name = std::move(name);
    doc.apply({"Create reference image", {{body->id, nullptr, body}}}, doc.revision());
    return body->id;
}
ChangeReport setReferenceImage(Document &doc, Id id, ReferenceImage image) {
    const auto old = doc.bodies().at(id);
    if (!old->referenceImage)
        throw std::runtime_error("Entity is not a reference image");
    image.validate();
    if (*old->referenceImage == image)
        return {};
    auto body = std::make_shared<Body>(*old);
    body->referenceImage = image;
    return doc.apply({"Edit reference image", {{id, old, body}}}, doc.revision());
}
ChangeReport calibrateReferenceImage(Document &doc, Id id, ImagePoint first, ImagePoint second,
                                     double knownLength) {
    const auto old = doc.bodies().at(id);
    if (!old->referenceImage)
        throw std::runtime_error("Entity is not a reference image");
    const auto calibrated = calibrateReferenceImage(
        *old->referenceImage, old->transform,
        old->parent ? doc.worldTransform(old->parent) : Transform{}, first, second, knownLength);
    if (*old->referenceImage == calibrated.image && old->transform == calibrated.local)
        return {};
    auto body = std::make_shared<Body>(*old);
    body->referenceImage = calibrated.image;
    body->transform = calibrated.local;
    return doc.apply({"Calibrate reference image", {{id, old, body}}}, doc.revision());
}
void validateReferenceImageAssets(const std::map<Id, BodyPtr> &bodies, const AssetRecords &assets) {
    for (const auto &[id, body] : bodies)
        if (body->referenceImage && !assets.contains(body->referenceImage->asset))
            throw std::runtime_error(
                "Reference image uses an unknown asset; retain an explicit missing record");
}
} // namespace sketchy
