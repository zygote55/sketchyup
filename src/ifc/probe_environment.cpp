// --probe: versions, loaded libraries, schema and kernel availability.
#include "ifc/probe.hpp"

#include <dlfcn.h>
#include <unistd.h>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <set>
#include <sstream>

#include "ifc/json_out.hpp"
#include "ifcgeom/conversion_settings.h"
#include "ifcgeom/kernel_registry.h"
#include "ifcparse/file.h"
#include "ifcparse/parse.h"
#include "ifcparse/schema.h"
#include "plugin/plugin.h"
#include "sketchyup/version.hpp"

#ifndef SKETCHYUP_IFC_PIN_COMMIT_STRING
#define SKETCHYUP_IFC_PIN_COMMIT_STRING "unknown"
#endif

namespace sketchyup::ifc {
namespace {

namespace fs = std::filesystem;

// libname.so, libname.so.1, libname.so.1.2.3 (not ld.so.cache or a data file that has ".so" in it)
bool isSharedObjectName(const std::string& name) {
  const auto at = name.rfind(".so");
  if (at == std::string::npos) return false;
  const std::string rest = name.substr(at + 3);
  if (rest.empty()) return true;
  if (rest[0] != '.') return false;
  for (char c : rest)
    if (c != '.' && (c < '0' || c > '9')) return false;
  return true;
}

std::vector<std::string> mappedSharedObjects() {
  std::set<std::string> paths;
  std::ifstream maps("/proc/self/maps");
  std::string line;
  while (std::getline(maps, line)) {
    const auto slash = line.find('/');
    if (slash == std::string::npos) continue;
    std::string path = line.substr(slash);
    if (path.size() > 10 && path.compare(path.size() - 10, 10, " (deleted)") == 0) path.resize(path.size() - 10);
    if (!isSharedObjectName(fs::path(path).filename().string())) continue;
    std::error_code error;
    const fs::path real = fs::weakly_canonical(path, error);
    paths.insert(error ? path : real.string());
  }
  return {paths.begin(), paths.end()};
}

struct KernelStatus {
  std::string name;
  std::string pluginFile;
  bool filePresent = false;
  bool available = false;
  std::string reason;
};

// A kernel is available when its plugin file is there, it loads with every library it needs
// (checked with a plain dlopen first: IfcOpenShell's own loader aborts the process with an
// uncaught boost::system_error when an OCCT library is missing, which the prototype saw), and the
// library can then construct it for an empty file.
KernelStatus kernelStatus(const fs::path& directory, const std::string& name) {
  KernelStatus status;
  status.name = name;
  const fs::path plugin = directory / ("ifcopenshell_geometry_kernel_" + name + ".so");
  status.pluginFile = plugin.string();
  std::error_code error;
  status.filePresent = fs::exists(plugin, error);
  if (!status.filePresent) {
    status.reason = "plugin file not found";
    return status;
  }
  void* handle = dlopen(plugin.c_str(), RTLD_NOW | RTLD_LOCAL);
  if (handle == nullptr) {
    const char* message = dlerror();
    status.reason = message != nullptr ? message : "dlopen failed";
    return status;
  }
  try {
    ifcopenshell::logger log;
    log.output_format(ifcopenshell::logger::FMT_INMEMORY);
    ifcopenshell::file file(ifcopenshell::schema_by_name("IFC4"), ifcopenshell::FT_AUTODETECT, "", log);
    ifcopenshell::geom::settings settings;
    auto kernel = ifcopenshell::geom::kernels::construct(&file, name, settings, log);
    status.available = kernel != nullptr;
    if (!status.available) status.reason = "the library could not construct the kernel";
  } catch (const std::exception& problem) {
    status.reason = problem.what();
  } catch (...) {
    status.reason = "unknown exception";
  }
  return status;
}

}  // namespace

std::string pinPluginDirectory() {
  Dl_info where{};
  if (dladdr(reinterpret_cast<void*>(&ifcopenshell::plugin::set_search_paths), &where) == 0 || where.dli_fname == nullptr)
    return {};
  std::error_code error;
  const fs::path directory = fs::weakly_canonical(fs::path(where.dli_fname), error).parent_path();
  if (error || directory.empty()) return {};
  ifcopenshell::plugin::set_search_paths({directory.string()});
  return directory.string();
}

std::string probeEnvironment() {
  JsonOut json;
  json.beginObject();
  json.key("worker").beginObject();
  json.field("name", "sketchyup-ifc-worker");
  json.field("version", SKETCHYUP_VERSION);
  json.field("probeVersion", 1);
  json.endObject();

  json.key("ifcopenshell").beginObject();
  json.field("version", IFCOPENSHELL_VERSION);
  json.field("pinnedCommit", SKETCHYUP_IFC_PIN_COMMIT_STRING);
  json.endObject();

  const fs::path directory = pinPluginDirectory();
  json.field("pluginDirectory", directory.string());
  json.key("pluginSearchPaths").beginArray();
  for (const std::string& path : ifcopenshell::plugin::search_paths()) json.value(path);
  json.endArray();

  json.key("schemas").beginArray();
  for (const char* name : {"IFC2X3", "IFC4", "IFC4X3_ADD2"}) {
    bool available = false;
    std::string reason;
    try {
      available = ifcopenshell::schema_by_name(name) != nullptr;
    } catch (const std::exception& problem) {
      reason = problem.what();
    }
    json.beginObject().field("name", name).field("available", available);
    if (!reason.empty()) json.field("reason", reason);
    json.endObject();
  }
  json.endArray();

  json.key("kernels").beginArray();
  bool geometryKernelAvailable = false;
  for (const char* name : {"opencascade", "passthrough"}) {
    const KernelStatus status = kernelStatus(directory, name);
    if (status.name == "opencascade") geometryKernelAvailable = status.available;
    json.beginObject()
        .field("name", status.name)
        .field("pluginFile", status.pluginFile)
        .field("pluginPresent", status.filePresent)
        .field("available", status.available);
    if (!status.available) json.field("reason", status.reason);
    json.endObject();
  }
  json.endArray();
  // The kernel that interprets extrusions, sweeps and Booleans (the pass-through kernel only
  // forwards items that are already tessellated).
  json.field("kernelAvailable", geometryKernelAvailable);
  json.field("kernelStatus", geometryKernelAvailable ? "available" : "kernel unavailable");

  const std::vector<std::string> libraries = mappedSharedObjects();
  json.key("loadedLibraries").beginArray();
  for (const std::string& path : libraries) json.value(path);
  json.endArray();
  std::string occt;
  for (const std::string& path : libraries) {
    const std::string leaf = fs::path(path).filename().string();
    if (leaf.rfind("libTKernel.so.", 0) == 0) occt = leaf.substr(std::string("libTKernel.so.").size());
  }
  json.field("occtVersion", occt);
  json.endObject();
  return json.str();
}

}  // namespace sketchyup::ifc
