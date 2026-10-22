#pragma once
#include "core/texture_mapping.hpp"
#include "core/reference_image.hpp"
#include "core/indirect_optional.hpp"
#include "core/terrain_tile_key.hpp"
#include "core/text_source.hpp"
#include "core/transform.hpp"
#include "geometry/cleanup.hpp"
#include "geometry/curves.hpp"
#include "geometry/guides.hpp"
#include "geometry/planar.hpp"
#include "geometry/push_pull.hpp"
#include "geometry/topology.hpp"
#include <memory>
#include <optional>
#include <string>
#include <variant>
namespace sketchy {
struct TerrainRecord; // core/terrain_records.hpp
// Terrain is a container (it behaves like a group) that carries a TerrainRecord;
// its children are geometry tiles (ADR 0168 §2).
enum class BodyKind { Geometry, Group, ReferenceImage, Terrain };
struct MaterialSides {
    Id front{}, back{};
    bool operator==(const MaterialSides &) const = default;
};
struct TextureMappingSides {
    std::optional<TextureMapping> front, back;
    bool operator==(const TextureMappingSides &) const = default;
};
// Independent display flags; none of them changes incidence or triangulation.
struct EdgeAppearance {
    bool hidden{}, soft{}, smooth{};
    bool operator==(const EdgeAppearance &) const = default;
};
struct Body {
    Id id{};
    std::string name{"Face"};
    std::array<float, 3> color{0.73f, 0.79f, 0.73f};
    Surface surface;
    Topology topology;
    std::map<Id, EdgeAppearance> edgeAppearances;
    std::map<Id, std::array<float, 3>> faceColors;
    MaterialSides materials;
    std::map<Id, MaterialSides> faceMaterials;
    std::map<Id, TextureMappingSides> faceTextureMappings;
    std::map<Id, Curve> curves;
    std::map<Id, Guide> guides;
    Transform transform;
    Id parent{};
    BodyKind kind{BodyKind::Geometry};
    bool hidden{}, locked{};
    Id tag{}; // Zero is Untagged; organizational visibility never owns geometry.
    std::map<std::string, std::variant<bool, double, std::string>> properties;
    std::optional<TextSource> textSource;
    std::optional<ReferenceImage> referenceImage;
    // Present exactly on BodyKind::Terrain. Behaves as std::optional<TerrainRecord>; held
    // indirectly so that this header does not include core/terrain_records.hpp.
    IndirectOptional<TerrainRecord> terrain;
    // A geometry body that is one tile of its parent terrain: triangles only, no
    // wires, curves or guides, identity transform, implied edge appearance.
    bool terrainTile{};
    // R096.a: the tile's key in its terrain's stored grid (ADR 0169 §3); default on other bodies.
    TerrainTileKey tileKey;
    bool operator==(const Body &) const = default;
};
using BodyPtr = std::shared_ptr<const Body>;
} // namespace sketchy
