#pragma once
#include "core/model.hpp"
namespace sketchy {
Id createMaterial(Document &doc, std::string name, std::array<float, 3> color, float opacity = 1,
                  Id asset = 0);
void editMaterial(Document &doc, Id material, std::optional<std::string> name,
                  std::optional<std::array<float, 3>> color, std::optional<float> opacity,
                  std::optional<Id> asset = {});
void eraseMaterial(Document &doc, Id material);
// No face means the record's default and all existing faces on the requested side.
// Material zero restores legacy color. The other side is preserved independently.
ChangeReport assignMaterial(Document &doc, Id body, std::optional<Id> face, Id material,
                            bool front = true, bool back = true);
} // namespace sketchy
