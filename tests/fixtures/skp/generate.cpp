// R102.a: the synthetic .skp fixtures (tests/fixtures/skp/).
//
// Built with OpenSKP's own writer (packages/cpp, commit 23a15e8742e936694bf14aa029cb0e2862dce5b9),
// which can only write the legacy (v17) container; nothing it writes is a VFF file. The
// generator takes the output directory as its only argument and writes one file per fixture.
// The exact build + run command and the resulting SHA-256s are recorded in SKP-SOURCE.txt.
//
// R102.b adds scaled_instance.skp, mirrored_instance.skp and textured_face.skp (the last takes the
// checker image, checker.png). The component fixtures are not byte-reproducible: the writer stamps a
// random GUID and a timestamp on each definition, so a rerun changes component_instances.skp and
// nested_component.skp. The committed bytes are the reference (SKP-SOURCE.txt).
//
// Every fixture is small on purpose: the import test compares its package against the helper's
// --probe and the raw (inches) bounding box x 0.0254, so the expected values stay hand-checkable.

#include <openskp/create.hpp>

#include <cstdio>
#include <filesystem>
#include <iostream>
#include <stdexcept>

namespace fs = std::filesystem;

using openskp::AttributeBool;
using openskp::AttributeLength;
using openskp::Color3;
using openskp::ComponentDefinitionBuilder;
using openskp::FaceOptions;
using openskp::InstanceOptions;
using openskp::LayerOptions;
using openskp::Matrix3x3;
using openskp::Point3;
using openskp::UvCorrespondence;
using openskp::create;

// 1. A box: a 1 x 1 x 1 inch cube, six coplanar faces.
static void box(const fs::path& out) {
  auto b = create();
  b->add_face({Point3{0, 0, 0}, Point3{1, 0, 0}, Point3{1, 1, 0}, Point3{0, 1, 0}});
  b->add_face({Point3{0, 0, 1}, Point3{1, 0, 1}, Point3{1, 1, 1}, Point3{0, 1, 1}});
  b->add_face({Point3{0, 0, 0}, Point3{1, 0, 0}, Point3{1, 0, 1}, Point3{0, 0, 1}});
  b->add_face({Point3{0, 0, 0}, Point3{0, 1, 0}, Point3{0, 1, 1}, Point3{0, 0, 1}});
  b->add_face({Point3{1, 0, 0}, Point3{1, 1, 0}, Point3{1, 1, 1}, Point3{1, 0, 1}});
  b->add_face({Point3{0, 1, 0}, Point3{1, 1, 0}, Point3{1, 1, 1}, Point3{0, 1, 1}});
  b->save(out);
}

// 2. A face with a hole: a 4 x 4 square with a 1 x 1 square cut out of its middle.
static void faceWithHole(const fs::path& out) {
  auto b = create();
  FaceOptions o;
  o.holes = {{Point3{1.5, 1.5, 0}, Point3{2.5, 1.5, 0}, Point3{2.5, 2.5, 0},
              Point3{1.5, 2.5, 0}}};
  b->add_face({Point3{0, 0, 0}, Point3{4, 0, 0}, Point3{4, 4, 0}, Point3{0, 4, 0}}, o);
  b->save(out);
}

// 3. Soft and smooth edges: two adjacent faces, the lower declared soft, the upper smooth.
// The shared middle edge keeps the flags of its first declaration (soft); each face's outer
// edges carry that face's own flag, so the file has both a soft and a smooth edge.
static void softSmooth(const fs::path& out) {
  auto b = create();
  FaceOptions soft;
  soft.soft_edges = true;
  b->add_face({Point3{0, 0, 0}, Point3{1, 0, 0}, Point3{1, 0.5, 0}, Point3{0, 0.5, 0}}, soft);
  FaceOptions smooth;
  smooth.smooth_edges = true;
  b->add_face({Point3{0, 0.5, 0}, Point3{1, 0.5, 0}, Point3{1, 1, 0}, Point3{0, 1, 0}}, smooth);
  b->save(out);
}

// 4. Two materials: two adjacent faces, each painted its own solid colour.
static void twoMaterials(const fs::path& out) {
  auto b = create();
  const int red = b->add_material("Red", Color3{255, 0, 0});
  const int blue = b->add_material("Blue", Color3{0, 0, 255});
  FaceOptions redFace;
  redFace.material = red;
  b->add_face({Point3{0, 0, 0}, Point3{1, 0, 0}, Point3{1, 1, 0}, Point3{0, 1, 0}}, redFace);
  FaceOptions blueFace;
  blueFace.material = blue;
  b->add_face({Point3{1, 0, 0}, Point3{2, 0, 0}, Point3{2, 1, 0}, Point3{1, 1, 0}}, blueFace);
  b->save(out);
}

// 5. A component with 3 instances: a "Widget" square placed three times, the third hidden.
static void componentInstances(const fs::path& out) {
  auto b = create();
  auto& widget = b->add_component_definition("Widget");
  widget.add_face({Point3{0, 0, 0}, Point3{1, 0, 0}, Point3{1, 1, 0}, Point3{0, 1, 0}});
  widget.close();
  b->add_instance(widget);
  InstanceOptions atTwo;
  atTwo.translation = {2, 0, 0};
  b->add_instance(widget, atTwo);
  InstanceOptions hidden;
  hidden.translation = {0, 2, 0};
  hidden.hidden = true;
  b->add_instance(widget, hidden);
  b->save(out);
}

// 6. A hidden tag: a layer set to hidden, with a face assigned to it.
static void hiddenTag(const fs::path& out) {
  auto b = create();
  LayerOptions hidden;
  hidden.hidden = true;
  const int layer = b->add_layer("Off", hidden);
  FaceOptions onLayer;
  onLayer.layer = layer;
  b->add_face({Point3{0, 0, 0}, Point3{1, 0, 0}, Point3{1, 1, 0}, Point3{0, 1, 0}}, onLayer);
  b->save(out);
}

// 7. A nested component: "Outer" contains a face and an instance of the already-closed
// "Inner", and is itself placed once at the model root.
static void nestedComponent(const fs::path& out) {
  auto b = create();
  auto& inner = b->add_component_definition("Inner");
  inner.add_face({Point3{0, 0, 0}, Point3{1, 0, 0}, Point3{1, 1, 0}, Point3{0, 1, 0}});
  inner.close();
  auto& outer = b->add_component_definition("Outer");
  outer.add_face({Point3{0, 0, 0}, Point3{2, 0, 0}, Point3{2, 2, 0}, Point3{0, 2, 0}});
  InstanceOptions placed;
  placed.translation = {0.5, 0.5, 0};
  outer.add_instance(inner, placed);
  outer.close();
  b->add_instance(outer);
  b->save(out);
}

// 8. A scaled instance (R102.b): a 1-inch block with outward faces, placed with a sheared,
// non-uniform 3x3 (the scale and the shear live in the placement's matrix; the writer writes w = 1).
// Row-major: x' = 2x + 0.5y, y' = y, z' = 1.5z, then moved to (1, 2, 3) inches.
static void addBlock(ComponentDefinitionBuilder& block) {
  block.add_face({Point3{0, 0, 0}, Point3{0, 1, 0}, Point3{1, 1, 0}, Point3{1, 0, 0}});  // bottom
  block.add_face({Point3{0, 0, 1}, Point3{1, 0, 1}, Point3{1, 1, 1}, Point3{0, 1, 1}});  // top
  block.add_face({Point3{0, 0, 0}, Point3{1, 0, 0}, Point3{1, 0, 1}, Point3{0, 0, 1}});  // front (y = 0)
  block.add_face({Point3{0, 1, 0}, Point3{0, 1, 1}, Point3{1, 1, 1}, Point3{1, 1, 0}});  // back (y = 1)
  block.add_face({Point3{0, 0, 0}, Point3{0, 0, 1}, Point3{0, 1, 1}, Point3{0, 1, 0}});  // left (x = 0)
  block.add_face({Point3{1, 0, 0}, Point3{1, 1, 0}, Point3{1, 1, 1}, Point3{1, 0, 1}});  // right (x = 1)
  block.close();
}
static void scaledInstance(const fs::path& out) {
  auto b = create();
  auto& block = b->add_component_definition("Block");
  addBlock(block);
  InstanceOptions scaled;
  scaled.translation = {1, 2, 3};
  scaled.matrix3x3 = Matrix3x3{2, 0.5, 0, 0, 1, 0, 0, 0, 1.5};
  b->add_instance(block, scaled);
  b->save(out);
}

// 9. A mirrored instance (R102.b): the same block, mirrored in X (determinant -1), moved to x = 2.
static void mirroredInstance(const fs::path& out) {
  auto b = create();
  auto& block = b->add_component_definition("Block");
  addBlock(block);
  InstanceOptions mirrored;
  mirrored.translation = {2, 0, 0};
  mirrored.matrix3x3 = Matrix3x3{-1, 0, 0, 0, 1, 0, 0, 0, 1};
  b->add_instance(block, mirrored);
  b->save(out);
}

// 10. A textured face (R102.b): a 4 x 4 square with a checker image, positioned by three explicit
// (point, (u, v)) pairs, so the stored UV transform is known exactly. The image is applied 2 inches
// to a tile (the texture's own width and height in the file).
static void texturedFace(const fs::path& out, const fs::path& image) {
  auto b = create();
  const int checker = b->add_texture_material("Checker", image, 2.0, 2.0);
  FaceOptions o;
  o.material = checker;
  o.front_uv = UvCorrespondence{{Point3{0, 0, 0}, {0, 0}}, {Point3{4, 0, 0}, {1, 0}}, {Point3{0, 4, 0}, {0, 1}}};
  b->add_face({Point3{0, 0, 0}, Point3{4, 0, 0}, Point3{4, 4, 0}, Point3{0, 4, 0}}, o);
  b->save(out);
}

// 11. Named instances with attributes (R102.b): two placements of one "Chair", named. The first
// carries a string, an integer, a float, a boolean and a Length attribute (a Length is not imported);
// the second carries none.
static void namedAttributes(const fs::path& out) {
  auto b = create();
  auto& chair = b->add_component_definition("Chair");
  chair.add_face({Point3{0, 0, 0}, Point3{1, 0, 0}, Point3{1, 1, 0}, Point3{0, 1, 0}});
  chair.close();
  InstanceOptions first;
  first.name = std::string("Chair A");
  first.attributes = {{"Material", std::string("Oak")},
                      {"Count", std::int32_t(3)},
                      {"Price", 9.5},
                      {"Fire", AttributeBool{true}},
                      {"Span", AttributeLength{12.0}}};
  b->add_instance(chair, first);
  InstanceOptions second;
  second.name = std::string("Chair B");
  second.translation = {2, 0, 0};
  b->add_instance(chair, second);
  b->save(out);
}

// ADR 0176 "Follow-up: attribute reporting" (2026-10-26): the attributes the import does not carry. A
// definition with its own dictionary "Spec" (four values: a string, a float, a boolean and a Length, none of
// them carried), and one instance whose dictionary "Props" holds a Length (not carried), a key of 130 bytes
// (over the 128-byte key cap) and 130 integers, so the 128-property cap drops two of them. The legacy writer
// refuses a string value over 255 bytes, so no fixture holds an over-2,048-byte string: the truncation is
// tested with an in-test package (tests/skp_mapping_tests.cpp).
static void attributeReporting(const fs::path& out) {
  auto b = create();
  openskp::DefinitionOptions spec;
  spec.attribute_dict_name = "Spec";
  spec.attributes = {{"Finish", std::string("Oak")},
                     {"Weight", 1.5},
                     {"Fire", AttributeBool{true}},
                     {"Span", AttributeLength{12.0}}};
  auto& chair = b->add_component_definition("Chair", spec);
  chair.add_face({Point3{0, 0, 0}, Point3{1, 0, 0}, Point3{1, 1, 0}, Point3{0, 1, 0}});
  chair.close();
  InstanceOptions placed;
  placed.name = std::string("Chair A");
  placed.attribute_dict_name = "Props";
  placed.attributes = {{std::string(130, 'k'), std::int32_t(1)},
                       {"Span", AttributeLength{12.0}}};
  for (int i = 0; i < 130; ++i) {
    char key[16];
    std::snprintf(key, sizeof key, "Item%03d", i);
    placed.attributes[key] = std::int32_t(i);
  }
  b->add_instance(chair, placed);
  b->save(out);
}

int main(int argc, char** argv) {
  if (argc != 3) {
    std::cerr << "usage: skp_fixture_generator OUTPUT_DIR CHECKER_PNG\n";
    return 2;
  }
  const fs::path dir = argv[1];
  const fs::path checker = argv[2];
  std::error_code ec;
  fs::create_directories(dir, ec);
  struct {
    const char* name;
    void (*write)(const fs::path&);
  } const fixtures[] = {
      {"box.skp", box},
      {"face_with_hole.skp", faceWithHole},
      {"soft_smooth_edges.skp", softSmooth},
      {"two_materials.skp", twoMaterials},
      {"component_instances.skp", componentInstances},
      {"hidden_tag.skp", hiddenTag},
      {"nested_component.skp", nestedComponent},
      {"scaled_instance.skp", scaledInstance},
      {"mirrored_instance.skp", mirroredInstance},
  };
  for (const auto& f : fixtures) {
    const fs::path path = dir / f.name;
    f.write(path);
    std::cout << f.name << " " << fs::file_size(path) << " bytes\n";
  }
  const fs::path textured = dir / "textured_face.skp";
  texturedFace(textured, checker);
  std::cout << "textured_face.skp " << fs::file_size(textured) << " bytes\n";
  const fs::path named = dir / "named_attributes.skp";
  namedAttributes(named);
  std::cout << "named_attributes.skp " << fs::file_size(named) << " bytes\n";
  const fs::path reporting = dir / "attribute_reporting.skp";
  attributeReporting(reporting);
  std::cout << "attribute_reporting.skp " << fs::file_size(reporting) << " bytes\n";
  return 0;
}
