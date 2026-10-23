// The worker's reader of the IFC export package (R097.d, ADR 0171 section 3.8). The application
// captures the document into this bounded, versioned container (src/io/ifc_export.hpp documents
// the layout) and the helper builds the IFC4 file from it; the helper has no Qt and treats the
// package as data: every count, index and string is checked before it is used.
#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace sketchyup::ifc {

struct ExportLimits {
  static constexpr std::size_t manifestBytes = 32u * 1024 * 1024;
  static constexpr std::size_t meshBytes = 128u * 1024 * 1024;
  static constexpr std::size_t headerBytes = 24;
  static constexpr std::size_t nodes = 400'000;
  static constexpr std::size_t meshes = 400'000;
  static constexpr std::size_t styles = 65'536;
  static constexpr std::size_t properties = 4'000'000;
  static constexpr std::size_t nodeProperties = 20'000;
  static constexpr std::size_t depth = 4096;  // the tree's depth
  static constexpr std::size_t nameBytes = 4096;
  static constexpr std::size_t valueBytes = 32u * 1024;
};

struct ExportStyle {
  std::string name;
  std::array<double, 3> color{};
  double opacity{1};
  bool material{false};  // also an IfcMaterial of this name
};

struct ExportMesh {
  std::vector<double> vertices;            // x y z per vertex, in the node's frame
  std::vector<std::uint32_t> triangles;    // three 0-based vertex indices per triangle
  std::vector<std::uint32_t> colorIndex;   // empty, or one 0-based index into `colors` per triangle
  std::vector<std::array<float, 3>> colors;
  bool closed{false};
};

using ExportValue = std::variant<bool, double, std::string>;
struct ExportProperty {
  std::string set, name;
  ExportValue value;
};

struct ExportNode {
  long long id{};  // the body's stable ID; 0 for a node the export made up
  std::string role;  // project, site, building, storey, space, assembly, element
  std::string requestedClass, guid, name, longName, objectType, predefinedType;
  long long parent{-1};
  std::array<double, 9> placement{0, 0, 0, 0, 0, 1, 1, 0, 0};  // origin, Axis (z), RefDirection (x)
  long long mesh{-1}, style{-1};
  bool hidden{false};
  bool hasElevation{false};
  double elevation{};
  std::vector<ExportProperty> properties;
};

struct ExportGeoreference {
  bool write{false};
  std::string crs;
  double eastings{}, northings{}, height{};
};

struct ExportPackage {
  std::string documentId, applicationName, applicationVersion, timestamp, fileName;
  ExportGeoreference georeference;
  std::vector<ExportStyle> styles;
  std::vector<ExportMesh> meshes;
  std::vector<ExportNode> nodes;
};

// Throws std::runtime_error naming the first problem; never reads beyond `bytes`.
ExportPackage readExportPackage(std::string_view bytes);

// 22 characters of 0-9A-Za-z_$ whose first character is 0 to 3 (a compressed 128-bit value).
bool validGlobalId(std::string_view guid);
// The compression of IfcGloballyUniqueId (ISO 10303-21 base 64 variant) of 16 bytes.
std::string compressGuid(const std::array<std::uint8_t, 16>& bytes);
// SHA-256 of `seed`, compressed: the deterministic GlobalId of decision 16.
std::string derivedGlobalId(const std::string& seed);

}  // namespace sketchyup::ifc
