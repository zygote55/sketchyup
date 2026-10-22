// The helper's probes (ADR 0171 section 1, R097.a). They report what the pinned IfcOpenShell
// library really does, as JSON on stdout, so a test can compare it with the ADR's claims.
#pragma once

#include <string>
#include <vector>

namespace sketchyup::ifc {

// Pins IfcOpenShell's plugin search path to the directory of the libifcopenshell.plugin.so this
// process loaded, and returns it ("" when it cannot be told). The library otherwise also searches
// the install directory compiled into it (here the build tree's prefix), so a plugin could be
// loaded from a path that is not the one the process's libraries came from, and replacing the
// libraries through LD_LIBRARY_PATH (check L2) would not replace the plugins. Call it first.
std::string pinPluginDirectory();

// --probe: the versions, the libraries loaded, schema and kernel availability. Never throws and
// never aborts when a kernel or one of its libraries is missing (check L6).
std::string probeEnvironment();

// What --probe-api runs: one IFC file through the iterator with the given settings.
struct GeometryRunOptions {
  std::string file;
  std::string kernel = "opencascade";
  int threads = 1;
  std::vector<std::pair<std::string, std::string>> settings;  // name, value as text
  std::vector<int> excludeSteps;                              // element filter: exclude these #ids
};
// --probe-api: iterate the file's geometry; per element: units, meshes, materials, messages.
std::string probeGeometry(const GeometryRunOptions& options);

// --probe-write: build a small IFC4 file with the typed writer API, write it to `path`, read it
// back and iterate it; reports what the writer produced and what the reader saw.
std::string probeWriter(const std::string& path);

}  // namespace sketchyup::ifc
