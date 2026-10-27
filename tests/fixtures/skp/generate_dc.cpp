// R104.a (ADR 0178, decision 6): the self-authored Dynamic Component fixtures (tests/fixtures/skp/dc_*.skp).
//
// Written by OpenSKP's own C++ writer at the commit in SKP-SOURCE.txt, the way generate.cpp writes the
// other fixtures. The fixtures prove how the importer handles the dynamic_attributes shape. They do not
// prove SketchUp's own key spellings, and they do not prove whether SketchUp stores a Dynamic Component
// length as a Float: the owner has no sample file (ADR 0178 decision 6). The writer makes only the
// legacy (v17) container, and it refuses a string over 255 bytes and cannot write a vector, an array or
// a null. So no fixture holds an over-2,048-byte formula, or a value the package cannot carry: those
// paths are in-test packages (tests/skp_dc_tests.cpp).
//
// The build and run command is in SKP-SOURCE.txt. It takes the output directory as its only argument.

#include <openskp/create.hpp>

#include <cstdint>
#include <filesystem>
#include <iostream>
#include <string>

namespace fs = std::filesystem;

using openskp::AttributeBool;
using openskp::AttributeLength;
using openskp::DefinitionOptions;
using openskp::InstanceOptions;
using openskp::Point3;
using openskp::create;

namespace {

// A square face of the given size in inches: the geometry each definition here carries.
void addSquare(openskp::ComponentDefinitionBuilder& definition, double size) {
  definition.add_face({Point3{0, 0, 0}, Point3{size, 0, 0}, Point3{size, size, 0}, Point3{0, size, 0}});
}

// (a) Instance-level values of every kind the import reads: a String, an Integer, a Float, a Boolean and
// two Lengths, 12 inches and 1 inch (0.3048 m and 0.0254 m once converted).
void instanceKinds(const fs::path& out) {
  auto b = create();
  auto& panel = b->add_component_definition("Panel");
  addSquare(panel, 1);
  panel.close();
  InstanceOptions placed;
  placed.name = std::string("Panel A");
  placed.attribute_dict_name = "dynamic_attributes";
  placed.attributes = {{"Label", std::string("Oak panel")},
                       {"Qty", std::int32_t(3)},
                       {"Weight", 1.5},
                       {"Flag", AttributeBool{true}},
                       {"LenX", AttributeLength{12.0}},
                       {"Unit", AttributeLength{1.0}}};
  b->add_instance(panel, placed);
  b->save(out);
}

// (b) Definition-level values. "Desk" carries a Length (LenX, 36 inches), two formulas and an onClick
// that calls ALERT and REDRAW. Three placements: "Desk A" and "Desk B" at the root, and "Desk C" nested
// in "Room". "Desk B" names its own LenX (24 inches), so its instance value wins over the definition's.
void definitionKeys(const fs::path& out) {
  auto b = create();
  DefinitionOptions deskOptions;
  deskOptions.attribute_dict_name = "dynamic_attributes";
  deskOptions.attributes = {{"LenX", AttributeLength{36.0}},
                            {"_LenX_formula", std::string("LenX+10")},
                            {"_material_formula", std::string("IF(LenX>1,1,0)")},
                            {"onClick", std::string("ALERT(\"Hi\"); REDRAW()")}};
  auto& desk = b->add_component_definition("Desk", deskOptions);
  addSquare(desk, 1);
  desk.close();
  auto& room = b->add_component_definition("Room");
  addSquare(room, 4);
  InstanceOptions nested;
  nested.name = std::string("Desk C");
  nested.translation = {5, 0, 0};
  room.add_instance(desk, nested);
  room.close();
  InstanceOptions first;
  first.name = std::string("Desk A");
  b->add_instance(desk, first);
  InstanceOptions second;
  second.name = std::string("Desk B");
  second.translation = {10, 0, 0};
  second.attribute_dict_name = "dynamic_attributes";
  second.attributes = {{"LenX", AttributeLength{24.0}}};
  b->add_instance(desk, second);
  InstanceOptions roomPlaced;
  roomPlaced.name = std::string("Room A");
  roomPlaced.translation = {0, 10, 0};
  b->add_instance(room, roomPlaced);
  b->save(out);
}

// (c) Copies and a formula. "Shelf A" has Copies 3 (an Integer) and a 200-byte formula, which is under the
// writer's 255-byte limit and under the import's 2,048-byte cap. "Shelf B" has Copies 0 (a Float).
void copiesLimits(const fs::path& out) {
  auto b = create();
  auto& shelf = b->add_component_definition("Shelf");
  addSquare(shelf, 1);
  shelf.close();
  InstanceOptions first;
  first.name = std::string("Shelf A");
  first.attribute_dict_name = "dynamic_attributes";
  first.attributes = {{"Copies", std::int32_t(3)}, {"_Qty_formula", std::string(200, 'x')}};
  b->add_instance(shelf, first);
  InstanceOptions second;
  second.name = std::string("Shelf B");
  second.translation = {2, 0, 0};
  second.attribute_dict_name = "dynamic_attributes";
  second.attributes = {{"Copies", 0.0}};
  b->add_instance(shelf, second);
  b->save(out);
}

}  // namespace

int main(int argc, char** argv) {
  if (argc != 2) {
    std::cerr << "usage: skp_dc_fixture_generator OUTPUT_DIR\n";
    return 2;
  }
  const fs::path dir = argv[1];
  std::error_code ec;
  fs::create_directories(dir, ec);
  struct {
    const char* name;
    void (*write)(const fs::path&);
  } const fixtures[] = {
      {"dc_instance_kinds.skp", instanceKinds},
      {"dc_definition_keys.skp", definitionKeys},
      {"dc_copies_limits.skp", copiesLimits},
  };
  for (const auto& f : fixtures) {
    const fs::path path = dir / f.name;
    f.write(path);
    std::cout << f.name << " " << fs::file_size(path) << " bytes\n";
  }
  return 0;
}
