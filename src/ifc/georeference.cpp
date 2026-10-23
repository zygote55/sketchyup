#include "ifc/georeference.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstdio>

#include "ifc/json_out.hpp"
#include "ifc/placement.hpp"

namespace sketchyup::ifc {
namespace {

constexpr double pi = 3.14159265358979323846;
// The document's site coordinate bound (core/site_origin.hpp, siteCoordinateLimit).
constexpr double siteLimit = 1e8;

struct Reason {
  std::string code, message;
};

std::string upper(std::string s) {
  for (char& c : s) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
  return s;
}

// A CRS label: valid UTF-8, no control characters, at most 64 bytes (the document's bound).
std::string label(const std::string& raw, const std::function<std::string(const std::string&)>& texts) {
  std::string s = texts(raw);
  for (char& c : s)
    if (static_cast<unsigned char>(c) < 0x20 || c == 0x7F) c = ' ';
  return clip(s, 64);
}

Inst rootContext(Inst context) {
  for (int depth = 0; depth < 8 && isA(context, "IfcGeometricRepresentationSubContext"); ++depth) {
    const Inst parent = ref(context, "ParentContext");
    if (!parent) break;
    context = parent;
  }
  return context;
}

std::vector<Inst> instancesOf(ifcopenshell::file& file, const char* className) {
  std::vector<Inst> found;
  try {
    found = file.instances_by_type(className);
  } catch (...) {
    // The schema has no such class (IFC2X3 has no map conversion).
  }
  std::sort(found.begin(), found.end(), [](const Inst& a, const Inst& b) { return stepOf(a) < stepOf(b); });
  return found;
}

bool isIdentity(const Mat& m) {
  const Mat identity = identityMatrix();
  for (std::size_t i = 0; i < 16; ++i)
    if (!(std::abs(m[i] - identity[i]) <= 1e-9)) return false;
  return true;
}

std::string degreesText(double degrees) {
  char buffer[40];
  std::snprintf(buffer, sizeof buffer, "%.9g", degrees);
  return buffer;
}

}  // namespace

Georeference readGeoreference(ifcopenshell::file& file, const Inst& project, const Units& units, bool gridAligned,
                              const std::string& schema, Report& report,
                              const std::function<std::string(const std::string&)>& texts) {
  Georeference out;
  const double metresPerUnit = units.length.perUnit;
  const char* const modeName = gridAligned ? "grid" : "keepAxes";

  // The Model contexts of the project (the contexts the geometry is in).
  std::vector<Inst> modelContexts;
  for (const Inst& context : refs(project, "RepresentationContexts"))
    if (isA(context, "IfcGeometricRepresentationContext") && !isA(context, "IfcGeometricRepresentationSubContext") &&
        upper(text(context, "ContextType")) == "MODEL")
      modelContexts.push_back(context);

  // A world coordinate system that is not the identity is neither applied nor ignored: applying it and
  // ignoring it would both be a guess (3.6), so the file is refused, naming the context.
  for (const Inst& context : modelContexts) {
    const Inst wcs = ref(context, "WorldCoordinateSystem");
    if (!wcs) continue;
    const auto matrix = axisPlacementMatrix(wcs, metresPerUnit);
    if (!matrix || !isIdentity(*matrix)) {
      out.refusalCode = "worldCoordinateSystem";
      out.refusalMessage = "The Model context #" + std::to_string(stepOf(context)) + " (" +
                           clip(texts(text(context, "ContextIdentifier")), 32) +
                           ") has a world coordinate system that is " + (matrix ? "not the identity" : "unreadable") +
                           "; it is neither applied nor ignored, so the file is not imported.";
      return out;
    }
  }

  const auto conversions = instancesOf(file, "IfcMapConversion");
  long long otherOperations = 0;
  for (const Inst& operation : instancesOf(file, "IfcCoordinateOperation"))
    if (!isA(operation, "IfcMapConversion")) ++otherOperations;
  // The ones that convert a Model context of this project.
  std::vector<Inst> onModel;
  for (const Inst& conversion : conversions) {
    const Inst source = rootContext(ref(conversion, "SourceCRS"));
    for (const Inst& context : modelContexts)
      if (stepOf(context) == stepOf(source)) {
        onModel.push_back(conversion);
        break;
      }
  }
  // ePSet_MapConversion and ePSet_ProjectedCRS: a community convention, not standardised; read as properties.
  long long epsets = 0;
  for (const Inst& set : instancesOf(file, "IfcPropertySet")) {
    const std::string name = text(set, "Name");
    if (name == "ePSet_MapConversion" || name == "ePSet_ProjectedCRS") ++epsets;
  }
  if (epsets > 0) report.notice("georeferenceEpsets", epsets);

  std::vector<Reason> reasons;
  std::string result = "none";
  std::optional<MapConversionFacts> facts;
  bool mapUnitGiven = true;
  std::string mapUnitName = "METRE";
  double mapUnitMetres = 1;
  std::string crsDescription, crsDatum, crsProjection, crsZone;
  std::array<double, 3> factors{1, 1, 1};
  bool haveFactors = false;
  bool coreOk = false, axisOk = true;

  if (modelContexts.size() > 1)
    reasons.push_back({"severalModelContexts", "The project has " + std::to_string(modelContexts.size()) +
                                                   " Model contexts; one map conversion cannot describe them."});
  if (conversions.empty()) {
    // Nothing to convert: IFC2X3 has no IfcMapConversion, and a file without one is simply not georeferenced.
    if (modelContexts.size() > 1) reasons.clear();
  } else if (onModel.empty()) {
    reasons.push_back({"notOnModelContext", "The file's map conversion does not convert the project's Model context."});
  } else if (onModel.size() > 1) {
    reasons.push_back({"severalConversions", "The Model context has " + std::to_string(onModel.size()) +
                                                 " map conversions; which one applies is not decided here."});
  } else {
    const Inst conversion = onModel.front();
    const Inst crs = ref(conversion, "TargetCRS");
    MapConversionFacts f;
    f.step = stepOf(conversion);
    const auto eastings = number(conversion, "Eastings");
    const auto northings = number(conversion, "Northings");
    const auto height = number(conversion, "OrthogonalHeight");
    const auto abscissa = number(conversion, "XAxisAbscissa");
    const auto ordinate = number(conversion, "XAxisOrdinate");
    const auto scale = number(conversion, "Scale");
    const bool readable = eastings && northings && height && std::isfinite(*eastings) && std::isfinite(*northings) &&
                          std::isfinite(*height);
    if (!readable) {
      reasons.push_back({"incompleteConversion", "Eastings, Northings or OrthogonalHeight is missing or not a number."});
    } else {
      f.eastings = *eastings;
      f.northings = *northings;
      f.orthogonalHeight = *height;
      coreOk = true;
    }
    if (scale && !(std::isfinite(*scale) && *scale > 0)) reasons.push_back({"badScale", "The conversion's Scale is not a positive number."});
    f.scale = scale ? *scale : 1.0;
    if (abscissa && ordinate) {
      f.xAxisAbscissa = *abscissa;
      f.xAxisOrdinate = *ordinate;
      const double length = std::hypot(*abscissa, *ordinate);
      if (!std::isfinite(length) || !(length > 1e-12)) {
        reasons.push_back({"badAxis", "The conversion's X axis direction is zero or not a number."});
        axisOk = false;
      } else {
        f.rotationDegrees = std::atan2(*ordinate, *abscissa) * 180.0 / pi;
      }
    } else if (abscissa || ordinate) {
      reasons.push_back({"incompleteAxis", "Only one of XAxisAbscissa and XAxisOrdinate is given."});
      axisOk = false;
    }
    // The target CRS.
    if (!isA(crs, "IfcProjectedCRS")) {
      reasons.push_back({"crsNotProjected", "The target of the conversion is not an IfcProjectedCRS."});
    } else {
      f.crsName = label(text(crs, "Name"), texts);
      crsDescription = clip(texts(text(crs, "Description")), 120);
      crsDatum = clip(texts(text(crs, "GeodeticDatum")), 64);
      crsProjection = clip(texts(text(crs, "MapProjection")), 64);
      crsZone = clip(texts(text(crs, "MapZone")), 64);
      const Inst unit = ref(crs, "MapUnit");
      if (!unit) {
        mapUnitGiven = false;
        report.notice("georeferenceMapUnitAssumed");
      } else {
        std::vector<std::string> ignored;
        const auto magnitude = unitMagnitude(unit, "LENGTHUNIT", &ignored);
        if (!magnitude) {
          std::string name = isA(unit, "IfcConversionBasedUnit") ? text(unit, "Name") : keyword(unit, "Name");
          mapUnitName = clip(texts(name.empty() ? className(unit) : name), 48);
          mapUnitMetres = 0;
          reasons.push_back({"mapUnit", "The CRS's map unit (" + mapUnitName + ") is not a length unit the site origin can use."});
        } else {
          mapUnitName = magnitude->name;
          mapUnitMetres = magnitude->perUnit;
          if (!(std::abs(magnitude->perUnit - 1.0) <= 1e-9))
            reasons.push_back({"mapUnit", "The CRS's map unit (" + clip(mapUnitName, 48) +
                                              ") is not the metre; the site origin is in metres."});
        }
      }
    }
    // Per-axis factors (IfcMapConversionScaled) and a scale that is not the project unit in metres.
    if (const auto fx = number(conversion, "FactorX")) factors[0] = *fx;
    if (const auto fy = number(conversion, "FactorY")) factors[1] = *fy;
    if (const auto fz = number(conversion, "FactorZ")) factors[2] = *fz;
    for (const double factor : factors)
      if (!(std::abs(factor - 1.0) <= 1e-9)) haveFactors = true;
    for (const char* name : {"ScaleY", "ScaleZ"})
      if (const auto s = number(conversion, name))
        if (!(std::abs(*s - f.scale) <= 1e-9 * std::abs(f.scale))) haveFactors = true;
    if (haveFactors)
      reasons.push_back({"perAxisFactors", "The conversion scales the axes differently; a translation-only site origin cannot hold that."});
    if (!(std::abs(f.scale - metresPerUnit) <= 1e-9 * metresPerUnit))
      reasons.push_back({"scale", "The conversion's Scale (" + degreesText(f.scale) + ") is not the project length unit in metres (" +
                                      degreesText(metresPerUnit) + "), so the map coordinates are not in metres."});
    if (coreOk && (std::abs(f.eastings) > siteLimit || std::abs(f.northings) > siteLimit || std::abs(f.orthogonalHeight) > siteLimit))
      reasons.push_back({"siteCoordinateRange", "The conversion's coordinates are beyond the site origin's bound of 100,000 km."});
    facts = f;
  }

  if (conversions.empty()) {
    reasons.clear();  // nothing to convert; several Model contexts are of no consequence
  } else {
    // A rotation is the one reason grid mode answers (decision 6).
    const bool rotated = facts && axisOk && !(facts->xAxisAbscissa > 0 && std::abs(facts->xAxisOrdinate) <= 1e-12 * facts->xAxisAbscissa);
    bool rotationReason = false;
    if (rotated && !gridAligned) {
      rotationReason = true;
      reasons.push_back({"rotation", "The conversion rotates the project axes by " + degreesText(facts->rotationDegrees) +
                                         " degrees; the project axes are kept and no site origin is set. Grid mode rotates the model instead."});
    }
    if (reasons.empty()) {
      result = rotated ? "gridAligned" : "siteOrigin";
      out.setsSiteOrigin = true;
      if (rotated) {
        const double length = std::hypot(facts->xAxisAbscissa, facts->xAxisOrdinate);
        out.rotate = true;
        out.cosine = facts->xAxisAbscissa / length;
        out.sine = facts->xAxisOrdinate / length;
        report.notice("georeferenceGridAligned");
      }
    } else {
      result = "notRepresentable";
      report.notice(reasons.size() == 1 && rotationReason ? "georeferenceRotationKept" : "georeferenceNotRepresentable");
    }
    if (coreOk) out.facts = facts;
    if (out.facts) out.consumed.push_back(onModel.front());
  }

  out.json = [&] {
    JsonOut j;
    j.beginObject();
    j.field("mode", modeName).field("result", result);
    j.field("mapConversionsInFile", conversions.size());
    j.field("mapConversionsOnModelContext", onModel.size());
    j.field("modelContexts", modelContexts.size());
    if (otherOperations) j.field("otherCoordinateOperations", otherOperations);
    if (epsets) j.field("epsets", epsets);
    if (conversions.empty()) {
      j.field("reason", schema == "IFC2X3" ? "IFC2X3 has no IfcMapConversion; no georeferencing is claimed for it."
                                            : "The file has no IfcMapConversion.");
    }
    if (out.facts) {
      const MapConversionFacts& f = *out.facts;
      j.key("mapConversion").beginObject();
      j.field("step", f.step);
      j.field("eastings", f.eastings).field("northings", f.northings).field("orthogonalHeight", f.orthogonalHeight);
      j.field("xAxisAbscissa", f.xAxisAbscissa).field("xAxisOrdinate", f.xAxisOrdinate);
      j.field("rotationDegrees", f.rotationDegrees).field("scale", f.scale);
      if (haveFactors) j.key("factors").doubles({factors[0], factors[1], factors[2]});
      j.endObject();
      j.key("crs").beginObject();
      j.field("name", f.crsName);
      if (!crsDescription.empty()) j.field("description", crsDescription);
      if (!crsDatum.empty()) j.field("geodeticDatum", crsDatum);
      if (!crsProjection.empty()) j.field("mapProjection", crsProjection);
      if (!crsZone.empty()) j.field("mapZone", crsZone);
      j.field("mapUnit", mapUnitName).field("mapUnitMetres", mapUnitMetres).field("mapUnitGiven", mapUnitGiven);
      j.endObject();
    }
    if (!reasons.empty()) {
      j.key("reasons").beginArray();
      for (const Reason& r : reasons) j.beginObject().field("code", r.code).field("message", clip(r.message, 240)).endObject();
      j.endArray();
    }
    if (out.setsSiteOrigin && out.facts) {
      const MapConversionFacts& f = *out.facts;
      j.key("siteOrigin").beginObject();
      j.field("easting", f.eastings).field("northing", f.northings).field("elevation", f.orthogonalHeight).field("crs", f.crsName);
      j.endObject();
      j.field("rotationAppliedDegrees", out.rotate ? f.rotationDegrees : 0.0);
    }
    j.endObject();
    return j.str();
  }();
  return out;
}

}  // namespace sketchyup::ifc
