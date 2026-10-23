#pragma once
// R097.b: the IFC conversion package (ADR 0171 section 1.6 and "Implementation notes (R097.b)").
//
// sketchyup-ifc-worker links IfcOpenShell and is the only process that parses an IFC file; what it
// hands to the application is not the library's data but this bounded, versioned package, which the
// application decodes with the checked decoder below (fuzz target parser_fuzz_ifc_package).
//
// Layout, little-endian:
//   "SKIFCP01"              8 bytes
//   u32 manifest bytes      at most 32 MiB
//   u32 reserved            zero
//   u64 mesh bytes          at most 128 MiB
//   manifest                UTF-8 JSON object (see below)
//   mesh chunk              the meshes of the manifest back to back, in order; per mesh:
//                             f64 x3 per vertex, u32 x3 per triangle, u32 per triangle (material
//                             index into the manifest's materials, 0xFFFFFFFF none), u32 x2 per
//                             feature edge (vertex indices), and when the mesh has colours f32 x3
//                             per triangle (NaN x3 for a triangle without its own colour).
// The file is exactly 24 + manifest + mesh bytes long. The manifest records the SHA-256 of the mesh
// chunk. Vertex coordinates are metres in the element's own frame; `transform` takes that frame to
// the parent node's frame (row-major 4x4, translation in the last column, metres).
//
// Manifest keys (all required, no others): format "sketchyup-ifc-package", version 1, meshSha256,
// engine, source, units and report (objects, validated for size and depth, interpreted by the
// conversion), meshes [{vertices, triangles, edges, colors}], materials [{name, color, opacity}],
// nodes. A node is {step, class, guid, name, parent (an earlier node or -1), role, hidden,
// transform, mesh (-1 none), ifc [[key, type, value]...], props [[set, name, origin, type,
// value]...]}; type is "s", "b" or "d"; origin is 0 occurrence property, 1 occurrence quantity,
// 2 type property, 3 type quantity. Nodes are in tree order (a parent precedes its children, and the
// siblings of one parent are in file order).
//
// R097.c: `report.georeference` is the worker's reading of the Model context's IfcMapConversion and
// IfcProjectedCRS ({mode, result, mapConversion, crs, reasons, siteOrigin, rotationAppliedDegrees};
// ADR 0171 section 3.6). It is data: the conversion rebuilds the report's block from known fields and sets
// the document's site origin only if the numbers agree with each other and with the roots' transforms.
#include "geometry/surface.hpp"
#include <QByteArray>
#include <QJsonObject>
#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <variant>
#include <vector>
namespace sketchy {
struct IfcPackageLimits {
    static constexpr std::size_t manifestBytes = 32 * 1024 * 1024;
    static constexpr std::size_t meshBytes = 128 * 1024 * 1024;
    static constexpr std::size_t headerBytes = 24;
    static constexpr std::size_t nodes = 400000;
    static constexpr std::size_t meshes = 200000;
    static constexpr std::size_t materials = 65536;
    // The kernel's own bounds (ADR 0171 section 6.4): 100,000 triangles per element, 1,000,000 in all.
    static constexpr std::size_t meshTriangles = 100000;
    static constexpr std::size_t totalTriangles = 1000000;
    static constexpr std::size_t meshVertices = 3 * meshTriangles;
    static constexpr std::size_t nodeProperties = 20000;
    static constexpr std::size_t nodeProvenance = 64;
    static constexpr std::size_t stringBytes = 64 * 1024;
    static constexpr std::size_t nameBytes = 4096;
    static constexpr std::size_t depth = 128;
    static constexpr std::size_t sectionNodes = 200000;
};
using IfcPackageValue = std::variant<bool, double, std::string>;
enum class IfcNodeRole { Project, Site, Building, Storey, Space, Assembly, Element, Opening, Spatial };
struct IfcPackageProperty {
    std::string set, name;
    // 0 occurrence property, 1 occurrence quantity, 2 type property, 3 type quantity.
    int origin{};
    IfcPackageValue value;
    bool operator==(const IfcPackageProperty &) const = default;
};
struct IfcPackageNode {
    std::uint32_t step{};
    std::string className, guid, name;
    std::int64_t parent{-1};
    IfcNodeRole role{IfcNodeRole::Element};
    bool hidden{};
    std::array<double, 16> transform{1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
    std::int64_t mesh{-1};
    std::vector<std::pair<std::string, IfcPackageValue>> provenance;
    std::vector<IfcPackageProperty> properties;
    bool operator==(const IfcPackageNode &) const = default;
};
struct IfcPackageMesh {
    std::vector<Vec3> vertices;
    std::vector<std::array<std::uint32_t, 3>> triangles;
    // Per triangle: an index into the package's materials, or noMaterial.
    std::vector<std::uint32_t> materials;
    std::vector<std::array<std::uint32_t, 2>> featureEdges;
    // Empty, or one entry per triangle (nullopt: no colour of its own).
    std::vector<std::optional<std::array<float, 3>>> colors;
    static constexpr std::uint32_t noMaterial = 0xFFFFFFFFu;
    bool operator==(const IfcPackageMesh &) const = default;
};
struct IfcPackageMaterial {
    std::string name;
    std::array<float, 3> color{0.7f, 0.7f, 0.7f};
    float opacity{1};
    bool operator==(const IfcPackageMaterial &) const = default;
};
struct IfcPackage {
    // Sections the conversion interprets field by field; the decoder bounds their size and depth.
    QJsonObject engine, source, units, report;
    std::vector<IfcPackageMaterial> materials;
    std::vector<IfcPackageNode> nodes;
    std::vector<IfcPackageMesh> meshes;
    bool operator==(const IfcPackage &) const = default;
};
struct IfcPackageDecodeOptions {
    // Test seam: the fuzz target turns the digest check off so mutated chunks reach the mesh
    // checks, and runs the same input again with it on.
    bool checkMeshDigest{true};
};
// Throws std::runtime_error naming the first problem; never reads beyond `bytes`, and checks
// every count against the limits and the exact lengths before allocating.
IfcPackage decodeIfcPackage(const QByteArray &bytes, IfcPackageDecodeOptions options = {});
// The canonical form: decode(encode(p)) == p, and encode(decode(x)) decodes to the same package.
QByteArray encodeIfcPackage(const IfcPackage &package);
const char *ifcNodeRoleName(IfcNodeRole role);
} // namespace sketchy
