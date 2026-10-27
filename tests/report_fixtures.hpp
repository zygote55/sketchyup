#pragma once
// The R101 report fixtures (ADR 0175 §11). The builders are shared by the engine tests and the
// output tests, so quantities-a is the same document wherever it is built.
#include "core/components.hpp"
#include "core/entity_measure.hpp"
#include "core/groups.hpp"
#include "core/materials.hpp"
#include "core/model.hpp"
#include "core/selection.hpp"
#include "core/tags.hpp"
#include "core/terrain.hpp"
#include "core/terrain_records.hpp"
#include "core/transform.hpp"
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace sketchy::report_fixtures {

// A box with its base at `origin`, extruded upward (the Document's own construction).
Id box(Document &doc, Vec3 origin, Vec3 size, const std::string &name = "Box") {
    const auto x = origin.x + size.x, y = origin.y + size.y;
    const auto id = doc.addFace(
        {{origin, {x, origin.y, origin.z}, {x, y, origin.z}, {origin.x, y, origin.z}}}, name);
    doc.extrude(id, doc.bodies().at(id)->surface.faces.begin()->first, size.z);
    return id;
}

// A body made from a raw surface, the way Document::addFace makes one (Draw face).
Id addSurface(Document &doc, const Surface &surface, const std::string &name) {
    auto body = std::make_shared<Body>();
    body->id = doc.nextId();
    body->name = name;
    body->surface = surface;
    doc.apply({"Draw face", {{body->id, nullptr, body}}}, doc.revision());
    return body->id;
}

// Appends every face of `source` to `target`, as its own loops.
void append(Surface &target, const Surface &source) {
    for (const auto &[id, face] : source.faces) {
        std::vector<std::vector<Vec3>> loops;
        for (const auto &loop : face.loops) {
            loops.emplace_back();
            for (auto vertex : loop)
                loops.back().push_back(source.vertices.at(vertex));
        }
        target.addFace(loops);
    }
}

// A closed prism from a base face extruded upward: the solid shapes of the fixtures.
Surface prism(const std::vector<std::vector<Vec3>> &loops, double depth) {
    Surface surface;
    surface.extrude(surface.addFace(loops), depth);
    return surface;
}

Surface unitCube(Vec3 origin) {
    return prism({{origin, origin + Vec3{1, 0, 0}, origin + Vec3{1, 1, 0}, origin + Vec3{0, 1, 0}}},
                 1);
}

// A group with no geometry of its own (a component root with no members, Table).
Id emptyGroup(Document &doc, const std::string &name) {
    auto body = std::make_shared<Body>();
    body->id = doc.nextId();
    body->name = name;
    body->kind = BodyKind::Group;
    doc.apply({"Create group", {{body->id, nullptr, body}}}, doc.revision());
    return body->id;
}

// Flat terrain: a (n × n) grid of `spacing` metres at z = 0, two triangles per cell.
TerrainMesh flatGrid(int n, double spacing) {
    TerrainMesh mesh;
    for (int j = 0; j <= n; ++j)
        for (int i = 0; i <= n; ++i)
            mesh.vertices.push_back({i * spacing, j * spacing, 0});
    auto at = [&](int i, int j) { return std::uint32_t(j * (n + 1) + i); };
    for (int j = 0; j < n; ++j)
        for (int i = 0; i < n; ++i) {
            mesh.triangles.push_back({at(i, j), at(i + 1, j), at(i + 1, j + 1)});
            mesh.triangles.push_back({at(i, j), at(i + 1, j + 1), at(i, j + 1)});
        }
    return mesh;
}

TerrainRecord terrainRecord(const TerrainMesh &mesh) {
    TerrainRecord result;
    result.generator = "test/terrain-v1";
    result.parameters.maxEdgeLength = 3;
    TerrainSource contour;
    contour.role = TerrainSourceRole::Contour;
    contour.reference = "DXF handle 2F";
    contour.points = {mesh.vertices.front(), mesh.vertices.back()};
    result.sources.push_back(contour);
    return result;
}

// The ADR's ids for components and tags, read back from the document.
struct Q1 {
    Id steel{}, wood{}, boxesTag{}, boxDefinition{}, postDefinition{}, topDefinition{},
        tableDefinition{};
    std::vector<Id> boxes, tables;
};

// createComponent leaves its source as the definition's first placement. The ADR counts the
// placements it names explicitly, so the source placement is erased once its definition exists.
ComponentResult definition(Document &doc, Id source, const std::string &name) {
    auto made = createComponent(doc, source, name);
    Selection selection;
    selection.apply(doc, {{made.instance, SelectionKind::Body, 0}}, SelectionMode::Replace);
    eraseSelected(doc, selection);
    return made;
}

Q1 buildComponents(Document &doc) {
    Q1 q;
    q.steel =
        createMaterial(doc, "Steel, \"galvanised\"", {.5f, .5f, .5f}); // V10: a comma and quotes
    q.wood = createMaterial(doc, "Wood", {.6f, .4f, .2f});
    q.boxesTag = createTag(doc, "Boxes");
    // BoxP: 2 × 2 × 1 m.
    q.boxDefinition = definition(doc, box(doc, {0, 0, 0}, {2, 2, 1}), "BoxP").definition;
    // Post: 0.5 × 0.5 × 2 m, painted Steel before it becomes a definition.
    const auto post = box(doc, {0, 0, 0}, {.5, .5, 2}, "Post");
    assignMaterial(doc, post, std::nullopt, q.steel);
    q.postDefinition = definition(doc, post, "Post").definition;
    // Top: 2 × 1 × 0.1 m, painted Wood.
    const auto top = box(doc, {0, 0, 0}, {2, 1, .1}, "Top");
    assignMaterial(doc, top, std::nullopt, q.wood);
    q.topDefinition = definition(doc, top, "Top").definition;
    // Table: four Post references and one Top reference, no members of its own.
    q.tableDefinition = definition(doc, emptyGroup(doc, "Table"), "Table").definition;
    editComponentDefinition(doc, q.tableDefinition, [&](Document &draft) {
        for (int i = 0; i < 4; ++i)
            placeComponent(draft, q.postDefinition,
                           Transform::translation({.25 + i * .75, .25, 0}));
        placeComponent(draft, q.topDefinition, Transform::translation({0, 0, 2}));
        return ChangeReport{};
    });
    for (int i = 0; i < 3; ++i) {
        const auto placement =
            placeComponent(doc, q.boxDefinition, Transform::translation({i * 5., 0, 0}));
        assignTag(doc, placement.instance, q.boxesTag);
        q.boxes.push_back(placement.instance);
    }
    q.tables.push_back(
        placeComponent(doc, q.tableDefinition, Transform::translation({0, 10, 0})).instance);
    q.tables.push_back(
        placeComponent(doc, q.tableDefinition, Transform::translation({0, 15, 0})).instance);
    return q;
}

// The quantities-a document: Q1 with Q2's copies and hidden content, Q3's invalid solids and the
// 200 m × 200 m terrain (Q4). The Stash group and the terrain are returned for the later checks.
struct QuantitiesA {
    Id stash{}, mirrored{}, terrain{};
    std::size_t tiles{};
};

QuantitiesA buildQuantitiesA(Document &doc) {
    QuantitiesA a;
    buildComponents(doc);
    // Q2: a scaled copy, a mirrored copy and the three-level chain, each a plain body.
    const auto scaled = box(doc, {10, 0, 0}, {2, 2, 1}, "Scaled");
    doc.transform(scaled, Transform::scaling({2, 2, 2}));
    a.mirrored = box(doc, {20, 0, 0}, {2, 2, 1}, "Mirrored");
    doc.transform(a.mirrored, Transform::scaling({-1, 1, 1}));
    const auto cube = box(doc, {30, 0, 0}, {1, 1, 1}, "Chain cube");
    const auto child = createGroup(doc, {cube}, "Chain child");
    const auto root = createGroup(doc, {child}, "Chain root");
    doc.transform(child, Transform::scaling({2, 2, 2}), root);
    doc.transform(root, Transform::scaling({1.5, 1.5, 1.5}));
    // Hidden content: a hidden group holding a unit cube, and a 2 × 2 × 0.5 box on a hidden tag.
    const auto stashed = box(doc, {40, 0, 0}, {1, 1, 1}, "Stash box");
    a.stash = createGroup(doc, {stashed}, "Stash");
    setEntityState(doc, a.stash, true, std::nullopt);
    const auto off = createTag(doc, "Off");
    editTag(doc, off, std::nullopt, std::nullopt, false);
    assignTag(doc, box(doc, {50, 0, 0}, {2, 2, .5}, "Off box"), off);
    // Q3: the open plate and the shared-face cubes.
    doc.addFace({{{60, 0, 0}, {61, 0, 0}, {61, 2, 0}, {60, 2, 0}}}, "Plate");
    auto pair = unitCube({70, 0, 0});
    append(pair, unitCube({70, 0, 1}));
    addSurface(doc, pair, "Shared-face cubes");
    // Q4: the terrain.
    const auto mesh = flatGrid(200, 1.);
    const auto created = createTerrain(doc, mesh, terrainRecord(mesh));
    a.terrain = created.terrain;
    a.tiles = created.tiles.size();
    return a;
}

// quantities-b (ADR 0175 §11, §12), the budget fixture: a 250,000-triangle terrain (353 × 353
// cells, 249,218 triangles, inside the terrain limit) and `placements` placements of a 12-face part
// (two joined 6-face boxes), one component. Built in memory; the budgets run on this document.
inline Document buildQuantitiesB(std::size_t placements) {
    Document doc;
    const auto mesh = flatGrid(353, 1.);
    createTerrain(doc, mesh, terrainRecord(mesh));
    // The part: two 6-face boxes in one group, made a component (the source placement is removed).
    const auto first = box(doc, {0, 0, 0}, {.5, .5, 2});
    const auto second = box(doc, {1, 0, 0}, {.5, .5, 2});
    const auto group = createGroup(doc, {first, second}, "Part");
    const auto definition = createComponent(doc, group, "Part").definition;
    Selection origin;
    origin.apply(doc, {{doc.instances().begin()->first, SelectionKind::Body, 0}},
                 SelectionMode::Replace);
    eraseSelected(doc, origin);
    const std::size_t columns = 100;
    for (std::size_t i = 0; i < placements; ++i)
        placeComponent(
            doc, definition,
            Transform::translation({double(i % columns) * 2, double(i / columns) * 2 + 10, 0}));
    return doc;
}

} // namespace sketchy::report_fixtures
