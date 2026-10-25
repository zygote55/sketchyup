// The worker's reader of the STEP export package (R098.c, ADR 0172 section 8). The application
// captures the document into this bounded, versioned container (src/io/step_export.hpp documents the
// layout) and the helper writes the STEP file from it. The helper has no Qt and treats the package
// as data: every count, index and string is checked before it is used.
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace sketchyup::ifc {

struct StepExportLimits {
  static constexpr std::size_t manifestBytes = 32u * 1024 * 1024;
  static constexpr std::size_t meshBytes = 128u * 1024 * 1024;
  static constexpr std::size_t headerBytes = 24;
  static constexpr std::size_t nodes = 400'000;
  static constexpr std::size_t meshes = 400'000;
  static constexpr std::size_t meshVertices = 300'000;
  static constexpr std::size_t meshTriangles = 100'000;  // per element (ADR 0172 section 6)
  static constexpr std::size_t totalTriangles = 1'000'000;
  static constexpr std::size_t nameBytes = 2048;  // the product name cap (ADR 0172 section 4)
  static constexpr std::size_t layerBytes = 2048;
};

// One mesh: a body's triangles in the frame of the element that uses it. Its faces are the native
// faces: every triangle names its face, and every face has one colour.
struct StepExportMesh {
  std::vector<double> vertices;            // x y z per vertex
  std::vector<std::uint32_t> triangles;    // three 0-based vertex indices per triangle, counter-clockwise from outside
  std::vector<std::uint32_t> faceOf;       // per triangle, the index of its face (0 to faces - 1)
  std::vector<std::uint32_t> faceColor;    // per face, an index into colors
  std::vector<std::array<float, 3>> colors;
  std::size_t faces{};
  bool closed{};  // every edge is shared by two triangles that wind opposite ways
};

struct StepExportNode {
  bool assembly{};  // true: a group (an assembly); false: an element, a part with a mesh
  std::string name, layer;
  long long parent{-1};  // an earlier node, or -1 at the top; only a group is a parent
  long long mesh{-1};    // an element's mesh; -1 for a group
  std::array<double, 9> placement{0, 0, 0, 0, 0, 1, 1, 0, 0};  // origin, Axis (z), RefDirection (x), relative to the parent
  bool hidden{};
};

struct StepExportPackage {
  std::string fileName;
  std::vector<StepExportMesh> meshes;
  std::vector<StepExportNode> nodes;
};

// Throws std::runtime_error naming the first problem; never reads beyond `bytes`.
StepExportPackage readStepExportPackage(std::string_view bytes);

}  // namespace sketchyup::ifc
