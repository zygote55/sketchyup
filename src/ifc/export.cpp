// sketchyup-ifc-worker --export (R097.d, ADR 0171 section 3.8). Builds an IFC4 file from the application's export
// package with IfcOpenShell's writer. Like the import it goes through IfcParse's schema-independent entity interface
// (attributes by name), so a wrong attribute is an exception here and a failed check in the tests, not a silent
// default. Everything that makes the file differ between runs is injected: the header time, the owner history time
// and every GlobalId (the application derives the nodes' identifiers from the document identity; the helper derives
// the relationships' and property sets' the same way from their owners').
#include "ifc/export.hpp"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <fstream>
#include <map>
#include <set>
#include <stdexcept>
#include <tuple>

#include "ifc/export_package.hpp"
#include "ifc/json_out.hpp"
#include "ifc/progress.hpp"
#include "ifcparse/file.h"
#include "ifcparse/instance_data.h"
#include "ifcparse/schema.h"
#include "ifcparse/spf_header.h"
#include "ifcparse/schemas/Header_section_schema.h"
#include "sketchyup/version.hpp"

namespace sketchyup::ifc {
namespace {

using Base = express::base;

constexpr std::size_t maxIfcBytes = 256u * 1024 * 1024;
constexpr std::size_t labelCodePoints = 255;

struct Refusal : std::runtime_error {
  Refusal(std::string code_, const std::string& message) : std::runtime_error(message), code(std::move(code_)) {}
  std::string code;
};

// A UTF-8 prefix of at most `bytes` bytes.
std::string clip(const std::string& text, std::size_t bytes) {
  if (text.size() <= bytes) return text;
  std::size_t cut = bytes;
  while (cut > 0 && (static_cast<unsigned char>(text[cut]) & 0xC0) == 0x80) --cut;
  return text.substr(0, cut);
}

std::size_t codePoints(const std::string& text) {
  std::size_t count = 0;
  for (const char c : text)
    if ((static_cast<unsigned char>(c) & 0xC0) != 0x80) ++count;
  return count;
}

// A float as the double nearest to its shortest decimal form, so a colour of 0.73f is written 0.73.
double shortDouble(float value) {
  char buffer[40];
  const auto end = std::to_chars(buffer, buffer + sizeof buffer, value).ptr;
  return std::strtod(std::string(buffer, end).c_str(), nullptr);
}

struct Builder {
  Builder(const ExportPackage& package, Progress& progressLines)
      : pkg(package), progress(progressLines), file(ifcopenshell::schema_by_name("IFC4")), schema(file.schema()) {}

  const ExportPackage& pkg;
  Progress& progress;
  ifcopenshell::file file;
  const ifcopenshell::schema_definition* schema;

  std::set<std::string> used;
  Base owner, modelContext, bodyContext, units, metre, project;
  std::vector<Base> styleEntity, materialEntity, entity, placement, hiddenShapes;  // hiddenShapes: the face sets on the hidden layer
  std::vector<std::vector<Base>> materialUsers;
  std::map<std::string, long long> counts;
  std::map<std::tuple<std::string, std::string, std::string>, long long> fallbacks;
  long long predefinedTypeDropped = 0, namesClipped = 0;
  bool georeferenceWritten = false;

  // ---- Entities, by name ----
  const ifcopenshell::declaration& declaration(const std::string& name) const { return *schema->declaration_by_name(name); }
  Base make(const std::string& name) { return file.create(&declaration(name)); }
  template <class V> static void set(Base entity_, const char* attribute, const V& value) {
    entity_.set_attribute_value(std::string(attribute), value);
  }
  static void setInt(Base entity_, const char* attribute, long long value) { set(entity_, attribute, static_cast<int64_t>(value)); }
  static bool has(const Base& entity_, const char* attribute) {
    const auto* declared = entity_.declaration().as_entity();
    return declared != nullptr && declared->attribute_index(std::string(attribute)) >= 0;
  }
  static const ifcopenshell::enumeration_type* enumerationOf(const Base& entity_, const char* attribute) {
    const auto* declared = entity_.declaration().as_entity();
    if (declared == nullptr) return nullptr;
    const auto index = declared->attribute_index(std::string(attribute));
    if (index < 0) return nullptr;
    const auto* type = declared->attribute_by_index(static_cast<std::size_t>(index))->type_of_attribute();
    if (type == nullptr || type->as_named_type() == nullptr || type->as_named_type()->declared_type() == nullptr) return nullptr;
    return type->as_named_type()->declared_type()->as_enumeration_type();
  }
  static bool enumHas(const Base& entity_, const char* attribute, const std::string& value) {
    const auto* type = enumerationOf(entity_, attribute);
    if (type == nullptr) return false;
    const auto& items = type->enumeration_items();
    return std::find(items.begin(), items.end(), value) != items.end();
  }
  static void setEnum(Base entity_, const char* attribute, const std::string& value) {
    const auto* type = enumerationOf(entity_, attribute);
    if (type == nullptr) throw std::runtime_error(std::string("not an enumeration attribute: ") + attribute);
    set(entity_, attribute, ifcopenshell::enumeration_reference(type, type->lookup_enum_offset(value)));
  }

  // The GlobalId of something the nodes do not name: derived from the document identity, a kind and the owner's id.
  std::string aux(const std::string& kind, const std::string& owner_, std::size_t index = 0) {
    for (int attempt = 0;; ++attempt) {
      std::string seed = "SketchyUp IFC GlobalId v1";
      seed.push_back('\0');
      seed += pkg.documentId;
      seed.push_back('\0');
      seed += "aux:" + kind;
      seed.push_back('\0');
      seed += owner_;
      seed.push_back('\0');
      seed += std::to_string(index);
      if (attempt > 0) {
        seed.push_back('\0');
        seed += std::to_string(attempt);
      }
      auto guid = derivedGlobalId(seed);
      if (used.insert(guid).second) return guid;
    }
  }

  Base point(double x, double y, double z) {
    auto p = make("IfcCartesianPoint");
    set(p, "Coordinates", std::vector<double>{x, y, z});
    return p;
  }
  Base direction(double x, double y, double z) {
    auto d = make("IfcDirection");
    set(d, "DirectionRatios", std::vector<double>{x, y, z});
    return d;
  }
  Base axisPlacement(const std::array<double, 9>& p) {
    auto result = make("IfcAxis2Placement3D");
    set(result, "Location", point(p[0], p[1], p[2]));
    set(result, "Axis", direction(p[3], p[4], p[5]));
    set(result, "RefDirection", direction(p[6], p[7], p[8]));
    return result;
  }
  Base localPlacement(const Base& relativeTo, const std::array<double, 9>& p) {
    auto result = make("IfcLocalPlacement");
    if (relativeTo) set(result, "PlacementRelTo", relativeTo);
    set(result, "RelativePlacement", axisPlacement(p));
    return result;
  }
  void root(Base entity_, const std::string& guid, const std::string& name) {
    set(entity_, "GlobalId", guid);
    set(entity_, "OwnerHistory", owner);
    if (!name.empty()) set(entity_, "Name", name);
  }
  void label(Base entity_, const char* attribute, const std::string& text) {
    if (text.empty() || !has(entity_, attribute)) return;
    set(entity_, attribute, text);
  }

  // ---- The class a node is written as ----
  struct Resolved {
    std::string name, reason;
  };
  Resolved resolveClass(const ExportNode& node) const {
    const std::string fallback = node.role == "assembly" ? "IfcElementAssembly" : "IfcBuildingElementProxy";
    if (node.requestedClass.empty()) return {fallback, {}};
    const ifcopenshell::declaration* found = nullptr;
    try {
      found = schema->declaration_by_name(node.requestedClass);
    } catch (const std::exception&) {
    }
    if (found == nullptr || found->as_entity() == nullptr) return {fallback, "unknownClass"};
    // IFC4's deprecated IfcWallStandardCase, IfcSlabElementedCase and the like require parametric geometry and a layer set
    // usage that a tessellated body does not have: they are written as the class they refine.
    std::string reason;
    const std::string& name = found->name();
    const auto endsWith = [&](const char* suffix) { return name.size() > std::strlen(suffix) && name.compare(name.size() - std::strlen(suffix), std::string::npos, suffix) == 0; };
    if ((endsWith("StandardCase") || endsWith("ElementedCase")) && found->as_entity()->supertype() != nullptr) {
      found = found->as_entity()->supertype();
      reason = "standardCase";
    }
    if (found->as_entity()->is_abstract()) return {fallback, "abstractClass"};
    if (!found->is("IfcElement") || found->is("IfcFeatureElement") || found->is("IfcVirtualElement")) return {fallback, "notAnElement"};
    return {found->name(), reason};
  }

  // ---- The pieces ----
  void writeHeader() {
    auto description = file.header().file_description();
    description.setdescription(std::vector<std::string>{"SketchyUp export, tessellated geometry"});
    description.setimplementation_level("2;1");
    auto name = file.header().file_name();
    name.setname(pkg.fileName);
    name.settime_stamp(pkg.timestamp);
    name.setauthor(std::vector<std::string>{""});
    name.setorganization(std::vector<std::string>{""});
    name.setpreprocessor_version(std::string("IfcOpenShell ") + IFCOPENSHELL_VERSION);
    name.setoriginating_system(pkg.applicationName + " " + pkg.applicationVersion);
    name.setauthorization("");
    file.header().file_schema().setschema_identifiers(std::vector<std::string>{"IFC4"});
  }

  void makeOwnerHistory() {
    auto person = make("IfcPerson");
    set(person, "Identification", std::string("unknown"));
    auto organization = make("IfcOrganization");
    set(organization, "Name", pkg.applicationName);
    auto both = make("IfcPersonAndOrganization");
    set(both, "ThePerson", person);
    set(both, "TheOrganization", organization);
    auto application = make("IfcApplication");
    set(application, "ApplicationDeveloper", organization);
    set(application, "Version", pkg.applicationVersion);
    set(application, "ApplicationFullName", pkg.applicationName);
    set(application, "ApplicationIdentifier", pkg.applicationName);
    struct tm parts {};
    std::sscanf(pkg.timestamp.c_str(), "%d-%d-%dT%d:%d:%d", &parts.tm_year, &parts.tm_mon, &parts.tm_mday, &parts.tm_hour, &parts.tm_min,
                &parts.tm_sec);
    parts.tm_year -= 1900;
    parts.tm_mon -= 1;
    owner = make("IfcOwnerHistory");
    set(owner, "OwningUser", both);
    set(owner, "OwningApplication", application);
    setInt(owner, "CreationDate", static_cast<long long>(timegm(&parts)));
  }

  void makeUnitsAndContexts() {
    const auto unit = [&](const char* type, const char* name) {
      auto u = make("IfcSIUnit");
      setEnum(u, "UnitType", type);
      setEnum(u, "Name", name);
      return u;
    };
    metre = unit("LENGTHUNIT", "METRE");
    units = make("IfcUnitAssignment");
    set(units, "Units", std::vector<Base>{metre, unit("AREAUNIT", "SQUARE_METRE"), unit("VOLUMEUNIT", "CUBIC_METRE"), unit("PLANEANGLEUNIT", "RADIAN")});

    modelContext = make("IfcGeometricRepresentationContext");
    set(modelContext, "ContextType", std::string("Model"));
    setInt(modelContext, "CoordinateSpaceDimension", 3);
    set(modelContext, "Precision", 1e-7);
    set(modelContext, "WorldCoordinateSystem", axisPlacement({0, 0, 0, 0, 0, 1, 1, 0, 0}));
    auto north = make("IfcDirection");
    set(north, "DirectionRatios", std::vector<double>{0, 1});
    set(modelContext, "TrueNorth", north);

    bodyContext = make("IfcGeometricRepresentationSubContext");
    set(bodyContext, "ContextIdentifier", std::string("Body"));
    set(bodyContext, "ContextType", std::string("Model"));
    set(bodyContext, "ParentContext", modelContext);
    setEnum(bodyContext, "TargetView", "MODEL_VIEW");
  }

  // One IfcSurfaceStyle per style; one IfcMaterial (with its definition representation) when the style came from a
  // material.
  void makeStyles() {
    // A material is made only for a style that an element (not a space or a spatial element) uses: IfcRelAssociatesMaterial relates
    // elements only, and a material nothing refers to would be dead weight.
    std::vector<bool> usedByElement(pkg.styles.size(), false);
    for (const auto& node : pkg.nodes)
      if ((node.role == "assembly" || node.role == "element") && node.mesh >= 0 && node.style >= 0) usedByElement[static_cast<std::size_t>(node.style)] = true;
    for (std::size_t i = 0; i < pkg.styles.size(); ++i) {
      const auto& style = pkg.styles[i];
      auto colour = make("IfcColourRgb");
      set(colour, "Red", style.color[0]);
      set(colour, "Green", style.color[1]);
      set(colour, "Blue", style.color[2]);
      auto rendering = make("IfcSurfaceStyleRendering");
      set(rendering, "SurfaceColour", colour);
      set(rendering, "Transparency", 1 - style.opacity);
      setEnum(rendering, "ReflectanceMethod", "NOTDEFINED");
      auto surface = make("IfcSurfaceStyle");
      set(surface, "Name", style.name);
      setEnum(surface, "Side", "BOTH");
      set(surface, "Styles", std::vector<Base>{rendering});
      styleEntity.push_back(surface);
      materialEntity.emplace_back();
      materialUsers.emplace_back();
      ++counts["styles"];
      if (!style.material || !usedByElement[i]) continue;
      auto material = make("IfcMaterial");
      set(material, "Name", style.name);
      auto styled = make("IfcStyledItem");
      set(styled, "Styles", std::vector<Base>{surface});
      auto representation = make("IfcStyledRepresentation");
      set(representation, "ContextOfItems", modelContext);
      set(representation, "RepresentationIdentifier", std::string("Style"));
      set(representation, "RepresentationType", std::string("Material"));
      set(representation, "Items", std::vector<Base>{styled});
      auto definition = make("IfcMaterialDefinitionRepresentation");
      set(definition, "Representations", std::vector<Base>{representation});
      set(definition, "RepresentedMaterial", material);
      materialEntity.back() = material;
      ++counts["materials"];
    }
  }

  // The IfcProductDefinitionShape of a node's mesh: one IfcTriangulatedFaceSet, its style, its colour map.
  Base makeShape(const ExportNode& node) {
    const auto& mesh = pkg.meshes[static_cast<std::size_t>(node.mesh)];
    auto points = make("IfcCartesianPointList3D");
    std::vector<std::vector<double>> coordinates;
    coordinates.reserve(mesh.vertices.size() / 3);
    for (std::size_t i = 0; i < mesh.vertices.size(); i += 3)
      coordinates.push_back({mesh.vertices[i], mesh.vertices[i + 1], mesh.vertices[i + 2]});
    set(points, "CoordList", coordinates);
    std::vector<std::vector<int64_t>> triangles;
    triangles.reserve(mesh.triangles.size() / 3);
    for (std::size_t i = 0; i < mesh.triangles.size(); i += 3)
      triangles.push_back({static_cast<int64_t>(mesh.triangles[i]) + 1, static_cast<int64_t>(mesh.triangles[i + 1]) + 1,
                           static_cast<int64_t>(mesh.triangles[i + 2]) + 1});
    auto faceSet = make("IfcTriangulatedFaceSet");
    set(faceSet, "Coordinates", points);
    if (mesh.closed) set(faceSet, "Closed", true);
    set(faceSet, "CoordIndex", triangles);
    if (node.style >= 0) {
      auto styled = make("IfcStyledItem");
      set(styled, "Item", faceSet);
      set(styled, "Styles", std::vector<Base>{styleEntity[static_cast<std::size_t>(node.style)]});
    }
    if (!mesh.colors.empty()) {
      auto list = make("IfcColourRgbList");
      std::vector<std::vector<double>> colours;
      for (const auto& rgb : mesh.colors) colours.push_back({shortDouble(rgb[0]), shortDouble(rgb[1]), shortDouble(rgb[2])});
      set(list, "ColourList", colours);
      auto map = make("IfcIndexedColourMap");
      set(map, "MappedTo", faceSet);
      set(map, "Colours", list);
      std::vector<int64_t> index;
      index.reserve(mesh.colorIndex.size());
      for (const auto i : mesh.colorIndex) index.push_back(static_cast<int64_t>(i) + 1);
      set(map, "ColourIndex", index);
      ++counts["colourMaps"];
    }
    auto shape = make("IfcShapeRepresentation");
    set(shape, "ContextOfItems", bodyContext);
    set(shape, "RepresentationIdentifier", std::string("Body"));
    set(shape, "RepresentationType", std::string("Tessellation"));
    set(shape, "Items", std::vector<Base>{faceSet});
    auto definition = make("IfcProductDefinitionShape");
    set(definition, "Representations", std::vector<Base>{shape});
    if (node.hidden) hiddenShapes.push_back(faceSet);
    ++counts["representations"];
    counts["triangles"] += static_cast<long long>(mesh.triangles.size() / 3);
    counts["vertices"] += static_cast<long long>(mesh.vertices.size() / 3);
    if (mesh.closed) ++counts["closedFaceSets"];
    return definition;
  }

  void makeProperties(const ExportNode& node, const Base& owned) {
    std::vector<std::string> order;
    std::map<std::string, std::vector<const ExportProperty*>> sets;
    for (const auto& property : node.properties) {
      if (!sets.contains(property.set)) order.push_back(property.set);
      sets[property.set].push_back(&property);
    }
    std::size_t index = 0;
    for (const auto& name : order) {
      std::vector<Base> values;
      for (const ExportProperty* property : sets[name]) {
        auto single = make("IfcPropertySingleValue");
        set(single, "Name", property->name);
        Base nominal;
        if (const auto* flag = std::get_if<bool>(&property->value)) {
          nominal = make("IfcBoolean");
          nominal.set_attribute_value(std::size_t(0), *flag);
        } else if (const auto* number = std::get_if<double>(&property->value)) {
          nominal = make("IfcReal");
          nominal.set_attribute_value(std::size_t(0), *number);
        } else {
          const auto& text = std::get<std::string>(property->value);
          nominal = make(codePoints(text) <= labelCodePoints ? "IfcLabel" : "IfcText");
          nominal.set_attribute_value(std::size_t(0), text);
        }
        set(single, "NominalValue", nominal);
        values.push_back(single);
        ++counts["properties"];
      }
      const std::string& owningGuid = node.guid;
      auto propertySet = make("IfcPropertySet");
      root(propertySet, aux("propertySet", owningGuid, index), name);
      set(propertySet, "HasProperties", values);
      auto defines = make("IfcRelDefinesByProperties");
      root(defines, aux("definesByProperties", owningGuid, index), {});
      set(defines, "RelatedObjects", std::vector<Base>{owned});
      set(defines, "RelatingPropertyDefinition", propertySet);
      ++counts["propertySets"];
      ++index;
    }
  }

  void makeNodes() {
    const std::size_t count = pkg.nodes.size();
    entity.assign(count, Base());
    placement.assign(count, Base());
    // Which nodes have a part of their own kind (a site of sites), found in one pass.
    std::vector<bool> composedOfSameRole(count, false);
    for (const auto& node : pkg.nodes)
      if (node.parent >= 0 && pkg.nodes[static_cast<std::size_t>(node.parent)].role == node.role)
        composedOfSameRole[static_cast<std::size_t>(node.parent)] = true;
    for (std::size_t i = 0; i < count; ++i) {
      const auto& node = pkg.nodes[i];
      const Base parentPlacement = node.parent >= 0 ? placement[static_cast<std::size_t>(node.parent)] : Base();
      Base made;
      if (node.role == "project") {
        made = make("IfcProject");
        root(made, node.guid, node.name);
        label(made, "LongName", node.longName);
        set(made, "RepresentationContexts", std::vector<Base>{modelContext});
        set(made, "UnitsInContext", units);
        project = made;
        ++counts["projects"];
      } else if (node.role == "site" || node.role == "building" || node.role == "storey") {
        made = make(node.role == "site" ? "IfcSite" : node.role == "building" ? "IfcBuilding" : "IfcBuildingStorey");
        root(made, node.guid, node.name);
        label(made, "LongName", node.longName);
        // (The project has no placement, so a site directly under it is placed in the project's coordinates.)
        placement[i] = localPlacement(parentPlacement, node.placement);
        set(made, "ObjectPlacement", placement[i]);
        // A site made of sites is COMPLEX and its parts PARTIAL (likewise buildings and storeys); anything else is an ELEMENT.
        const bool composed = composedOfSameRole[i];
        const bool part = node.parent > 0 && pkg.nodes[static_cast<std::size_t>(node.parent)].role == node.role;
        setEnum(made, "CompositionType", composed ? "COMPLEX" : part ? "PARTIAL" : "ELEMENT");
        if (node.role == "storey" && node.hasElevation) set(made, "Elevation", node.elevation);
        // A site, building or storey that has a body of its own (as an IFC import makes them) keeps it.
        if (node.mesh >= 0) set(made, "Representation", makeShape(node));
        ++counts[node.role == "site" ? "sites" : node.role == "building" ? "buildings" : "storeys"];
      } else {
        const bool space = node.role == "space";
        std::string written;
        if (space) {
          written = "IfcSpace";
          if (!node.requestedClass.empty() && node.requestedClass != "IfcSpace") ++fallbacks[{node.requestedClass, written, "spaceClass"}];
        } else {
          const auto resolved = resolveClass(node);
          written = resolved.name;
          if (!resolved.reason.empty()) ++fallbacks[{node.requestedClass, written, resolved.reason}];
        }
        made = make(written);
        root(made, node.guid, node.name);
        placement[i] = localPlacement(parentPlacement, node.placement);
        set(made, "ObjectPlacement", placement[i]);
        if (space) {
          label(made, "LongName", node.longName);
          setEnum(made, "CompositionType", "ELEMENT");
        }
        std::string objectType = node.objectType;
        if (written == "IfcBuildingElementProxy" && objectType.empty()) objectType = node.name;
        label(made, "ObjectType", objectType);
        if (!node.predefinedType.empty()) {
          if (has(made, "PredefinedType") && enumHas(made, "PredefinedType", node.predefinedType) &&
              !(node.predefinedType == "USERDEFINED" && objectType.empty())) {
            setEnum(made, "PredefinedType", node.predefinedType);
          } else {
            ++predefinedTypeDropped;
          }
        }
        if (node.mesh >= 0) {
          set(made, "Representation", makeShape(node));
          // A material is associated with elements only (IfcRelAssociatesMaterial.AllowedElements); a space keeps its style.
          if (!space && node.style >= 0 && pkg.styles[static_cast<std::size_t>(node.style)].material)
            materialUsers[static_cast<std::size_t>(node.style)].push_back(made);
        }
        ++counts[space ? "spaces" : node.role == "assembly" ? "assemblies" : "elements"];
        ++counts["classes:" + written];
      }
      entity[i] = made;
      if (!node.properties.empty()) makeProperties(node, made);
    }
  }

  // Aggregation and containment, one relationship per parent: the spatial structure aggregates its own children and
  // the spaces, assemblies aggregate their parts, and every other element is contained in its spatial parent (which
  // may be a space).
  void makeRelationships() {
    const std::size_t count = pkg.nodes.size();
    std::vector<std::vector<Base>> aggregated(count), contained(count);
    for (std::size_t i = 1; i < count; ++i) {
      const auto& node = pkg.nodes[i];
      const auto parent = static_cast<std::size_t>(node.parent);
      const std::string& parentRole = pkg.nodes[parent].role;
      const bool spatialParent = parentRole == "site" || parentRole == "building" || parentRole == "storey" || parentRole == "space";
      const bool spatialNode = node.role == "site" || node.role == "building" || node.role == "storey";
      if (spatialNode || node.role == "space" || !spatialParent)
        aggregated[parent].push_back(entity[i]);
      else
        contained[parent].push_back(entity[i]);
    }
    for (std::size_t i = 0; i < count; ++i) {
      if (!aggregated[i].empty()) {
        auto rel = make("IfcRelAggregates");
        root(rel, aux("aggregates", pkg.nodes[i].guid), {});
        set(rel, "RelatingObject", entity[i]);
        set(rel, "RelatedObjects", aggregated[i]);
        ++counts["aggregates"];
      }
      if (!contained[i].empty()) {
        auto rel = make("IfcRelContainedInSpatialStructure");
        root(rel, aux("contained", pkg.nodes[i].guid), {});
        set(rel, "RelatedElements", contained[i]);
        set(rel, "RelatingStructure", entity[i]);
        ++counts["containedIn"];
      }
    }
    for (std::size_t s = 0; s < materialUsers.size(); ++s) {
      if (materialUsers[s].empty()) continue;
      auto rel = make("IfcRelAssociatesMaterial");
      root(rel, aux("material", pkg.styles[s].name, s), {});
      set(rel, "RelatedObjects", materialUsers[s]);
      set(rel, "RelatingMaterial", materialEntity[s]);
      ++counts["materialAssociations"];
    }
  }

  void makeLayer() {
    if (hiddenShapes.empty()) return;
    auto layer = make("IfcPresentationLayerWithStyle");
    set(layer, "Name", std::string("SketchyUp hidden"));
    set(layer, "AssignedItems", hiddenShapes);
    set(layer, "LayerOn", boost::logic::tribool(false));
    set(layer, "LayerFrozen", boost::logic::tribool(false));
    set(layer, "LayerBlocked", boost::logic::tribool(false));
    set(layer, "LayerStyles", std::vector<Base>{});
    counts["layerAssigned"] = static_cast<long long>(hiddenShapes.size());
  }

  void makeGeoreference() {
    if (!pkg.georeference.write) return;
    auto crs = make("IfcProjectedCRS");
    set(crs, "Name", pkg.georeference.crs);
    set(crs, "MapUnit", metre);
    auto conversion = make("IfcMapConversion");
    set(conversion, "SourceCRS", modelContext);
    set(conversion, "TargetCRS", crs);
    set(conversion, "Eastings", pkg.georeference.eastings);
    set(conversion, "Northings", pkg.georeference.northings);
    set(conversion, "OrthogonalHeight", pkg.georeference.height);
    set(conversion, "XAxisAbscissa", 1.0);
    set(conversion, "XAxisOrdinate", 0.0);
    set(conversion, "Scale", 1.0);
    georeferenceWritten = true;
  }

  void build() {
    for (const char* key : {"projects", "sites", "buildings", "storeys", "spaces", "assemblies", "elements", "representations", "triangles",
                            "vertices", "closedFaceSets", "colourMaps", "styles", "materials", "propertySets", "properties", "aggregates",
                            "containedIn", "materialAssociations", "layerAssigned"})
      counts[key] = 0;
    for (const auto& node : pkg.nodes) used.insert(node.guid);
    progress.phase("build");
    writeHeader();
    makeOwnerHistory();
    makeUnitsAndContexts();
    makeStyles();
    makeNodes();
    makeRelationships();
    makeLayer();
    makeGeoreference();
    progress.done(pkg.nodes.size());
  }

  std::string finish(std::size_t bytes) const {
    std::map<std::string, long long> classes;
    long long entities = 0;
    for (const auto& entry : file) {
      ++entities;
      ++classes[entry.second.declaration().name()];
    }
    JsonOut json;
    json.beginObject().field("status", "ok").field("bytes", bytes).field("entities", entities);
    json.key("library").beginObject().field("name", "IfcOpenShell").field("version", IFCOPENSHELL_VERSION).field("commit", std::string(SKETCHYUP_IFC_PIN_COMMIT_STRING).substr(0, 8)).endObject();
    json.field("georeference", georeferenceWritten ? "written" : "none");
    json.key("classes").beginObject();
    for (const auto& [name, number] : classes) json.field(name, number);
    json.endObject();
    json.key("counts").beginObject();
    for (const auto& [name, number] : counts)
      if (name.rfind("classes:", 0) != 0) json.field(name, number);
    json.endObject();
    json.key("written").beginObject();
    for (const auto& [name, number] : counts)
      if (name.rfind("classes:", 0) == 0) json.field(name.substr(8), number);
    json.endObject();
    // At most 64 distinct requests are listed; the rest are counted together, so the result line stays short.
    json.key("fallbacks").beginArray();
    {
      std::size_t listed = 0;
      long long other = 0;
      for (const auto& [key, number] : fallbacks) {
        if (listed >= 64) {
          other += number;
          continue;
        }
        ++listed;
        json.beginObject()
            .field("requested", clip(std::get<0>(key), 64))
            .field("written", std::get<1>(key))
            .field("reason", std::get<2>(key))
            .field("count", number)
            .endObject();
      }
      if (other > 0) json.beginObject().field("requested", "other").field("written", "").field("reason", "other").field("count", other).endObject();
    }
    json.endArray();
    json.field("predefinedTypeDropped", predefinedTypeDropped);
    json.endObject();
    return json.str();
  }
};

std::string readAll(const std::string& path) {
  std::ifstream in(path, std::ios::binary);
  if (!in) throw Refusal("packageUnreadable", "The export package cannot be read.");
  in.seekg(0, std::ios::end);
  const auto size = static_cast<std::size_t>(in.tellg());
  if (size > ExportLimits::headerBytes + ExportLimits::manifestBytes + ExportLimits::meshBytes)
    throw Refusal("packageTooLarge", "The export package is over its bound.");
  in.seekg(0);
  std::string bytes(size, '\0');
  in.read(bytes.data(), static_cast<std::streamsize>(size));
  if (!in) throw Refusal("packageUnreadable", "The export package cannot be read.");
  return bytes;
}

}  // namespace

int runExport(const ExportOptions& options) {
  Progress progress;
  try {
    progress.phase("read");
    const std::string bytes = readAll(options.input);
    ExportPackage package;
    try {
      package = readExportPackage(bytes);
    } catch (const std::runtime_error& problem) {
      throw Refusal("packageInvalid", problem.what());
    }
    Builder builder(package, progress);
    builder.build();
    progress.phase("write");
    {
      std::ofstream out(options.output, std::ios::binary | std::ios::trunc);
      if (!out) throw Refusal("outputUnwritable", "The IFC file cannot be created.");
      out << builder.file;
      out.flush();
      if (!out) throw Refusal("outputTooLarge", "The IFC file could not be written completely (it may be over its size limit).");
    }
    std::size_t size = 0;
    {
      std::ifstream in(options.output, std::ios::binary | std::ios::ate);
      size = static_cast<std::size_t>(in.tellg());
    }
    if (size > maxIfcBytes) throw Refusal("outputTooLarge", "The IFC file is larger than 256 MiB.");
    std::printf("R %s\n", builder.finish(size).c_str());
    std::fflush(stdout);
    return 0;
  } catch (const Refusal& refusal) {
    JsonOut json;
    json.beginObject().field("status", "refused").field("code", refusal.code).field("message", std::string(refusal.what()).substr(0, 240)).endObject();
    std::printf("R %s\n", json.str().c_str());
    std::fflush(stdout);
    return 3;
  }
}

}  // namespace sketchyup::ifc
