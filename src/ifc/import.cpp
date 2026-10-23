#include "ifc/import.hpp"

#include <sys/resource.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <functional>
#include <map>
#include <set>
#include <sstream>

#include "ifc/access.hpp"
#include "ifc/account.hpp"
#include "ifc/geometry.hpp"
#include "ifc/georeference.hpp"
#include "ifc/json_out.hpp"
#include "ifc/model.hpp"
#include "ifc/package.hpp"
#include "ifc/placement.hpp"
#include "ifc/progress.hpp"
#include "ifc/report.hpp"
#include "ifc/units.hpp"
#include "ifcgeom/conversion_settings.h"
#include "ifcparse/file.h"
#include "ifcparse/logger.h"
#include "ifcparse/schema.h"
#include "ifcparse/spf_header.h"
#include "ifcparse/schemas/Header_section_schema.h"
#include "sketchyup/version.hpp"

#ifndef SKETCHYUP_IFC_PIN_COMMIT_STRING
#define SKETCHYUP_IFC_PIN_COMMIT_STRING "unknown"
#endif

namespace sketchyup::ifc {
namespace {

constexpr double nativeCoordinateLimit = 1e6;
// The node bound is ImportOptions::maxNodes (40,000, the application's materialized limit): more nodes are reported as
// not imported here instead of making a manifest the decoder would refuse.
constexpr std::size_t maxNodeProperties = 4096;
constexpr std::size_t maxPropertyBytes = 8u << 20;  // text of all properties together
constexpr std::size_t maxPackageMaterials = 1024;  // the application keeps 1,024 (DocumentLimits)
constexpr std::size_t maxNameBytes = 4096;
constexpr std::size_t maxTextBytes = 32768;

struct Refusal {
  std::string code, message;
};

// Valid UTF-8, or the text read as ISO 8859-1 (ADR 0171 section 3.4): a name is never blanked.
bool validUtf8(const std::string& s) {
  for (std::size_t i = 0; i < s.size();) {
    const unsigned char c = static_cast<unsigned char>(s[i]);
    std::size_t length = 1;
    if (c < 0x80) {
      length = 1;
    } else if (c >= 0xC2 && c <= 0xDF) {
      length = 2;
    } else if (c >= 0xE0 && c <= 0xEF) {
      length = 3;
    } else if (c >= 0xF0 && c <= 0xF4) {
      length = 4;
    } else {
      return false;
    }
    if (i + length > s.size()) return false;
    for (std::size_t k = 1; k < length; ++k)
      if ((static_cast<unsigned char>(s[i + k]) & 0xC0) != 0x80) return false;
    if (length == 3) {
      const unsigned char d = static_cast<unsigned char>(s[i + 1]);
      if ((c == 0xE0 && d < 0xA0) || (c == 0xED && d >= 0xA0)) return false;
    }
    if (length == 4) {
      const unsigned char d = static_cast<unsigned char>(s[i + 1]);
      if ((c == 0xF0 && d < 0x90) || (c == 0xF4 && d >= 0x90)) return false;
    }
    i += length;
  }
  return true;
}

class Texts {
public:
  explicit Texts(Report& report) : report_(report) {}
  std::string operator()(const std::string& s) {
    if (validUtf8(s)) return s;
    std::string out;
    for (const unsigned char c : s) {
      if (c < 0x80) {
        out.push_back(static_cast<char>(c));
      } else {
        out.push_back(static_cast<char>(0xC0 | (c >> 6)));
        out.push_back(static_cast<char>(0x80 | (c & 0x3F)));
      }
    }
    report_.notice("latin1Fallback");
    return out;
  }

private:
  Report& report_;
};

struct ProductInfo {
  Inst inst;
  bool hasRepresentation{false};
  bool hasBody{false};
  bool accepted{false};  // a class the import converts
  bool skipped{false};   // a space left out on request
};

bool acceptedClass(const Inst& product) {
  if (isA(product, "IfcVirtualElement")) return false;
  return isA(product, "IfcElement") || isA(product, "IfcGeographicElement") || isA(product, "IfcSpatialElement") ||
         isA(product, "IfcSpatialStructureElement") || isA(product, "IfcSpatialZone");
}

bool spatialClass(const Inst& e) {
  return isA(e, "IfcSpatialElement") || isA(e, "IfcSpatialStructureElement") || isA(e, "IfcSpatialZone");
}

std::string roleOf(const Inst& e, bool hasKids) {
  if (isA(e, "IfcProject")) return "project";
  if (isA(e, "IfcSite")) return "site";
  if (isA(e, "IfcBuilding")) return "building";
  if (isA(e, "IfcBuildingStorey")) return "storey";
  if (isA(e, "IfcSpace") || isA(e, "IfcSpatialZone")) return "space";
  if (isA(e, "IfcOpeningElement")) return "opening";
  if (spatialClass(e)) return "spatial";
  return hasKids ? "assembly" : "element";
}

bool coreSpatial(const std::string& role) {
  return role == "project" || role == "site" || role == "building" || role == "storey";
}

bool tierOne(const std::string& cls) {
  return cls == "IfcWall" || cls == "IfcWallStandardCase" || cls == "IfcSlab" || cls == "IfcDoor" || cls == "IfcWindow" ||
         cls == "IfcBuildingElementProxy";
}

double degrees(const std::vector<double>& parts) {
  // IfcCompoundPlaneAngleMeasure: degrees, minutes, seconds, millionths of a second.
  if (parts.size() < 3) return 0;
  double sign = 1;
  for (const double p : parts)
    if (p != 0) {
      sign = p < 0 ? -1 : 1;
      break;
    }
  double value = std::abs(parts[0]) + std::abs(parts[1]) / 60.0 + std::abs(parts[2]) / 3600.0;
  if (parts.size() > 3) value += std::abs(parts[3]) / 3600.0e6;
  return sign * value;
}

struct Entry {
  int step{};
  Inst inst;
  std::string role;
  Mat world{};
  bool emitted{false};
  int meshOfGeometry{-1};
  bool opening{false};
  bool forceHidden{false};
  std::string conversion;
  int parentEntry{-1};
};

struct Context {
  ifcopenshell::file& file;
  Model model;
  Report report;
  Texts texts;
  ImportOptions options;
  Progress& progress;
  Accountant accountant;
  GeometryResult geometry;
  std::set<int> expected;
  std::map<int, ProductInfo> products;
  std::vector<Entry> entries;
  std::set<int> placed;      // steps already given a node
  std::set<int> onStack;
  std::set<int> usedForSupport;
  std::set<std::string> guids;
  std::set<int> groupsRecorded;
  Context(ifcopenshell::file& f, Progress& p, const ImportOptions& o)
      : file(f), texts(report), options(o), progress(p), accountant(f) {}
};

void collectProducts(Context& ctx) {
  std::vector<Inst> all;
  try {
    all = ctx.file.instances_by_type("IfcProduct");
  } catch (...) {
  }
  std::sort(all.begin(), all.end(), [](const Inst& a, const Inst& b) { return stepOf(a) < stepOf(b); });
  for (const Inst& product : all) {
    ProductInfo info;
    info.inst = product;
    const Inst shape = ref(product, "Representation");
    info.hasRepresentation = static_cast<bool>(shape);
    for (const Inst& representation : refs(shape, "Representations"))
      if (text(representation, "RepresentationIdentifier") == "Body") info.hasBody = true;
    info.accepted = acceptedClass(product);
    info.skipped = ctx.options.skipSpaces && (isA(product, "IfcSpace") || isA(product, "IfcSpatialZone"));
    ctx.products[stepOf(product)] = info;
    if (info.accepted && info.hasBody && !info.skipped) ctx.expected.insert(stepOf(product));
  }
}

// The Model-context placement for any product (a project has none).
std::optional<Mat> worldOf(Context& ctx, const Inst& entity, std::string& reason) {
  if (isA(entity, "IfcProject")) {
    reason.clear();
    return identityMatrix();
  }
  return placementWorld(ref(entity, "ObjectPlacement"), ctx.model.units.length.perUnit, reason);
}

// Whether the entity (a spatial element, assembly or element) deserves a node: it is emitted, a
// core spatial element, or something under it does.
bool worthy(Context& ctx, int step, std::map<int, int>& memo, std::set<int>& visiting) {
  if (const auto found = memo.find(step); found != memo.end()) return found->second != 0;
  if (!visiting.insert(step).second) return false;
  const Inst inst = ctx.file.instance_by_id(step);
  bool result = false;
  if (isA(inst, "IfcProject")) {
    result = true;
  } else if (isEntity(inst) && (isA(inst, "IfcObjectDefinition") || isA(inst, "IfcProduct"))) {
    const std::string role = roleOf(inst, false);
    if (coreSpatial(role)) {
      result = true;
    } else if (ctx.geometry.elements.contains(step)) {
      result = true;
    }
    if (!result) {
      for (const auto* list : {&ctx.model.aggregated, &ctx.model.contained}) {
        const auto found = list->find(step);
        if (found == list->end()) continue;
        for (const int child : found->second)
          if (worthy(ctx, child, memo, visiting)) {
            result = true;
            break;
          }
        if (result) break;
      }
    }
  }
  visiting.erase(step);
  memo[step] = result ? 1 : 0;
  return result;
}

void locate(Context& ctx, const std::string& kind, int step, const std::string& message) {
  Location place;
  place.kind = kind;
  place.step = step;
  place.globalId = text(ctx.file.instance_by_id(step), "GlobalId");
  place.message = message;
  ctx.report.location(std::move(place));
}

struct Tree {
  std::map<int, int> worthMemo;
};

// Whether the library subtracted the host's openings (a host the kernel failed on, skipped for its
// voids, or whose cut is not trusted, was converted uncut).
bool openingsApplied(const Context& ctx, int host) {
  return ctx.geometry.elements.contains(host) && !ctx.geometry.failedHosts.contains(host) &&
         !ctx.geometry.skippedHosts.contains(host) && !ctx.geometry.untrustedHosts.contains(host);
}

// A host's openings that were not subtracted follow it as hidden siblings (decision 5).
void placeUnappliedOpenings(Context& ctx, int host, int parent) {
  const auto openings = ctx.model.openingsOf.find(host);
  if (openings == ctx.model.openingsOf.end() || !ctx.placed.contains(host)) return;
  for (const int opening : openings->second) {
    if (ctx.placed.contains(opening) || !ctx.geometry.elements.contains(opening)) continue;
    if (openingsApplied(ctx, host)) continue;
    std::string reason;
    const Inst openingInst = ctx.file.instance_by_id(opening);
    if (!worldOf(ctx, openingInst, reason)) continue;
    Entry entry;
    entry.step = opening;
    entry.inst = openingInst;
    entry.world = ctx.geometry.elements.at(opening).world;
    entry.emitted = true;
    entry.meshOfGeometry = ctx.geometry.elements.at(opening).mesh;
    entry.role = "opening";
    entry.opening = true;
    entry.forceHidden = true;
    entry.parentEntry = parent;
    ctx.entries.push_back(std::move(entry));
    ctx.placed.insert(opening);
  }
}

// Appends the entity (and, recursively, what it holds) to `entries`. `parent` is the nearest kept
// ancestor entry (-1 for the root).
void visit(Context& ctx, Tree& tree, int step, int parent, int depth) {
  if (ctx.onStack.contains(step)) {
    // The step is its own ancestor (a space that contains a space that contains it, for example): the edge that
    // closes the loop is dropped, both ends keep the place they were first given.
    ++ctx.model.cycles;
    locate(ctx, "containmentCycle", step, "the relationship that makes this element its own ancestor was not followed");
    ctx.report.notice("containmentCycle");
    return;
  }
  if (ctx.placed.contains(step)) return;
  if (depth > 128) return;
  const Inst inst = ctx.file.instance_by_id(step);
  if (!isEntity(inst) || !isA(inst, "IfcObjectDefinition")) return;
  std::set<int> visiting;
  if (!worthy(ctx, step, tree.worthMemo, visiting)) return;
  ctx.onStack.insert(step);

  const bool emitted = ctx.geometry.elements.contains(step);
  std::string reason;
  const auto mine = worldOf(ctx, inst, reason);
  bool keep = mine.has_value();
  if (!keep) {
    // A node whose placement cannot be composed is not imported (nothing goes to the origin); what it
    // holds keeps its true position under the nearest kept ancestor.
    ctx.report.notImported(className(inst), reason.empty() ? "placement" : reason);
    locate(ctx, "notImported", step, "placement cannot be composed: " + reason);
    ctx.report.notice("groupDropped");
  }
  int self = parent;
  if (keep) {
    Entry entry;
    entry.step = step;
    entry.inst = inst;
    entry.world = *mine;
    entry.emitted = emitted;
    if (emitted) {
      const auto& element = ctx.geometry.elements.at(step);
      // The library's matrix is what the mesh was built against; keep it, and say if ours differs.
      double difference = 0;
      for (std::size_t i = 0; i < 16; ++i) difference = std::max(difference, std::abs(element.world[i] - (*mine)[i]));
      if (difference > 1e-6) ctx.report.notice("placementMismatch");
      entry.world = element.world;
      entry.meshOfGeometry = element.mesh;
    }
    const auto aggregates = ctx.model.aggregated.find(step);
    const bool hasKids = aggregates != ctx.model.aggregated.end() && !aggregates->second.empty();
    entry.role = roleOf(inst, hasKids);
    entry.parentEntry = parent;
    ctx.entries.push_back(std::move(entry));
    self = static_cast<int>(ctx.entries.size()) - 1;
  }
  ctx.placed.insert(step);
  // Children: aggregated parts first, then contained elements, in file order.
  for (const auto* list : {&ctx.model.aggregated, &ctx.model.contained}) {
    const auto found = list->find(step);
    if (found == list->end()) continue;
    for (const int child : found->second) {
      visit(ctx, tree, child, self, depth + 1);
      placeUnappliedOpenings(ctx, child, self);
    }
  }
  ctx.onStack.erase(step);
}

std::string jsonObject(const std::function<void(JsonOut&)>& body) {
  JsonOut json;
  json.beginObject();
  body(json);
  json.endObject();
  return json.str();
}

void writeCounts(JsonOut& json, const std::map<std::string, long long>& counts) {
  json.beginObject();
  for (const auto& [key, count] : counts) json.field(key, count);
  json.endObject();
}

std::string messageSample(const std::string& text) { return clip(text, 160); }

}  // namespace

int runImport(const ImportOptions& options) {
  Progress progress(true);
  const auto refuse = [&](const std::string& code, const std::string& message) {
    JsonOut json;
    json.beginObject().field("status", "refused").field("code", code).field("message", clip(message, 240)).endObject();
    progress.line("R " + json.str());
    return 3;
  };

  progress.phase("parse");
  ifcopenshell::logger parseLog;
  parseLog.output_format(ifcopenshell::logger::FMT_INMEMORY);
  parseLog.verbosity(ifcopenshell::logger::LOG_WARNING);
  std::unique_ptr<ifcopenshell::file> filePointer;
  try {
    filePointer = std::make_unique<ifcopenshell::file>(options.input, ifcopenshell::FT_AUTODETECT, true, parseLog);
  } catch (const std::exception& problem) {
    return refuse("parse", std::string("The file could not be read: ") + problem.what());
  }
  ifcopenshell::file& file = *filePointer;
  if (!file.good()) return refuse("parse", "The file could not be parsed as IFC STEP.");
  const std::string schema = file.schema() ? file.schema()->name() : std::string();
  if (schema != "IFC2X3" && schema != "IFC4" && schema != "IFC4X3_ADD2")
    return refuse("unsupportedSchema", "The schema " + clip(schema, 40) + " is not imported.");

  Context ctx(file, progress, options);
  ctx.model.schema = schema;
  std::size_t entityCount = 0;
  for (auto it = file.begin(); it != file.end(); ++it) ++entityCount;
  if (entityCount > options.maxEntities)
    return refuse("entityLimit", "The file has " + std::to_string(entityCount) + " instances; the limit is " +
                                     std::to_string(options.maxEntities) + ".");
  if (!parseLog.log_messages().empty()) ctx.report.notice("parseWarnings", static_cast<long long>(parseLog.log_messages().size()));
  for (const auto& message : parseLog.log_messages())
    ctx.report.kernelMessage("parse", std::string(message.code), message.severity == ifcopenshell::logger::LOG_ERROR ? "error" : "warning",
                             messageSample(message.message));

  progress.phase("units");
  std::vector<Inst> projects;
  try {
    projects = file.instances_by_type("IfcProject");
  } catch (...) {
  }
  if (projects.size() != 1)
    return refuse("projectCount", projects.empty() ? "The file has no IfcProject." : "The file has more than one IfcProject.");
  ctx.model.project = projects.front();
  const auto unitsOutcome = readUnits(ctx.model.project, options.lengthUnitOverride);
  if (!unitsOutcome.refusalCode.empty()) return refuse(unitsOutcome.refusalCode, unitsOutcome.refusalMessage);
  ctx.model.units = unitsOutcome.units;
  for (const auto& code : ctx.model.units.notices) ctx.report.notice(code);

  // Georeferencing (3.6): read before any geometry, so that a world coordinate system we cannot apply refuses cheaply.
  const Georeference geo = readGeoreference(file, ctx.model.project, ctx.model.units, options.gridAligned, schema, ctx.report,
                                            [&](const std::string& s) { return ctx.texts(s); });
  if (!geo.refusalCode.empty()) return refuse(geo.refusalCode, geo.refusalMessage);

  progress.phase("model");
  ctx.model.build(file, ctx.report);
  collectProducts(ctx);

  // Geometry.
  progress.phase("geometry");
  GeometryOptions geometryOptions;
  geometryOptions.kernel = options.kernel;
  geometryOptions.threads = options.threads;
  geometryOptions.linearDeflection = options.linearDeflectionMetres;
  geometryOptions.angularDeflection = options.angularDeflectionRadians;
  geometryOptions.maxVoidsPerElement = options.maxVoidsPerElement;
  geometryOptions.skipSpaces = options.skipSpaces;
  geometryOptions.excludeSteps = options.excludeSteps;
  geometryOptions.skipSteps.assign(ctx.model.cyclic.begin(), ctx.model.cyclic.end());
  geometryOptions.testFault = options.testFault;
  geometryOptions.diagnoseFrom = options.diagnoseFrom;
  geometryOptions.diagnoseSeconds = options.diagnoseSeconds;
  if (!ctx.expected.empty()) {
    ctx.geometry = runGeometry(file, ctx.model, ctx.expected, geometryOptions, ctx.report, progress);
    if (ctx.geometry.refusalCode == "geometryUnavailable" && options.kernel != "passthrough") {
      // The OpenCASCADE kernel cannot load: tessellated items can still be read by the pass-through kernel.
      ctx.report.notice("kernelUnavailable");
      geometryOptions.kernel = "passthrough";
      ctx.geometry = runGeometry(file, ctx.model, ctx.expected, geometryOptions, ctx.report, progress);
    }
    if (!ctx.geometry.refusalCode.empty()) return refuse(ctx.geometry.refusalCode, ctx.geometry.refusalMessage);
    if (!ctx.geometry.unitsTrusted) ctx.report.notice("unitsRescaled");
  } else {
    ctx.geometry.kernel = options.kernel;
  }
  if (options.dryRun) {
    JsonOut json;
    json.beginObject().field("status", "ok").field("dryRun", true).field("elements", ctx.geometry.elements.size()).endObject();
    progress.line("R " + json.str());
    return 0;
  }

  progress.phase("assemble");
  // Excluded products: reported, never imported.
  for (const int step : ctx.geometry.excluded) {
    const Inst inst = file.instance_by_id(step);
    if (!isEntity(inst)) continue;
    ctx.report.notImported(className(inst), "excluded");
    locate(ctx, "excluded", step, "left out after the worker stopped on it");
  }

  // The tree.
  Tree tree;
  visit(ctx, tree, stepOf(ctx.model.project), -1, 0);
  // Products and spatial elements that no relationship reaches go under the project, in step order.
  const int root = ctx.entries.empty() ? -1 : 0;
  {
    std::vector<int> loose;
    for (const auto& [step, element] : ctx.geometry.elements)
      if (!ctx.placed.contains(step)) loose.push_back(step);
    for (const int step : loose) {
      const Inst inst = file.instance_by_id(step);
      if (ctx.placed.contains(step)) continue;
      const bool related = ctx.model.aggregateParent.contains(step) || ctx.model.container.contains(step);
      if (related && !isA(inst, "IfcOpeningElement")) {
        const bool cyclic = ctx.model.cyclic.contains(step);
        ctx.report.notImported(className(inst), cyclic ? "cycle" : "unreachable");
        locate(ctx, "notImported", step, cyclic ? "part of a cycle of aggregation" : "not reachable from the project");
        continue;
      }
      if (isA(inst, "IfcOpeningElement")) {
        // With its host imported, the opening is either consumed or was placed beside it.
        const auto host = ctx.model.hostOf.find(step);
        if (host != ctx.model.hostOf.end() && ctx.placed.contains(host->second)) continue;
      }
      std::string reason;
      const auto mine = worldOf(ctx, inst, reason);
      if (!mine) {
        ctx.report.notImported(className(inst), reason);
        locate(ctx, "notImported", step, "placement cannot be composed: " + reason);
        continue;
      }
      Entry entry;
      entry.step = step;
      entry.inst = inst;
      entry.world = ctx.geometry.elements.at(step).world;
      entry.emitted = true;
      entry.meshOfGeometry = ctx.geometry.elements.at(step).mesh;
      entry.role = roleOf(inst, false);
      if (entry.role == "opening") {
        entry.opening = true;
        entry.forceHidden = true;
      }
      entry.parentEntry = root;
      if (!isA(inst, "IfcOpeningElement")) ctx.report.notice("uncontainedElements");
      ctx.entries.push_back(std::move(entry));
      ctx.placed.insert(step);
      placeUnappliedOpenings(ctx, step, root);
    }
  }
  if (ctx.entries.empty()) return refuse("nothingToImport", "The file holds no spatial structure or geometry to import.");
  if (geo.rotate) {
    // Grid mode (decision 6b): the model is turned by +theta about Z (the conversion's rotation), so that +X is east. Every world
    // matrix is premultiplied by it; a node's matrix relative to its parent is unchanged and the roots carry the rotation.
    const Mat turn{geo.cosine, -geo.sine, 0, 0, geo.sine, geo.cosine, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
    for (auto& entry : ctx.entries) entry.world = multiply(turn, entry.world);
  }

  std::set<int> droppedByBounds;  // not imported because of the package's bounds (already reported)
  // The package's own bounds (6.4): 100,000 triangles for one element and 1,000,000 in all (a shared mesh counts
  // once). What does not fit is not imported and is reported, with what hangs below it.
  {
    std::set<int> counted;
    long long total = 0;
    std::vector<char> gone(ctx.entries.size(), 0);
    std::vector<int> newIndex(ctx.entries.size(), -1);
    std::vector<Entry> kept;
    // A mesh whose local coordinates the decoder would refuse (not finite, or beyond 1e6 m) is decided once.
    std::map<int, bool> meshUsable;
    const auto usable = [&](int geometryIndex) {
      const auto found = meshUsable.find(geometryIndex);
      if (found != meshUsable.end()) return found->second;
      bool good = true;
      for (const double v : ctx.geometry.meshes[static_cast<std::size_t>(geometryIndex)].vertices)
        if (!std::isfinite(v) || std::abs(v) > nativeCoordinateLimit) {
          good = false;
          break;
        }
      return meshUsable[geometryIndex] = good;
    };
    for (std::size_t i = 0; i < ctx.entries.size(); ++i) {
      Entry& e = ctx.entries[i];
      std::string why;
      bool finite = true;
      for (const double v : e.world)
        if (!std::isfinite(v)) finite = false;
      if (finite && e.parentEntry >= 0 && !gone[static_cast<std::size_t>(e.parentEntry)]) {
        // The node's transform relative to its parent must be finite too (a near-singular parent can overflow it).
        if (const auto inverse = inverseAffine(ctx.entries[static_cast<std::size_t>(e.parentEntry)].world))
          for (const double v : multiply(*inverse, e.world))
            if (!std::isfinite(v)) finite = false;
      }
      if (e.parentEntry >= 0 && gone[static_cast<std::size_t>(e.parentEntry)]) {
        why = "parentNotImported";
      } else if (!finite) {
        why = "placementNotFinite";
      } else if (kept.size() >= options.maxNodes) {
        why = "nodeLimit";
      } else if (e.meshOfGeometry >= 0 && !usable(e.meshOfGeometry)) {
        why = "meshCoordinates";
      } else if (e.meshOfGeometry >= 0) {
        const long long triangles = static_cast<long long>(ctx.geometry.meshes[static_cast<std::size_t>(e.meshOfGeometry)].triangles.size() / 3);
        const bool seen = counted.contains(e.meshOfGeometry);
        if (triangles > 100000) why = "meshTriangleLimit";
        else if (!seen && total + triangles > 1000000) why = "triangleBudget";
        else if (!seen) {
          counted.insert(e.meshOfGeometry);
          total += triangles;
        }
      }
      if (!why.empty()) {
        gone[i] = 1;
        ctx.placed.erase(e.step);
        droppedByBounds.insert(e.step);
        ctx.report.notImported(className(e.inst), why);
        locate(ctx, "notImported", e.step, why);
        continue;
      }
      newIndex[i] = static_cast<int>(kept.size());
      if (e.parentEntry >= 0) e.parentEntry = newIndex[static_cast<std::size_t>(e.parentEntry)];
      kept.push_back(std::move(e));
    }
    ctx.entries = std::move(kept);
  }
  if (ctx.entries.empty()) return refuse("nothingToImport", "Nothing of the file fits the package's bounds.");

  // Openings: counts, and the host's conversion note.
  long long declared = 0, applied = 0, notApplied = 0;
  std::map<int, std::string> conversion;
  for (const auto& [host, openings] : ctx.model.openingsOf) {
    bool anyApplied = false, anyNot = false;
    for (const int opening : openings) {
      ++declared;
      const bool hostThere = ctx.geometry.elements.contains(host) && ctx.placed.contains(host);
      const bool openingThere = ctx.geometry.elements.contains(opening);
      std::string why;
      if (!hostThere) why = ctx.geometry.excluded.contains(host) ? "hostExcluded" : "hostNotImported";
      else if (ctx.geometry.failedHosts.contains(host)) why = "kernelError";
      else if (ctx.geometry.skippedHosts.contains(host)) why = "tooManyVoids";
      else if (ctx.geometry.untrustedHosts.contains(host)) why = "untrustedCut";
      else if (!openingThere) why = "openingHasNoGeometry";
      if (why.empty()) {
        ++applied;
        anyApplied = true;
        ctx.accountant.supporting(opening);
        ctx.accountant.claimTree(file.instance_by_id(opening));
      } else {
        ++notApplied;
        anyNot = true;
        ctx.report.count("openingsNotApplied", why);
        if (hostThere) {
          Location place;
          place.kind = "openingNotApplied";
          place.step = opening;
          place.globalId = text(file.instance_by_id(host), "GlobalId");
          place.message = "host #" + std::to_string(host) + ": " + why;
          ctx.report.location(std::move(place));
        }
      }
    }
    conversion[host] = anyApplied && anyNot ? "tessellated, voids partly subtracted"
                       : anyApplied          ? "tessellated, voids subtracted"
                                             : "tessellated, voids not subtracted";
  }
  // Products that were expected but did not come out, and the ones that are not converted at all.
  for (const auto& [step, info] : ctx.products) {
    const std::string cls = className(info.inst);
    if (ctx.placed.contains(step) || droppedByBounds.contains(step)) continue;
    // An element with no representation at all is reported; a spatial element or assembly without one is just a group.
    const bool element = isA(info.inst, "IfcElement") || isA(info.inst, "IfcGeographicElement");
    if (!info.hasRepresentation && !element) continue;
    if (ctx.geometry.excluded.contains(step)) continue;
    std::string reason;
    if (ctx.model.cyclic.contains(step)) reason = "cycle";
    else if (info.skipped) reason = "spacesSkipped";
    else if (!info.accepted) reason = "unsupportedClass";
    else if (!info.hasRepresentation) reason = "noRepresentation";
    else if (!info.hasBody) reason = "noBodyRepresentation";
    else if (const auto failed = ctx.geometry.failedProducts.find(step); failed != ctx.geometry.failedProducts.end()) reason = failed->second;
    else if (ctx.geometry.elements.contains(step)) continue;  // placed elsewhere (counted above)
    else reason = "noGeometry";
    if (isA(info.inst, "IfcOpeningElement") && ctx.model.hostOf.contains(step) && reason == "noGeometry") reason = "openingHasNoGeometry";
    if (reason == "noGeometry" || reason.rfind("kernelError", 0) == 0) {
      // A placement the library cannot compose is the reason the element has no geometry; say so (nothing is
      // placed at the origin).
      std::string placementReason;
      if (!worldOf(ctx, info.inst, placementReason) && !placementReason.empty()) reason = placementReason;
    }
    ctx.report.notImported(cls, reason);
    locate(ctx, "notImported", step, reason);
  }

  // Materials (global), meshes (compact), nodes.
  PackageData package;
  std::map<std::tuple<std::string, std::array<float, 3>, float>, std::uint32_t> materialIndex;
  std::map<int, long long> meshRemap;
  std::map<int, std::set<std::string>> meshItemClasses;
  double extent = 0;
  long long trianglesOut = 0, sharedMeshes = 0;
  std::map<int, int> meshUses;
  for (const auto& entry : ctx.entries)
    if (entry.meshOfGeometry >= 0) ++meshUses[entry.meshOfGeometry];
  for (const auto& [index, uses] : meshUses)
    if (uses > 1) ++sharedMeshes;
  const auto globalMaterial = [&](const StyleOut& style) -> std::uint32_t {
    const auto key = std::make_tuple(style.name, style.color, style.opacity);
    const auto found = materialIndex.find(key);
    if (found != materialIndex.end()) return found->second;
    if (package.materials.size() >= maxPackageMaterials) {
      ctx.report.notice("materialsOverLimit");
      return 0xFFFFFFFFu;
    }
    PackageMaterial m;
    m.name = clip(ctx.texts(style.name), maxNameBytes);
    m.color = style.color;
    m.opacity = style.opacity;
    const auto id = static_cast<std::uint32_t>(package.materials.size());
    package.materials.push_back(std::move(m));
    materialIndex.emplace(key, id);
    return id;
  };
  const auto emitMesh = [&](int geometryIndex) -> long long {
    if (const auto found = meshRemap.find(geometryIndex); found != meshRemap.end()) return found->second;
    const MeshOut& source = ctx.geometry.meshes[static_cast<std::size_t>(geometryIndex)];
    PackageMesh mesh;
    mesh.vertices = source.vertices;
    mesh.triangles = source.triangles;
    std::vector<std::uint32_t> styleToGlobal;
    for (const auto& style : source.styles) styleToGlobal.push_back(globalMaterial(style));
    for (const int style : source.triangleStyle)
      mesh.materials.push_back(style >= 0 ? styleToGlobal[static_cast<std::size_t>(style)] : 0xFFFFFFFFu);
    mesh.edges = source.featureEdges;
    mesh.colors = source.colors;
    trianglesOut += static_cast<long long>(mesh.triangles.size() / 3);
    const long long id = static_cast<long long>(package.meshes.size());
    package.meshes.push_back(std::move(mesh));
    meshRemap[geometryIndex] = id;
    return id;
  };

  std::set<int> supportRoots;
  std::map<std::string, long long> imported, importedGeneric, byRepresentation;
  long long propertyCount = 0;
  std::size_t propertyBytes = 0;
  const double metresPerUnit = ctx.model.units.length.perUnit;
  const auto spatialSteps = [&](const std::string& role) {
    long long n = 0;
    for (const auto& e : ctx.entries)
      if (e.role == role) ++n;
    return n;
  };

  // Entries are in tree order (a parent precedes its children), so indexes are stable.
  package.nodes.reserve(ctx.entries.size());
  for (std::size_t index = 0; index < ctx.entries.size(); ++index) {
    Entry& entry = ctx.entries[index];
    const Inst& inst = entry.inst;
    PackageNode node;
    node.step = static_cast<std::uint32_t>(entry.step);
    node.className = className(inst);
    node.role = entry.role;
    node.parent = entry.parentEntry;
    std::string guid = text(inst, "GlobalId");
    node.guid = clip(ctx.texts(guid), 256);
    if (!guid.empty() && !validGlobalId(guid)) ctx.report.notice("invalidGlobalId");
    if (!guid.empty() && !ctx.guids.insert(guid).second) ctx.report.notice("duplicateGlobalId");
    std::string name = ctx.texts(text(inst, "Name"));
    bool shortened = false;
    if (name.empty()) {
      name = node.className + " " + node.guid.substr(0, 6);
    } else if (name.size() > 256) {
      name = clip(name, 256);
      shortened = true;
    }
    node.name = name;
    // Placement relative to the parent.
    Mat local = entry.world;
    if (entry.parentEntry >= 0) {
      const auto inverse = inverseAffine(ctx.entries[static_cast<std::size_t>(entry.parentEntry)].world);
      if (!inverse) {
        ctx.report.notice("parentSingular");
      } else {
        local = multiply(*inverse, entry.world);
      }
    }
    for (std::size_t i = 0; i < 16; ++i)
      if (std::abs(local[i]) < 1e-15) local[i] = 0;
    local[12] = 0;
    local[13] = 0;
    local[14] = 0;
    local[15] = 1;
    for (std::size_t i = 0; i < 16; ++i) node.transform[i] = local[i];
    extent = std::max({extent, std::abs(entry.world[3]), std::abs(entry.world[7]), std::abs(entry.world[11])});

    // Provenance.
    auto& ifc = node.provenance;
    ifc.emplace_back("ifc.Class", node.className);
    ifc.emplace_back("ifc.GlobalId", node.guid);
    ifc.emplace_back("ifc.Step", static_cast<double>(entry.step));
    if (entry.role == "project") ifc.emplace_back("ifc.Schema", schema);
    if (entry.role == "project" && geo.facts) {
      // The map conversion, read-only (3.6): whatever the result, the file's own numbers are kept on the project. They come
      // right after the identity entries, so that the 16 provenance slots never drop them.
      const MapConversionFacts& f = *geo.facts;
      ifc.emplace_back("ifc.mapConversion.eastings", f.eastings);
      ifc.emplace_back("ifc.mapConversion.northings", f.northings);
      ifc.emplace_back("ifc.mapConversion.orthogonalHeight", f.orthogonalHeight);
      ifc.emplace_back("ifc.mapConversion.rotationDegrees", f.rotationDegrees);
      if (std::isfinite(f.scale)) ifc.emplace_back("ifc.mapConversion.scale", f.scale);
      if (!f.crsName.empty()) ifc.emplace_back("ifc.mapConversion.crs", f.crsName);
    }
    if (shortened) ifc.emplace_back("ifc.Name", clip(ctx.texts(text(inst, "Name")), 2048));
    if (const auto longName = ctx.texts(text(inst, "LongName")); !longName.empty()) ifc.emplace_back("ifc.LongName", clip(longName, 2048));
    if (entry.role == "project")
      if (const auto phase = ctx.texts(text(inst, "Phase")); !phase.empty()) ifc.emplace_back("ifc.Phase", clip(phase, 2048));
    std::string predefined = keyword(inst, "PredefinedType");
    const std::string objectType = ctx.texts(text(inst, "ObjectType"));
    if (!predefined.empty()) ifc.emplace_back("ifc.PredefinedType", predefined);
    if (!objectType.empty()) ifc.emplace_back("ifc.ObjectType", clip(objectType, 2048));
    if (isA(inst, "IfcDoor") || isA(inst, "IfcWindow")) {
      // A door or window is its geometry; its overall size and kind are kept as properties (lengths in metres).
      if (const auto width = number(inst, "OverallWidth")) ifc.emplace_back("ifc.OverallWidth", *width * metresPerUnit);
      if (const auto height = number(inst, "OverallHeight")) ifc.emplace_back("ifc.OverallHeight", *height * metresPerUnit);
      for (const char* attribute : {"PartitioningType", "OperationType"}) {
        const std::string kind = keyword(inst, attribute);
        if (!kind.empty()) ifc.emplace_back(std::string("ifc.") + attribute, kind);
      }
    }
    Inst type;
    if (const auto typeStep = ctx.model.typeOf.find(entry.step); typeStep != ctx.model.typeOf.end()) {
      type = file.instance_by_id(typeStep->second);
      std::string typeName = ctx.texts(text(type, "Name"));
      if (typeName.empty()) typeName = className(type);
      ifc.emplace_back("ifc.Type", clip(typeName, 2048));
      ctx.accountant.claimTree(type, {"RepresentationMaps", "HasPropertySets"});
    }
    if (entry.role == "storey")
      if (const auto elevation = number(inst, "Elevation")) ifc.emplace_back("ifc.Elevation", *elevation * metresPerUnit);
    if (entry.role == "site") {
      const auto latitude = numbers(inst, "RefLatitude");
      const auto longitude = numbers(inst, "RefLongitude");
      if (latitude.size() >= 3) ifc.emplace_back("ifc.RefLatitude", degrees(latitude));
      if (longitude.size() >= 3) ifc.emplace_back("ifc.RefLongitude", degrees(longitude));
      if (const auto height = number(inst, "RefElevation")) ifc.emplace_back("ifc.RefElevation", *height * metresPerUnit);
    }
    if (entry.role == "project") {
      for (const Inst& context : refs(inst, "RepresentationContexts")) {
        const auto north = numbers(context, "TrueNorth");
        if (north.size() >= 2) {
          const double angle = std::atan2(-north[0], north[1]) * 180.0 / 3.14159265358979323846;
          ifc.emplace_back("ifc.TrueNorth", angle);
          if (std::abs(angle) > 1e-9) ctx.report.notice("trueNorthRecorded");
          break;
        }
      }
    }

    // Geometry-related provenance.
    if (entry.meshOfGeometry >= 0) {
      const MeshOut& source = ctx.geometry.meshes[static_cast<std::size_t>(entry.meshOfGeometry)];
      std::set<std::string> classes;
      for (const int item : source.itemSteps) {
        const Inst itemInst = file.instance_by_id(item);
        if (isA(itemInst, "IfcRepresentationItem")) classes.insert(className(itemInst));
      }
      std::string representationType;
      for (const Inst& representation : refs(ref(inst, "Representation"), "Representations"))
        if (text(representation, "RepresentationIdentifier") == "Body") {
          representationType = text(representation, "RepresentationType");
          break;
        }
      std::string items;
      for (const auto& c : classes) {
        if (!items.empty()) items += ", ";
        items += c;
        ++byRepresentation[c];
      }
      ifc.emplace_back("ifc.Representation", clip(representationType + (items.empty() ? "" : ": " + items), 2048));
      std::string how = "tessellated";
      if (const auto found = conversion.find(entry.step); found != conversion.end()) how = found->second;
      if (entry.opening) how = "tessellated, not subtracted from its host";
      if (ctx.geometry.elements.at(entry.step).rerunUncut) how += " (cut not trusted: converted uncut)";
      ifc.emplace_back("ifc.Conversion", how);
      node.mesh = emitMesh(entry.meshOfGeometry);
      ctx.accountant.converted(entry.step);
      ctx.accountant.claimTree(inst, {"HasPropertySets"});
    } else {
      ctx.accountant.converted(entry.step);
      ctx.accountant.claimTree(inst, {"HasPropertySets"});
    }
    const auto layers = describeLayers(ctx.model, inst, supportRoots);
    if (!layers.names.empty()) ifc.emplace_back("ifc.Layer", ctx.texts(layers.names));
    if (layers.hidden) {
      entry.forceHidden = true;
      ctx.report.notice("layerHidden");
    }
    const auto material = describeMaterial(ctx.model, inst, type, ctx.report, supportRoots);
    if (!material.name.empty()) ifc.emplace_back("ifc.Material", ctx.texts(material.name));
    if (!material.layers.empty()) ifc.emplace_back("ifc.MaterialLayers", ctx.texts(material.layers));
    if (material.layered) {
      ctx.report.loss("materialLayering");
      ctx.report.notice("materialLayeringDescriptive");
    }
    const auto groups = describeGroups(ctx.model, inst, supportRoots);
    if (!groups.empty()) {
      ifc.emplace_back("ifc.Groups", ctx.texts(groups));
      if (const auto found = ctx.model.groupsOf.find(entry.step); found != ctx.model.groupsOf.end())
        for (const Inst& group : found->second) ctx.groupsRecorded.insert(stepOf(group));
    }
    std::string classification, classificationUri;
    describeClassification(ctx.model, inst, classification, classificationUri, supportRoots);
    if (!classification.empty()) ifc.emplace_back("ifc.Classification", ctx.texts(classification));
    if (!classificationUri.empty()) ifc.emplace_back("ifc.ClassificationUri", ctx.texts(classificationUri));
    node.hidden = entry.forceHidden;

    // Properties and quantities (occurrence, then type).
    std::vector<ReadProperty> read;
    readProperties(ctx.model, inst, type, read, ctx.report, supportRoots);
    for (auto& p : read) {
      if (p.kind == ReadProperty::Kind::Real && !std::isfinite(p.real)) {
        ctx.report.notice("propertyNotFinite");
        continue;
      }
      PackageNode::Property property;
      property.set = clip(ctx.texts(p.set), maxNameBytes);
      property.name = clip(ctx.texts(p.name), maxNameBytes);
      property.origin = p.origin;
      if (p.kind == ReadProperty::Kind::Bool) property.value = p.boolean;
      else if (p.kind == ReadProperty::Kind::Real) property.value = p.real;
      else {
        const std::string whole = ctx.texts(p.text);
        property.value = clip(whole, maxTextBytes);
        if (std::get<std::string>(property.value).size() < whole.size()) ctx.report.notice("propertyTextClipped");
      }
      const std::size_t bytes = property.set.size() + property.name.size() +
                                (std::holds_alternative<std::string>(property.value) ? std::get<std::string>(property.value).size() : 8) + 32;
      if (node.properties.size() >= maxNodeProperties || propertyBytes + bytes > maxPropertyBytes) {
        ctx.report.notice("propertiesOmitted");
        continue;
      }
      propertyBytes += bytes;
      node.properties.push_back(std::move(property));
      ++propertyCount;
    }
    // Relationships that named this object, and the opening that was applied to it, are consumed.
    if (const auto found = ctx.model.relationshipsOf.find(entry.step); found != ctx.model.relationshipsOf.end())
      for (const int rel : found->second) {
        ctx.accountant.supporting(rel);
        ctx.accountant.claimTree(file.instance_by_id(rel), {"RelatedObjects", "RelatedElements", "RelatingObject",
                                                             "RelatingStructure", "RelatingBuildingElement", "RelatedOpeningElement",
                                                             "RelatingType", "RelatingPropertyDefinition", "RelatingMaterial",
                                                             "RelatingClassification", "RelatingGroup"});
      }
    // Counts for the report.
    if (entry.role == "element" || entry.role == "assembly") {
      if (tierOne(node.className)) ++imported[node.className];
      else ++importedGeneric[node.className];
    } else if (entry.role == "opening") {
      ++importedGeneric[node.className];
    }
    // Whatever the decoder bounds: text values, and numbers that are finite.
    for (auto it = node.provenance.begin(); it != node.provenance.end();) {
      if (auto* text = std::get_if<std::string>(&it->second)) {
        *text = clip(*text, 8192);
      } else if (auto* number = std::get_if<double>(&it->second); number && !std::isfinite(*number)) {
        ctx.report.notice("propertyNotFinite");
        it = node.provenance.erase(it);
        continue;
      }
      ++it;
    }
    package.nodes.push_back(std::move(node));
  }
  for (const int step : supportRoots) {
    ctx.accountant.supporting(step);
  }
  for (const int step : supportRoots) {
    const Inst root = file.instance_by_id(step);
    ctx.accountant.claimTree(root, isA(root, "IfcPresentationLayerAssignment") ? std::set<std::string>{"AssignedItems"}
                                                                                : std::set<std::string>{});
  }
  for (const int step : ctx.groupsRecorded) ctx.accountant.converted(step);
  // Groups, zones and systems keep their own owner history and the like.
  for (const int step : ctx.groupsRecorded) ctx.accountant.claimTree(file.instance_by_id(step), {"IsGroupedBy"});

  if (extent > nativeCoordinateLimit) {
    return refuse("farFromOrigin", "The model lies " + std::to_string(static_cast<long long>(extent)) +
                                       " m from the origin; coordinates beyond 1,000,000 m are not imported.");
  }
  // Vertex extents in world space.
  for (const auto& entry : ctx.entries) {
    if (entry.meshOfGeometry < 0) continue;
    const MeshOut& source = ctx.geometry.meshes[static_cast<std::size_t>(entry.meshOfGeometry)];
    for (std::size_t v = 0; v + 2 < source.vertices.size(); v += 3) {
      for (int axis = 0; axis < 3; ++axis) {
        const std::size_t row = static_cast<std::size_t>(axis) * 4;
        const double w = entry.world[row] * source.vertices[v] + entry.world[row + 1] * source.vertices[v + 1] +
                         entry.world[row + 2] * source.vertices[v + 2] + entry.world[row + 3];
        extent = std::max(extent, std::abs(w));
      }
    }
  }
  if (extent > nativeCoordinateLimit)
    return refuse("farFromOrigin", "The model extends " + std::to_string(static_cast<long long>(extent)) +
                                       " m from the origin; coordinates beyond 1,000,000 m are not imported.");

  progress.phase("account");
  // The map conversion and its CRS were read into the report and the project's properties: supporting data.
  for (const Inst& conversion : geo.consumed) {
    ctx.accountant.supporting(stepOf(conversion));
    ctx.accountant.claimTree(conversion);
  }
  const Accounting accounting = ctx.accountant.finish();
  for (const auto& [cls, count] : accounting.omittedByClass) ctx.report.omitted(cls, count);

  progress.phase("write");
  // Header facts.
  std::string viewDefinition, application;
  try {
    for (const auto& line : file.header().file_description().description()) {
      const auto at = line.find("ViewDefinition");
      if (at != std::string::npos) viewDefinition = clip(line.substr(at), 200);
    }
    application = clip(file.header().file_name().originating_system(), 200);
    if (application.empty()) application = clip(file.header().file_name().preprocessor_version(), 200);
  } catch (...) {
  }

  package.engineJson = jsonObject([&](JsonOut& j) {
    j.field("library", "IfcOpenShell");
    j.field("version", IFCOPENSHELL_VERSION);
    j.field("commit", std::string(SKETCHYUP_IFC_PIN_COMMIT_STRING).substr(0, 8));
    j.field("kernel", ctx.geometry.kernel.empty() ? options.kernel : ctx.geometry.kernel);
    j.field("linearDeflectionMetres", options.linearDeflectionMetres);
    j.field("angularDeflectionRadians", options.angularDeflectionRadians);
    j.field("creaseDegrees", 30.0);
    j.field("threads", ctx.geometry.threads);
    j.field("maxVoidsPerElement", options.maxVoidsPerElement);
    j.field("contextFallback", ctx.geometry.usedFallbackContext);
    j.field("unitsTrusted", ctx.geometry.unitsTrusted);
    j.field("meshScale", ctx.geometry.meshScale);
    j.field("georeferenceMode", options.gridAligned ? "grid" : "keepAxes");
    j.field("completed", true);
  });
  package.sourceJson = jsonObject([&](JsonOut& j) {
    j.field("schema", schema);
    j.field("viewDefinition", ctx.texts(viewDefinition));
    j.field("application", ctx.texts(application));
    j.field("entities", accounting.entities);
  });
  package.unitsJson = jsonObject([&](JsonOut& j) {
    const Units& u = ctx.model.units;
    j.field("lengthUnit", u.length.name);
    j.field("metresPerUnit", u.length.perUnit);
    j.field("metresPerUnitExact", u.length.exact);
    j.field("override", u.overridden);
    j.field("planeAngle", u.planeAngle);
    j.field("areaUnit", u.areaDefaulted ? std::string("SQUARE_METRE (default)") : u.area.name);
    j.field("areaSquareMetresPerUnit", u.area.perUnit);
    j.field("volumeUnit", u.volumeDefaulted ? std::string("CUBIC_METRE (default)") : u.volume.name);
    j.field("volumeCubicMetresPerUnit", u.volume.perUnit);
  });
  package.reportJson = jsonObject([&](JsonOut& j) {
    j.key("accounting").beginObject();
    j.field("entities", accounting.entities).field("converted", accounting.converted);
    j.field("supporting", accounting.supporting).field("omitted", accounting.omitted);
    j.endObject();
    {
      // At most 128 kinds, then "other".
      std::map<std::string, long long> kinds;
      long long other = 0;
      std::vector<std::pair<std::string, long long>> sorted(accounting.omittedByClass.begin(), accounting.omittedByClass.end());
      for (std::size_t i = 0; i < sorted.size(); ++i) {
        if (i < 128) kinds[sorted[i].first] = sorted[i].second;
        else other += sorted[i].second;
      }
      if (other) kinds["other"] = other;
      j.key("omittedEntities");
      writeCounts(j, kinds);
    }
    j.key("georeference").raw(geo.json);
    j.key("spatial").beginObject();
    j.field("projects", spatialSteps("project")).field("sites", spatialSteps("site"));
    j.field("buildings", spatialSteps("building")).field("storeys", spatialSteps("storey"));
    j.field("spaces", spatialSteps("space"));
    j.endObject();
    j.key("elements").beginObject();
    j.key("imported");
    writeCounts(j, imported);
    j.key("importedGeneric");
    writeCounts(j, importedGeneric);
    j.key("notImported").beginArray();
    for (const auto& [key, count] : ctx.report.notImportedClasses())
      j.beginObject().field("class", key.first).field("reason", key.second).field("count", count).endObject();
    j.endArray();
    j.endObject();
    j.key("geometry").beginObject();
    j.field("bodies", std::count_if(package.nodes.begin(), package.nodes.end(), [](const PackageNode& n) { return n.mesh >= 0; }));
    j.field("meshes", package.meshes.size()).field("sharedMeshes", sharedMeshes).field("triangles", trianglesOut);
    j.key("byRepresentation");
    writeCounts(j, byRepresentation);
    j.field("openingsDeclared", declared).field("openingsApplied", applied).field("openingsNotApplied", notApplied);
    j.field("extentMetres", extent);
    j.field("tasks", ctx.geometry.tasks);
    j.endObject();
    j.key("properties").beginObject();
    j.field("emitted", propertyCount);
    for (const auto& [group, entries] : ctx.report.counts()) {
      j.key(group);
      writeCounts(j, entries);
    }
    j.endObject();
    j.key("materials").beginObject();
    j.field("materials", package.materials.size());
    j.endObject();
    j.key("losses");
    writeCounts(j, ctx.report.losses());
    j.key("kernelMessages").beginArray();
    for (const auto& m : ctx.report.kernelMessages())
      j.beginObject()
          .field("category", m.category)
          .field("code", m.code)
          .field("severity", m.severity)
          .field("count", m.count)
          .field("message", ctx.texts(m.sample))
          .endObject();
    j.endArray();
    j.key("notices").beginArray();
    for (const auto& [code, count] : ctx.report.notices()) j.beginObject().field("code", code).field("count", count).endObject();
    j.endArray();
    j.key("locations").beginArray();
    for (const auto& place : ctx.report.locations())
      j.beginObject()
          .field("kind", place.kind)
          .field("step", place.step)
          .field("globalId", ctx.texts(place.globalId))
          .field("message", ctx.texts(place.message))
          .endObject();
    j.endArray();
    j.key("locationTotals").beginObject();
    for (const auto& [kind, total] : ctx.report.locationTotals()) j.field(kind, total);
    j.endObject();
    j.key("unitsUnconverted").beginArray();
    for (const auto& [unit, count] : ctx.report.unconverted()) j.beginObject().field("unit", unit).field("count", count).endObject();
    j.endArray();
    j.key("excludedSteps").beginArray();
    for (const int step : ctx.geometry.excluded) j.value(step);
    j.endArray();
    j.key("precision").beginArray();
    for (const Inst& context : refs(ctx.model.project, "RepresentationContexts"))
      if (const auto precision = number(context, "Precision"))
        j.beginObject().field("contextType", text(context, "ContextType")).field("precision", *precision)
            .field("metres", *precision * ctx.model.units.length.perUnit).endObject();
    j.endArray();
    j.key("duplicates").beginObject();
    j.field("multipleParents", ctx.model.multipleParents).field("cycles", ctx.model.cycles);
    j.endObject();
  });

  std::string problem;
  const std::string bytes = serializePackage(package, problem);
  if (bytes.empty()) return refuse("packageTooLarge", problem);
  {
    std::ofstream out(options.output, std::ios::binary | std::ios::trunc);
    out.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    out.flush();
    if (!out) return refuse("writeFailed", "The package could not be written.");
  }
  JsonOut json;
  json.beginObject().field("status", "ok").field("bytes", bytes.size()).field("nodes", package.nodes.size()).endObject();
  progress.line("R " + json.str());
  return 0;
}

}  // namespace sketchyup::ifc
