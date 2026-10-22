#include "ifc/units.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <numbers>

namespace sketchyup::ifc {
namespace {

std::optional<double> prefixFactor(const std::string& prefix) {
  static const std::map<std::string, double> table{
      {"EXA", 1e18},   {"PETA", 1e15},  {"TERA", 1e12},  {"GIGA", 1e9},   {"MEGA", 1e6},   {"KILO", 1e3},
      {"HECTO", 1e2},  {"DECA", 1e1},   {"DECI", 1e-1},  {"CENTI", 1e-2}, {"MILLI", 1e-3}, {"MICRO", 1e-6},
      {"NANO", 1e-9},  {"PICO", 1e-12}, {"FEMTO", 1e-15}, {"ATTO", 1e-18}};
  if (prefix.empty()) return 1.0;
  const auto found = table.find(prefix);
  if (found == table.end()) return std::nullopt;
  return found->second;
}

std::string squeeze(const std::string& name) {
  std::string result;
  for (const unsigned char c : name)
    if (std::isalnum(c)) result += static_cast<char>(std::toupper(c));
  return result;
}

bool near(double a, double b, double relative) { return std::abs(a - b) <= relative * std::abs(b); }

std::optional<UnitMagnitude> magnitude(const Inst& unit, const std::string& dimension, std::vector<std::string>* notices,
                                       int depth) {
  if (!unit || depth > 4) return std::nullopt;
  if (keyword(unit, "UnitType") != dimension) return std::nullopt;
  if (isA(unit, "IfcSIUnit")) {
    const auto prefix = prefixFactor(keyword(unit, "Prefix"));
    if (!prefix) return std::nullopt;
    const std::string name = keyword(unit, "Name");
    if (dimension == "LENGTHUNIT" && name == "METRE") return UnitMagnitude{keyword(unit, "Prefix").empty() ? "METRE" : keyword(unit, "Prefix") + "METRE", *prefix, true};
    if (dimension == "AREAUNIT" && name == "SQUARE_METRE") return UnitMagnitude{"SQUARE_METRE", *prefix * *prefix, true};
    if (dimension == "VOLUMEUNIT" && name == "CUBIC_METRE") return UnitMagnitude{"CUBIC_METRE", *prefix * *prefix * *prefix, true};
    if (dimension == "PLANEANGLEUNIT" && name == "RADIAN") return UnitMagnitude{"RADIAN", *prefix, true};
    return std::nullopt;
  }
  if (isA(unit, "IfcConversionBasedUnit")) {
    const Inst measure = ref(unit, "ConversionFactor");
    if (!measure) return std::nullopt;
    const Wrapped value = unwrap(ref(measure, "ValueComponent"));
    double factor = 0;
    if (value.kind == Wrapped::Kind::Real) factor = value.real;
    else if (value.kind == Wrapped::Kind::Integer) factor = static_cast<double>(value.integer);
    if (!(factor > 0) || !std::isfinite(factor)) return std::nullopt;
    const auto inner = magnitude(ref(measure, "UnitComponent"), dimension, notices, depth + 1);
    if (!inner) return std::nullopt;
    UnitMagnitude result{text(unit, "Name"), factor * inner->perUnit, inner->exact};
    const std::string key = squeeze(result.name);
    if (dimension == "LENGTHUNIT") {
      double canonical = 0;
      if (key == "INCH" || key == "INCHES") canonical = 0.0254;
      else if (key == "FOOT" || key == "FEET") canonical = 0.3048;
      else if (key == "YARD") canonical = 0.9144;
      else if (key.find("SURVEY") != std::string::npos && key.find("FOOT") != std::string::npos) canonical = 1200.0 / 3937.0;
      if (canonical == 0) return std::nullopt;  // an unrecognised length unit is refused (A4)
      if (near(result.perUnit, canonical, 1e-6)) {
        if (result.perUnit != canonical && notices) notices->push_back("unitFactorSnapped");
        result.perUnit = canonical;
        result.exact = true;
      } else {
        result.exact = false;
        if (notices) notices->push_back("unitFactorAsWritten");
      }
    } else if (dimension == "PLANEANGLEUNIT") {
      if (key != "DEGREE" && key != "DEGREES") return std::nullopt;
      if (near(result.perUnit, std::numbers::pi / 180.0, 1e-3)) {
        if (result.perUnit != std::numbers::pi / 180.0 && notices) notices->push_back("unitFactorSnapped");
        result.perUnit = std::numbers::pi / 180.0;
      } else {
        return std::nullopt;
      }
    } else {
      result.exact = false;
      if (notices) notices->push_back("unitFactorAsWritten");
    }
    return result;
  }
  return std::nullopt;
}

}  // namespace

std::optional<UnitMagnitude> unitMagnitude(const Inst& unit, const std::string& dimension, std::vector<std::string>* notices) {
  return magnitude(unit, dimension, notices, 0);
}

UnitsOutcome readUnits(const Inst& project, const std::string& overrideUnit) {
  UnitsOutcome outcome;
  Units& units = outcome.units;
  std::map<std::string, std::vector<Inst>> byType;
  const Inst assignment = ref(project, "UnitsInContext");
  for (const Inst& unit : refs(assignment, "Units")) {
    if (isA(unit, "IfcNamedUnit")) {
      byType[keyword(unit, "UnitType")].push_back(unit);
    } else if (isA(unit, "IfcDerivedUnit")) {
      byType["DERIVED:" + keyword(unit, "UnitType")].push_back(unit);
    } else if (isA(unit, "IfcMonetaryUnit")) {
      byType["MONETARYUNIT"].push_back(unit);
    }
  }
  for (const char* type : {"LENGTHUNIT", "AREAUNIT", "VOLUMEUNIT", "PLANEANGLEUNIT"}) {
    const auto found = byType.find(type);
    if (found != byType.end() && found->second.size() > 1) {
      outcome.refusalCode = "mixedUnits";
      outcome.refusalMessage = std::string("The project assigns more than one ") + type +
                               "; mixed unit claims are not imported.";
      return outcome;
    }
  }
  if (const auto found = byType.find("LENGTHUNIT"); found != byType.end()) {
    const auto length = magnitude(found->second.front(), "LENGTHUNIT", &units.notices, 0);
    if (!length) {
      std::string name = isA(found->second.front(), "IfcConversionBasedUnit") ? text(found->second.front(), "Name")
                                                                                : keyword(found->second.front(), "Name");
      if (name.size() > 48) name.resize(48);
      outcome.refusalCode = "unsupportedLengthUnit";
      outcome.refusalMessage = "The project's length unit (" + name + ") is not a metre with an SI prefix, an inch, a foot, a yard or a survey foot.";
      return outcome;
    }
    units.hasLength = true;
    units.length = *length;
  }
  if (!units.hasLength) {
    static const std::map<std::string, UnitMagnitude> overrides{
        {"mm", {"MILLIMETRE", 0.001, true}}, {"cm", {"CENTIMETRE", 0.01, true}}, {"m", {"METRE", 1, true}},
        {"in", {"INCH", 0.0254, true}},      {"ft", {"FOOT", 0.3048, true}}};
    const auto found = overrides.find(overrideUnit);
    if (found == overrides.end()) {
      outcome.refusalCode = "noLengthUnit";
      outcome.refusalMessage = "The file assigns no length unit; supply one (mm, cm, m, in or ft) to import it.";
      return outcome;
    }
    units.length = found->second;
    units.overridden = true;
  } else if (!overrideUnit.empty()) {
    units.notices.push_back("lengthUnitOverrideIgnored");
  }
  if (const auto found = byType.find("AREAUNIT"); found != byType.end()) {
    const auto area = magnitude(found->second.front(), "AREAUNIT", &units.notices, 0);
    if (!area) {
      outcome.refusalCode = "unsupportedAreaUnit";
      outcome.refusalMessage = "The project's area unit is not a square metre or a conversion of one.";
      return outcome;
    }
    units.area = *area;
  } else {
    units.areaDefaulted = true;
  }
  if (const auto found = byType.find("VOLUMEUNIT"); found != byType.end()) {
    const auto volume = magnitude(found->second.front(), "VOLUMEUNIT", &units.notices, 0);
    if (!volume) {
      outcome.refusalCode = "unsupportedVolumeUnit";
      outcome.refusalMessage = "The project's volume unit is not a cubic metre or a conversion of one.";
      return outcome;
    }
    units.volume = *volume;
  } else {
    units.volumeDefaulted = true;
  }
  if (const auto found = byType.find("PLANEANGLEUNIT"); found != byType.end()) {
    const auto angle = magnitude(found->second.front(), "PLANEANGLEUNIT", &units.notices, 0);
    if (!angle) {
      outcome.refusalCode = "unsupportedAngleUnit";
      outcome.refusalMessage = "The project's plane angle unit is neither radian nor degree.";
      return outcome;
    }
    units.planeAngle = angle->name.empty() ? "RADIAN" : angle->name;
  }
  for (const auto& [type, list] : byType) {
    if (type == "LENGTHUNIT" || type == "AREAUNIT" || type == "VOLUMEUNIT" || type == "PLANEANGLEUNIT") continue;
    const Inst& unit = list.front();
    std::string label = isA(unit, "IfcConversionBasedUnit") ? text(unit, "Name") : keyword(unit, "Name");
    if (label.empty()) label = isA(unit, "IfcMonetaryUnit") ? (text(unit, "Currency").empty() ? keyword(unit, "Currency") : text(unit, "Currency")) : className(unit);
    if (!keyword(unit, "Prefix").empty()) label = keyword(unit, "Prefix") + label;
    if (label.size() > 48) label.resize(48);
    units.others[type] = label;
  }
  return outcome;
}

}  // namespace sketchyup::ifc
