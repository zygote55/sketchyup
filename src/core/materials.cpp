#include "core/materials.hpp"
namespace sketchy {
Id createMaterial(Document &doc, std::string name, std::array<float, 3> color, float opacity,
                  Id asset) {
    const auto id = doc.nextMaterialId();
    auto record = std::make_shared<MaterialRecord>(
        MaterialRecord{id, std::move(name), color, opacity, asset});
    Edit edit{"Create material", {}};
    edit.materials.push_back({id, nullptr, record});
    doc.apply(std::move(edit), doc.revision());
    return id;
}
void editMaterial(Document &doc, Id id, std::optional<std::string> name,
                  std::optional<std::array<float, 3>> color, std::optional<float> opacity,
                  std::optional<Id> asset) {
    if (!name && !color && !opacity && !asset)
        throw std::runtime_error("Material edit requires a field");
    const auto old = doc.materials().at(id);
    auto record = std::make_shared<MaterialRecord>(*old);
    if (name)
        record->name = *name;
    if (color)
        record->color = *color;
    if (opacity)
        record->opacity = *opacity;
    if (asset)
        record->asset = *asset;
    if (*record == *old)
        return;
    Edit edit{"Edit material", {}};
    edit.materials.push_back({id, old, record});
    doc.apply(std::move(edit), doc.revision());
}
void eraseMaterial(Document &doc, Id id) {
    Edit edit{"Delete material", {}};
    edit.materials.push_back({id, doc.materials().at(id), nullptr});
    // Transaction validation checks both scene assignments and unused definitions.
    doc.apply(std::move(edit), doc.revision());
}
ChangeReport assignMaterial(Document &doc, Id id, std::optional<Id> face, Id material, bool front,
                            bool back) {
    if (!front && !back)
        throw std::runtime_error("Choose at least one material side");
    if (material && !doc.materials().contains(material))
        throw std::runtime_error("Unknown material");
    const auto old = doc.bodies().at(id);
    auto body = std::make_shared<Body>(*old);
    auto set = [&](MaterialSides &sides) {
        if (front)
            sides.front = material;
        if (back)
            sides.back = material;
    };
    if (face) {
        if (!body->surface.faces.contains(*face))
            throw std::runtime_error("Unknown face");
        auto sides = faceMaterials(*body, *face);
        set(sides);
        if (sides == body->materials)
            body->faceMaterials.erase(*face);
        else
            body->faceMaterials[*face] = sides;
    } else {
        set(body->materials);
        for (auto it = body->faceMaterials.begin(); it != body->faceMaterials.end();) {
            set(it->second);
            if (it->second == body->materials)
                it = body->faceMaterials.erase(it);
            else
                ++it;
        }
    }
    if (*body == *old)
        return {};
    return doc.apply({"Assign material", {{id, old, body}}}, doc.revision());
}
} // namespace sketchy
