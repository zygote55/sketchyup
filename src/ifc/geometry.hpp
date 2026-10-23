// The geometry half of the import (ADR 0171 sections 1.2, 3.2, 3.3 and the R097.a findings): the
// iterator with the settings the probe proved, one pass over the products, the library's messages
// mapped to report entries, hosts whose cut is not trusted re-run uncut, per-face colours and
// feature edges. Meshes leave in metres in the element's own frame.
#pragma once

#include <array>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <vector>

#include "ifc/access.hpp"
#include "ifc/model.hpp"
#include "ifc/progress.hpp"
#include "ifc/report.hpp"

namespace sketchyup::ifc {

struct StyleOut {
  std::string name;
  std::array<float, 3> color{0.7f, 0.7f, 0.7f};
  float opacity{1};
  int styleStep{};  // the IfcSurfaceStyle (or 0)
};

struct MeshOut {
  std::vector<double> vertices;          // x y z, metres
  std::vector<std::uint32_t> triangles;  // 3 indices per triangle
  std::vector<int> triangleStyle;        // per triangle: index into `styles`, -1 none
  std::vector<int> triangleItem;         // per triangle: the representation item's step id
  std::vector<StyleOut> styles;
  std::vector<std::uint32_t> featureEdges;  // vertex index pairs
  std::vector<std::optional<std::array<float, 3>>> colors;  // empty, or per triangle
  std::set<int> itemSteps;
  bool tessellatedOnly{false};
};

struct ElementOut {
  int step{};
  std::array<double, 16> world{};  // row-major 4x4, metres
  int mesh{-1};
  bool voidsSubtracted{false};
  bool rerunUncut{false};
};

struct LibraryMessage {
  std::string severity, code, text;
  int instance{}, product{};
};

struct GeometryOptions {
  std::string kernel{"opencascade"};
  int threads{1};
  double linearDeflection{0.001};   // metres
  double angularDeflection{0.5};    // radians
  int maxVoidsPerElement{100};
  bool skipSpaces{false};
  std::vector<int> excludeSteps;  // left out after the worker stopped on them: reported as excluded
  std::vector<int> skipSteps;     // never given to the library (decomposition cycles): not reported as excluded
  int diagnoseFrom{-1};      // >= 0: try the tasks from this index on in child processes first (see geometry.cpp)
  int diagnoseSeconds{20};   // what one such task may take
  std::string testFault;  // "crash:N", "hang:N", "abort:N", "alloc:N" (tests only)
};

struct GeometryResult {
  bool ran{false};
  bool initialized{false};
  bool usedFallbackContext{false};
  std::string kernel;
  int threads{1};
  std::vector<MeshOut> meshes;
  std::map<int, ElementOut> elements;  // by product step
  std::vector<LibraryMessage> messages;
  double libraryUnit{1};
  std::string libraryUnitName;
  bool unitsTrusted{true};  // false: the library assumed metres and returned the file's numbers
  double meshScale{1};
  // Hosts by step.
  std::set<int> failedHosts, skippedHosts, untrustedHosts;
  std::set<int> excluded;
  std::size_t tasks{0};
  std::size_t emitted{0};
  std::string refusalCode, refusalMessage;
  std::map<int, std::string> failedProducts;  // product step -> reason for products that came out empty
};

// Runs the iterator. `expected` are the products the file defines a Body representation for (the
// accepted classes); the rest of the products are ignored. Never throws for a library failure.
GeometryResult runGeometry(ifcopenshell::file& file, const Model& model, const std::set<int>& expected,
                           const GeometryOptions& options, Report& report, Progress& progress);

// The category a library message code belongs to for the report.
std::string messageCategory(const std::string& code);

}  // namespace sketchyup::ifc
