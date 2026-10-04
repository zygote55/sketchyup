#pragma once
#include "core/body.hpp"
namespace sketchy {
struct MaterialRecord {
    Id id{};
    std::string name;
    std::array<float, 3> color{0.73f, 0.79f, 0.73f};
    float opacity{1};
    Id asset{};
    bool operator==(const MaterialRecord &) const = default;
};
using MaterialPtr = std::shared_ptr<const MaterialRecord>;
using MaterialRecords = std::map<Id, MaterialPtr>;
void validateMaterialRecords(const MaterialRecords &materials, Id next);
void validateMaterialAssignments(const MaterialRecords &materials,
                                 const std::map<Id, BodyPtr> &bodies);
MaterialSides faceMaterials(const Body &body, Id face);
struct SurfaceAppearance {
    std::array<float, 3> color;
    float opacity{1};
    Id material{};
    bool operator==(const SurfaceAppearance &) const = default;
};
SurfaceAppearance surfaceAppearance(const MaterialRecords &materials, const Body &body, Id face,
                                    bool back = false);
} // namespace sketchy
