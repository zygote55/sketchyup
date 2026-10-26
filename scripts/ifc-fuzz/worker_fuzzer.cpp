// R097.f, ADR 0171 check J3: the "worker" libFuzzer target. It parses an input from memory with
// IfcParse and iterates its geometry with the OpenCASCADE kernel the way sketchyup-ifc-worker's
// import does (welded vertices, the import's deflections, one thread, the Model context's Body
// representation), then reads every triangulation. Upstream's own fuzzer stops at the parser, and
// IfcOpenShell does not fuzz geometry. Build it against the sanitizer build of the library; the
// steps and the sanitizer options are in docs/verification/R097f-ifc-exchange.md ("J3").
//
// Library errors on malformed input are expected and caught; memory and undefined-behaviour
// errors are reported by the sanitizers, which abort the run (-fno-sanitize-recover=all).
#include "ifcgeom/conversion_settings.h"
#include "ifcgeom/element.h"
#include "ifcgeom/filter.h"
#include "ifcgeom/iterator.h"
#include "ifcgeom/kernel_registry.h"
#include "ifcgeom/representation.h"
#include "ifcparse/file.h"
#include "ifcparse/logger.h"
#include "plugin/plugin.h"

#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
// The helper's own limit is 128 MiB; a fuzzing input is kept small so that one run is fast.
constexpr std::size_t maxInput = 1u << 20;
}  // namespace

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) {
    if (size == 0 || size > maxInput || size > std::size_t(std::numeric_limits<int>::max())) return 0;
    // The kernel is a plugin found through the search paths: the directory of libifcopenshell.plugin.so and
    // the directory of the geometry kernels (R097.a note F13). The helper uses one install directory instead.
    static const bool pluginsReady = [] {
        ifcopenshell::plugin::set_search_paths({PLUGIN_DIR, GEOM_PLUGIN_DIR});
        return true;
    }();
    static_cast<void>(pluginsReady);
    try {
        ifcopenshell::file file(const_cast<void*>(static_cast<const void*>(data)), static_cast<int>(size));
        if (!file.good()) return 0;

        ifcopenshell::geom::settings settings;
        settings.set("use-world-coords", false);
        settings.set("weld-vertices", true);
        settings.set("mesher-linear-deflection", 0.001);
        settings.set("mesher-angular-deflection", 0.5);
        settings.set("max-voids-per-element", 100);
        settings.set("disable-opening-subtractions", false);
        settings.set("apply-default-materials", false);
        settings.set("no-normals", true);
        settings.set("context-priorities", std::vector<std::string>{"Body[ContextType=Model,TargetView=MODEL_VIEW]"});

        ifcopenshell::logger log;
        std::vector<ifcopenshell::geom::filter_function> filters;
        ifcopenshell::geom::iterator iterator(
            ifcopenshell::geom::kernels::construct(&file, "opencascade", settings, log), settings, &file, filters, 1, log);
        if (!iterator.initialize()) return 0;

        volatile double sink = 0;
        bool more = true;
        while (more) {
            const std::unique_ptr<ifcopenshell::geom::element> element = iterator.get();
            if (const auto* triangles = dynamic_cast<const ifcopenshell::geom::triangulation_element*>(element.get())) {
                for (const double v : triangles->geometry().verts()) sink = sink + v;
                for (const int f : triangles->geometry().faces()) sink = sink + f;
            }
            more = static_cast<bool>(iterator.next());
        }
    } catch (const std::exception&) {
        // Expected for malformed input: the library reports it as an exception.
    }
    return 0;
}
