// The relationships of an IFC file that the import reads (ADR 0171 sections 3.1, 3.2, 3.4, 3.7),
// gathered in one pass over each relationship class so that no schema's inverse attribute names
// are needed. Everything is keyed by STEP id; lists are in ascending relationship id then list
// order, which is the order the file defines.
#pragma once

#include <map>
#include <set>
#include <string>
#include <vector>

#include "ifc/access.hpp"
#include "ifc/report.hpp"
#include "ifc/units.hpp"

namespace sketchyup::ifc {

struct Model {
  ifcopenshell::file* file{};
  std::string schema;
  Inst project;
  Units units;

  std::map<int, std::vector<int>> aggregated;  // relating object -> related objects
  std::map<int, int> aggregateParent;
  std::map<int, std::vector<int>> contained;  // relating structure -> related elements
  std::map<int, int> container;
  std::map<int, std::vector<int>> openingsOf;  // host -> opening elements
  std::map<int, int> hostOf;                   // opening -> host
  std::map<int, int> typeOf;                   // occurrence -> type object
  std::map<int, std::vector<Inst>> propertySetsOf;
  std::map<int, std::vector<Inst>> materialsOf;  // the RelatingMaterial instances
  std::map<int, std::vector<Inst>> classificationsOf;
  std::map<int, std::vector<Inst>> groupsOf;
  std::map<int, std::vector<Inst>> layersOf;  // by the step id of the layered representation or item
  // Relationship instances whose related objects were read (they are "supporting" in the
  // accounting once any of their objects is imported).
  std::map<int, std::vector<int>> relationshipsOf;  // object step -> relationship steps that name it

  long long multipleParents{};
  long long cycles{};
  // Objects on a cycle of IfcRelAggregates, or below one: the library walks the decomposition upwards
  // without a visited set, so a product like that would hang it. They are never given to it.
  std::set<int> cyclic;

  void build(ifcopenshell::file& source, Report& report);
};

// A property or quantity as read for one object.
struct ReadProperty {
  std::string set, name;
  int origin{};  // 0 occurrence property, 1 occurrence quantity, 2 type property, 3 type quantity
  enum class Kind { Bool, Real, Text } kind{Kind::Text};
  bool boolean{};
  double real{};
  std::string text;
};

// Reads the property sets and quantity sets of `object` (occurrence) and of its type, in the
// precedence order of ADR 0171 section 3.4, into `out`. `used` collects every instance read, for
// the accounting.
void readProperties(const Model& model, const Inst& object, const Inst& type, std::vector<ReadProperty>& out,
                    Report& report, std::set<int>& used);

struct MaterialDescription {
  std::string name;    // ifc.Material
  std::string layers;  // ifc.MaterialLayers
  bool layered{false};
};
// The material association(s) of an object, described; the associated instances join `used`.
// An object without its own association takes its type's.
MaterialDescription describeMaterial(const Model& model, const Inst& object, const Inst& type, Report& report,
                                     std::set<int>& used);

// ifc.Classification and ifc.ClassificationUri.
void describeClassification(const Model& model, const Inst& object, std::string& label, std::string& uri,
                            std::set<int>& used);

// The names of the groups, systems and zones the object belongs to, comma-joined.
std::string describeGroups(const Model& model, const Inst& object, std::set<int>& used);

struct LayerState {
  std::string names;
  bool hidden{false};
};
LayerState describeLayers(const Model& model, const Inst& product, std::set<int>& used);

// The compressed GlobalId alphabet and length.
bool validGlobalId(const std::string& id);

}  // namespace sketchyup::ifc
