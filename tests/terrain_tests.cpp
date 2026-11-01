// R095.a terrain core (ADR 0168 §2, §4): tiling, the terrain limit class, refusal of
// generic topology tools, Explode, the mesh digest and "changed since generated", and
// the document site origin with re-anchoring and model <-> site conversion.
#include "core/components.hpp"
#include "core/document_limits.hpp"
#include "core/edge_appearance.hpp"
#include "core/face_orientation.hpp"
#include "core/groups.hpp"
#include "core/intersection_edit.hpp"
#include "core/scenes.hpp"
#include "core/sections.hpp"
#include "core/selection.hpp"
#include "core/solid_boolean.hpp"
#include "core/tags.hpp"
#include "core/terrain.hpp"
#include "core/transform_selection.hpp"
#include <chrono>
#include <cmath>
#include <functional>
#include <iostream>
#include <set>
#include <stdexcept>
#include <string>
#include <sys/resource.h>
#include <sys/wait.h>
#include <unistd.h>
using namespace sketchy;
namespace {
void check(bool value, const std::string &message) {
    if (!value)
        throw std::runtime_error(message);
}
// The message of the refusal, or empty when the operation succeeds.
template <class F> std::string rejection(F operation, std::string *code = nullptr) {
    try {
        operation();
    } catch (const TerrainError &error) {
        if (code)
            *code = error.code();
        return error.what();
    } catch (const std::exception &error) {
        if (code)
            *code = {};
        return error.what();
    }
    return {};
}
// A gently rolling (nx x ny)-cell grid, two counter-clockwise triangles per cell.
TerrainMesh grid(int nx, int ny, double spacing = 1, Vec3 origin = {}) {
    TerrainMesh mesh;
    for (int j = 0; j <= ny; ++j)
        for (int i = 0; i <= nx; ++i)
            mesh.vertices.push_back({origin.x + i * spacing, origin.y + j * spacing,
                                     origin.z + .25 * std::sin(i * .3) + .15 * std::cos(j * .2)});
    auto at = [&](int i, int j) { return std::uint32_t(j * (nx + 1) + i); };
    for (int j = 0; j < ny; ++j)
        for (int i = 0; i < nx; ++i) {
            mesh.triangles.push_back({at(i, j), at(i + 1, j), at(i + 1, j + 1)});
            mesh.triangles.push_back({at(i, j), at(i + 1, j + 1), at(i, j + 1)});
        }
    return mesh;
}
TerrainRecord record(const TerrainMesh &mesh) {
    TerrainRecord result;
    result.generator = "test/terrain-v1";
    result.parameters.maxEdgeLength = 3;
    TerrainSource contour;
    contour.role = TerrainSourceRole::Contour;
    contour.reference = "DXF handle 2F";
    contour.points = {mesh.vertices.front(), mesh.vertices.back()};
    result.sources.push_back(contour);
    TerrainSource points;
    points.role = TerrainSourceRole::Points;
    points.reference = "CSV line 4";
    points.points = {mesh.vertices[1]};
    result.sources.push_back(points);
    return result;
}
Id firstFace(const Document &doc, Id body) {
    return doc.bodies().at(body)->surface.faces.begin()->first;
}
Id firstEdge(const Document &doc, Id body) {
    return doc.bodies().at(body)->topology.edges.begin()->first;
}
void tiling() {
    // A 100 x 100 grid: 20,000 triangles in tiles of at most 2,048, a complete partition.
    const auto mesh = grid(100, 100);
    const auto tiling = tileTerrainTriangles(mesh);
    std::vector<int> seen(mesh.triangles.size());
    for (const auto &tile : tiling.tiles) {
        check(!tile.empty() && tile.size() <= TerrainLimits::tileTriangles,
              "Every tile holds 1 to 2,048 triangles");
        for (const auto index : tile)
            ++seen.at(index);
    }
    check(std::all_of(seen.begin(), seen.end(), [](int count) { return count == 1; }),
          "The tiles partition the triangle set");
    check(tiling.tiles.size() >= mesh.triangles.size() / TerrainLimits::tileTriangles,
          "The grid is fine enough");
    // A dense cluster forces smaller cells; the partition still holds.
    auto clustered = grid(60, 60, 1e-3);
    const auto sparse = grid(4, 4, 50, {100, 100, 0});
    const auto offset = std::uint32_t(clustered.vertices.size());
    clustered.vertices.insert(clustered.vertices.end(), sparse.vertices.begin(),
                              sparse.vertices.end());
    for (auto triangle : sparse.triangles)
        clustered.triangles.push_back(
            {triangle[0] + offset, triangle[1] + offset, triangle[2] + offset});
    const auto dense = tileTerrainTriangles(clustered);
    std::vector<int> covered(clustered.triangles.size());
    for (const auto &tile : dense.tiles) {
        check(tile.size() <= TerrainLimits::tileTriangles, "Clustered tiles stay within 2,048");
        for (const auto index : tile)
            ++covered.at(index);
    }
    check(std::all_of(covered.begin(), covered.end(), [](int count) { return count == 1; }),
          "Clustered tiles partition the triangle set");
    // The same input always gives the same tiles.
    check(tileTerrainTriangles(clustered).tiles == dense.tiles, "Tiling is deterministic");

    // Built tiles: canonical IDs, the mesh preserved, interior edges soft and smooth.
    Document doc;
    const auto built = createTerrain(doc, mesh, record(mesh));
    check(doc.revision() == 1 && doc.canUndo(), "Building a terrain is one edit");
    check(doc.history().entries.back().label == "Create terrain", "The edit is labelled");
    const auto &terrain = *doc.bodies().at(built.terrain);
    check(terrain.kind == BodyKind::Terrain && terrain.terrain && terrain.surface.faces.empty(),
          "The container carries the record and no geometry");
    check(built.tiles == terrainTiles(doc, built.terrain) &&
              built.tiles.size() == tiling.tiles.size(),
          "One tile body per grid cell");
    size_t faces = 0;
    std::set<std::array<double, 3>> positions;
    for (const auto id : built.tiles) {
        const auto &tile = *doc.bodies().at(id);
        check(tile.terrainTile && tile.kind == BodyKind::Geometry &&
                  tile.parent == built.terrain && tile.transform == Transform{},
              "Tiles are ordinary geometry children of the terrain");
        check(tile.surface.faces.size() <= TerrainLimits::tileTriangles, "Tile within limits");
        const auto n = tile.surface.vertices.size();
        check(tile.surface.vertices.begin()->first == 1 &&
                  tile.surface.faces.begin()->first == n + 1,
              "Implicit vertex and face IDs");
        faces += tile.surface.faces.size();
        for (const auto &[vertex, point] : tile.surface.vertices)
            positions.insert({point.x, point.y, point.z});
        size_t soft = 0;
        for (const auto &[edge, style] : tile.edgeAppearances)
            soft += style.soft && style.smooth && !style.hidden;
        check(soft > 0 && soft == tile.edgeAppearances.size(),
              "Interior edges are soft and smooth");
    }
    check(faces == mesh.triangles.size(), "Every triangle is in exactly one tile");
    check(positions.size() == mesh.vertices.size(), "Border vertices duplicate exact positions");
    doc.undo();
    check(doc.bodies().empty(), "Undo removes the terrain");
    doc.redo();
    check(doc.bodies().size() == built.tiles.size() + 1, "Redo restores it");
}
void limits() {
    // The terrain limit class: 250,001 triangles refuse with the count and the limit.
    TerrainMesh huge;
    huge.vertices = {{0, 0, 0}, {1, 0, 0}, {0, 1, 0}};
    huge.triangles.assign(TerrainLimits::triangles + 1, {0, 1, 2});
    Document doc;
    std::string code;
    const auto message = rejection([&] { createTerrain(doc, huge, record(huge)); }, &code);
    check(code == "LIMIT_EXCEEDED" && message.find("250,001") != std::string::npos &&
              message.find("250,000") != std::string::npos,
          "Over-limit terrain names the count and the limit: " + message);
    check(doc.revision() == 0 && doc.bodies().empty(), "A refusal changes nothing");
    // The same limit applies across terrains in one document.
    const auto small = grid(4, 4);
    createTerrain(doc, small, record(small));
    TerrainMesh rest = huge;
    rest.triangles.resize(TerrainLimits::triangles);
    const auto across = rejection([&] { createTerrain(doc, rest, record(rest)); }, &code);
    check(code == "LIMIT_EXCEEDED" && across.find("250,032") != std::string::npos,
          "The limit counts every terrain in the document: " + across);
    // The document check itself (edits and loads).
    check(rejection([] { checkTerrainLimit(250001, "Document load"); }) ==
              "Document load exceeds the document terrain limit: terrain triangles would be "
              "250,001 of 250,000",
          "Document terrain limit diagnostics");
    check(rejection([] { checkTerrainLimit(250000, "Document load"); }).empty(),
          "Exactly 250,000 is allowed");
    // Terrain triangles are counted apart from the 300,000-face modelling budget.
    check(doc.materializedTotals().terrainTriangles == 32 &&
              doc.materializedTotals().faces == 0 && doc.materializedTotals().vertices == 0,
          "Tile faces count only as terrain triangles");
    const auto unique = uniqueGeometryCounts(doc.materializedTotals(), {}, doc.definitions(),
                                             &doc.componentVariants());
    check(unique.faces == 0 && unique.bodies == 2, "Tiles are records but not modelling faces");
}
// Every generic topology tool refuses a terrain tile with the shared message and code;
// paint, tags, hide/lock and intersecting *with* terrain stay allowed.
void guards() {
    Document doc;
    const auto mesh = grid(4, 4);
    const auto built = createTerrain(doc, mesh, record(mesh));
    const auto tile = built.tiles.front();
    const auto face = firstFace(doc, tile);
    const auto edge = firstEdge(doc, tile);
    const auto vertex = doc.bodies().at(tile)->surface.vertices.begin()->first;
    const auto digest = doc.bodies().at(built.terrain)->terrain->meshDigest;
    auto refused = [&](const std::string &name, const std::function<void()> &operation) {
        const auto revision = doc.revision();
        std::string code;
        const auto message = rejection(operation, &code);
        check(message == terrainProtectedMessage && code == "UNSUPPORTED_TARGET",
              name + " refuses terrain tiles (got: " + message + ")");
        check(doc.revision() == revision && !terrainChangedSinceGenerated(doc, built.terrain),
              name + " leaves the terrain unchanged");
        std::cout << "guarded: " << name << '\n';
    };
    refused("push/pull", [&] { doc.pushPull(tile, face, 1); });
    refused("push/pull new face", [&] { doc.pushPull(tile, face, 1, true); });
    refused("extrude", [&] { doc.extrude(tile, face, 1); });
    refused("offset", [&] { doc.offsetFace(tile, face, -.05); });
    refused("erase face", [&] { doc.eraseFace(tile, face); });
    refused("erase edge", [&] { doc.eraseEdge(tile, edge); });
    refused("split edge", [&] { doc.splitEdge(tile, edge, .5); });
    refused("cleanup", [&] { doc.cleanup(tile); });
    refused("heal face", [&] { doc.healFace(tile, edge, {0, 0, 0}, {0, 0, 1}); });
    refused("insert edges",
            [&] { doc.insertEdges(tile, {0, 0, 0}, {0, 0, 1}, {{Vec3{.1, .1, 0}, {.2, .1, 0}}}); });
    refused("draw edge", [&] { doc.addWire(tile, {0, 0, 5}, {1, 0, 5}); });
    refused("draw into terrain", [&] { doc.addWire(built.terrain, {0, 0, 5}, {1, 0, 5}); });
    refused("guide", [&] { doc.addGuide(tile, Guide{GuideKind::Point, {0, 0, 0}, {}}); });
    refused("move vertex", [&] {
        transformSelected(doc, {{tile, TransformKind::Vertex, vertex}},
                          Transform::translation({0, 0, 1}));
    });
    refused("move edge", [&] {
        transformSelected(doc, {{tile, TransformKind::Edge, edge}},
                          Transform::translation({0, 0, 1}));
    });
    refused("move face", [&] {
        transformSelected(doc, {{tile, TransformKind::Face, face}},
                          Transform::translation({0, 0, 1}));
    });
    refused("move one tile", [&] {
        transformSelected(doc, {{tile, TransformKind::Context, 0}},
                          Transform::translation({0, 0, 1}));
    });
    refused("move one tile (document)", [&] { doc.move(tile, {1, 0, 0}); });
    refused("delete one tile", [&] { doc.erase(tile); });
    refused("delete selected tile face", [&] {
        Selection selection;
        selection.enter(doc, tile);
        selection.apply(doc, {{tile, SelectionKind::Face, face}}, SelectionMode::Replace);
        eraseSelected(doc, selection);
    });
    refused("delete selected tile", [&] {
        Selection selection;
        selection.enter(doc, built.terrain);
        selection.apply(doc, {{tile, SelectionKind::Body, 0}}, SelectionMode::Replace);
        eraseSelected(doc, selection);
    });
    refused("reparent tile", [&] { doc.transform(tile, {}, 0); });
    refused("edge appearance", [&] {
        setEdgeAppearance(doc, {{tile, SelectionKind::Edge, edge}}, tile, true, {}, {});
    });
    refused("reverse faces",
            [&] { reverseSelectedFaces(doc, {{tile, SelectionKind::Face, face}}, tile); });
    refused("intersect into terrain", [&] {
        intersectSelected(doc, {{tile, SelectionKind::Face, face}}, IntersectionMode::Model,
                          tile);
    });
    {
        // A solid next to the terrain: booleans refuse a tile operand.
        Document solid = doc;
        const auto box = solid.addFace({{{0, 0, -2}, {1, 0, -2}, {1, 1, -2}, {0, 1, -2}}});
        solid.extrude(box, firstFace(solid, box), 1);
        std::string code;
        check(rejection([&] {
                  solidBodies(solid, tile, box, SolidAction::Union, 0, false);
              }, &code) == terrainProtectedMessage &&
                  code == "UNSUPPORTED_TARGET",
              "Solid booleans refuse terrain tiles");
        std::cout << "guarded: solid boolean\n";
    }
    {
        std::string code;
        const auto message = rejection([&] { explodeGroup(doc, built.terrain); }, &code);
        check(code == "UNSUPPORTED_TARGET" && message.find("Explode terrain") != std::string::npos,
              "Group explode points to Explode terrain: " + message);
        const auto component = rejection([&] { createComponent(doc, built.terrain, "Site"); });
        check(!component.empty() && doc.definitions().empty(),
              "A terrain cannot become a component: " + component);
        std::cout << "guarded: group explode, make component\n";
    }
    check(doc.bodies().at(built.terrain)->terrain->meshDigest == digest, "Digest untouched");

    // Allowed: paint, material and texture state, tags, names, hide and lock.
    auto allowed = [&](const std::string &name, const std::function<void()> &operation) {
        const auto revision = doc.revision();
        operation();
        check(doc.revision() == revision + 1 && !terrainChangedSinceGenerated(doc, built.terrain),
              name + " is allowed and keeps the mesh");
        std::cout << "allowed: " << name << '\n';
    };
    allowed("paint tile", [&] { doc.paint(tile, {.2f, .5f, .2f}); });
    allowed("paint terrain", [&] { doc.paint(built.terrain, {.4f, .3f, .2f}); });
    const auto tag = createTag(doc, "Site");
    allowed("tag tile", [&] { assignTag(doc, tile, tag); });
    allowed("tag terrain", [&] { assignTag(doc, built.terrain, tag); });
    allowed("hide tile", [&] { setEntityState(doc, tile, true, {}); });
    allowed("show tile", [&] { setEntityState(doc, tile, false, {}); });
    allowed("rename tile", [&] { renameEntity(doc, tile, "North tile"); });
    allowed("move whole terrain", [&] { doc.move(built.terrain, {0, 0, 2}); });
    allowed("lock terrain", [&] { setEntityState(doc, built.terrain, {}, true); });
    refused("push/pull on a locked terrain", [&] { doc.pushPull(tile, face, 1); });
    allowed("unlock terrain", [&] { setEntityState(doc, built.terrain, {}, false); });
    {
        // Intersect *with* terrain: an ordinary face crossing it gains edges; tiles do not.
        const auto wall = doc.addFace({{{.5, .5, -5}, {3.5, .5, -5}, {3.5, .5, 5}, {.5, .5, 5}}});
        std::map<Id, BodyPtr> before;
        for (const auto id : built.tiles)
            before[id] = doc.bodies().at(id);
        const auto faces = doc.bodies().at(wall)->surface.faces.size();
        intersectSelected(doc, {{wall, SelectionKind::Face, firstFace(doc, wall)}},
                          IntersectionMode::Model, 0);
        check(doc.bodies().at(wall)->topology.edges.size() > 4 ||
                  doc.bodies().at(wall)->surface.faces.size() > faces,
              "The ordinary face is intersected with the terrain");
        for (const auto id : built.tiles)
            check(doc.bodies().at(id) == before.at(id), "Terrain tiles are only a reference");
        std::cout << "allowed: intersect with terrain as reference\n";
    }
    {
        // Deleting the whole terrain (container with its tiles) is allowed and undoable.
        Selection selection;
        selection.apply(doc, {{built.terrain, SelectionKind::Body, 0}}, SelectionMode::Replace);
        eraseSelected(doc, selection);
        check(!doc.bodies().contains(built.terrain) && terrainTiles(doc, built.terrain).empty(),
              "Delete removes the terrain and its tiles");
        doc.undo();
        check(isTerrain(doc, built.terrain) && !terrainChangedSinceGenerated(doc, built.terrain),
              "Undo restores the whole terrain");
        std::cout << "allowed: delete the whole terrain\n";
    }
}
void explode() {
    Document doc;
    const auto mesh = grid(6, 5);
    const auto built = createTerrain(doc, mesh, record(mesh));
    const auto before = doc.materializedTotals();
    explodeTerrain(doc, built.terrain);
    check(doc.history().entries.back().label == "Explode terrain", "Explode is one labelled edit");
    const auto &group = *doc.bodies().at(built.terrain);
    check(group.kind == BodyKind::Group && !group.terrain, "The terrain becomes a plain group");
    for (const auto id : built.tiles) {
        const auto &body = *doc.bodies().at(id);
        check(!body.terrainTile && body.parent == built.terrain &&
                  body.surface == doc.bodies().at(id)->surface,
              "Tiles become ordinary geometry in the group");
    }
    check(doc.materializedTotals().terrainTriangles == 0 &&
              doc.materializedTotals().faces == before.terrainTriangles,
          "Exploded faces now count in the modelling budget");
    // Ordinary geometry: generic tools now work.
    const auto tile = built.tiles.front();
    doc.pushPull(tile, firstFace(doc, tile), .5, true);
    doc.undo();
    doc.undo();
    check(isTerrain(doc, built.terrain) && !terrainChangedSinceGenerated(doc, built.terrain) &&
              doc.materializedTotals() == before,
          "Undo restores the terrain and its counts");
    std::string code;
    check(!rejection([&] { explodeTerrain(doc, built.tiles.front()); }, &code).empty() &&
              code == "NOT_FOUND",
          "Explode needs a terrain");

    // Explode joins the terrain's triangles to the modelling faces against the unique face limit
    // (DocumentLimits::unique.faces: 300,000 since 1.7.0, R110). The boundary at that size, with a
    // 249,218-triangle terrain, is checked in document_limits_tests, because building it is slow.
    const auto start = std::chrono::steady_clock::now();
    Document large;
    const auto big = grid(224, 224); // 100,352 triangles
    const auto terrain = createTerrain(large, big, record(big));
    large.addFace({{{0, 0, 10}, {1, 0, 10}, {1, 1, 10}, {0, 1, 10}}});
    check(uniqueGeometryCounts(large.materializedTotals(), {}, large.definitions(),
                               &large.componentVariants())
                  .faces == 1,
          "A 100,352-triangle terrain leaves the modelling budget free");
    const auto budget = terrainExplodeBudget(large, terrain.terrain);
    check(budget.faceLimit == DocumentLimits::unique.faces && budget.faceLimit == 300000 &&
              budget.terrainFaces == 100352 && budget.fits(),
          "Explode checks the terrain's triangles against the unique face limit");
    TerrainExplodeBudget edge;
    edge.terrainFaces = 249218;
    edge.faceLimit = DocumentLimits::unique.faces;
    edge.modelFaces = 50782;
    check(edge.fits(), "Exactly 300,000 faces fit the explode budget");
    edge.modelFaces = 50783;
    check(!edge.fits(), "One more modelling face does not fit the explode budget");
    std::cout << "large terrain build and refusal: "
              << std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count()
              << " s\n";
}
void digest() {
    Document doc;
    const auto mesh = grid(8, 3);
    const auto built = createTerrain(doc, mesh, record(mesh));
    const auto &stored = doc.bodies().at(built.terrain)->terrain->meshDigest;
    check(stored.size() == 64 && stored == terrainMeshDigest(doc, built.terrain) &&
              !terrainChangedSinceGenerated(doc, built.terrain),
          "The record holds the digest of its tiles");
    // The same mesh gives the same digest in another document, wherever it is placed.
    Document other;
    const auto again = createTerrain(other, mesh, record(mesh));
    other.move(again.terrain, {100, 0, 0});
    check(terrainMeshDigest(other, again.terrain) == stored, "The digest is of the tile meshes");
    // A tool that changes a tile while touching the record (as a buggy terrain tool
    // could) leaves the stored digest stale: "changed since generated".
    const auto tile = built.tiles.front();
    auto moved = std::make_shared<Body>(*doc.bodies().at(tile));
    moved->surface.vertices.at(1).z += .5;
    auto container = std::make_shared<Body>(*doc.bodies().at(built.terrain));
    container->terrain->parameters.maxEdgeLength = 4;
    Edit edit{"Faulty terrain tool", {}};
    edit.changes.push_back({built.terrain, doc.bodies().at(built.terrain), container});
    edit.changes.push_back({tile, doc.bodies().at(tile), moved});
    doc.apply(std::move(edit), doc.revision());
    check(terrainChangedSinceGenerated(doc, built.terrain) &&
              terrainMeshDigest(doc, built.terrain) != stored,
          "A changed tile no longer matches the digest");
    doc.undo();
    check(!terrainChangedSinceGenerated(doc, built.terrain), "Undo matches again");
}
void site() {
    Document doc;
    // Site coordinates (UTM-scale) of a terrain, triangulated in the local frame.
    const SiteOrigin origin{512300, 5234500, 0, "EPSG:32633", {"terrain-utm.dxf", ""}};
    std::vector<Vec3> source;
    auto mesh = grid(12, 9, .5);
    for (auto &point : mesh.vertices) {
        const Vec3 utm{512345.678 + point.x * 1.0001, 5234567.891 + point.y * .9999,
                       1850.25 + point.z};
        source.push_back(utm);
        point = siteToModel(origin, utm);
    }
    check(rejection([&] { setSiteOrigin(doc, origin); }).empty() && doc.site() == origin,
          "site.set establishes the origin");
    check(doc.history().entries.back().label == "Set site origin", "One labelled edit");
    std::string code;
    check(!rejection([&] { setSiteOrigin(doc, origin); }, &code).empty() &&
              code == "INVALID_REQUEST",
          "site.set refuses when an origin exists");
    const auto built = createTerrain(doc, mesh, record(mesh));
    // Every vertex converts back to its source within 1e-6 m (in fact within 1e-9).
    double worst = 0;
    auto measure = [&](const Document &state) {
        worst = 0;
        for (const auto id : terrainTiles(state, built.terrain)) {
            const auto world = state.worldTransform(id);
            for (const auto &[vertex, point] : state.bodies().at(id)->surface.vertices) {
                const auto site = modelToSite(*state.site(), world.point(point));
                double nearest = 1e9;
                for (const auto &candidate : source)
                    nearest = std::min(nearest, length(candidate - site));
                worst = std::max(worst, nearest);
            }
        }
        return worst;
    };
    check(measure(doc) <= 1e-6, "UTM vertices survive conversion within 1e-6 m");
    std::cout << "UTM conversion error: " << worst << " m\n";
    // Other geometry, a world section and a saved camera move with a re-anchor.
    const auto box = doc.addFace({{{1, 1, 0}, {2, 1, 0}, {2, 2, 0}, {1, 2, 0}}});
    auto siteOf = [&](Id body) {
        return modelToSite(*doc.site(),
                           doc.worldTransform(body).point(
                               doc.bodies().at(body)->surface.vertices.begin()->second));
    };
    const auto boxSite = siteOf(box);
    const auto section = createSection(doc, "Cut", 0, SectionPlane::through({0, 0, 1}, {0, 0, 1}));
    SceneSnapshot snapshot;
    snapshot.camera = SceneCamera{{3, 4, 5}};
    const auto scene = createScene(doc, "View", snapshot);
    const auto revision = doc.revision();
    reanchorSite(doc, 512000, 5234000, 10);
    check(doc.revision() == revision + 1 &&
              doc.history().entries.back().label == "Re-anchor site origin",
          "Re-anchor is one labelled edit");
    check(doc.site()->easting == 512000 && doc.site()->northing == 5234000 &&
              doc.site()->elevation == 10 && doc.site()->crs == "EPSG:32633",
          "Re-anchor changes only the origin");
    check(length(siteOf(box) - boxSite) <= 1e-9, "Site coordinates of geometry are unchanged");
    check(measure(doc) <= 1e-6, "Terrain site coordinates are unchanged");
    check(!terrainChangedSinceGenerated(doc, built.terrain),
          "Re-anchoring moves the terrain without changing its tiles");
    check(std::abs(doc.sections().at(section)->plane.distance({0, 0, -9}) - 0) < 1e-12,
          "A world section plane moves with the geometry");
    check(doc.scenes().at(scene)->snapshot.camera->target == Vec3{303, 504, -5},
          "A saved camera moves with the geometry");
    doc.undo();
    check(doc.site() == origin && length(siteOf(box) - boxSite) <= 1e-9 &&
              doc.worldTransform(box) == Transform{},
          "Undo restores the origin and positions");
    doc.redo();
    check(doc.site()->easting == 512000 && doc.worldTransform(box) != Transform{},
          "Redo re-anchors again");
    check(!rejection([&] { reanchorSite(doc, 512000, 5234000, 10); }, &code).empty() &&
              code == "INVALID_REQUEST",
          "Re-anchor needs a different origin");
    // Moving geometry past the coordinate limit refuses the whole re-anchor.
    check(!rejection([&] { reanchorSite(doc, -5e6, 5234000, 10); }).empty() &&
              doc.site()->easting == 512000,
          "A re-anchor that would exceed the coordinate limit changes nothing");
    Document none;
    check(!rejection([&] { reanchorSite(none, 1, 2, 3); }, &code).empty() && code == "NOT_FOUND",
          "Re-anchor needs an origin");
    check(!rejection([&] {
              setSiteOrigin(none, SiteOrigin{2e8, 0, 0, {}, {}});
          }).empty() &&
              !rejection([&] {
                  setSiteOrigin(none, SiteOrigin{0, 0, 0, std::string(65, 'x'), {}});
              }).empty() &&
              !none.site(),
          "Invalid origins and CRS labels refuse");
    // Conversion helpers are exact inverses at these magnitudes.
    const Vec3 sample{512345.6789, 5234567.8912, 1850.1234};
    const auto local = siteToModel(origin, sample);
    check(length(modelToSite(origin, local) - sample) <= 1e-9, "Conversion round trip");
}
void elevation() {
    Document doc;
    const auto mesh = grid(10, 10);
    const auto built = createTerrain(doc, mesh, record(mesh));
    std::vector<std::array<double, 2>> points;
    for (const auto point : mesh.vertices)
        if (points.size() < terrainPointBatch)
            points.push_back({point.x, point.y});
    const auto result = terrainElevationAt(doc, built.terrain, points);
    for (size_t i = 0; i < points.size(); ++i)
        check(result[i].found && result[i].elevation == mesh.vertices[i].z,
              "elevation_at returns each vertex's exact elevation");
    const auto outside = terrainElevationAt(doc, built.terrain, {{-5, -5}, {2.5, 3.25}});
    check(!outside[0].found && outside[1].found && outside[1].tile && outside[1].face,
          "Points outside report none; inside points name their triangle");
    doc.move(built.terrain, {0, 0, 2});
    check(terrainElevationAt(doc, built.terrain, {{0, 0}})[0].elevation == mesh.vertices[0].z + 2,
          "Elevation is in model coordinates");
    std::string code;
    std::vector<std::array<double, 2>> many(terrainPointBatch + 1, {0, 0});
    check(!rejection([&] { terrainElevationAt(doc, built.terrain, many); }, &code).empty() &&
              code == "LIMIT_EXCEEDED",
          "Elevation batches are bounded");
}
} // namespace
// R095.c regression: a plain 50 m sliver-free triangle from the terrain import fixture sent
// Clipper2's Delaunay triangulation into unbounded edge creation (tens of GB) when a tile was
// validated. The face is triangulated in a child process under a 2 GiB address-space cap (no
// cap under AddressSanitizer, which reserves more), so a regression fails instead of exhausting
// the machine.
void largeTriangle() {
    const pid_t child = fork();
    check(child >= 0, "fork for the capped triangle check");
    if (child == 0) {
#if !defined(__SANITIZE_ADDRESS__)
        const rlimit cap{2ull << 30, 2ull << 30};
        setrlimit(RLIMIT_AS, &cap);
#endif
        alarm(60);
        try {
            Surface surface;
            const auto face = surface.addFace(
                {{{46.3626, 51.310000000000002, 96},
                  {46.366300000000003, 49.009999999999998, 96},
                  {53.497500000000002, -2.1360999999999999, 96}}});
            const auto triangles = surface.triangulateExact(face);
            surface.validate();
            _exit(triangles.size() == 1 ? 0 : 2);
        } catch (...) {
            _exit(3);
        }
    }
    int status = 0;
    check(waitpid(child, &status, 0) == child && WIFEXITED(status) && WEXITSTATUS(status) == 0,
          "a large triangle face tessellates without Clipper2 (bounded)");
    // Zero-area triangles are still refused.
    Surface surface;
    bool refused = false;
    try {
        surface.addFace({{{0, 0, 0}, {1, 0, 0}, {2, 0, 0}}});
    } catch (const std::exception &) {
        refused = true;
    }
    check(refused, "a zero-area triangle face is refused");
}
int main() {
    try {
        largeTriangle();
        tiling();
        limits();
        guards();
        explode();
        digest();
        site();
        elevation();
        std::cout << "terrain tests passed\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
