// sketchyup-ifc-worker --import: reads one IFC file and writes an IFC conversion package
// (src/io/ifc_package.hpp) for the application (ADR 0171, R097.b).
#pragma once

#include <string>
#include <vector>

namespace sketchyup::ifc {

struct ImportOptions {
  std::string input, output;
  std::string kernel{"opencascade"};
  int threads{1};
  double linearDeflectionMetres{0.001};
  double angularDeflectionRadians{0.5};
  std::string lengthUnitOverride;  // mm, cm, m, in, ft or empty
  bool skipSpaces{false};
  bool gridAligned{false};  // --georef grid: rotate the model by the map conversion's rotation (decision 6b)
  int maxVoidsPerElement{100};
  std::size_t maxNodes{40000};  // the package's node bound (lowered by tests)
  std::size_t maxEntities{2000000};
  std::vector<int> excludeSteps;
  int diagnoseFrom{-1};
  int diagnoseSeconds{20};
  std::string testFault;
  bool dryRun{false};  // parse and iterate, write no package (the fault corpus)
};

// Runs the import; prints progress lines and one final result line ("R {json}") on stdout. Returns
// 0 for a package written (or a dry run completed), 3 for a refusal (also reported on stdout).
int runImport(const ImportOptions& options);

}  // namespace sketchyup::ifc
