#include "automation/extension.hpp"
#include "component_parameter_fixture.hpp"
#include "terrain_import_fixtures.hpp"
#include "terrain_edit_test_kinds.hpp"
#include "core/annotation_anchors.hpp"
#include "core/components.hpp"
#include "core/drawing_sheets.hpp"
#include "core/scenes.hpp"
#include "core/terrain.hpp"
#include "core/terrain_edit.hpp"
#include "core/terrain_sculpt.hpp"
#include "core/terrain_stamp.hpp"
#include "core/terrain_drape.hpp"
#include "core/terrain_source_edit.hpp"
#include "fuzz/parameter_expression_harness.hpp"
#include "integrations/glb_export.hpp"
#include "io/dxf_export.hpp"
#include "io/gltf_import.hpp"
#include "io/library_bundle.hpp"
#include "io/obj_source.hpp"
#include "io/stl_export.hpp"
#include "io/texture_image.hpp"
#include "io/terrain_import.hpp"
#include "io/terrain_io.hpp"
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QFile>
#include <QJsonDocument>
#include <QJsonArray>
#include <QTemporaryDir>
#include <QtEndian>
#include <functional>
#include <iostream>
#include <new>
#include <set>
using namespace sketchy;
namespace {
class CorpusFailure : public std::runtime_error {
    using std::runtime_error::runtime_error;
};
void check(bool value, const char *message) {
    if (!value)
        throw CorpusFailure(message);
}
void coordinate(Vec3 p) {
    check(std::isfinite(p.x) && std::isfinite(p.y) && std::isfinite(p.z) && std::abs(p.x) <= 1e6 &&
              std::abs(p.y) <= 1e6 && std::abs(p.z) <= 1e6,
          "Accepted parser result contains invalid native coordinates");
}
void roundtrip(const Document &document) {
    const auto bytes = encodeContainer(document);
    check(encodeContainer(decodeContainer(bytes)) == bytes,
          "Accepted document does not survive canonical container validation");
}
QByteArray read(const QString &path) {
    QFile file(path);
    check(file.open(QIODevice::ReadOnly), "Read corpus seed");
    return file.readAll();
}
void write(const QString &path, const QByteArray &bytes) {
    QFile file(path);
    check(file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size(),
          "Write private corpus input");
    file.close();
}
// Component capture intentionally gives its standalone document a fresh identity.
// Normalize only that synthetic identity and its envelope hash for reproducible seeds.
QByteArray stableComponentSeed(QByteArray bytes) {
    const auto manifestSize = qFromLittleEndian<quint32>(bytes.constData() + 8);
    const auto modelSize = qFromLittleEndian<quint64>(bytes.constData() + 12);
    const auto thumbnail = bytes.mid(24 + manifestSize + qsizetype(modelSize));
    auto manifest = QJsonDocument::fromJson(bytes.mid(24, manifestSize)).object();
    const auto document = decodeContainer(bytes.mid(24 + manifestSize, qsizetype(modelSize)));
    auto json = QJsonDocument::fromJson(encodeDocument(document)).object();
    json["documentId"] = "0000000000000000000000000000083b";
    const auto model =
        encodeContainer(decodeDocument(QJsonDocument(json).toJson(QJsonDocument::Compact)));
    auto record = manifest["model"].toObject();
    record["bytes"] = model.size();
    record["sha256"] =
        QString::fromLatin1(QCryptographicHash::hash(model, QCryptographicHash::Sha256).toHex());
    manifest["model"] = record;
    const auto encoded = QJsonDocument(manifest).toJson(QJsonDocument::Compact);
    bytes.resize(24);
    qToLittleEndian<quint32>(quint32(encoded.size()), bytes.data() + 8);
    qToLittleEndian<quint64>(quint64(model.size()), bytes.data() + 12);
    return bytes + encoded + model + thumbnail;
}
QJsonObject exercise(const QString &name, const QByteArray &seed,
                     const std::function<void(const QByteArray &, bool &)> &parse) {
    bool valid{};
    parse(seed, valid);
    check(valid, "Original corpus seed must parse successfully");
    std::uint32_t state = 0x83a5c0de;
    const auto random = [&]() {
        state ^= state << 13;
        state ^= state >> 17;
        state ^= state << 5;
        return state;
    };
    size_t accepted{}, rejected{};
    for (int iteration = 0; iteration < 512; ++iteration) {
        auto bytes = seed;
        const auto at = qsizetype(random() % std::uint32_t(seed.size()));
        switch (iteration % 4) {
        case 0:
            bytes.truncate(at);
            break;
        case 1:
            bytes[at] = char(static_cast<unsigned char>(bytes[at]) ^ (1u << (random() % 8)));
            break;
        case 2:
            for (int i = 0; i < 1 + iteration % 31; ++i)
                bytes.append(char(random() & 255));
            break;
        case 3:
            for (qsizetype i = at; i < std::min(at + 16, bytes.size()); ++i)
                bytes[i] = char(random() & 255);
            break;
        }
        bool parsed{};
        try {
            parse(bytes, parsed);
            check(parsed, "Parser returned without declaring acceptance");
            ++accepted;
        } catch (const std::bad_alloc &) {
            throw; // Resource exhaustion is never classified as ordinary validation rejection.
        } catch (const CorpusFailure &) {
            throw;
        } catch (const std::exception &error) {
            if (parsed)
                throw CorpusFailure(
                    (name + ": accepted result invariant failed: " + error.what()).toStdString());
            ++rejected;
        }
    }
    check(rejected > 0, "Corpus must exercise rejection paths");
    return {
        {"format", name},
        {"mutations", 512},
        {"accepted", qint64(accepted)},
        {"rejected", qint64(rejected)},
        {"seedBytes", seed.size()},
        {"seedSha256",
         QString::fromLatin1(QCryptographicHash::hash(seed, QCryptographicHash::Sha256).toHex())}};
}
// Schema 27 page items (R091.b): every kind on one sheet, bound to the plan view and,
// for one label, to the broken view; the sheet has a nondefault dimension format (R091.g).
Document withDrawingItems(Document doc) {
    const auto sheet = doc.drawingSheets().begin()->first;
    updateDrawingSheetPage(doc, sheet, "A-101", 10,
                           DrawingDimensionFormat{DrawingDimensionUnit::Centimetres, 1, true});
    const auto plan = doc.drawingViews().begin()->first;
    const auto broken = doc.drawingViews().rbegin()->first;
    const auto body = doc.bodies().begin()->first;
    const auto &record = *doc.bodies().at(body);
    const auto vertex = vertexAnchor(doc, body, record.surface.vertices.begin()->first);
    const auto edge = edgeAnchor(doc, body, record.topology.edges.begin()->first, .5);
    DrawingTextItem text;
    text.frame = {20, 180, 120, 20};
    text.text = "Notes";
    createDrawingItem(doc, sheet, text);
    DrawingLineItem line;
    line.from = {10, 287};
    line.to = {410, 287};
    createDrawingItem(doc, sheet, line);
    DrawingRectangleItem rectangle;
    rectangle.frame = {10, 10, 400, 277};
    rectangle.dash = DrawingStrokeDash::Dashed;
    createDrawingItem(doc, sheet, rectangle);
    DrawingTitleBlockItem block;
    block.frame = {250, 230, 150, 47};
    block.fields = {"Corpus", "Plan", "2026-10-14", "A"};
    createDrawingItem(doc, sheet, block);
    DrawingDimensionItem dimension;
    dimension.view = plan;
    dimension.anchors = {vertex, edge};
    dimension.orientation = DrawingDimensionOrientation::Horizontal;
    dimension.offsetMm = -8;
    dimension.lastMm = {DrawingPointMm{60, 120}, DrawingPointMm{100, 120}};
    createDrawingItem(doc, sheet, dimension);
    DrawingLabelItem label;
    label.text = "W2";
    label.textAt = {300, 200};
    label.target.kind = DrawingLabelTargetKind::View;
    label.target.view = broken;
    label.target.anchor = vertex;
    label.target.lastMm = {260, 90};
    createDrawingItem(doc, sheet, label);
    DrawingLabelItem north;
    north.text = "North";
    north.textAt = {380, 30};
    north.target.pointMm = {400, 40};
    createDrawingItem(doc, sheet, north);
    return doc;
}
} // namespace
int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    try {
        QTemporaryDir temporary;
        check(temporary.isValid(), "Corpus scratch");
        Document source;
        const auto body = source.addFace({{{0, 0, 0}, {2, 0, 0}, {2, 3, 0}, {0, 3, 0}}});
        source.extrude(body, source.bodies().at(body)->surface.faces.begin()->first, 4);
        const auto component = createComponent(source, body, "Corpus block");
        auto canonical = QJsonDocument::fromJson(encodeDocument(source)).object();
        canonical["documentId"] = "0000000000000000000000000000083a";
        source = decodeDocument(QJsonDocument(canonical).toJson(QJsonDocument::Compact));
        const auto original = encodeContainer(source);
        QJsonArray results;
        results.append(
            exercise("native-json", encodeDocument(source), [](const auto &bytes, bool &parsed) {
                const auto document = decodeDocument(bytes);
                parsed = true;
                roundtrip(document);
            }));
        results.append(exercise("native-container", original, [](const auto &bytes, bool &parsed) {
            const auto document = decodeContainer(bytes);
            parsed = true;
            roundtrip(document);
        }));
        // Schema 27 (R090.b): drawing sheets and views, one of them a broken scene link.
        Document sheets;
        {
            sheets.addFace({{{0, 0, 0}, {2, 0, 0}, {2, 3, 0}, {0, 3, 0}}});
            SceneSnapshot snapshot;
            snapshot.camera = SceneCamera{};
            snapshot.camera->orthographic = true;
            const auto plan = createScene(sheets, "Plan", snapshot);
            const auto gone = createScene(sheets, "Removed", snapshot);
            const auto sheet = createDrawingSheet(sheets, "Sheet 1", {420, 297});
            DrawingViewRecord view;
            view.name = "Plan view";
            view.sheet = sheet;
            view.source = {DrawingViewSourceKind::Scene, plan};
            view.frame = {20, 20, 200, 150};
            view.scaleDenominator = 50;
            view.accepted = DrawingViewAcceptance{sheets.revision(), std::string(64, 'a')};
            createDrawingView(sheets, view);
            view.name = "Broken view";
            view.source.scene = gone;
            view.frame = {230, 20, 150, 150};
            view.content = DrawingViewContent::RasterAppearance;
            view.accepted.reset();
            createDrawingView(sheets, view);
            eraseScene(sheets, gone);
            auto tree = QJsonDocument::fromJson(encodeDocument(sheets)).object();
            tree["documentId"] = "0000000000000000000000000000083d";
            sheets = decodeDocument(QJsonDocument(tree).toJson(QJsonDocument::Compact));
        }
        const auto sheetContainer = encodeContainer(sheets);
        results.append(exercise("native-json-drawing-sheets", encodeDocument(sheets),
                                [](const auto &bytes, bool &parsed) {
                                    const auto document = decodeDocument(bytes);
                                    parsed = true;
                                    roundtrip(document);
                                }));
        results.append(exercise("native-container-drawing-sheets", sheetContainer,
                                [](const auto &bytes, bool &parsed) {
                                    const auto document = decodeContainer(bytes);
                                    parsed = true;
                                    roundtrip(document);
                                }));
        // Schema 27 (R091.b): the same sheets with every page-item kind.
        Document items;
        {
            auto tree = QJsonDocument::fromJson(encodeDocument(withDrawingItems(sheets))).object();
            tree["documentId"] = "0000000000000000000000000000083e";
            items = decodeDocument(QJsonDocument(tree).toJson(QJsonDocument::Compact));
        }
        const auto itemContainer = encodeContainer(items);
        results.append(exercise("native-json-drawing-items", encodeDocument(items),
                                [](const auto &bytes, bool &parsed) {
                                    const auto document = decodeDocument(bytes);
                                    parsed = true;
                                    roundtrip(document);
                                }));
        results.append(exercise("native-container-drawing-items", itemContainer,
                                [](const auto &bytes, bool &parsed) {
                                    const auto document = decodeContainer(bytes);
                                    parsed = true;
                                    roundtrip(document);
                                }));
        // Schema 28 (R093.c): the ADR 0165 table with every parameter type and driver
        // kind, a nested childParameter and a repeat at its maxCount.
        Document parameters;
        Id room{};
        {
            auto table = parameter_fixture::makeTable(4);
            overrideComponentParameters(table.doc, table.wide,
                                        {{"shelves", parameter_fixture::count(4)}});
            room = table.room;
            auto tree = QJsonDocument::fromJson(encodeDocument(table.doc)).object();
            tree["documentId"] = "0000000000000000000000000000083f";
            parameters = decodeDocument(QJsonDocument(tree).toJson(QJsonDocument::Compact));
        }
        const auto parameterContainer = encodeContainer(parameters);
        results.append(exercise("native-json-component-parameters", encodeDocument(parameters),
                                [](const auto &bytes, bool &parsed) {
                                    const auto document = decodeDocument(bytes);
                                    parsed = true;
                                    roundtrip(document);
                                }));
        results.append(exercise("native-container-component-parameters", parameterContainer,
                                [](const auto &bytes, bool &parsed) {
                                    const auto document = decodeContainer(bytes);
                                    parsed = true;
                                    roundtrip(document);
                                }));
        // Schema 29 (R095.a): a site origin and a two-tile terrain, and the terrain-mesh
        // chunk decoder on its own.
        Document terrain;
        {
            setSiteOrigin(terrain, SiteOrigin{512300, 5234500, 0, "EPSG:32633", {}});
            TerrainMesh mesh;
            for (int j = 0; j <= 40; ++j)
                for (int i = 0; i <= 30; ++i)
                    mesh.vertices.push_back({i * .5, j * .5, .1 * ((i * 7 + j * 3) % 5)});
            for (std::uint32_t j = 0; j < 40; ++j)
                for (std::uint32_t i = 0; i < 30; ++i) {
                    const auto a = j * 31 + i;
                    mesh.triangles.push_back({a, a + 1, a + 32});
                    mesh.triangles.push_back({a, a + 32, a + 31});
                }
            TerrainRecord record;
            record.generator = "corpus/terrain-v1";
            createTerrain(terrain, mesh, record);
            auto tree = QJsonDocument::fromJson(encodeDocument(terrain)).object();
            tree["documentId"] = "00000000000000000000000000000840";
            terrain = decodeDocument(QJsonDocument(tree).toJson(QJsonDocument::Compact));
        }
        {
            // R096.a: two tiles of one terrain with one key are refused at load.
            auto tree = QJsonDocument::fromJson(encodeDocument(terrain)).object();
            auto bodies = tree["bodies"].toArray();
            QJsonValue key;
            bool found = false;
            for (qsizetype i = 0; i < bodies.size(); ++i) {
                auto o = bodies[i].toObject();
                if (!o.contains("terrainTile"))
                    continue;
                if (!found) {
                    key = o["terrainTile"];
                    found = true;
                    continue;
                }
                o["terrainTile"] = key;
                bodies[i] = o;
                break;
            }
            check(found, "The terrain seed has two tiles");
            tree["bodies"] = bodies;
            bool refused = false;
            try {
                decodeDocument(QJsonDocument(tree).toJson(QJsonDocument::Compact));
            } catch (const std::exception &error) {
                refused = std::string(error.what()).find("two tiles with the key") !=
                          std::string::npos;
            }
            check(refused, "Duplicate terrain tile keys are refused, naming the key");
        }
        const auto terrainContainer = encodeContainer(terrain);
        results.append(exercise("native-container-terrain", terrainContainer,
                                [](const auto &bytes, bool &parsed) {
                                    const auto document = decodeContainer(bytes);
                                    parsed = true;
                                    roundtrip(document);
                                }));
        results.append(exercise("terrain-mesh", encodeTerrainMesh(terrain.bodies()),
                                [](const auto &bytes, bool &parsed) {
                                    const auto tiles = decodeTerrainMesh(bytes);
                                    parsed = true;
                                    size_t triangles = 0;
                                    for (const auto &tile : tiles) {
                                        check(!tile.triangles.empty() &&
                                                  tile.triangles.size() <= 2048,
                                              "Accepted terrain tile exceeds its limits");
                                        triangles += tile.triangles.size();
                                        for (const auto point : tile.vertices)
                                            coordinate(point);
                                        for (const auto &triangle : tile.triangles)
                                            for (const auto index : triangle)
                                                check(index < tile.vertices.size(),
                                                      "Accepted terrain index is out of range");
                                    }
                                    check(triangles <= 250000,
                                          "Accepted terrain exceeds the document limit");
                                }));
        // R096.a: the terrain edit list decoder, with the test-only kinds R096.a has.
        {
            terrain_test_kinds::registerKinds();
            Id terrainId = 0;
            for (const auto &[id, body] : terrain.bodies())
                if (body->terrain)
                    terrainId = id;
            Document edited = terrain;
            const auto tile = edited.bodies().at(terrainTiles(edited, terrainId).front());
            applyTerrainEdit(edited, terrainId, "test.lift",
                             terrain_test_kinds::lift(tile->tileKey, .5), "Lift");
            applyTerrainEdit(edited, terrainId, "test.split", terrain_test_kinds::split(7.3, 9.1),
                             "Split");
            QJsonArray list;
            for (const auto &edit : edited.bodies().at(terrainId)->terrain->edits)
                list.append(encodeTerrainEdit(*edit));
            // R096.c: the production source edits join the mutated list.
            {
                auto added = std::make_shared<TerrainSourceEditData>();
                added->source = 101;
                added->role = TerrainSourceRole::Breakline;
                added->points = {{1, 2, 3}, {4, 5, 6}, {7, 5, 6.5}};
                auto removed = std::make_shared<TerrainSourceEditData>();
                removed->source = 2;
                removed->before = {-1, -1, 1, 1};
                list.append(encodeTerrainEdit({10, terrainAddSourceKind, added}));
                list.append(encodeTerrainEdit({11, terrainRemoveSourceKind, removed}));
            }
            // R096.b: sculpt and smooth join it too.
            {
                auto brush = std::make_shared<TerrainSculptData>();
                brush->path = {{1, 2}, {4, 6}};
                brush->radius = 2;
                brush->amount = 0.5;
                auto region = std::make_shared<TerrainSculptData>();
                region->region = {{0, 0}, {6, 0}, {3, 5}};
                region->width = 1;
                region->falloff = TerrainFalloff::Constant;
                region->strength = 1;
                region->iterations = 8;
                list.append(encodeTerrainEdit({12, terrainSculptKind, brush}));
                list.append(encodeTerrainEdit({13, terrainSmoothKind, region}));
            }
            // R096.d: a resolved pad joins it too.
            {
                auto pad = std::make_shared<TerrainStampData>();
                pad->footprint = {{0, 0}, {4, 0}, {4, 3}, {0, 3}};
                pad->elevation = 2;
                pad->cutSlope = pad->fillSlope = 0.5;
                pad->maxDistance = 50;
                pad->outline = pad->footprint;
                pad->rays = {{0, {-1, -1, 1.5}}, {1, {5, -1, 1.5}}, {2, {5, 4, 2.5}},
                             {3, {-1, 4, 2.5}}};
                list.append(encodeTerrainEdit({14, terrainStampKind, pad}));
            }
            // R096.e: a resolved drape with region paint joins it too.
            {
                auto drape = std::make_shared<TerrainDrapeData>();
                drape->lines = {{{{0, 0}, {5, 1}}, false}, {{{2, 2}, {6, 2}, {6, 5}, {2, 5}}, true}};
                drape->paint = {{{{{2, 2}, {6, 2}, {6, 5}, {2, 5}}}, 3}};
                drape->runs = {{{0, 0, 1}, {5, 1, 1.5}},
                               {{2, 2, 1.2}, {6, 2, 1.4}, {6, 5, 1.6}, {2, 5, 1.3}, {2, 2, 1.2}}};
                drape->splits = {{{1, 0}, {1, 3}, {{1, 0.2, 1.05}}}};
                list.append(encodeTerrainEdit({15, terrainDrapeKind, drape}));
            }
            results.append(exercise("terrain-edits", QJsonDocument(list).toJson(QJsonDocument::Compact),
                                    [](const auto &bytes, bool &parsed) {
                                        const auto json = QJsonDocument::fromJson(bytes);
                                        if (!json.isArray())
                                            throw std::runtime_error("not a list");
                                        const auto edits = decodeTerrainEdits(json.array());
                                        parsed = true;
                                        check(!edits.empty() && edits.size() <= 4096,
                                              "Accepted terrain edits exceed their bound");
                                        std::set<std::uint32_t> ids;
                                        for (const auto &edit : edits)
                                            check(edit->id && ids.insert(edit->id).second,
                                                  "Accepted terrain edit ids repeat");
                                    }));
        }
        const auto glb = exportGlb(RenderSnapshot::capture(source)).glb;
        const auto glbPath = temporary.filePath("mutated.glb");
        results.append(exercise("glb", glb, [&](const auto &bytes, bool &parsed) {
            write(glbPath, bytes);
            auto result = loadGltf(glbPath);
            parsed = true;
            roundtrip(result.document);
        }));
        for (const auto encoding : {StlEncoding::Binary, StlEncoding::Ascii}) {
            const auto seed = exportStl(source, {1, StlUpAxis::Z}, encoding).bytes;
            results.append(exercise(encoding == StlEncoding::Binary ? "stl-binary" : "stl-ascii",
                                    seed, [](const auto &bytes, bool &parsed) {
                                        const auto result = parseStl(bytes, {1, StlUpAxis::Z});
                                        parsed = true;
                                        for (const auto &facet : result.facets)
                                            for (const auto p : facet.vertices)
                                                coordinate(p);
                                    }));
        }
        results.append(exercise("obj", "v 0 0 0\nv 2 0 0\nv 0 3 0\nf 1 2 3\n",
                                [](const auto &bytes, bool &parsed) {
                                    const auto result = parseObj(bytes, {1, ObjUpAxis::Z});
                                    parsed = true;
                                    for (const auto point : result.vertices)
                                        coordinate(point);
                                    for (const auto &face : result.faces)
                                        for (const auto corner : face.corners)
                                            check(corner.vertex < result.vertices.size(),
                                                  "Accepted OBJ references a missing vertex");
                                }));
        Document planar;
        planar.addFace({{{0, 0, 0}, {2, 0, 0}, {2, 3, 0}, {0, 3, 0}}});
        results.append(
            exercise("dxf", exportDxf(planar, 1).bytes, [](const auto &bytes, bool &parsed) {
                const auto result = parseDxf(bytes);
                parsed = true;
                for (const auto &entity : result.entities)
                    for (const auto &segment : entity.segments) {
                        coordinate(segment.start);
                        coordinate(segment.end);
                        coordinate(segment.center);
                    }
            }));
        // R095.c terrain sources: an accepted source keeps finite, bounded coordinates and
        // its dry-run import keeps the bounded report.
        auto terrainSource = [](const TerrainSourceSet &set) {
            for (const auto &polyline : set.polylines) {
                check(polyline.points.size() >= 2 && polyline.layer < set.layers.size(),
                      "Accepted terrain polyline is malformed");
                for (const auto &p : polyline.points)
                    check(std::isfinite(p.x) && std::isfinite(p.y) && std::isfinite(p.z),
                          "Accepted terrain coordinate is not finite");
            }
            for (const auto &point : set.points)
                check(point.layer < set.layers.size() && std::isfinite(point.position.z),
                      "Accepted terrain point is malformed");
            const auto result = runTerrainImport(set, {}, std::nullopt);
            check(QJsonDocument(result.report).toJson(QJsonDocument::Compact).size() <= 64 * 1024,
                  "Terrain import report exceeds 64 KiB");
        };
        {
            // A small slice of the contour fixture keeps 512 dry-run imports quick.
            std::vector<terrain_fixture::Item> items;
            for (const auto &item : terrain_fixture::site())
                if (item.handle >= 0x10C && item.handle <= 0x112)
                    items.push_back(item);
            results.append(exercise("terrain-dxf", terrain_fixture::dxf(items, {}, 6),
                                    [&](const auto &bytes, bool &parsed) {
                                        const auto set = parseDxfTerrain(bytes);
                                        parsed = true;
                                        terrainSource(set);
                                    }));
        }
        results.append(exercise("terrain-points",
                                "P,N,E,Z,D\n1,200.5,100.25,10.5,GND\n2,201,103,11,BL\n"
                                "3,205,101,12,BL\n4,199,104,9.5,GND\n5,198,99,9.75,GND\n",
                                [&](const auto &bytes, bool &parsed) {
                                    TerrainPointOptions options;
                                    options.unit = *terrainUnit("m");
                                    const auto set = parseTerrainPoints(bytes, options);
                                    parsed = true;
                                    terrainSource(set);
                                }));
        const auto png = encodeTexturePng(TextureImage(1, 1, {10, 20, 30, 255}));
        const TemplateMetadata metadata{"Corpus", "Bounded corruption fixture", {"test"}, 0};
        results.append(exercise("template", encodeTemplateBundle(source, metadata, png),
                                [](const auto &bytes, bool &parsed) {
                                    const auto result = decodeTemplateBundle(bytes);
                                    parsed = true;
                                    roundtrip(result.document);
                                }));
        results.append(exercise(
            "component",
            stableComponentSeed(encodeComponentBundle(source, component.definition, metadata, png)),
            [](const auto &bytes, bool &parsed) {
                const auto result = decodeComponentBundle(bytes);
                parsed = true;
                roundtrip(result.document);
            }));
        results.append(exercise("template-component-parameters",
                                encodeTemplateBundle(parameters, metadata, png),
                                [](const auto &bytes, bool &parsed) {
                                    const auto result = decodeTemplateBundle(bytes);
                                    parsed = true;
                                    roundtrip(result.document);
                                }));
        results.append(exercise(
            "component-component-parameters",
            stableComponentSeed(encodeComponentBundle(parameters, room, metadata, png)),
            [](const auto &bytes, bool &parsed) {
                const auto result = decodeComponentBundle(bytes);
                parsed = true;
                roundtrip(result.document);
            }));
        results.append(exercise(
            "extension", read(QStringLiteral(SOURCE_DIR "/examples/extensions/panel.sketchyext")),
            [](const auto &bytes, bool &parsed) {
                const auto manifest = parseExtensionManifest(bytes);
                parsed = true;
                check(!manifest.actions.isEmpty(), "Accepted manifest has actions");
                // Manifest parsing owns structural/capability validity; expanded command
                // geometry is validated only when executed against a document.
                check(manifest.source == bytes, "Extension retains exact validated source");
            }));
        // R093.a: parameter expressions (ADR 0165 §7). A harness invariant failure is a
        // corpus failure, never an ordinary rejection.
        results.append(exercise(
            "parameter-expression",
            "clamp(width - 60 mm, 400 mm, 2.4 m) * if(style == \"oak\", 1, ratio ^ 2)",
            [](const auto &bytes, bool &parsed) {
                bool accepted = false;
                try {
                    accepted = fuzz::parameterExpressionAccepted(
                        std::string_view(bytes.constData(), std::size_t(bytes.size())));
                } catch (const fuzz::ParameterHarnessFailure &failure) {
                    throw CorpusFailure(std::string("parameter-expression: ") + failure.what());
                }
                if (!accepted)
                    throw std::runtime_error("parameter expression rejected");
                parsed = true;
            }));
        check(encodeContainer(source) == original, "Corpus never mutates its source model");
        check(encodeContainer(sheets) == sheetContainer, "Corpus never mutates its sheet model");
        check(encodeContainer(items) == itemContainer, "Corpus never mutates its item model");
        check(encodeContainer(parameters) == parameterContainer,
              "Corpus never mutates its parameter model");
        check(encodeContainer(terrain) == terrainContainer,
              "Corpus never mutates its terrain model");
        check(results.size() == 24,
              "Corpus covers fifteen formats plus the four schema-27, four schema-28 and one "
              "schema-29 seeds");
        std::cout << QJsonDocument(QJsonObject{{"corpusVersion", 1},
                                               {"seed", "0x83a5c0de"},
                                               {"formats", results},
                                               {"mutations", 11776},
                                               {"sourcePreserved", true}})
                         .toJson()
                         .toStdString();
        return 0;
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
