// sketchyup-ifc-worker --export: reads an IFC export package (src/io/ifc_export.hpp, src/ifc/export_package.hpp)
// and writes an IFC4 file with IfcOpenShell's writer (ADR 0171 section 3.8, R097.d). No geometry kernel is used:
// every body is one IfcTriangulatedFaceSet.
#pragma once

#include <string>

namespace sketchyup::ifc {

struct ExportOptions {
  std::string input, output;
};

// Prints one result line ("R {json}") on stdout. Returns 0 for a file written, 3 for a refusal (also reported on
// stdout).
int runExport(const ExportOptions& options);

}  // namespace sketchyup::ifc
