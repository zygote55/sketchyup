#pragma once
// R102.a: the SketchUp conversion package (ADR 0176 section 1.6 and "Implementation notes").
//
// sketchyup-skp-worker links the vendored OpenSKP parser subset (third_party/openskp) and is the
// one executable that parses a .skp file; what it hands to the application is not the library's
// data but this bounded, versioned package, which the application decodes with the checked
// decoder in io/skp_package.hpp (fuzz target parser_fuzz_skp_package).
//
// Layout, little-endian:
//   "SKSKPP01"             8 bytes
//   u32 manifest bytes      at most 32 MiB
//   u32 reserved             zero
//   u64 geometry bytes      at most 256 MiB
//   u64 texture bytes       at most 256 MiB
//   manifest                UTF-8 JSON object (see below)
//   geometry chunk           the definitions of the manifest back to back, in order; self-describing
//   texture chunk            the raw image bytes of the manifest's textures, in order
//
// The file is exactly 32 + manifest + geometry + texture bytes long. The manifest records the
// SHA-256 of the geometry and texture chunks. The geometry chunk is self-describing: per
// definition a u32 count and the f64 x3 vertices (metres), a u32 count and the u32 x2 + u8
// flags edges (0x01 soft, 0x02 smooth, 0x04 hidden, 0x08 wire), a u32 count and the faces, and a
// u32 count and the instances.
//
// Per face: u8 flags (0x01 hidden, 0x02 uvFront, 0x04 uvBack, 0x08 uvFrontProjected,
// 0x10 uvBackProjected), u32 front material (0xFFFFFFFF none), u32 back material (0xFFFFFFFF
// none), u32 tag (0xFFFFFFFF none), u32 loop count, then per loop u32 count (at least 3) and the
// vertex indices; the stored 3x3 UV transform (f64 x9, as stored, unconverted) follows when its
// flag is set.
//
// Per instance: u32 definition (0xFFFFFFFF none), u8 flags (0x01 hidden), u32 tag (0xFFFFFFFF
// none), u32 material (0xFFFFFFFF none) and f64 x16 (the row-major 4x4: the parser's 13-value
// matrix with its w dropped, as the parser's own glTF exporter drops it; the 3x3 is unitless
// and the translation in metres, the last column 0 0 0 1).
//
// Manifest keys (all required, no others): format "sketchyup-skp-package", version 1,
// geometrySha256, textureSha256, source {name, size, sha256}, formatVersion, container, units
// (string or null), unitScale, report, definitions [{name, guid, vertices, edges, faces,
// instances} (the counts the decoder checks against the chunk)], tags [{name, color, hidden}],
// materials [{name, color, opacity, texture}], textures [{filename, width, height, bytes}], counts.
// Vertex coordinates are metres in the definition's own frame, the definition's raw coordinates
// converted with the model's unit system (unitScale metres per raw unit). An instance's
// transform takes that frame to the placement.
#include <array>
#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace sketchyup::skp {
struct SkpPackageLimits {
  static constexpr std::size_t manifestBytes = 32 * 1024 * 1024;
  static constexpr std::size_t geometryBytes = 256 * 1024 * 1024;
  static constexpr std::size_t textureChunkBytes = 256 * 1024 * 1024;
  static constexpr std::size_t headerBytes = 32;
  static constexpr std::size_t definitions = 100000;
  static constexpr std::size_t vertices = 5000000;  // the model, total
  static constexpr std::size_t edges = 5000000;
  static constexpr std::size_t faces = 5000000;
  static constexpr std::size_t instances = 5000000;
  static constexpr std::size_t loopsPerFace = 64;  // 1 outer + up to 63 holes
  static constexpr std::size_t loopVertices = 65536;
  static constexpr std::size_t materials = 65536;
  static constexpr std::size_t tags = 65536;
  static constexpr std::size_t textures = 4096;
  static constexpr std::size_t perTextureBytes = 16 * 1024 * 1024;  // one image
  static constexpr std::size_t textureDimension = 4096;  // after decoding
  static constexpr std::size_t stringBytes = 64 * 1024;
  static constexpr std::size_t nameBytes = 4096;
  static constexpr std::size_t locationsPerKind = 32;
  static constexpr std::size_t noticeCodes = 64;
};

struct SkpPackageTag {
  std::string name;
  std::array<std::uint8_t, 3> color{200, 200, 200};
  bool hidden{};
};

struct SkpPackageMaterial {
  std::string name;
  std::array<std::uint8_t, 4> color{200, 200, 200, 255};
  double opacity{1.0};
  long long texture{-1};
};

struct SkpPackageTexture {
  std::string filename;
  double width{};
  double height{};
  std::vector<std::uint8_t> bytes;  // the image bytes as stored (the 4096 x 4096 after decoding,
                                    // the 16 MiB per image)
};

struct SkpPackageEdge {
  std::uint32_t v1{};
  std::uint32_t v2{};
  bool soft{};
  bool smooth{};
  bool hidden{};
  bool wire{};
};

struct SkpPackageFace {
  std::vector<std::vector<std::uint32_t>> loops;  // loops[0] the outer boundary
  long long materialFront{-1};
  long long materialBack{-1};
  long long tag{-1};
  bool hidden{};
  bool hasUvFront{};
  bool hasUvBack{};
  bool uvFrontProjected{};
  bool uvBackProjected{};
  std::array<double, 9> uvFront{};  // the 3x3 as stored, when hasUvFront
  std::array<double, 9> uvBack{};  // when hasUvBack
};

struct SkpPackageInstance {
  long long definition{-1};
  bool hidden{};
  long long tag{-1};
  long long material{-1};
  std::array<double, 16> transform{1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};  // row-major 4x4
};

struct SkpPackageDefinition {
  std::string name;  // "ROOT" for the model's root
  std::string guid;
  std::vector<std::array<double, 3>> vertices;  // metres
  std::vector<SkpPackageEdge> edges;
  std::vector<SkpPackageFace> faces;
  std::vector<SkpPackageInstance> instances;
};

struct SkpPackageLocation {
  std::string kind;  // face-uv, instance-tag, ...
  std::string definition;  // the definition name, for the user
  std::string message;  // clipped to 256 bytes
};

struct SkpPackageCounts {
  long long definitions{};
  long long vertices{};
  long long edges{};
  long long faces{};
  long long holes{};
  long long instances{};
  long long materials{};
  long long tags{};
  long long uvTransforms{};
  long long textures{};
};

struct SkpPackage {
  std::string formatVersion{"unknown"};  // the SketchUp version, e.g. "24.0.553"
  std::string container{"mfc"};  // vff or mfc
  std::string units;  // the model's unit system, "" when unknown
  double unitScale{};  // metres per raw unit
  std::string sourceName;
  std::string sourceSha256;
  std::uint64_t sourceSize{};
  // The report block (the IFC report shape, ADR 0176 section 4).
  SkpPackageCounts counts;
  std::map<std::string, long long> notices;
  std::map<std::string, long long> omitted;  // the report block: entities left out, per kind
  std::map<std::string, long long> sourceTotals;  // the file's own counts (the identity: counts +
                                                 // omitted = sourceTotals, for edges, faces,
                                                 // instances and materials)
  std::vector<SkpPackageLocation> locations;  // clipped to 32 per kind
  std::map<std::string, int> locationTotals;
  std::vector<SkpPackageDefinition> definitions;  // definitions[0] the root
  std::vector<SkpPackageTag> tags;
  std::vector<SkpPackageMaterial> materials;
  std::vector<SkpPackageTexture> textures;
};

// The checked writer: rejects the packages over the limits with a bounded message, writes the
// three sections with the exact byte counts and the manifest's digest. Returns "" when the
// package is over the limits.
std::string serializeSkpPackage(const SkpPackage& package, std::string& problem);

}  // namespace sketchyup::skp
