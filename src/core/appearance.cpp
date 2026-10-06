#include "core/appearance.hpp"
#include "core/face_textures.hpp"
#include <algorithm>
namespace sketchy {
std::array<float, 3> faceColor(const Body &body, Id face) {
    return body.faceColors.contains(face) ? body.faceColors.at(face) : body.color;
}
void inheritFaceAppearance(const Body &before, Body &after,
                           const std::map<Id, std::vector<Id>> &descendants,
                           std::optional<std::array<float, 3>> newFaceColor,
                           std::optional<MaterialSides> newFaceMaterials,
                           std::optional<TextureMappingSides> newFaceMappings) {
    std::map<Id, std::array<float, 3>> inherited;
    std::map<Id, MaterialSides> assigned;
    std::map<Id, TextureMappingSides> mapped;
    for (const auto &[source, targets] : descendants) {
        const auto color = faceColor(before, source);
        const auto materials = faceMaterials(before, source);
        const auto mapping = faceTextureMappings(before, source);
        for (auto target : targets) {
            const auto found = inherited.find(target);
            if (found != inherited.end() && found->second != color)
                throw std::runtime_error("Merged faces have different colors; paint them alike "
                                         "before removing the boundary");
            inherited[target] = color;
            if (assigned.contains(target) && assigned.at(target) != materials)
                throw std::runtime_error("Merged faces have different front/back materials; "
                                         "assign them alike before removing the boundary");
            assigned[target] = materials;
            if (mapped.contains(target) && mapped.at(target) != mapping)
                throw std::runtime_error("Merged faces have different texture mappings; "
                                         "map them alike before removing the boundary");
            mapped[target] = mapping;
        }
    }
    std::erase_if(after.faceColors,
                  [&](const auto &entry) { return !after.surface.faces.contains(entry.first); });
    std::erase_if(after.faceMaterials,
                  [&](const auto &entry) { return !after.surface.faces.contains(entry.first); });
    std::erase_if(after.faceTextureMappings,
                  [&](const auto &entry) { return !after.surface.faces.contains(entry.first); });
    for (const auto &[face, record] : after.surface.faces) {
        if (mapped.contains(face))
            setFaceTextureMappings(after, face, mapped.at(face));
        else if (!before.surface.faces.contains(face) && newFaceMappings)
            setFaceTextureMappings(after, face, *newFaceMappings);
        auto materials = assigned.contains(face)                ? std::optional(assigned.at(face))
                         : !before.surface.faces.contains(face) ? newFaceMaterials
                                                                : std::nullopt;
        if (materials) {
            if (*materials == after.materials)
                after.faceMaterials.erase(face);
            else
                after.faceMaterials[face] = *materials;
        }
        const auto found = inherited.find(face);
        if (found != inherited.end()) {
            if (found->second == after.color)
                after.faceColors.erase(face);
            else
                after.faceColors[face] = found->second;
        } else if (newFaceColor && !before.surface.faces.contains(face) &&
                   !after.faceColors.contains(face)) {
            if (*newFaceColor != after.color)
                after.faceColors[face] = *newFaceColor;
        }
    }
}
} // namespace sketchy
