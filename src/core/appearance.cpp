#include "core/appearance.hpp"
#include <algorithm>
namespace sketchy {
std::array<float, 3> faceColor(const Body &body, Id face) {
    return body.faceColors.contains(face) ? body.faceColors.at(face) : body.color;
}
void inheritFaceColors(const Body &before, Body &after,
                       const std::map<Id, std::vector<Id>> &descendants,
                       std::optional<std::array<float, 3>> newFaceColor) {
    std::map<Id, std::array<float, 3>> inherited;
    for (const auto &[source, targets] : descendants) {
        const auto color = faceColor(before, source);
        for (auto target : targets) {
            const auto found = inherited.find(target);
            if (found != inherited.end() && found->second != color)
                throw std::runtime_error("Merged faces have different colors; paint them alike "
                                         "before removing the boundary");
            inherited[target] = color;
        }
    }
    std::erase_if(after.faceColors,
                  [&](const auto &entry) { return !after.surface.faces.contains(entry.first); });
    for (const auto &[face, record] : after.surface.faces) {
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
