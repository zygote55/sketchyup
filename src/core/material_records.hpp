#pragma once
#include "core/body.hpp"
#include <optional>
#include <set>
namespace sketchy {
// The real-world size, in metres, that one repeat of a material's texture covers (ADR 0174
// section 3, R100.b2). Absent, as SketchUp leaves it, the implicit face mapping uses one metre.
struct TextureSize {
    double width{}, height{};
    bool operator==(const TextureSize &) const = default;
};
// Each side is finite and within one centimetre to one hundred metres.
inline constexpr double minimumTextureSize = 0.01, maximumTextureSize = 100;
void validateTextureSize(const TextureSize &size);
struct MaterialRecord {
    Id id{};
    std::string name;
    std::array<float, 3> color{0.73f, 0.79f, 0.73f};
    float opacity{1};
    Id asset{};
    std::optional<TextureSize> textureSize;
    bool operator==(const MaterialRecord &) const = default;
};
using MaterialPtr = std::shared_ptr<const MaterialRecord>;
using MaterialRecords = std::map<Id, MaterialPtr>;
void validateMaterialRecords(const MaterialRecords &materials, Id next);
void validateMaterialAssignments(const MaterialRecords &materials,
                                 const std::map<Id, BodyPtr> &bodies);
// R111: adds every material that `body` names (its sides, face overrides and terrain paint) to
// `materials`. A material that stays named is in use; it is the rule validateMaterialAssignments
// enforces on erase.
void addMaterialReferences(const Body &body, std::set<Id> &materials);
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
