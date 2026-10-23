// sketchyup-ifc-worker --check FILE.ifc: re-reads a written IFC file with IfcParse and checks every attribute of every
// instance against the declared schema (R097.d, check G1: "re-read through the helper's own IfcParse, which
// type-checks attributes"). It is a consistency check by the same library that wrote the file, not an independent
// validator, and it does not evaluate EXPRESS WHERE rules or inverse cardinalities.
#pragma once

#include <string>

namespace sketchyup::ifc {

// Prints one result line ("R {json}") on stdout: status "ok" (no issue), or "invalid" with the first issues. Returns
// 0 when the file is valid, 3 when it has issues or cannot be read.
int runCheck(const std::string& path);

}  // namespace sketchyup::ifc
