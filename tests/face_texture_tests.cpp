#include "core/components.hpp"
#include "core/consolidation.hpp"
#include "core/face_orientation.hpp"
#include "core/face_textures.hpp"
#include "core/groups.hpp"
#include "core/host_regeneration.hpp"
#include "core/materials.hpp"
#include "core/profile_sweep.hpp"
#include "core/solid_boolean.hpp"
#include "core/transform_selection.hpp"
#include "io/assets.hpp"
#include "io/document_io.hpp"
#include "legacy_texture_fields.hpp"
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QTemporaryDir>
#include <QtEndian>
#include <iostream>
using namespace sketchy;
namespace {
void check(bool value, const char *message) {
    if (!value)
        throw std::runtime_error(message);
}
template <class F> void rejects(F action) {
    try {
        action();
    } catch (const std::exception &) {
        return;
    }
    throw std::runtime_error("Expected face texture rejection");
}
void near(TextureCoordinate a, TextureCoordinate b, const char *message) {
    check(std::abs(a.u - b.u) < 1e-7 && std::abs(a.v - b.v) < 1e-7, message);
}
Id square(Document &doc, double size = 4) {
    return doc.addFace({{{0, 0, 0}, {size, 0, 0}, {size, size, 0}, {0, size, 0}}});
}
Id first(const Document &doc, Id body) {
    return doc.bodies().at(body)->surface.faces.begin()->first;
}
TextureMapping front() {
    return planarTextureMapping({.25, .5, 0}, {0, 0, 1}, {1, 0, 0}, 2, 4, 0, {.1, .2});
}
TextureMapping back() {
    return planarTextureMapping({.5, .25, 0}, {0, 0, -1}, {1, 0, 0}, -3, 2, .4, {.7, -.1});
}
void both(Document &doc, Id body, Id face) {
    assignTextureMapping(doc, body, face, front(), true, false);
    assignTextureMapping(doc, body, face, back(), false, true);
}
void historyAndSides() {
    Document doc;
    const auto body = square(doc), face = first(doc, body);
    const auto original = doc.bodies().at(body);
    const auto history = doc.history().total, bytes = doc.readSnapshotBytes();
    const auto change = assignTextureMapping(doc, body, face, front(), true, false);
    check(change.at(body).faces.modified == std::vector<Id>{face},
          "Mapping-only assignment reports the changed face");
    check(doc.history().total == history + 1 && doc.readSnapshotBytes() > bytes,
          "Mapping storage is accounted and has one undo step");
    both(doc, body, face);
    const auto mapped = doc.bodies().at(body);
    check(mapped->surface == original->surface && mapped->topology == original->topology &&
              original->faceTextureMappings.empty(),
          "Mappings preserve geometry and immutable source");
    check(faceTextureMappings(*mapped, face) == TextureMappingSides{front(), back()},
          "Front and back have independent mappings");
    const auto prepared = doc.prepareEdit(
        [&](Document &draft) { assignTextureMapping(draft, body, face, {}, true, false); });
    check(doc.bodies().at(body) == mapped &&
              faceTextureMappings(*prepared.snapshot().bodies().at(body), face) ==
                  TextureMappingSides{{}, back()},
          "Reset one side is private and preserves the other");
    doc.applyPrepared(prepared);
    doc.undo();
    check(*doc.bodies().at(body) == *mapped, "Undo exactly restores mappings");
    doc.redo();
    assignTextureMapping(doc, body, face, {}, false, true);
    check(doc.bodies().at(body)->faceTextureMappings.empty(), "Both resets prune sparse record");
    const auto revision = doc.revision();
    check(assignTextureMapping(doc, body, face, {}).empty() && doc.revision() == revision,
          "Repeated reset does not create history");
    both(doc, body, face);
    const auto material = createMaterial(doc, "Texture swatch", {1, 1, 1});
    assignMaterial(doc, body, face, material);
    check(faceTextureMappings(*doc.bodies().at(body), face) == TextureMappingSides{front(), back()},
          "Changing swatch retains face placement");
    reverseSelectedFaces(doc, {{body, SelectionKind::Face, face}}, 0);
    check(faceTextureMappings(*doc.bodies().at(body), face) == TextureMappingSides{back(), front()},
          "Face reversal preserves physical mappings with the material sides");
    const auto beforePaint = doc.bodies().at(body);
    doc.paint(body, {.2f, .3f, .4f});
    check(doc.bodies().at(body)->faceTextureMappings.empty(),
          "Legacy color paint clears named mapping");
    doc.undo();
    check(*doc.bodies().at(body) == *beforePaint, "Paint Undo restores all mappings");
    const auto stable = encodeContainer(doc);
    rejects([&] { assignTextureMapping(doc, body, 999, front()); });
    rejects([&] { assignTextureMapping(doc, body, face, front(), false, false); });
    auto bad = front();
    bad.vGradient = bad.uGradient;
    rejects([&] { assignTextureMapping(doc, body, face, bad); });
    check(encodeContainer(doc) == stable, "Invalid mapping assignments are atomic");
}
void splitsCopiesAndFrames() {
    Document doc;
    const auto body = square(doc), face = first(doc, body);
    both(doc, body, face);
    doc.insertEdges(body, {}, {0, 0, 1}, {{Vec3{2, 0, 0}, Vec3{2, 4, 0}}});
    const auto expected = TextureMappingSides{front(), back()};
    for (const auto &[id, record] : doc.bodies().at(body)->surface.faces)
        check(faceTextureMappings(*doc.bodies().at(body), id) == expected,
              "Split descendants preserve both projections");
    const auto second = doc.bodies().at(body)->surface.faces.rbegin()->first;
    assignTextureMapping(doc, body, second, {}, true, false);
    Id seam = 0;
    const auto adjacency =
        doc.bodies().at(body)->topology.adjacency(doc.bodies().at(body)->surface);
    for (const auto &[id, uses] : adjacency.edgeFaces)
        if (uses.size() == 2)
            seam = id;
    const auto before = encodeContainer(doc);
    rejects([&] { doc.eraseEdge(body, seam); });
    check(encodeContainer(doc) == before, "Merging mismatching projections rejects atomically");
    assignTextureMapping(doc, body, second, front(), true, false);
    doc.eraseEdge(body, seam);
    const auto merged = first(doc, body);
    Transform shear;
    shear.m[4] = .4;
    const auto frame = Transform::translation({8, 0, 0}) * shear * Transform::scaling({-2, 3, 1});
    const auto copied = transformSelected(doc, {{body, TransformKind::Face, merged}}, frame, {},
                                          TransformSpace::Local, true);
    const auto copy = copied.geometryCopies.at(body).faces.at(merged);
    const auto actual = faceTextureMappings(*doc.bodies().at(body), copy);
    for (const auto &point : std::vector<Vec3>{{0, 0, 0}, {1, 2, 0}, {4, 4, 0}}) {
        near(actual.front->coordinates(frame.point(point)), front().coordinates(point),
             "Reflected/sheared raw-copy UV matches source");
        near(actual.back->coordinates(frame.point(point)), back().coordinates(point),
             "Raw-copy back mapping matches independently");
    }
    Selection selection;
    selection.apply(doc, {{body, SelectionKind::Face, copy}}, SelectionMode::Replace);
    const auto grouped = groupSelected(doc, selection);
    const auto member = grouped.movedGeometry.at(body);
    check(!doc.bodies().at(body)->faceTextureMappings.contains(copy) &&
              faceTextureMappings(*doc.bodies().at(member), copy) == actual,
          "Grouping transfers mappings and prunes retired source entries");
    doc.pushPull(member, copy, 2);
    for (const auto &[id, record] : doc.bodies().at(member)->surface.faces)
        check(faceTextureMappings(*doc.bodies().at(member), id) == actual,
              "Push/pull retains explicit projection on generated caps and walls");
    const auto placed = doc.bodies().at(member);
    doc.move(grouped.group, {100000.125, 200000.25, 12.5});
    check(doc.bodies().at(member) == placed,
          "Site placement preserves body-local mappings exactly");
    check(encodeContainer(decodeContainer(encodeContainer(doc))) == encodeContainer(doc),
          "Edited far-site mappings reopen exactly");
}
void partialEditsAndSweep() {
    Document doc;
    const auto body = square(doc), face = first(doc, body);
    both(doc, body, face);
    const auto vertex = doc.bodies().at(body)->surface.faces.at(face).loops.front().front();
    transformSelected(doc, {{body, TransformKind::Vertex, vertex}},
                      Transform::translation({.5, 0, 0}), {}, TransformSpace::Local, false);
    const auto mapping = faceTextureMappings(*doc.bodies().at(body), face);
    check(mapping == TextureMappingSides{front(), back()},
          "Partial vertex deformation keeps the authored local projection fixed");
    near(mapping.front->coordinates(doc.bodies().at(body)->surface.vertices.at(vertex)),
         {.225, .075}, "Moved vertex samples independent local projection oracle");
    doc.eraseFace(body, face);
    check(doc.bodies().at(body)->faceTextureMappings.empty(), "Deleting a face prunes its mapping");
    doc.undo();
    check(faceTextureMappings(*doc.bodies().at(body), face) == mapping,
          "Face deletion Undo restores both mappings");

    Document sweep;
    const auto profile = sweep.addFace({{{-.2, -.1, 0}, {.2, -.1, 0}, {.2, .1, 0}, {-.2, .1, 0}}});
    both(sweep, profile, first(sweep, profile));
    const auto frame = Transform::translation({10, 20, 30}) * Transform::rotation({0, 1, 0}, .7);
    sweep.transform(profile, frame);
    const auto original = sweep.bodies().at(profile);
    const auto result =
        sweepFace(sweep, profile, first(sweep, profile),
                  {frame.point({}), frame.point({0, 0, 3}), frame.point({3, 0, 3})}, false, true);
    const auto &generated = *sweep.bodies().at(result.body);
    check(sweep.bodies().at(profile) == original, "Sweep preserves the mapped source profile");
    for (const auto &[id, faceRecord] : generated.surface.faces) {
        const auto side = faceTextureMappings(generated, id);
        check(side == mapping, "Sweep retains the driving projection in source-local coordinates");
        for (auto corner : faceRecord.loops.front()) {
            const auto point = generated.surface.vertices.at(corner);
            near(side.front->coordinates(point),
                 {.1 + (point.x - .25) / 2, .2 + (point.y - .5) / 4},
                 "Swept vertices sample the explicit projection without an invented unwrap");
        }
    }
}
void consolidationAndComponents() {
    Document doc;
    const auto a = square(doc), b = square(doc);
    both(doc, a, first(doc, a));
    both(doc, b, first(doc, b));
    const auto aFace = first(doc, a), bFace = first(doc, b);
    doc.transform(a, Transform::translation({10, 0, 0}));
    Transform shear;
    shear.m[4] = .3;
    doc.transform(b, Transform::translation({30, 0, 0}) * shear * Transform::scaling({-2, 3, 1}));
    const auto oldB = doc.bodies().at(b);
    const auto oldWorld = doc.worldTransform(b);
    const auto result = consolidateContext(doc, 0, std::set<Id>{a, b});
    const auto targetFace = result.transfers.at(b).faces.at(bFace);
    const auto inverse = doc.worldTransform(result.destination).inverse();
    const auto mapping = faceTextureMappings(*doc.bodies().at(result.destination), targetFace);
    for (const auto &[id, point] : oldB->surface.vertices) {
        const auto transferred = inverse.point(oldWorld.point(point));
        near(mapping.front->coordinates(transferred), front().coordinates(point),
             "Consolidation preserves source-frame front mapping");
        near(mapping.back->coordinates(transferred), back().coordinates(point),
             "Consolidation preserves source-frame back mapping");
    }
    const auto component = createComponent(doc, result.destination, "Mapped component");
    const auto canonical = component.movedGeometry.at(result.destination);
    check(doc.bodies().at(component.instance)->faceTextureMappings.empty(),
          "Component placement root retains no geometry mappings");
    const auto second =
        placeComponent(doc, component.definition, Transform::translation({100, 0, 0}));
    editComponentDefinition(doc, component.definition, [&](Document &draft) {
        return assignTextureMapping(draft, canonical, aFace, back(), true, false);
    });
    const auto firstMember = doc.instances().at(component.instance)->members.at(canonical);
    const auto secondMember = doc.instances().at(second.instance)->members.at(canonical);
    check(faceTextureMappings(*doc.bodies().at(firstMember), aFace).front == back() &&
              faceTextureMappings(*doc.bodies().at(secondMember), aFace).front == back(),
          "Shared definition mapping edits reach both instances");
    const auto sibling = doc.bodies().at(secondMember);
    const auto unique = makeComponentUnique(doc, component.instance);
    editComponentDefinition(doc, unique.definition, [&](Document &draft) {
        return assignTextureMapping(draft, canonical, aFace, front(), true, false);
    });
    check(*doc.bodies().at(secondMember) == *sibling, "Unique mapping edit preserves sibling");
    const auto maps = doc.bodies().at(secondMember)->faceTextureMappings;
    setComponentAxes(doc, component.definition, Transform::translation({1, 2, 3}));
    check(doc.bodies().at(secondMember)->faceTextureMappings == maps,
          "Changing component axes leaves member-local projections exact");
    check(encodeContainer(decodeContainer(encodeContainer(doc))) == encodeContainer(doc),
          "Canonical and materialized mappings roundtrip together");
}
void booleanAndHost() {
    Document doc;
    const auto a = square(doc), b = square(doc, 2);
    doc.extrude(a, first(doc, a), 2);
    doc.extrude(b, first(doc, b), 3);
    for (auto body : {a, b})
        for (const auto &[face, record] : doc.bodies().at(body)->surface.faces)
            both(doc, body, face);
    doc.transform(b, Transform::translation({3, 1, -.5}) * Transform::scaling({-1, 1, 1}));
    const auto before = doc.readSnapshot();
    const auto result = solidBodies(doc, a, b, SolidAction::Subtract, 0, true);
    size_t reversed = 0;
    for (const auto &part : result.parts) {
        const auto &output = *doc.bodies().at(part.body);
        for (const auto &[face, source] : part.sources) {
            const auto sourceBody = source.operand ? b : a;
            const auto sourceInverse = before.worldTransform(sourceBody).inverse();
            const auto actual = faceTextureMappings(output, face);
            reversed += source.reversed;
            for (auto vertex : output.surface.faces.at(face).loops.front()) {
                const auto point = output.surface.vertices.at(vertex);
                const auto original =
                    sourceInverse.point(doc.worldTransform(part.body).point(point));
                near(actual.front->coordinates(point),
                     (source.reversed ? back() : front()).coordinates(original),
                     "Boolean front UV follows source provenance and reflected operand frame");
                near(actual.back->coordinates(point),
                     (source.reversed ? front() : back()).coordinates(original),
                     "Boolean cut reverses physical texture sides");
            }
        }
    }
    check(reversed > 0, "Subtraction fixture exercises reversed tool mappings");
    doc.undo();
    check(doc.bodies() == before.bodies(), "Boolean Undo restores original immutable records");
    auto wall = *before.bodies().at(a);
    Id entry = 0;
    for (const auto &[face, record] : wall.surface.faces)
        if (wall.surface.normal(face).z > .9)
            entry = face;
    const OpeningProfile profile{
        entry, {{11, {.5, .5, 2}}, {12, {1.5, .5, 2}}, {13, {1.5, 1.5, 2}}, {14, {.5, 1.5, 2}}}};
    const auto uncut = wall.surface;
    auto cut = regenerateHost(uncut, wall, {}, {{100, profile}});
    const auto jamb = cut.openings.at(100).jambs.begin()->second;
    check(faceTextureMappings(cut.body, jamb) == TextureMappingSides{front(), back()},
          "New opening reveal inherits current entry projection");
    setFaceTextureMappings(cut.body, jamb, {back(), {}});
    auto moved = profile;
    for (auto &corner : moved.corners)
        corner.point.x += .25;
    const auto updated = regenerateHost(uncut, cut.body, cut.openings, {{100, moved}});
    check(faceTextureMappings(updated.body, jamb) == TextureMappingSides{back(), {}},
          "Regenerated reveal preserves its current authored projection");
    const auto filled = regenerateHost(uncut, updated.body, updated.openings, {});
    check(!filled.body.faceTextureMappings.contains(jamb), "Removed reveals prune stale mappings");
}
void persistenceAndMalformed() {
    Document doc;
    const auto body = square(doc), face = first(doc, body);
    both(doc, body, face);
    // A complete 1x1 white RGBA PNG with valid chunk CRCs (not merely opaque test bytes).
    const auto png = QByteArray::fromBase64(
        "iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAYAAAAfFcSJAAAAC0lEQVR4nGP4DwQACfsD/fteaysAAAAASUVORK5CYII=");
    const auto asset = createAsset(doc, "Tile.png", "image/png", assetPayload(png));
    const auto missing = createAsset(doc, "Missing.png", "image/png");
    const auto material = createMaterial(doc, "Tile", {1, 1, 1}, .5f, asset);
    const auto backMaterial = createMaterial(doc, "Missing", {1, 1, 1}, .75f, missing);
    assignMaterial(doc, body, face, material, true, false);
    assignMaterial(doc, body, face, backMaterial, false, true);
    QTemporaryDir files;
    check(files.isValid(), "Owned texture persistence directory");
    saveDocument(doc, files.filePath("source.sketchyup"));
    check(QDir(files.path()).mkdir("relocated") &&
              QFile::rename(files.filePath("source.sketchyup"),
                            files.filePath("relocated/model.sketchyup")),
          "Relocate packaged document");
    auto loaded = loadDocument(files.filePath("relocated/model.sketchyup"));
    check(encodeContainer(loaded) == encodeContainer(doc) &&
              assetByteArray(loaded.assets().at(asset)->payload) == png &&
              !loaded.assets().at(missing)->payload &&
              faceTextureMappings(*loaded.bodies().at(body), face) ==
                  TextureMappingSides{front(), back()},
          "Relocation retains image bytes, missing resource and independent side mappings");
    const auto mappedBody = loaded.bodies().at(body);
    replaceAsset(loaded, missing, assetPayload(png));
    check(loaded.assets().at(missing)->payload && loaded.bodies().at(body) == mappedBody &&
              loaded.materials().at(backMaterial)->asset == missing,
          "Resolving a missing image preserves texture placement and material identity");
    loaded.undo();
    check(!loaded.assets().at(missing)->payload && loaded.bodies().at(body) == mappedBody,
          "Image resolution Undo preserves independent mappings");
    const auto raw = encodeDocument(doc);
    check(encodeDocument(decodeDocument(raw)) == raw, "Inline texture records roundtrip exactly");
    const auto json = QJsonDocument::fromJson(raw).object();
    check(json["version"] == 26, "Mapping schema version is explicit");
    for (int variant = 0; variant < 11; ++variant) {
        auto bad = json;
        auto bodies = bad["bodies"].toArray();
        auto record = bodies[0].toObject();
        auto maps = record["faceTextureMappings"].toObject();
        auto sides = maps[QString::number(face)].toArray();
        auto mapping = sides[0].toObject();
        if (variant == 0)
            mapping.remove("origin");
        if (variant == 1)
            mapping["offset"] = QJsonArray{0};
        if (variant == 2)
            mapping["uGradient"] = QJsonArray{0, 0, 0};
        if (variant == 3)
            mapping["vGradient"] = mapping["uGradient"];
        if (variant == 4)
            mapping["future"] = true;
        if (variant == 5)
            mapping["origin"] = QJsonArray{1000001, 0, 0};
        if (variant == 6)
            mapping["offset"] = QJsonArray{0, "1"};
        sides[0] = mapping;
        if (variant == 7)
            sides = QJsonArray{QJsonValue::Null, QJsonValue::Null};
        if (variant == 8)
            sides = QJsonArray{mapping};
        maps[QString::number(face)] = sides;
        if (variant == 9)
            maps["999"] = sides;
        record["faceTextureMappings"] = maps;
        if (variant == 10)
            record.remove("faceTextureMappings");
        bodies[0] = record;
        bad["bodies"] = bodies;
        rejects([&] { decodeDocument(QJsonDocument(bad).toJson()); });
    }
    auto legacy = json;
    legacy["version"] = 15;
    legacy.remove("style");
    legacy.remove("scenes");
    legacy.remove("sections");
    legacy.remove("solar");
    legacy.remove("displayPrecision");
    legacy.remove("annotations");
    legacy.remove("nextAnnotationId");
    legacy.remove("nextSectionId");
    legacy.remove("activeSections");
    legacy.remove("nextSceneId");
    rejects([&] { decodeDocument(QJsonDocument(legacy).toJson()); });
    removeTextureMappingFields(legacy);
    const auto migrated = decodeDocument(QJsonDocument(legacy).toJson());
    check(migrated.bodies().at(body)->faceTextureMappings.empty() &&
              migrated.materials().at(material)->opacity == .5f,
          "Old schema retains appearance without inventing UV records");
    const auto historical =
        loadDocument(QStringLiteral(SOURCE_DIR "/tests/fixtures/container-hosted-v15.sketchyup"));
    check(!historical.hostedComponents().attachments.empty(), "Actual v15 hosted fixture migrates");
    for (const auto &[id, record] : historical.bodies())
        check(record->faceTextureMappings.empty(), "Historical bytes have implicit mapping");
}
} // namespace
int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    try {
        historyAndSides();
        splitsCopiesAndFrames();
        partialEditsAndSweep();
        consolidationAndComponents();
        booleanAndHost();
        persistenceAndMalformed();
        std::cout << "Face texture lineage, frames, components, hosted booleans, history and "
                     "schema 16 passed\n";
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
