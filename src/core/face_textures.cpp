#include "core/face_textures.hpp"
#include <algorithm>
namespace sketchy {
TextureMappingSides faceTextureMappings(const Body &body, Id face) {
    if (!body.surface.faces.contains(face))
        throw std::runtime_error("Texture mapping references a missing face");
    const auto found = body.faceTextureMappings.find(face);
    return found == body.faceTextureMappings.end() ? TextureMappingSides{} : found->second;
}
TextureMapping effectiveFaceTextureMapping(const Body &body, Id face, bool back) {
    const auto sides = faceTextureMappings(body, face);
    const auto &explicitMapping = back ? sides.back : sides.front;
    if (explicitMapping)
        return *explicitMapping;
    auto normal = body.surface.normal(face);
    const std::array<double, 3> components{normal.x, normal.y, normal.z};
    size_t dominant = 0;
    for (size_t axis = 1; axis < 3; ++axis)
        if (std::abs(components[axis]) > std::abs(components[dominant]))
            dominant = axis;
    if (components[dominant] < 0)
        normal = normal * -1;
    // Winding reversal must not mirror an otherwise unchanged implicit image.
    const Vec3 tangent = std::abs(normal.x) < .9 ? Vec3{1, 0, 0} : Vec3{0, 1, 0};
    return planarTextureMapping({}, normal, tangent, 1, 1);
}
void setFaceTextureMappings(Body &body, Id face, const TextureMappingSides &mapping) {
    if (!body.surface.faces.contains(face))
        throw std::runtime_error("Texture mapping references a missing face");
    if (mapping.front)
        mapping.front->validate();
    if (mapping.back)
        mapping.back->validate();
    if (mapping == TextureMappingSides{})
        body.faceTextureMappings.erase(face);
    else
        body.faceTextureMappings[face] = mapping;
}
void validateFaceTextureMappings(const Body &body) {
    if (body.faceTextureMappings.size() > body.surface.faces.size())
        throw std::runtime_error("Too many face texture mappings");
    for (const auto &[face, mapping] : body.faceTextureMappings) {
        if (!body.surface.faces.contains(face) || mapping == TextureMappingSides{})
            throw std::runtime_error(
                "Texture mapping requires an existing face and at least one side");
        if (mapping.front)
            mapping.front->validate();
        if (mapping.back)
            mapping.back->validate();
    }
}
void reportFaceTextureMappingChanges(const Body &before, const Body &after,
                                     EntityChanges &changes) {
    if (before.faceTextureMappings == after.faceTextureMappings)
        return;
    for (const auto &[face, record] : after.surface.faces)
        if (before.surface.faces.contains(face) &&
            faceTextureMappings(before, face) != faceTextureMappings(after, face))
            changes.modified.push_back(face);
    std::sort(changes.modified.begin(), changes.modified.end());
    changes.modified.erase(std::unique(changes.modified.begin(), changes.modified.end()),
                           changes.modified.end());
}
TextureMappingSides transformTextureMappings(const TextureMappingSides &mapping,
                                             const Transform &oldToNew) {
    TextureMappingSides result;
    if (mapping.front)
        result.front = transformTextureMapping(*mapping.front, oldToNew);
    if (mapping.back)
        result.back = transformTextureMapping(*mapping.back, oldToNew);
    return result;
}
ChangeReport assignTextureMapping(Document &doc, Id body, Id face,
                                  std::optional<TextureMapping> mapping, bool front, bool back) {
    if (!front && !back)
        throw std::runtime_error("Choose at least one texture mapping side");
    const auto before = doc.bodies().at(body);
    auto sides = faceTextureMappings(*before, face);
    if (front)
        sides.front = mapping;
    if (back)
        sides.back = mapping;
    auto after = std::make_shared<Body>(*before);
    setFaceTextureMappings(*after, face, sides);
    if (*before == *after)
        return {};
    return doc.apply({"Map face texture", {{body, before, after}}}, doc.revision());
}
} // namespace sketchy
