#include "ifc/geometry.hpp"

#include <algorithm>
#include <cmath>
#include <csignal>
#include <cstdlib>
#include <memory>
#include <sstream>
#include <thread>
#include <sys/prctl.h>
#include <sys/wait.h>
#include <unistd.h>

#include <cerrno>
#include <cstdio>

#include "ifcgeom/conversion_settings.h"
#include "ifcgeom/element.h"
#include "ifcgeom/filter.h"
#include "ifcgeom/iterator.h"
#include "ifcgeom/kernel_registry.h"
#include "ifcgeom/representation.h"
#include "ifcparse/logger.h"

namespace sketchyup::ifc {
namespace {

namespace geom = ifcopenshell::geom;

constexpr double creaseDegrees = 30;

const char* severityName(int severity) {
  switch (severity) {
    case ifcopenshell::logger::LOG_PERF: return "perf";
    case ifcopenshell::logger::LOG_DEBUG: return "debug";
    case ifcopenshell::logger::LOG_NOTICE: return "notice";
    case ifcopenshell::logger::LOG_WARNING: return "warning";
    case ifcopenshell::logger::LOG_ERROR: return "error";
    default: return "unknown";
  }
}

// "#123=IFCWALL(...)" -> 123
int stepFromReference(const std::string& text) {
  if (text.size() < 2 || text[0] != '#') return 0;
  long long value = 0;
  for (std::size_t i = 1; i < text.size() && std::isdigit(static_cast<unsigned char>(text[i])); ++i) {
    value = value * 10 + (text[i] - '0');
    if (value > 0x7FFFFFFF) return 0;
  }
  return static_cast<int>(value);
}

void setSetting(geom::settings& settings, const char* name, const geom::settings::value_variant_t& value) {
  settings.set(name, value);
}

geom::settings makeSettings(const GeometryOptions& options, bool disableOpenings, bool withPriority, bool deferFirst) {
  geom::settings settings;
  setSetting(settings, "use-world-coords", false);
  setSetting(settings, "weld-vertices", true);
  setSetting(settings, "mesher-linear-deflection", options.linearDeflection);
  setSetting(settings, "mesher-angular-deflection", options.angularDeflection);
  setSetting(settings, "max-voids-per-element", options.maxVoidsPerElement);
  setSetting(settings, "disable-opening-subtractions", disableOpenings);
  setSetting(settings, "enable-layerset-slicing", false);
  setSetting(settings, "no-normals", true);
  // The library colours unstyled elements by class; the file states no such colour, so none is applied.
  setSetting(settings, "apply-default-materials", false);
  setSetting(settings, "defer-processing-first-element", deferFirst);
  if (withPriority)
    setSetting(settings, "context-priorities", std::vector<std::string>{"Body[ContextType=Model,TargetView=MODEL_VIEW]"});
  return settings;
}

struct Quant {
  long long x, y, z;
  bool operator<(const Quant& o) const { return std::tie(x, y, z) < std::tie(o.x, o.y, o.z); }
  bool operator==(const Quant& o) const = default;
};
Quant quantize(double x, double y, double z) {
  const double grid = 1e-6;
  return {std::llround(x / grid), std::llround(y / grid), std::llround(z / grid)};
}
using TriangleKey = std::array<Quant, 3>;
TriangleKey keyOf(Quant a, Quant b, Quant c) {
  TriangleKey key{a, b, c};
  std::sort(key.begin(), key.end());
  return key;
}

// The per-face colours of an IfcIndexedColourMap, matched to the library's triangles by position
// (the library does not read the map, and its triangle order is not part of its contract).
struct ColourMap {
  std::map<TriangleKey, std::array<float, 3>> byTriangle;
  std::size_t triangles{0};
  std::size_t coloured{0};
};

class ColourMaps {
public:
  ColourMaps(ifcopenshell::file& file, double metresPerUnit) : factor_(metresPerUnit) {
    std::vector<Inst> maps;
    try {
      maps = file.instances_by_type("IfcIndexedColourMap");
    } catch (...) {
    }
    for (const Inst& map : maps) {
      const Inst faceSet = ref(map, "MappedTo");
      if (!isA(faceSet, "IfcTriangulatedFaceSet")) {
        unsupported_.insert(stepOf(faceSet));
        continue;
      }
      pending_.emplace(stepOf(faceSet), map);
    }
  }
  bool unsupported(int item) const { return unsupported_.contains(item); }
  const ColourMap* forItem(int item, ifcopenshell::file& file) {
    if (const auto found = built_.find(item); found != built_.end()) return &found->second;
    const auto pending = pending_.find(item);
    if (pending == pending_.end()) return nullptr;
    ColourMap result = build(pending->second, file.instance_by_id(item));
    return &built_.emplace(item, std::move(result)).first->second;
  }

private:
  ColourMap build(const Inst& map, const Inst& faceSet) {
    ColourMap out;
    const Inst coordinates = ref(faceSet, "Coordinates");
    std::vector<std::vector<double>> points;
    if (const auto value = attr(coordinates, "CoordList"))
      if (value->type() == ifcopenshell::Argument_AGGREGATE_OF_AGGREGATE_OF_DOUBLE)
        points = static_cast<std::vector<std::vector<double>>>(*value);
    std::vector<std::vector<std::int64_t>> triangles;
    if (const auto value = attr(faceSet, "CoordIndex"))
      if (value->type() == ifcopenshell::Argument_AGGREGATE_OF_AGGREGATE_OF_INT)
        triangles = static_cast<std::vector<std::vector<std::int64_t>>>(*value);
    std::vector<std::vector<double>> palette;
    if (const auto value = attr(ref(map, "Colours"), "ColourList"))
      if (value->type() == ifcopenshell::Argument_AGGREGATE_OF_AGGREGATE_OF_DOUBLE)
        palette = static_cast<std::vector<std::vector<double>>>(*value);
    std::vector<std::int64_t> index;
    if (const auto value = attr(map, "ColourIndex"))
      if (value->type() == ifcopenshell::Argument_AGGREGATE_OF_INT) index = static_cast<std::vector<std::int64_t>>(*value);
    out.triangles = triangles.size();
    for (std::size_t t = 0; t < triangles.size() && t < index.size(); ++t) {
      const auto& corner = triangles[t];
      if (corner.size() != 3) continue;
      std::array<Quant, 3> q{};
      bool ok = true;
      for (int k = 0; k < 3 && ok; ++k) {
        const auto at = corner[static_cast<std::size_t>(k)] - 1;
        if (at < 0 || static_cast<std::size_t>(at) >= points.size() || points[static_cast<std::size_t>(at)].size() < 3) {
          ok = false;
          break;
        }
        const auto& p = points[static_cast<std::size_t>(at)];
        q[static_cast<std::size_t>(k)] = quantize(p[0] * factor_, p[1] * factor_, p[2] * factor_);
      }
      const auto colour = index[t] - 1;
      if (!ok || colour < 0 || static_cast<std::size_t>(colour) >= palette.size() ||
          palette[static_cast<std::size_t>(colour)].size() < 3)
        continue;
      const auto& c = palette[static_cast<std::size_t>(colour)];
      if (!(c[0] >= 0 && c[0] <= 1 && c[1] >= 0 && c[1] <= 1 && c[2] >= 0 && c[2] <= 1)) continue;
      out.byTriangle.emplace(keyOf(q[0], q[1], q[2]),
                             std::array<float, 3>{static_cast<float>(c[0]), static_cast<float>(c[1]), static_cast<float>(c[2])});
      ++out.coloured;
    }
    return out;
  }

  double factor_;
  std::map<int, Inst> pending_;
  std::set<int> unsupported_;
  std::map<int, ColourMap> built_;
};

}  // namespace

std::string messageCategory(const std::string& code) {
  if (code == "GEO033" || code == "GEO403" || code == "GEO191" || code == "GEO188") return "openingSubtraction";
  if (code == "GEO308" || code == "GEO309" || code == "GEO312") return "unitsAssumed";
  if (code == "GEO031") return "defaultMaterial";
  if (code == "GEO051") return "noRepresentations";
  if (code == "GEO089" || code == "GEO283" || code == "GEO326" || code == "GEO327") return "convert";
  if (code.rfind("UNS", 0) == 0) return "unsupported";
  return "other";
}

namespace {

struct Pass {
  geom::settings settings;
  std::unique_ptr<ifcopenshell::logger> log;
  bool rerun{false};
};

std::string tessellatedKind(const Inst& item) { return isA(item, "IfcTessellatedFaceSet") ? "tessellated" : "other"; }

// Builds the mesh for a library triangulation: metres, per-triangle style and item, feature edges
// (the library's, except that edges of tessellated items are derived from the crease angle), and
// the per-face colours of the item's IfcIndexedColourMap.
MeshOut buildMesh(const geom::triangulation& tri, double scale, ifcopenshell::file& file, ColourMaps& colours,
                  Report& report) {
  MeshOut mesh;
  const auto& verts = tri.verts();
  const auto& faces = tri.faces();
  mesh.vertices.reserve(verts.size());
  for (const double v : verts) mesh.vertices.push_back(v * scale);
  const std::size_t triangles = faces.size() / 3;
  mesh.triangles.reserve(faces.size());
  for (const int index : faces) mesh.triangles.push_back(static_cast<std::uint32_t>(index));
  const auto& materialIds = tri.material_ids();
  const auto& items = tri.item_ids();
  mesh.triangleStyle.assign(triangles, -1);
  mesh.triangleItem.assign(triangles, 0);
  for (std::size_t t = 0; t < triangles; ++t) {
    if (t < materialIds.size()) mesh.triangleStyle[t] = materialIds[t];
    if (t < items.size()) {
      mesh.triangleItem[t] = items[t];
      mesh.itemSteps.insert(items[t]);
    }
  }
  // Styles. The library's own default (grey, with warning GEO031) means "no style": it becomes none.
  const auto& styles = tri.materials();
  std::vector<int> remap(styles.size(), -1);
  for (std::size_t i = 0; i < styles.size(); ++i) {
    const auto& style = styles[i];
    if (!style) continue;
    if (style->name == "DefaultMaterial") continue;
    StyleOut out;
    out.name = style->name;
    if (style->instance) {
      out.styleStep = stepOf(style->instance);
      const std::string own = text(style->instance, "Name");
      if (!own.empty()) out.name = own;
    }
    const auto& c = style->get_color();
    const auto channel = [](double v) { return static_cast<float>(std::isfinite(v) ? std::clamp(v, 0.0, 1.0) : 0.7); };
    out.color = {channel(c.r()), channel(c.g()), channel(c.b())};
    out.opacity = style->has_transparency() ? static_cast<float>(std::clamp(1.0 - style->transparency, 0.0, 1.0)) : 1.0f;
    remap[i] = static_cast<int>(mesh.styles.size());
    mesh.styles.push_back(std::move(out));
  }
  for (auto& style : mesh.triangleStyle)
    style = (style >= 0 && static_cast<std::size_t>(style) < remap.size()) ? remap[static_cast<std::size_t>(style)] : -1;

  // Feature edges.
  const auto& edges = tri.edges();
  const auto& edgeItems = tri.edges_item_ids();
  std::set<int> tessellatedItems;
  for (const int item : mesh.itemSteps)
    if (tessellatedKind(file.instance_by_id(item)) == "tessellated") tessellatedItems.insert(item);
  mesh.tessellatedOnly = !tessellatedItems.empty() && tessellatedItems.size() == mesh.itemSteps.size();
  for (std::size_t e = 0; e + 1 < edges.size(); e += 2) {
    const int item = (e / 2 < edgeItems.size()) ? edgeItems[e / 2] : 0;
    if (tessellatedItems.contains(item)) continue;
    if (edges[e] < 0 || edges[e + 1] < 0) continue;
    mesh.featureEdges.push_back(static_cast<std::uint32_t>(edges[e]));
    mesh.featureEdges.push_back(static_cast<std::uint32_t>(edges[e + 1]));
  }
  if (!tessellatedItems.empty()) {
    // Edges of a tessellated item: its triangles are arbitrary subdivisions, so an edge is visible
    // where the surface creases (or ends), not where the triangulation happens to put one.
    struct Use {
      std::uint32_t triangle;
    };
    std::map<std::pair<std::uint32_t, std::uint32_t>, std::vector<std::uint32_t>> uses;
    for (std::size_t t = 0; t < triangles; ++t) {
      if (!tessellatedItems.contains(mesh.triangleItem[t])) continue;
      for (int k = 0; k < 3; ++k) {
        auto a = mesh.triangles[3 * t + static_cast<std::size_t>(k)];
        auto b = mesh.triangles[3 * t + static_cast<std::size_t>((k + 1) % 3)];
        if (a > b) std::swap(a, b);
        uses[{a, b}].push_back(static_cast<std::uint32_t>(t));
      }
    }
    const auto normal = [&](std::uint32_t t) {
      const double* a = &mesh.vertices[3 * mesh.triangles[3 * t]];
      const double* b = &mesh.vertices[3 * mesh.triangles[3 * t + 1]];
      const double* c = &mesh.vertices[3 * mesh.triangles[3 * t + 2]];
      std::array<double, 3> n{(b[1] - a[1]) * (c[2] - a[2]) - (b[2] - a[2]) * (c[1] - a[1]),
                              (b[2] - a[2]) * (c[0] - a[0]) - (b[0] - a[0]) * (c[2] - a[2]),
                              (b[0] - a[0]) * (c[1] - a[1]) - (b[1] - a[1]) * (c[0] - a[0])};
      const double length = std::sqrt(n[0] * n[0] + n[1] * n[1] + n[2] * n[2]);
      if (length > 0)
        for (auto& v : n) v /= length;
      return n;
    };
    const double cosCrease = std::cos(creaseDegrees * 3.14159265358979323846 / 180.0);
    for (const auto& [edge, list] : uses) {
      bool feature = list.size() != 2;
      if (!feature) {
        const auto n0 = normal(list[0]), n1 = normal(list[1]);
        feature = (n0[0] * n1[0] + n0[1] * n1[1] + n0[2] * n1[2]) < cosCrease;
      }
      if (feature) {
        mesh.featureEdges.push_back(edge.first);
        mesh.featureEdges.push_back(edge.second);
      }
    }
  }

  // Per-face colours.
  std::vector<std::optional<std::array<float, 3>>> triangleColours(triangles);
  bool anyColour = false;
  for (const int item : mesh.itemSteps) {
    if (colours.unsupported(item)) {
      report.loss("indexedColourMapNotApplied");
      continue;
    }
    const ColourMap* map = colours.forItem(item, file);
    if (!map) continue;
    std::size_t matched = 0, total = 0;
    for (std::size_t t = 0; t < triangles; ++t) {
      if (mesh.triangleItem[t] != item) continue;
      ++total;
      const double* a = &mesh.vertices[3 * mesh.triangles[3 * t]];
      const double* b = &mesh.vertices[3 * mesh.triangles[3 * t + 1]];
      const double* c = &mesh.vertices[3 * mesh.triangles[3 * t + 2]];
      const auto found = map->byTriangle.find(keyOf(quantize(a[0], a[1], a[2]), quantize(b[0], b[1], b[2]), quantize(c[0], c[1], c[2])));
      if (found == map->byTriangle.end()) continue;
      triangleColours[t] = found->second;
      anyColour = true;
      ++matched;
    }
    if (matched < total) report.notice("faceColorsIncomplete");
  }
  if (anyColour) mesh.colors = std::move(triangleColours);
  return mesh;
}

void injectFault(const std::string& fault, const std::vector<int>& suspects) {
  if (fault.empty()) return;
  const auto colon = fault.find(':');
  if (colon == std::string::npos) return;
  const int step = std::atoi(fault.c_str() + colon + 1);
  if (std::find(suspects.begin(), suspects.end(), step) == suspects.end()) return;
  const std::string kind = fault.substr(0, colon);
  if (kind == "crash") {
    std::signal(SIGSEGV, SIG_DFL);
    std::raise(SIGSEGV);
  } else if (kind == "abort") {
    std::abort();
  } else if (kind == "hang") {
    for (;;) std::this_thread::sleep_for(std::chrono::seconds(60));
  } else if (kind == "alloc") {
    std::vector<std::unique_ptr<char[]>> hoard;
    for (;;) {
      hoard.emplace_back(new char[64 * 1024 * 1024]);
      std::fill_n(hoard.back().get(), 64 * 1024 * 1024, 1);
    }
  }
}

struct RunInputs {
  ifcopenshell::file& file;
  const Model& model;
  const std::set<int>& expected;
  const GeometryOptions& options;
  Report& report;
  Progress& progress;
  ColourMaps& colours;
};

// Finds the task that stopped a run on one thread. The iterator skips tasks that yield nothing, so the products named
// by the last progress line may be a task that did no harm. From options.diagnoseFrom on, ranges of tasks are tried
// in forked children (a copy of this process: the parsed file is shared copy-on-write) with an alarm of
// options.diagnoseSeconds, in growing ranges while they come back and halving ones when they do not, until a single
// task does not come back (it crashes, runs out of memory or runs past the alarm): its products are `culprits`.
// Tasks before the culprit all yielded nothing (the run had passed every task that did), so a child that produces an
// element has gone past the culprit's place: the range is halved, and a single task that produces one ends the search
// with nothing found.
enum class ChildOutcome { Silent, Yielded, Failed };
constexpr int childYielded = 42;  // not 1: a sanitizer's own exit code on an error is 1

template <typename Filters>
void diagnoseTasks(RunInputs& in, const std::vector<std::vector<int>>& tasks, bool disableOpenings, bool usePriority,
                   const Filters& buildFilters, std::set<int>& culprits) {
  const GeometryOptions& options = in.options;
  const auto tryRange = [&](std::size_t from, std::size_t to) {
    std::set<int> include;
    std::vector<int> products;
    for (std::size_t i = from; i < to; ++i)
      for (const int step : tasks[i]) {
        include.insert(step);
        products.push_back(step);
      }
    std::fflush(stdout);
    std::fflush(stderr);
    const pid_t parent = ::getpid();
    const pid_t child = ::fork();
    if (child < 0) return ChildOutcome::Yielded;  // cannot try: end the search
    if (child == 0) {
      ::prctl(PR_SET_PDEATHSIG, SIGKILL);
      if (::getppid() != parent) ::_exit(3);
      int code = 3;
      try {
        ::alarm(static_cast<unsigned>(std::max(1, options.diagnoseSeconds)));
        injectFault(options.testFault, products);
        ifcopenshell::logger log;
        log.output_format(ifcopenshell::logger::FMT_INMEMORY);
        log.verbosity(ifcopenshell::logger::LOG_ERROR);
        geom::settings settings = makeSettings(options, disableOpenings, usePriority, false);
        geom::iterator iterator(geom::kernels::construct(&in.file, options.kernel, settings, log), settings, &in.file,
                                buildFilters(&include), 1, log);
        code = 0;
        if (iterator.initialize()) {
          do {
            if (dynamic_cast<const geom::triangulation_element*>(iterator.get().get())) ::_exit(childYielded);
          } while (iterator.next());
        }
      } catch (...) {
        code = 3;
      }
      ::_exit(code);
    }
    int status = 0;
    while (::waitpid(child, &status, 0) < 0 && errno == EINTR) {
    }
    if (WIFEXITED(status) && WEXITSTATUS(status) == 0) return ChildOutcome::Silent;
    if (WIFEXITED(status) && WEXITSTATUS(status) == childYielded) return ChildOutcome::Yielded;
    return ChildOutcome::Failed;
  };
  std::size_t index = static_cast<std::size_t>(options.diagnoseFrom);
  std::size_t span = 1, limit = tasks.size();
  while (index < tasks.size()) {
    span = std::max<std::size_t>(1, std::min({span, limit, tasks.size() - index}));
    const auto outcome = tryRange(index, index + span);
    if (outcome == ChildOutcome::Silent) {
      index += span;
      span *= 2;
      continue;
    }
    if (span == 1) {
      if (outcome == ChildOutcome::Failed) culprits.insert(tasks[index].begin(), tasks[index].end());
      return;
    }
    limit = span / 2;  // failed (or went past): the first half next, and no larger range from here on
    span = limit;
  }
}

// One pass of the iterator over the products matching `include`/`exclude`; elements are added to
// `result` (replacing earlier ones of the same product when `replace`).
void iteratePass(RunInputs& in, GeometryResult& result, bool disableOpenings, const std::set<int>* onlyHosts,
                 std::vector<LibraryMessage>& messages, bool allowFallbackContext) {
  const GeometryOptions& options = in.options;
  const int threads = onlyHosts ? 1 : std::max(1, std::min(options.threads, 4));
  const bool single = threads == 1;
  bool usePriority = true;
  std::set<int> culpritSteps;  // found by the diagnosis (single thread only)
  for (int attempt = 0; attempt < 2; ++attempt) {
    ifcopenshell::logger log;
    log.output_format(ifcopenshell::logger::FMT_INMEMORY);
    log.verbosity(ifcopenshell::logger::LOG_NOTICE);
    geom::settings settings = makeSettings(options, disableOpenings, usePriority, false);
    const auto buildFilters = [&](const std::set<int>* include) {
      std::vector<geom::filter_function> list;
      std::set<std::string> accepted{"IfcElement", "IfcGeographicElement", "IfcSpatialElement",
                                     "IfcSpatialStructureElement", "IfcSpatialZone"};
      list.emplace_back(geom::entity_filter(true, false, accepted));
      std::set<std::string> rejected{"IfcVirtualElement"};
      if (options.skipSpaces) {
        rejected.insert("IfcSpace");
        rejected.insert("IfcSpatialZone");
      }
      list.emplace_back(geom::entity_filter(false, false, rejected));
      std::set<int> excluded(options.excludeSteps.begin(), options.excludeSteps.end());
      excluded.insert(options.skipSteps.begin(), options.skipSteps.end());
      excluded.insert(culpritSteps.begin(), culpritSteps.end());
      if (!excluded.empty()) list.emplace_back(geom::instance_id_filter(false, false, excluded));
      if (onlyHosts) list.emplace_back(geom::instance_id_filter(true, false, *onlyHosts));
      if (include) list.emplace_back(geom::instance_id_filter(true, false, *include));
      return list;
    };
    std::vector<geom::filter_function> filters = buildFilters(nullptr);
    if (single) {
      // The first task is converted inside initialize(), so the products it covers are found out
      // beforehand from a listing-only iterator (its first element is deferred, and so is the geometry):
      // if the kernel crashes on that task, the application knows what to leave out.
      const auto listTasks = [&]() {
        std::vector<std::vector<int>> tasks;
        try {
          ifcopenshell::logger listing;
          listing.output_format(ifcopenshell::logger::FMT_INMEMORY);
          geom::settings deferred = makeSettings(options, disableOpenings, usePriority, true);
          geom::iterator lister(geom::kernels::construct(&in.file, options.kernel, deferred, listing), deferred, &in.file,
                                filters, 1, listing);
          if (lister.initialize())
            for (const auto& group : lister.get_task_products()) {
              tasks.emplace_back();
              for (const auto& product : group) tasks.back().push_back(stepOf(product));
            }
        } catch (const std::exception&) {
        }
        return tasks;
      };
      auto tasks = listTasks();
      if (!tasks.empty() && !onlyHosts && options.diagnoseFrom >= 0) {
        // The helper stopped on a task that yielded nothing before it: the tasks from there on are tried one at a
        // time, each in a child process, and the first that does not come back is left out.
        diagnoseTasks(in, tasks, disableOpenings, usePriority, buildFilters, culpritSteps);
        if (!culpritSteps.empty()) {
          filters = buildFilters(nullptr);
          result.excluded.insert(culpritSteps.begin(), culpritSteps.end());
          in.progress.excluded(std::vector<int>(culpritSteps.begin(), culpritSteps.end()));
          tasks = listTasks();
        }
      }
      // The uncut pass over the hosts has its own list of tasks: it says so (-1) and is never diagnosed.
      if (onlyHosts) in.progress.at(-1);
      if (!tasks.empty()) {
        if (!onlyHosts) in.progress.at(0);
        in.progress.next(tasks.front());
        injectFault(options.testFault, tasks.front());
      }
    }
    std::unique_ptr<geom::iterator> iterator;
    bool initialized = false;
    try {
      iterator = std::make_unique<geom::iterator>(geom::kernels::construct(&in.file, options.kernel, settings, log),
                                                  settings, &in.file, filters, threads, log);
      initialized = iterator->initialize();
    } catch (const std::exception& problem) {
      result.refusalCode = "geometryUnavailable";
      result.refusalMessage = std::string("The geometry kernel could not start: ") + clip(problem.what(), 160);
      return;
    }
    result.initialized = result.initialized || initialized;
    for (const auto& message : log.log_messages()) {
      LibraryMessage m;
      m.severity = severityName(message.severity);
      m.code = std::string(message.code);
      m.text = message.message;
      m.instance = stepFromReference(message.instance);
      m.product = stepFromReference(message.product);
      messages.push_back(std::move(m));
    }
    const auto drainLog = [&](std::size_t from) {
      const auto& list = log.log_messages();
      for (std::size_t i = from; i < list.size(); ++i) {
        LibraryMessage m;
        m.severity = severityName(list[i].severity);
        m.code = std::string(list[i].code);
        m.text = list[i].message;
        m.instance = stepFromReference(list[i].instance);
        m.product = stepFromReference(list[i].product);
        messages.push_back(std::move(m));
      }
    };
    std::size_t drained = log.log_messages().size();
    if (!initialized) {
      // The query matched no representation: retry once with the library's default selection (it
      // also takes only the Body representation of the Model context) and say so.
      if (usePriority && allowFallbackContext && attempt == 0) {
        usePriority = false;
        result.usedFallbackContext = true;
        in.report.notice("contextFallback");
        continue;
      }
      return;
    }
    // Units: the library reads them only when the file has exactly one context; otherwise it logs
    // GEO308/GEO309 (or GEO312 when there is no length unit) and returns the file's own numbers as
    // if they were metres (R097.a note F1). The numbers are then scaled by what the file declares.
    result.libraryUnit = iterator->unit_magnitude();
    result.libraryUnitName = iterator->unit_name();
    bool untrusted = false;
    for (const auto& m : messages)
      if (m.code == "GEO308" || m.code == "GEO309" || m.code == "GEO312") untrusted = true;
    result.unitsTrusted = !untrusted;
    const double mine = in.model.units.length.perUnit;
    double scale = 1;
    if (untrusted) {
      if (!(result.libraryUnit > 0)) {
        result.refusalCode = "unitsDisagree";
        result.refusalMessage = "The geometry library did not report a length unit.";
        return;
      }
      scale = mine / result.libraryUnit;
    } else if (!(std::abs(result.libraryUnit - mine) <= 1e-6 * mine)) {
      result.refusalCode = "unitsDisagree";
      result.refusalMessage = "The geometry library reads the length unit as " + std::to_string(result.libraryUnit) +
                              " m but the project declares " + std::to_string(mine) + " m.";
      return;
    }
    // A factor written to nine digits (0.025400005 for an inch) is snapped to the exact one by the units reader
    // within 1e-6; the library's own reading of it differs from that by the same fraction, so its metres are
    // scaled by the ratio and every length of the package agrees with the snapped unit.
    if (!untrusted && result.libraryUnit != mine && result.libraryUnit > 0) scale = mine / result.libraryUnit;
    result.meshScale = scale;
    const auto productsByTask = iterator->get_task_products();
    result.tasks += productsByTask.size();
    std::map<int, std::size_t> taskOf;
    for (std::size_t i = 0; i < productsByTask.size(); ++i)
      for (const auto& product : productsByTask[i]) taskOf[stepOf(product)] = i;
    in.progress.tasks(productsByTask.size());
    result.threads = threads;
    in.progress.threads(threads);

    // The triangulations stay alive until the pass ends: a freed one's address could be handed to the
    // next, and the next would then be taken for a shared geometry.
    std::vector<std::shared_ptr<const geom::triangulation>> alive;
    const auto handle = [&](const geom::triangulation_element& element, std::map<const void*, int>& meshOf) {
      const int step = static_cast<int>(element.id());
      if (result.elements.contains(step) && !onlyHosts) return;
      const auto pointer = element.geometry_pointer();
      if (!pointer || pointer->faces().size() < 3) {
        result.failedProducts[step] = "noFaces";
        return;
      }
      alive.push_back(pointer);
      const auto key = static_cast<const void*>(pointer.get());
      int meshIndex;
      if (const auto found = meshOf.find(key); found != meshOf.end()) {
        meshIndex = found->second;
      } else {
        meshIndex = static_cast<int>(result.meshes.size());
        result.meshes.push_back(buildMesh(*pointer, scale, in.file, in.colours, in.report));
        meshOf.emplace(key, meshIndex);
      }
      ElementOut out;
      out.step = step;
      out.mesh = meshIndex;
      if (const auto& placement = element.transformation().data()) {
        const auto& m = placement->ccomponents();
        for (int row = 0; row < 4; ++row)
          for (int column = 0; column < 4; ++column)
            out.world[static_cast<std::size_t>(row * 4 + column)] = m(row, column) * (column == 3 && row < 3 ? scale : 1.0);
      } else {
        out.world = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
      }
      out.rerunUncut = onlyHosts != nullptr;
      result.elements[step] = out;
      ++result.emitted;
    };

    std::map<const void*, int> meshOf;
    std::size_t handled = 0;
    try {
      if (single) {
        std::vector<int> suspects;
        const auto suspectsAfter = [&](long long lastTask) {
          std::vector<int> list;
          const std::size_t next = static_cast<std::size_t>(lastTask + 1);
          if (next < productsByTask.size())
            for (const auto& product : productsByTask[next]) list.push_back(stepOf(product));
          return list;
        };
        long long lastTask = -1;
        bool more = true;
        while (more) {
          const std::unique_ptr<geom::element> element = iterator->get();
          if (const auto* triangles = dynamic_cast<const geom::triangulation_element*>(element.get())) {
            handle(*triangles, meshOf);
            if (const auto task = taskOf.find(static_cast<int>(element->id())); task != taskOf.end())
              lastTask = std::max<long long>(lastTask, static_cast<long long>(task->second));
          }
          ++handled;
          if (handled % 64 == 0) in.progress.done(handled);
          suspects = suspectsAfter(lastTask);
          if (!onlyHosts) in.progress.at(lastTask + 1);
          in.progress.next(suspects);
          injectFault(options.testFault, suspects);
          more = static_cast<bool>(iterator->next());
        }
      } else {
        bool more = true;
        while (more) {
          const std::unique_ptr<geom::element> element = iterator->get();
          if (const auto* triangles = dynamic_cast<const geom::triangulation_element*>(element.get())) {
            injectFault(options.testFault, {static_cast<int>(element->id())});
            handle(*triangles, meshOf);
          }
          ++handled;
          if (handled % 64 == 0) in.progress.done(handled);
          more = static_cast<bool>(iterator->next());
        }
      }
    } catch (const std::bad_alloc&) {
      throw;  // resource exhaustion ends the run: the application restarts it without the element at fault
    }
    in.progress.done(handled);
    drainLog(drained);
    return;
  }
}

}  // namespace

GeometryResult runGeometry(ifcopenshell::file& file, const Model& model, const std::set<int>& expected,
                           const GeometryOptions& options, Report& report, Progress& progress) {
  GeometryResult result;
  result.kernel = options.kernel;
  result.ran = true;
  result.excluded.insert(options.excludeSteps.begin(), options.excludeSteps.end());
  ColourMaps colours(file, model.units.length.perUnit);
  RunInputs in{file, model, expected, options, report, progress, colours};
  std::vector<LibraryMessage> messages;
  iteratePass(in, result, false, nullptr, messages, true);
  if (!result.refusalCode.empty()) return result;

  // Hosts and their openings, as the messages show them.
  std::set<int> withVoids;
  for (const auto& [host, openings] : model.openingsOf) withVoids.insert(host);
  for (const auto& m : messages) {
    if (m.severity != "notice") {
      report.kernelMessage(messageCategory(m.code), m.code, m.severity, m.text);
    }
    if (m.code == "GEO033" && m.severity == "error" && m.instance) result.failedHosts.insert(m.instance);
    if (m.code == "GEO403" && m.product) result.skippedHosts.insert(m.product);
    // GEO191: the host is not a manifold solid (an open tessellated host is cut into wrong geometry with only a
    // warning, R097.a note F5). GEO188 says the kernel repaired it by sewing and the cut is the right one.
    if (m.code == "GEO191" && withVoids.contains(m.product)) result.untrustedHosts.insert(m.product);
  }

  // Hosts whose cut is not trusted are converted again without subtracting their openings (decision 5),
  // and so are the openings of hosts the kernel failed on or skipped: the library does not emit them
  // when the subtraction fails, and they are imported (hidden) so that the user can act on them.
  {
    std::set<int> hosts;
    for (const int host : result.untrustedHosts)
      if (result.elements.contains(host)) hosts.insert(host);
    for (const auto* group : {&result.failedHosts, &result.skippedHosts, &result.untrustedHosts})
      for (const int host : *group) {
        const auto found = model.openingsOf.find(host);
        if (found == model.openingsOf.end()) continue;
        for (const int opening : found->second)
          if (!result.elements.contains(opening) && expected.contains(opening) && !result.excluded.contains(opening))
            hosts.insert(opening);
      }
    if (!hosts.empty()) {
      std::vector<LibraryMessage> second;
      GeometryResult again;
      again.initialized = false;
      iteratePass(in, again, true, &hosts, second, false);
      std::map<int, int> copied;
      for (const auto& [step, element] : again.elements) {
        // The second pass has its own mesh list; copy each mesh over once.
        ElementOut moved = element;
        const auto known = copied.find(element.mesh);
        if (known != copied.end()) {
          moved.mesh = known->second;
        } else {
          moved.mesh = static_cast<int>(result.meshes.size());
          copied[element.mesh] = moved.mesh;
          result.meshes.push_back(again.meshes[static_cast<std::size_t>(element.mesh)]);
        }
        moved.rerunUncut = true;
        result.elements[step] = moved;
      }
      if (!again.refusalCode.empty()) report.notice("uncutRerunFailed");
      for (const auto& m : second)
        if (m.severity == "error") report.kernelMessage("openingSubtraction", m.code, m.severity, m.text);
    }
  }

  // Which products came out empty, and why: the mapping-stage messages name a representation item
  // or a representation, not the product (R097.a note F6).
  std::map<int, int> productOfStep;
  for (const int product : expected) {
    const Inst instance = file.instance_by_id(product);
    productOfStep[product] = product;
    const Inst shape = ref(instance, "Representation");
    if (!shape) continue;
    productOfStep[stepOf(shape)] = product;
    for (const Inst& representation : refs(shape, "Representations")) {
      productOfStep[stepOf(representation)] = product;
      for (const Inst& item : refs(representation, "Items")) productOfStep[stepOf(item)] = product;
    }
  }
  std::map<int, std::string> reasonOf;
  for (const auto& m : messages) {
    if (m.severity != "error") continue;
    int product = m.product;
    if (!product) {
      if (const auto found = productOfStep.find(m.instance); found != productOfStep.end()) product = found->second;
    }
    if (product && !reasonOf.contains(product)) reasonOf[product] = m.code;
  }
  for (const int product : expected) {
    if (result.elements.contains(product) || result.excluded.contains(product)) continue;
    if (std::find(options.skipSteps.begin(), options.skipSteps.end(), product) != options.skipSteps.end()) continue;
    const auto reason = reasonOf.find(product);
    if (!result.failedProducts.contains(product))
      result.failedProducts[product] = reason != reasonOf.end() ? "kernelError:" + reason->second : "noGeometry";
  }
  result.messages = std::move(messages);
  return result;
}

}  // namespace sketchyup::ifc
