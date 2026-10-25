#pragma once
// R102.a (ADR 0176 section 1.6): the checked decoder for the SKSKPP01 conversion package
// sketchyup-skp-worker writes. The application never links the OpenSKP parser (decision 2); it
// decodes the package with this decoder and hands the conversion (R102.b) a bounded, versioned
// structure. The fuzz target parser_fuzz_skp_package feeds the decoder arbitrary bytes and
// requires it to fail cleanly (the invariants in tests/fuzz/parser_harness.hpp).
//
// Layout and the manifest keys: skp/package.hpp (the worker side). The two sides carry the same
// structs and the same limits by hand, the way the IFC sides do.
#include <array>
#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace sketchy {
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


// Test seams: the fuzz target turns the digest check off so mutated chunks reach the chunk
// checks, and runs the same input again with it on.
struct SkpPackageDecodeOptions {
  bool checkDigest{true};
  bool checkTextureSize{true};
};

// Throws std::runtime_error naming the first problem; never reads beyond `bytes`, checks every
// count against the limits before allocating, and requires the file to be exactly
// 32 + manifest + geometry + texture bytes long.
SkpPackage decodeSkpPackage(const std::vector<std::uint8_t>& bytes,
                            SkpPackageDecodeOptions options = {});

}  // namespace sketchy
