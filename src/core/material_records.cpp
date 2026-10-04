#include "core/material_records.hpp"
#include "core/appearance.hpp"
#include <set>
namespace sketchy {
void validateMaterialRecords(const MaterialRecords &materials, Id next) {
    if (!next || materials.size() > 1024)
        throw std::runtime_error("Invalid material allocator or count");
    std::set<std::string> names;
    for (const auto &[id, material] : materials) {
        if (!material || !id || id >= next || id != material->id || material->name.empty() ||
            material->name.size() > 1024 || !names.insert(material->name).second)
            throw std::runtime_error("Invalid or duplicate material identity/name");
        for (const auto component : material->color)
            if (!std::isfinite(component) || component < 0 || component > 1)
                throw std::runtime_error("Material color must be finite and between zero and one");
        if (!std::isfinite(material->opacity) || material->opacity < 0 || material->opacity > 1)
            throw std::runtime_error("Material opacity must be finite and between zero and one");
    }
}
void validateMaterialAssignments(const MaterialRecords &materials,
                                 const std::map<Id, BodyPtr> &bodies) {
    auto valid = [&](MaterialSides sides) {
        if ((sides.front && !materials.contains(sides.front)) ||
            (sides.back && !materials.contains(sides.back)))
            throw std::runtime_error("Assignment references a missing material");
    };
    for (const auto &[id, body] : bodies) {
        valid(body->materials);
        for (const auto &[face, sides] : body->faceMaterials) {
            if (!body->surface.faces.contains(face))
                throw std::runtime_error("Material assignment references a missing face");
            valid(sides);
        }
    }
}
MaterialSides faceMaterials(const Body &body, Id face) {
    return body.faceMaterials.contains(face) ? body.faceMaterials.at(face) : body.materials;
}
SurfaceAppearance surfaceAppearance(const MaterialRecords &materials, const Body &body, Id face,
                                    bool back) {
    const auto sides = faceMaterials(body, face);
    const auto id = back ? sides.back : sides.front;
    if (!id)
        return {faceColor(body, face), 1, 0};
    const auto &material = *materials.at(id);
    return {material.color, material.opacity, id};
}
} // namespace sketchy
