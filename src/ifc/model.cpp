#include "ifc/model.hpp"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <functional>

namespace sketchyup::ifc {
namespace {

std::vector<Inst> inOrder(std::vector<Inst> instances) {
  std::sort(instances.begin(), instances.end(), [](const Inst& a, const Inst& b) { return stepOf(a) < stepOf(b); });
  return instances;
}

std::vector<Inst> relationships(ifcopenshell::file& file, const char* className) {
  try {
    return inOrder(file.instances_by_type(className));
  } catch (...) {
    return {};
  }
}

std::string formatNumber(double value) {
  char buffer[40];
  std::snprintf(buffer, sizeof buffer, "%.9g", value);
  return buffer;
}

}  // namespace

bool validGlobalId(const std::string& id) {
  if (id.size() != 22) return false;
  for (const unsigned char c : id)
    if (!(std::isalnum(c) || c == '_' || c == '$')) return false;
  return true;
}

void Model::build(ifcopenshell::file& source, Report& report) {
  file = &source;
  (void)report;
  for (const Inst& rel : relationships(source, "IfcRelAggregates")) {
    const int relating = stepOf(ref(rel, "RelatingObject"));
    if (!relating) continue;
    for (const Inst& child : refs(rel, "RelatedObjects")) {
      const int step = stepOf(child);
      relationshipsOf[step].push_back(stepOf(rel));
      const auto existing = aggregateParent.find(step);
      if (existing != aggregateParent.end()) {
        if (existing->second != relating) ++multipleParents;
        continue;
      }
      aggregateParent[step] = relating;
      aggregated[relating].push_back(step);
    }
  }
  // Cycles in the aggregation: walk each object's parent chain; 1 = on the current walk, 2 = clear, 3 = cyclic.
  {
    std::map<int, int> state;
    for (const auto& [start, ignored] : aggregateParent) {
      if (state.contains(start)) continue;
      std::vector<int> walk;
      int at = start;
      int outcome = 2;
      while (true) {
        const auto known = state.find(at);
        if (known != state.end()) {
          outcome = known->second == 1 ? 3 : known->second;
          break;
        }
        state[at] = 1;
        walk.push_back(at);
        const auto up = aggregateParent.find(at);
        if (up == aggregateParent.end()) break;
        at = up->second;
      }
      for (const int step : walk) {
        state[step] = outcome;
        if (outcome == 3) cyclic.insert(step);
      }
    }
    cycles = static_cast<long long>(cyclic.size());
  }
  for (const Inst& rel : relationships(source, "IfcRelContainedInSpatialStructure")) {
    const int structure = stepOf(ref(rel, "RelatingStructure"));
    if (!structure) continue;
    for (const Inst& element : refs(rel, "RelatedElements")) {
      const int step = stepOf(element);
      relationshipsOf[step].push_back(stepOf(rel));
      const auto existing = container.find(step);
      if (existing != container.end()) {
        if (existing->second != structure) ++multipleParents;
        continue;
      }
      container[step] = structure;
      contained[structure].push_back(step);
    }
  }
  for (const Inst& rel : relationships(source, "IfcRelVoidsElement")) {
    const int host = stepOf(ref(rel, "RelatingBuildingElement"));
    const int opening = stepOf(ref(rel, "RelatedOpeningElement"));
    if (!host || !opening) continue;
    relationshipsOf[opening].push_back(stepOf(rel));
    relationshipsOf[host].push_back(stepOf(rel));
    if (hostOf.emplace(opening, host).second) openingsOf[host].push_back(opening);
  }
  for (const Inst& rel : relationships(source, "IfcRelDefinesByType")) {
    const int type = stepOf(ref(rel, "RelatingType"));
    if (!type) continue;
    for (const Inst& object : refs(rel, "RelatedObjects")) {
      relationshipsOf[stepOf(object)].push_back(stepOf(rel));
      typeOf.emplace(stepOf(object), type);
    }
  }
  for (const Inst& rel : relationships(source, "IfcRelDefinesByProperties")) {
    const Inst definition = ref(rel, "RelatingPropertyDefinition");
    if (!definition) continue;
    for (const Inst& object : refs(rel, "RelatedObjects")) {
      relationshipsOf[stepOf(object)].push_back(stepOf(rel));
      propertySetsOf[stepOf(object)].push_back(definition);
    }
  }
  for (const Inst& rel : relationships(source, "IfcRelAssociatesMaterial")) {
    const Inst material = ref(rel, "RelatingMaterial");
    if (!material) continue;
    for (const Inst& object : refs(rel, "RelatedObjects")) {
      relationshipsOf[stepOf(object)].push_back(stepOf(rel));
      materialsOf[stepOf(object)].push_back(material);
    }
  }
  for (const Inst& rel : relationships(source, "IfcRelAssociatesClassification")) {
    const Inst classification = ref(rel, "RelatingClassification");
    if (!classification) continue;
    for (const Inst& object : refs(rel, "RelatedObjects")) {
      relationshipsOf[stepOf(object)].push_back(stepOf(rel));
      classificationsOf[stepOf(object)].push_back(classification);
    }
  }
  for (const Inst& rel : relationships(source, "IfcRelAssignsToGroup")) {
    const Inst group = ref(rel, "RelatingGroup");
    if (!group) continue;
    for (const Inst& object : refs(rel, "RelatedObjects")) {
      relationshipsOf[stepOf(object)].push_back(stepOf(rel));
      groupsOf[stepOf(object)].push_back(group);
    }
  }
  for (const Inst& layer : relationships(source, "IfcPresentationLayerAssignment"))
    for (const Inst& item : refs(layer, "AssignedItems")) layersOf[stepOf(item)].push_back(layer);
}

namespace {

enum class Dimension { None, Length, Area, Volume };

Dimension measureDimension(const std::string& type) {
  if (type == "IfcLengthMeasure" || type == "IfcPositiveLengthMeasure" || type == "IfcNonNegativeLengthMeasure")
    return Dimension::Length;
  if (type == "IfcAreaMeasure") return Dimension::Area;
  if (type == "IfcVolumeMeasure") return Dimension::Volume;
  return Dimension::None;
}

bool dimensionless(const std::string& type) {
  return type == "IfcReal" || type == "IfcInteger" || type == "IfcNumericMeasure" || type == "IfcCountMeasure" ||
         type == "IfcRatioMeasure" || type == "IfcPositiveRatioMeasure" || type == "IfcNormalisedRatioMeasure" ||
         type == "IfcParameterValue" || type == "IfcPositiveInteger" || type == "IfcNonNegativeInteger";
}

const char* dimensionName(Dimension d) {
  return d == Dimension::Length ? "LENGTHUNIT" : d == Dimension::Area ? "AREAUNIT" : "VOLUMEUNIT";
}

// The number to store for a value of `dimension` (or None), given the property's own unit.
double convert(const Model& model, Dimension dimension, double value, const Inst& ownUnit, const std::string& label,
               Report& report, std::set<int>& used) {
  if (dimension == Dimension::None) {
    if (!label.empty()) report.unconverted(label + (ownUnit ? " (own unit)" : ""));
    if (ownUnit) used.insert(stepOf(ownUnit));
    return value;
  }
  double factor = dimension == Dimension::Length ? model.units.length.perUnit
                  : dimension == Dimension::Area ? model.units.area.perUnit
                                                 : model.units.volume.perUnit;
  if (ownUnit) {
    used.insert(stepOf(ownUnit));
    std::vector<std::string> ignored;
    if (const auto own = unitMagnitude(ownUnit, dimensionName(dimension), &ignored)) {
      factor = own->perUnit;
    } else {
      report.unconverted(label + " (own unit)");
      return value;
    }
  }
  return value * factor;
}

void addProperty(std::vector<ReadProperty>& out, const std::string& set, const std::string& name, int origin,
                 ReadProperty::Kind kind, bool boolean, double real, const std::string& textValue) {
  ReadProperty p;
  p.set = set;
  p.name = name;
  p.origin = origin;
  p.kind = kind;
  p.boolean = boolean;
  p.real = real;
  p.text = textValue;
  out.push_back(std::move(p));
}

// A value of any IfcValue type as text (for enumerations and lists).
std::string valueText(const Wrapped& value) {
  switch (value.kind) {
    case Wrapped::Kind::Boolean: return value.boolean ? "TRUE" : "FALSE";
    case Wrapped::Kind::Integer: return std::to_string(value.integer);
    case Wrapped::Kind::Real: return formatNumber(value.real);
    case Wrapped::Kind::Text: return value.text;
    default: return {};
  }
}

void readSimple(const Model& model, const Inst& property, const std::string& set, int origin,
                std::vector<ReadProperty>& out, Report& report, std::set<int>& used) {
  const std::string name = text(property, "Name");
  const Inst nominal = ref(property, "NominalValue");
  if (!nominal) {
    report.count("propertiesOmitted", "noValue");
    return;
  }
  const Wrapped value = unwrap(nominal);
  switch (value.kind) {
    case Wrapped::Kind::Boolean:
      addProperty(out, set, name, origin, ReadProperty::Kind::Bool, value.boolean, 0, {});
      break;
    case Wrapped::Kind::Logical:
      report.count("propertiesOmitted", "logicalUnknown");
      return;
    case Wrapped::Kind::Text:
      addProperty(out, set, name, origin, ReadProperty::Kind::Text, false, 0, value.text);
      break;
    case Wrapped::Kind::Integer:
    case Wrapped::Kind::Real: {
      const double raw = value.kind == Wrapped::Kind::Integer ? static_cast<double>(value.integer) : value.real;
      const Dimension dimension = measureDimension(value.typeName);
      const bool unitless = dimensionless(value.typeName);
      const double stored = convert(model, dimension, raw, ref(property, "Unit"),
                                    unitless ? std::string() : value.typeName, report, used);
      addProperty(out, set, name, origin, ReadProperty::Kind::Real, false, stored, {});
      break;
    }
    default:
      report.count("propertiesOmitted", "noValue");
      return;
  }
  used.insert(stepOf(property));
  report.count("properties", origin == 1 || origin == 3 ? "quantities" : "properties");
}

void readProperty(const Model& model, const Inst& property, const std::string& set, int origin,
                  std::vector<ReadProperty>& out, Report& report, std::set<int>& used) {
  if (text(property, "Name").empty() && !isA(property, "IfcComplexProperty")) {
    report.count("propertiesOmitted", "unnamed");
    return;
  }
  const std::string name = text(property, "Name");
  if (isA(property, "IfcPropertySingleValue")) {
    readSimple(model, property, set, origin, out, report, used);
  } else if (isA(property, "IfcPropertyEnumeratedValue")) {
    std::string joined;
    for (const Inst& entry : refs(property, "EnumerationValues")) {
      if (!joined.empty()) joined += ", ";
      joined += valueText(unwrap(entry));
    }
    addProperty(out, set, name, origin, ReadProperty::Kind::Text, false, 0, joined);
    used.insert(stepOf(property));
    report.count("properties", "properties");
  } else if (isA(property, "IfcPropertyListValue")) {
    std::string joined;
    for (const Inst& entry : refs(property, "ListValues")) {
      if (!joined.empty()) joined += ", ";
      joined += valueText(unwrap(entry));
    }
    addProperty(out, set, name, origin, ReadProperty::Kind::Text, false, 0, joined);
    used.insert(stepOf(property));
    report.count("properties", "properties");
  } else if (isA(property, "IfcPropertyBoundedValue")) {
    bool any = false;
    for (const char* side : {"LowerBoundValue", "UpperBoundValue"}) {
      const Wrapped value = unwrap(ref(property, side));
      if (value.kind != Wrapped::Kind::Integer && value.kind != Wrapped::Kind::Real) continue;
      const double raw = value.kind == Wrapped::Kind::Integer ? static_cast<double>(value.integer) : value.real;
      const bool unitless = dimensionless(value.typeName);
      const double stored = convert(model, measureDimension(value.typeName), raw, ref(property, "Unit"),
                                    unitless ? std::string() : value.typeName, report, used);
      addProperty(out, set, name + (side[0] == 'L' ? ".Lower" : ".Upper"), origin, ReadProperty::Kind::Real, false,
                  stored, {});
      any = true;
    }
    if (any) {
      used.insert(stepOf(property));
      report.count("properties", "properties");
    } else {
      report.count("propertiesOmitted", "noValue");
    }
  } else {
    // IfcComplexProperty, IfcPropertyReferenceValue, IfcPropertyTableValue and anything else.
    report.count("propertiesUnmapped", className(property));
  }
}

void readQuantity(const Model& model, const Inst& quantity, const std::string& set, int origin,
                  std::vector<ReadProperty>& out, Report& report, std::set<int>& used) {
  struct Kind {
    const char* className;
    const char* valueAttribute;
    Dimension dimension;
    const char* unitType;  // for the report when not converted
  };
  static const Kind kinds[] = {{"IfcQuantityLength", "LengthValue", Dimension::Length, nullptr},
                               {"IfcQuantityArea", "AreaValue", Dimension::Area, nullptr},
                               {"IfcQuantityVolume", "VolumeValue", Dimension::Volume, nullptr},
                               {"IfcQuantityCount", "CountValue", Dimension::None, nullptr},
                               {"IfcQuantityWeight", "WeightValue", Dimension::None, "MASSUNIT"},
                               {"IfcQuantityTime", "TimeValue", Dimension::None, "TIMEUNIT"},
                               {"IfcQuantityNumber", "NumberValue", Dimension::None, nullptr}};
  const std::string name = text(quantity, "Name");
  if (name.empty()) {
    report.count("propertiesOmitted", "unnamed");
    return;
  }
  for (const auto& kind : kinds) {
    if (!isA(quantity, kind.className)) continue;
    const auto raw = number(quantity, kind.valueAttribute);
    if (!raw) {
      report.count("propertiesOmitted", "noValue");
      return;
    }
    std::string label;
    if (kind.unitType) {
      const auto found = model.units.others.find(kind.unitType);
      label = std::string(kind.className) + " (" + (found == model.units.others.end() ? "SI default" : found->second) + ")";
    }
    const double stored = convert(model, kind.dimension, *raw, ref(quantity, "Unit"), label, report, used);
    addProperty(out, set, name, origin, ReadProperty::Kind::Real, false, stored, {});
    used.insert(stepOf(quantity));
    report.count("properties", "quantities");
    return;
  }
  report.count("propertiesUnmapped", className(quantity));
}

void readSet(const Model& model, const Inst& definition, bool type, std::vector<ReadProperty>& out, Report& report,
             std::set<int>& used) {
  std::vector<ReadProperty> local;
  if (isA(definition, "IfcPropertySet")) {
    std::string name = text(definition, "Name");
    if (name.empty()) {
      name = "IfcPropertySet";
      report.notice("unnamedPropertySet");
    }
    for (const Inst& property : refs(definition, "HasProperties"))
      readProperty(model, property, name, type ? 2 : 0, local, report, used);
    used.insert(stepOf(definition));
    report.count("properties", "psets");
  } else if (isA(definition, "IfcElementQuantity")) {
    std::string name = text(definition, "Name");
    if (name.empty()) {
      name = "IfcElementQuantity";
      report.notice("unnamedPropertySet");
    }
    for (const Inst& quantity : refs(definition, "Quantities"))
      readQuantity(model, quantity, name, type ? 3 : 1, local, report, used);
    used.insert(stepOf(definition));
    report.count("properties", "psets");
  } else {
    report.count("propertiesUnmapped", className(definition));
    return;
  }
  out.insert(out.end(), local.begin(), local.end());
}

}  // namespace

void readProperties(const Model& model, const Inst& object, const Inst& type, std::vector<ReadProperty>& out,
                    Report& report, std::set<int>& used) {
  std::vector<ReadProperty> occurrence, typed;
  if (const auto found = model.propertySetsOf.find(stepOf(object)); found != model.propertySetsOf.end())
    for (const Inst& definition : found->second) readSet(model, definition, false, occurrence, report, used);
  if (type)
    for (const Inst& definition : refs(type, "HasPropertySets")) readSet(model, definition, true, typed, report, used);
  const auto byName = [](const ReadProperty& a, const ReadProperty& b) {
    if (a.origin != b.origin) return a.origin < b.origin;
    if (a.set != b.set) return a.set < b.set;
    return a.name < b.name;
  };
  std::stable_sort(occurrence.begin(), occurrence.end(), byName);
  std::stable_sort(typed.begin(), typed.end(), byName);
  std::set<std::pair<std::string, std::string>> own;
  for (const auto& p : occurrence) own.insert({p.set, p.name});
  out.insert(out.end(), occurrence.begin(), occurrence.end());
  for (auto& p : typed) {
    if (own.contains({p.set, p.name})) {
      report.count("properties", "typeOverridden");
      continue;
    }
    out.push_back(std::move(p));
  }
}

namespace {

std::string materialName(const Inst& material) { return material ? text(material, "Name") : std::string(); }

}  // namespace

MaterialDescription describeMaterial(const Model& model, const Inst& object, const Inst& type, Report& report,
                                     std::set<int>& used) {
  MaterialDescription result;
  auto found = model.materialsOf.find(stepOf(object));
  if ((found == model.materialsOf.end() || found->second.empty()) && type) found = model.materialsOf.find(stepOf(type));
  if (found == model.materialsOf.end() || found->second.empty()) return result;
  std::vector<std::string> names, summary;
  const auto join = [](const std::vector<std::string>& parts, const char* separator) {
    std::string joined;
    for (const auto& part : parts) {
      if (!joined.empty()) joined += separator;
      joined += part;
    }
    return joined;
  };
  for (const Inst& association : found->second) {
    used.insert(stepOf(association));
    Inst layerSet = association;
    if (isA(association, "IfcMaterialLayerSetUsage")) layerSet = ref(association, "ForLayerSet");
    if (isA(association, "IfcMaterialProfileSetUsage")) layerSet = ref(association, "ForProfileSet");
    if (isA(layerSet, "IfcMaterialLayerSet")) {
      used.insert(stepOf(layerSet));
      std::vector<std::string> layerMaterials;
      for (const Inst& layer : refs(layerSet, "MaterialLayers")) {
        used.insert(stepOf(layer));
        const Inst material = ref(layer, "Material");
        const std::string name = materialName(material);
        if (material) used.insert(stepOf(material));
        if (!name.empty()) layerMaterials.push_back(name);
        const auto thickness = number(layer, "LayerThickness");
        summary.push_back((name.empty() ? std::string("(unnamed)") : name) + ":" +
                          (thickness ? formatNumber(*thickness * model.units.length.perUnit) : std::string("?")));
      }
      std::string setName = text(layerSet, "LayerSetName");
      if (setName.empty()) setName = text(layerSet, "Name");
      if (setName.empty()) setName = join(layerMaterials, ", ");
      names.push_back(setName);
      result.layered = true;
    } else if (isA(layerSet, "IfcMaterialConstituentSet")) {
      used.insert(stepOf(layerSet));
      std::vector<std::string> constituents;
      for (const Inst& constituent : refs(layerSet, "MaterialConstituents")) {
        used.insert(stepOf(constituent));
        const Inst material = ref(constituent, "Material");
        if (material) used.insert(stepOf(material));
        std::string name = text(constituent, "Name");
        const std::string materialLabel = materialName(material);
        if (name.empty()) name = materialLabel;
        if (!materialLabel.empty()) constituents.push_back(materialLabel);
        const auto fraction = number(constituent, "Fraction");
        summary.push_back(name + (fraction ? ":" + formatNumber(*fraction) : std::string()));
      }
      std::string setName = text(layerSet, "Name");
      if (setName.empty()) setName = join(constituents, ", ");
      names.push_back(setName);
      result.layered = true;
    } else if (isA(layerSet, "IfcMaterialProfileSet")) {
      used.insert(stepOf(layerSet));
      std::vector<std::string> profiles;
      for (const Inst& profile : refs(layerSet, "MaterialProfiles")) {
        used.insert(stepOf(profile));
        const Inst material = ref(profile, "Material");
        if (material) used.insert(stepOf(material));
        const std::string label = materialName(material);
        if (!label.empty()) profiles.push_back(label);
        summary.push_back(text(profile, "Name").empty() ? label : text(profile, "Name"));
      }
      std::string setName = text(layerSet, "Name");
      if (setName.empty()) setName = join(profiles, ", ");
      names.push_back(setName);
      result.layered = true;
    } else if (isA(association, "IfcMaterialList")) {
      std::vector<std::string> listed;
      for (const Inst& material : refs(association, "Materials")) {
        used.insert(stepOf(material));
        if (!materialName(material).empty()) listed.push_back(materialName(material));
      }
      names.push_back(join(listed, ", "));
      summary.push_back(join(listed, "; "));
      result.layered = true;
    } else if (isA(association, "IfcMaterialLayer")) {
      const Inst material = ref(association, "Material");
      if (material) used.insert(stepOf(material));
      names.push_back(materialName(material));
    } else if (isA(association, "IfcMaterial")) {
      names.push_back(materialName(association));
    } else {
      report.count("materialsUnmapped", className(association));
    }
  }
  result.name = clip(join(names, "; "), 2048);
  result.layers = clip(join(summary, "; "), 2048);
  return result;
}

void describeClassification(const Model& model, const Inst& object, std::string& label, std::string& uri,
                            std::set<int>& used) {
  const auto found = model.classificationsOf.find(stepOf(object));
  if (found == model.classificationsOf.end()) return;
  std::vector<std::string> labels, uris;
  for (const Inst& entry : found->second) {
    used.insert(stepOf(entry));
    std::string system;
    Inst source = entry;
    for (int depth = 0; depth < 8 && source; ++depth) {
      if (isA(source, "IfcClassification")) {
        system = text(source, "Name");
        used.insert(stepOf(source));
        break;
      }
      source = ref(source, "ReferencedSource");
      if (source) used.insert(stepOf(source));
    }
    std::string identification = text(entry, "Identification");
    if (identification.empty()) identification = text(entry, "ItemReference");
    const std::string name = isA(entry, "IfcClassification") ? std::string() : text(entry, "Name");
    std::string part = system;
    if (!identification.empty()) part += (part.empty() ? "" : ":") + identification;
    if (!name.empty()) part += (part.empty() ? "" : " ") + name;
    if (!part.empty()) labels.push_back(part);
    std::string location = text(entry, "Location");
    if (location.empty() && isA(entry, "IfcClassification")) location = text(entry, "Source");
    if (!location.empty()) uris.push_back(location);
  }
  const auto join = [](const std::vector<std::string>& parts) {
    std::string joined;
    for (const auto& part : parts) {
      if (!joined.empty()) joined += "; ";
      joined += part;
    }
    return joined;
  };
  label = clip(join(labels), 2048);
  uri = clip(join(uris), 2048);
  (void)model;
}

std::string describeGroups(const Model& model, const Inst& object, std::set<int>& used) {
  const auto found = model.groupsOf.find(stepOf(object));
  if (found == model.groupsOf.end()) return {};
  std::string joined;
  for (const Inst& group : found->second) {
    used.insert(stepOf(group));
    std::string name = text(group, "Name");
    if (name.empty()) {
      const std::string guid = text(group, "GlobalId");
      name = className(group) + " " + guid.substr(0, 6);
    }
    if (!joined.empty()) joined += ", ";
    joined += name;
  }
  return clip(joined, 2048);
}

LayerState describeLayers(const Model& model, const Inst& product, std::set<int>& used) {
  LayerState state;
  std::set<int> seen;
  const auto consider = [&](int step) {
    const auto found = model.layersOf.find(step);
    if (found == model.layersOf.end()) return;
    for (const Inst& layer : found->second) {
      if (!seen.insert(stepOf(layer)).second) continue;
      used.insert(stepOf(layer));
      const std::string name = text(layer, "Name");
      if (!name.empty()) {
        if (!state.names.empty()) state.names += ", ";
        state.names += name;
      }
      const auto on = flag(layer, "LayerOn");
      const auto frozen = flag(layer, "LayerFrozen");
      if ((on && !*on) || (frozen && *frozen)) state.hidden = true;
    }
  };
  const Inst shape = ref(product, "Representation");
  for (const Inst& representation : refs(shape, "Representations")) {
    consider(stepOf(representation));
    for (const Inst& item : refs(representation, "Items")) consider(stepOf(item));
  }
  state.names = clip(state.names, 2048);
  return state;
}

}  // namespace sketchyup::ifc
