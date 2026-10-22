#include "ifc/account.hpp"

#include <vector>

namespace sketchyup::ifc {

bool Accountant::supportClass(const Inst& instance) const {
  static const char* const classes[] = {
      "IfcRepresentationItem", "IfcRepresentation", "IfcProductRepresentation", "IfcObjectPlacement",
      "IfcRepresentationContext", "IfcRepresentationMap", "IfcProfileDef", "IfcOwnerHistory",
      "IfcPersonAndOrganization", "IfcPerson", "IfcOrganization", "IfcApplication", "IfcUnitAssignment",
      "IfcNamedUnit", "IfcDerivedUnit", "IfcDerivedUnitElement", "IfcDimensionalExponents", "IfcMeasureWithUnit",
      "IfcMonetaryUnit", "IfcPresentationItem", "IfcPresentationStyle", "IfcPresentationStyleAssignment",
      "IfcColourSpecification", "IfcSurfaceStyleShading", "IfcSurfaceStyleRendering", "IfcSurfaceStyleElementSelect",
      "IfcMaterial", "IfcMaterialLayer", "IfcMaterialLayerSet", "IfcMaterialLayerSetUsage", "IfcMaterialList",
      "IfcMaterialConstituent", "IfcMaterialConstituentSet", "IfcMaterialProfile", "IfcMaterialProfileSet",
      "IfcMaterialProfileSetUsage", "IfcMaterialDefinitionRepresentation", "IfcMaterialDefinition",
      "IfcCoordinateReferenceSystem", "IfcTextureCoordinate", "IfcSurfaceTexture", "IfcLightSource"};
  for (const char* name : classes)
    if (instance.declaration().is(name)) return true;
  return false;
}

void Accountant::claimTree(const Inst& root, const std::set<std::string>& skipAttributes) {
  if (!root) return;
  std::vector<Inst> stack;
  const auto push = [&](const Inst& instance) {
    if (!instance || !isEntity(instance)) return;
    const int step = stepOf(instance);
    if (converted_.contains(step) || supporting_.contains(step)) return;
    if (!supportClass(instance)) return;
    supporting_.insert(step);
    stack.push_back(instance);
  };
  // The root: always claimed (it is the caller's decision), but only through the attributes not skipped.
  if (!converted_.contains(stepOf(root))) supporting_.insert(stepOf(root));
  const auto visit = [&](const Inst& instance, bool isRoot) {
    const auto* entity = instance.declaration().as_entity();
    if (entity == nullptr) return;
    const auto& attributes = entity->all_attributes();
    for (std::size_t i = 0; i < attributes.size(); ++i) {
      if (isRoot && skipAttributes.contains(attributes[i]->name())) continue;
      ifcopenshell::attribute_value value;
      try {
        value = instance.get_attribute_value(i);
      } catch (...) {
        continue;
      }
      switch (value.type()) {
        case ifcopenshell::Argument_ENTITY_INSTANCE:
          push(static_cast<Inst>(value));
          break;
        case ifcopenshell::Argument_AGGREGATE_OF_ENTITY_INSTANCE:
          for (const Inst& target : static_cast<std::vector<Inst>>(value)) push(target);
          break;
        case ifcopenshell::Argument_AGGREGATE_OF_AGGREGATE_OF_ENTITY_INSTANCE:
          for (const auto& row : static_cast<std::vector<std::vector<Inst>>>(value))
            for (const Inst& target : row) push(target);
          break;
        default:
          break;
      }
    }
  };
  visit(root, true);
  while (!stack.empty()) {
    const Inst current = stack.back();
    stack.pop_back();
    visit(current, false);
  }
}

Accounting Accountant::finish() {
  // Styled items of claimed representation items; definition representations of claimed materials;
  // colour maps of claimed face sets. They point at the claimed instance, not the other way round.
  const auto claimed = [&](int step) { return converted_.contains(step) || supporting_.contains(step); };
  const auto sweep = [&](const char* className, const char* attribute) {
    std::vector<Inst> instances;
    try {
      instances = file_.instances_by_type(className);
    } catch (...) {
      return;
    }
    for (const Inst& instance : instances) {
      if (claimed(stepOf(instance))) continue;
      if (claimed(stepOf(ref(instance, attribute)))) claimTree(instance);
    }
  };
  sweep("IfcStyledItem", "Item");
  sweep("IfcMaterialDefinitionRepresentation", "RepresentedMaterial");
  sweep("IfcIndexedColourMap", "MappedTo");
  Accounting result;
  for (auto it = file_.begin(); it != file_.end(); ++it) {
    const Inst instance = it->second;
    ++result.entities;
    const int step = stepOf(instance);
    if (converted_.contains(step)) {
      ++result.converted;
    } else if (supporting_.contains(step)) {
      ++result.supporting;
    } else {
      ++result.omitted;
      ++result.omittedByClass[className(instance)];
    }
  }
  return result;
}

}  // namespace sketchyup::ifc
