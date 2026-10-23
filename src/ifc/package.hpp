// The worker's side of the IFC conversion package (src/io/ifc_package.hpp documents the layout;
// the application decodes it with a checked decoder and never trusts this writer).
#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <variant>
#include <vector>

#include "ifc/json_out.hpp"

namespace sketchyup::ifc {

using PackageValue = std::variant<bool, double, std::string>;

struct PackageNode {
  std::uint32_t step{};
  std::string className, guid, name, role;
  long long parent{-1};
  bool hidden{false};
  std::array<double, 16> transform{1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
  long long mesh{-1};
  std::vector<std::pair<std::string, PackageValue>> provenance;
  struct Property {
    std::string set, name;
    int origin{};
    PackageValue value;
  };
  std::vector<Property> properties;
};

struct PackageMesh {
  std::vector<double> vertices;
  std::vector<std::uint32_t> triangles;
  std::vector<std::uint32_t> materials;  // per triangle
  std::vector<std::uint32_t> edges;
  std::vector<std::optional<std::array<float, 3>>> colors;  // empty or per triangle
};

struct PackageMaterial {
  std::string name;
  std::array<float, 3> color{};
  float opacity{1};
};

struct PackageData {
  std::string engineJson, sourceJson, unitsJson, reportJson;  // each a complete JSON object
  std::vector<PackageMaterial> materials;
  std::vector<PackageNode> nodes;
  std::vector<PackageMesh> meshes;
};

// Serializes the package. Returns an empty string and sets `problem` when a limit would be exceeded.
std::string serializePackage(const PackageData& data, std::string& problem);

}  // namespace sketchyup::ifc
