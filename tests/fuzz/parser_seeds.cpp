#include "../catalog_fixtures.hpp"
#include "../component_parameter_fixture.hpp"
#include "../ifc_package_fixtures.hpp"
#include "../terrain_import_fixtures.hpp"
#include "core/annotation_anchors.hpp"
#include "core/components.hpp"
#include "core/drawing_sheets.hpp"
#include "core/presentation.hpp"
#include "core/scenes.hpp"
#include "core/terrain.hpp"
#include "core/terrain_drape.hpp"
#include "core/terrain_edit.hpp"
#include "core/terrain_sculpt.hpp"
#include "core/terrain_source_edit.hpp"
#include "core/terrain_stamp.hpp"
#include "geo/geographic.hpp"
#include "integrations/glb_export.hpp"
#include "io/dxf_export.hpp"
#include "io/model_style_io.hpp"
#include "io/presentation_io.hpp"
#include "io/stl_export.hpp"
#include "io/texture_image.hpp"
#include "parser_harness.hpp"
#include <QCryptographicHash>
#include <QDir>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QtEndian>
#include <iostream>
#include <map>

using namespace sketchy;
namespace {
Document stableIdentity(const Document &source, const QString &identity) {
    auto json = QJsonDocument::fromJson(encodeDocument(source)).object();
    json["documentId"] = identity;
    return decodeDocument(QJsonDocument(json).toJson(QJsonDocument::Compact));
}
QByteArray stableComponent(QByteArray bytes) {
    const auto manifestSize = qFromLittleEndian<quint32>(bytes.constData() + 8);
    const auto modelSize = qFromLittleEndian<quint64>(bytes.constData() + 12);
    const auto thumbnail = bytes.mid(24 + manifestSize + qsizetype(modelSize));
    auto manifest = QJsonDocument::fromJson(bytes.mid(24, manifestSize)).object();
    const auto document =
        stableIdentity(decodeContainer(bytes.mid(24 + manifestSize, qsizetype(modelSize))),
                       QStringLiteral("0000000000000000000000000000083b"));
    const auto model = encodeContainer(document);
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
        if (argc != 2)
            throw std::runtime_error("Usage: parser_fuzz_seeds NEW_DIRECTORY");
        const QString root = QString::fromLocal8Bit(argv[1]);
        if (QFileInfo::exists(root) || !QDir().mkpath(root))
            throw std::runtime_error("Seed output must be a new private directory");
        QTemporaryDir scratch;
        fuzz::require(scratch.isValid(), "seed validation scratch");
        Document source;
        const auto body = source.addFace({{{0, 0, 0}, {2, 0, 0}, {2, 3, 0}, {0, 3, 0}}});
        source.extrude(body, source.bodies().at(body)->surface.faces.begin()->first, 4);
        const auto component = createComponent(source, body, "Corpus block");
        source = stableIdentity(source, QStringLiteral("0000000000000000000000000000083a"));
        Document planar;
        planar.addFace({{{0, 0, 0}, {2, 0, 0}, {2, 3, 0}, {0, 3, 0}}});
        const auto png = encodeTexturePng(TextureImage(1, 1, {10, 20, 30, 255}));
        const TemplateMetadata metadata{"Corpus", "Bounded corruption fixture", {"test"}, 0};
        QFile extension(QStringLiteral(SOURCE_DIR "/examples/extensions/panel.sketchyext"));
        fuzz::require(extension.open(QIODevice::ReadOnly), "read shipped extension seed");
        const std::map<QString, QByteArray> seeds{
            {"native-json", encodeDocument(source)},
            {"native-container", encodeContainer(source)},
            {"glb", exportGlb(RenderSnapshot::capture(source)).glb},
            {"stl-binary", exportStl(source, {1, StlUpAxis::Z}, StlEncoding::Binary).bytes},
            {"stl-ascii", exportStl(source, {1, StlUpAxis::Z}, StlEncoding::Ascii).bytes},
            {"obj", "v 0 0 0\nv 2 0 0\nv 0 3 0\nf 1 2 3\n"},
            {"dxf", exportDxf(planar, 1).bytes},
            {"template", encodeTemplateBundle(source, metadata, png)},
            {"component",
             stableComponent(encodeComponentBundle(source, component.definition, metadata, png))},
            {"extension", extension.readAll()}};
        QJsonArray rows;
        const auto publish = [&](const QString &format, const QByteArray &bytes,
                                 const QString &name) {
            fuzz::require(bytes.size() <= qsizetype(fuzz::inputLimit), "bounded seed");
            fuzz::require(fuzz::parseInput(format, bytes, scratch.filePath("input.glb")),
                          "valid seed reaches accepted-result invariants");
            const auto directory = QDir(root).filePath(format);
            fuzz::require(QDir().mkpath(directory), "seed directory");
            const auto path = QDir(directory).filePath(name);
            QFile file(path);
            fuzz::require(file.open(QIODevice::WriteOnly | QIODevice::NewOnly) &&
                              file.write(bytes) == bytes.size(),
                          "publish original seed");
            file.close();
            rows.append(QJsonObject{
                {"format", format},
                {"name", name},
                {"bytes", bytes.size()},
                {"sha256",
                 QString::fromLatin1(
                     QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex())}});
        };
        // Invalid inputs that must reach a rejection, never an accepted result.
        QJsonArray rejectedRows;
        const auto publishRejected = [&](const QString &format, const QByteArray &bytes,
                                         const QString &name) {
            fuzz::require(bytes.size() <= qsizetype(fuzz::inputLimit), "bounded seed");
            fuzz::require(!fuzz::parseInput(format, bytes, scratch.filePath("input.glb")),
                          "invalid seed is rejected");
            const auto directory = QDir(root).filePath(format);
            fuzz::require(QDir().mkpath(directory), "seed directory");
            QFile file(QDir(directory).filePath(name));
            fuzz::require(file.open(QIODevice::WriteOnly | QIODevice::NewOnly) &&
                              file.write(bytes) == bytes.size(),
                          "publish rejected seed");
            rejectedRows.append(QJsonObject{
                {"format", format},
                {"name", name},
                {"bytes", bytes.size()},
                {"sha256",
                 QString::fromLatin1(
                     QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex())}});
        };
        for (const auto &[format, bytes] : seeds)
            publish(format, bytes, "block");
        const auto empty =
            stableIdentity(Document{}, QStringLiteral("0000000000000000000000000000083c"));
        publish("native-json", encodeDocument(empty), "empty-document");
        publish("native-container", encodeContainer(empty), "empty-document");
        // Schema 27: drawing sheets and views, one of them a broken scene link.
        auto sheets = planar;
        {
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
        }
        sheets = stableIdentity(sheets, QStringLiteral("0000000000000000000000000000083d"));
        publish("native-json", encodeDocument(sheets), "drawing-sheets");
        publish("native-container", encodeContainer(sheets), "drawing-sheets");
        const auto items = stableIdentity(withDrawingItems(sheets),
                                          QStringLiteral("0000000000000000000000000000083e"));
        publish("native-json", encodeDocument(items), "drawing-items");
        publish("native-container", encodeContainer(items), "drawing-items");
        // R093.c: schema 28. The ADR 0165 table with every parameter type and driver kind,
        // a nested childParameter (the room) and a repeat at its maxCount (four shelves).
        {
            auto table = parameter_fixture::makeTable(4);
            overrideComponentParameters(table.doc, table.wide,
                                        {{"shelves", parameter_fixture::count(4)}});
            const auto parameters = stableIdentity(
                table.doc, QStringLiteral("0000000000000000000000000000083f"));
            publish("native-json", encodeDocument(parameters), "component-parameters");
            publish("native-container", encodeContainer(parameters), "component-parameters");
            publish("template", encodeTemplateBundle(parameters, metadata, png),
                    "component-parameters");
            publish("component",
                    stableComponent(
                        encodeComponentBundle(parameters, table.room, metadata, png)),
                    "component-parameters");
            // A stored cycle: leg_x refers to itself. Readers must refuse it.
            auto cyclic = QJsonDocument::fromJson(encodeDocument(parameters)).object();
            auto tables = cyclic["componentParameters"].toArray();
            auto record = tables[0].toObject();
            auto list = record["parameters"].toArray();
            auto legX = list[5].toObject();
            legX["expression"] = "leg_x - 60 mm";
            list[5] = legX;
            record["parameters"] = list;
            tables[0] = record;
            cyclic["componentParameters"] = tables;
            publishRejected("native-json", QJsonDocument(cyclic).toJson(QJsonDocument::Compact),
                            "component-parameter-cycle");
            // R094.b: the room's record carries the optional assembly identity, so the
            // seeds above exercise it. A malformed identity must be refused.
            auto malformed = QJsonDocument::fromJson(encodeDocument(parameters)).object();
            auto records = malformed["componentParameters"].toArray();
            bool identified = false;
            for (qsizetype i = 0; i < records.size(); ++i) {
                auto entry = records[i].toObject();
                if (!entry.contains("assembly"))
                    continue;
                entry["assembly"] = QJsonObject{{"id", "Sketchyup/Room/1"}, {"version", 0}};
                records[i] = entry;
                identified = true;
            }
            fuzz::require(identified, "the parameter seed carries an assembly identity");
            malformed["componentParameters"] = records;
            publishRejected("native-json",
                            QJsonDocument(malformed).toJson(QJsonDocument::Compact),
                            "component-assembly-malformed");
        }
        // R095.a: schema 29. A site origin and a two-tile terrain; the terrain-mesh chunk
        // on its own, and one whose header claims more triangles than the limit.
        {
            Document terrain;
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
            record.generator = "seed/terrain-v1";
            record.sources.push_back(
                {TerrainSourceRole::Contour, "DXF handle 2F", {mesh.vertices[0], mesh.vertices[5]}});
            createTerrain(terrain, mesh, record);
            terrain = stableIdentity(terrain, QStringLiteral("00000000000000000000000000000840"));
            publish("native-json", encodeDocument(terrain), "terrain");
            {
                // R096.a: two tiles of the terrain with one key are refused at load.
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
                tree["bodies"] = bodies;
                publishRejected("native-json", QJsonDocument(tree).toJson(QJsonDocument::Compact),
                                "terrain-duplicate-tile-key");
            }
            publish("native-container", encodeContainer(terrain), "terrain");
            const auto chunk = encodeTerrainMesh(terrain.bodies());
            fuzz::require(chunk.size() > 12, "the terrain seed has a mesh chunk");
            publish("terrain-mesh", chunk, "block");
            QByteArray oversized("SKTMESH1", 8);
            char word[8];
            qToLittleEndian<quint32>(123, word);
            oversized.append(word, 4);
            for (quint64 tile = 1; tile <= 123; ++tile) {
                qToLittleEndian<quint64>(tile, word);
                oversized.append(word, 8);
                qToLittleEndian<quint32>(3000, word);
                oversized.append(word, 4);
                qToLittleEndian<quint32>(2048, word);
                oversized.append(word, 4);
            }
            publishRejected("terrain-mesh", oversized, "oversized-counts");
            // R096.a: the edit list decoder (with the test-only kinds R096.a has): a lift and a
            // split as an edit applies them, and a list one entry over the 4,096 bound.
            fuzz::registerTerrainTestKinds();
            Id terrainId = 0;
            for (const auto &[id, body] : terrain.bodies())
                if (body->terrain)
                    terrainId = id;
            const auto tile = terrain.bodies().at(terrainTiles(terrain, terrainId).front());
            applyTerrainEdit(terrain, terrainId, "test.lift",
                             terrain_test_kinds::lift(tile->tileKey, .5), "Lift");
            applyTerrainEdit(terrain, terrainId, "test.split", terrain_test_kinds::split(7.3, 9.1),
                             "Split");
            QJsonArray edits;
            for (const auto &edit : terrain.bodies().at(terrainId)->terrain->edits)
                edits.append(encodeTerrainEdit(*edit));
            publish("terrain-edits", QJsonDocument(edits).toJson(QJsonDocument::Compact), "list");
            QJsonArray tooMany;
            for (int i = 0; i < 4097; ++i) {
                auto entry = edits[0].toObject();
                entry["id"] = i + 1;
                tooMany.append(entry);
            }
            publishRejected("terrain-edits", QJsonDocument(tooMany).toJson(QJsonDocument::Compact),
                            "oversized-count");
            // R096.c: the production source edits (editSource, addSource, removeSource), and an
            // added "contour" at two elevations, which the decoder refuses.
            {
                auto added = std::make_shared<TerrainSourceEditData>();
                added->source = 101;
                added->role = TerrainSourceRole::Breakline;
                added->points = {{1, 2, 3}, {4, 5, 6}, {7, 5, 6.5}};
                auto edited = std::make_shared<TerrainSourceEditData>();
                edited->source = 1;
                edited->role = TerrainSourceRole::Contour;
                edited->points = {{0, 0, 10}, {2, 0, 10}, {2, 2, 10}, {0, 0, 10}};
                edited->before = {0, 0, 2, 2};
                auto removed = std::make_shared<TerrainSourceEditData>();
                removed->source = 2;
                removed->before = {-1, -1, 1, 1};
                QJsonArray sources{
                    encodeTerrainEdit({1, terrainAddSourceKind, added}),
                    encodeTerrainEdit({2, terrainEditSourceKind, edited}),
                    encodeTerrainEdit({3, terrainRemoveSourceKind, removed})};
                publish("terrain-edits", QJsonDocument(sources).toJson(QJsonDocument::Compact),
                        "source-edits");
                auto twoLevels = sources[1].toObject();
                twoLevels["points"] = QJsonArray{QJsonArray{0, 0, 10}, QJsonArray{2, 0, 11}};
                publishRejected("terrain-edits",
                                QJsonDocument(QJsonArray{twoLevels}).toJson(QJsonDocument::Compact),
                                "contour-two-elevations");
            }
            // R096.b: sculpt (a brush) and smooth (a region with a falloff width), and a smooth
            // with nine iterations, which the decoder refuses.
            {
                auto brush = std::make_shared<TerrainSculptData>();
                brush->path = {{1, 2}, {4, 6}, {9, 5}};
                brush->radius = 3.5;
                brush->falloff = TerrainFalloff::Linear;
                brush->amount = -0.75;
                brush->pieces = {{3, 4294967295u}};
                auto region = std::make_shared<TerrainSculptData>();
                region->region = {{0, 0}, {10, 0}, {10, 8}, {4, 12}, {0, 8}};
                region->width = 2;
                region->strength = 0.5;
                region->iterations = 3;
                QJsonArray sculpts{encodeTerrainEdit({1, terrainSculptKind, brush}),
                                   encodeTerrainEdit({2, terrainSmoothKind, region})};
                publish("terrain-edits", QJsonDocument(sculpts).toJson(QJsonDocument::Compact),
                        "sculpt-smooth");
                auto nine = sculpts[1].toObject();
                nine["iterations"] = 9;
                publishRejected("terrain-edits",
                                QJsonDocument(QJsonArray{nine}).toJson(QJsonDocument::Compact),
                                "smooth-nine-iterations");
            }
            // R096.d: a resolved daylight pad (rays, a dropped count and a recorded piece), an
            // unresolved fixed-width pad with its source body, and a pad whose ray starts run
            // backwards, which the decoder refuses.
            {
                auto pad = std::make_shared<TerrainStampData>();
                pad->footprint = {{0, 0}, {4, 0}, {4, 3}, {0, 3}};
                pad->elevation = 12.5;
                pad->cutSlope = 0.5;
                pad->fillSlope = 0.4;
                pad->maxDistance = 100;
                pad->outline = {{0, 0}, {2, 0}, {4, 0}, {4, 3}, {0, 3}};
                pad->rays = {{0, {-1, -1, 12}}, {1, {2, -1.5, 11.9}}, {2, {5, -1, 12.1}},
                             {3, {5, 4, 12.5}}, {4, {0, 3, 12.5}}};
                pad->dropped = 2;
                pad->pieces = {{9, 4294967293u}};
                auto band = std::make_shared<TerrainStampData>();
                band->footprint = {{10, 10}, {14, 10}, {12, 13}};
                band->elevation = -3;
                band->mode = TerrainStampMode::Width;
                band->width = 2.5;
                band->sourceBody = 42;
                band->sourceFace = 7;
                QJsonArray pads{encodeTerrainEdit({1, terrainStampKind, pad}),
                                encodeTerrainEdit({2, terrainStampKind, band})};
                publish("terrain-edits", QJsonDocument(pads).toJson(QJsonDocument::Compact),
                        "stamp-pads");
                auto backwards = pads[0].toObject();
                auto rays = backwards["rays"].toArray();
                const QJsonValue first = rays[0];
                rays[0] = rays[2];
                rays[2] = first;
                backwards["rays"] = rays;
                publishRejected("terrain-edits",
                                QJsonDocument(QJsonArray{backwards}).toJson(QJsonDocument::Compact),
                                "stamp-rays-backwards");
            }
            // R096.e: a resolved drape (an open and a closed line, a paint region with a hole, its
            // runs and a split contour segment), an unresolved drape, and a drape whose split names
            // its segment's larger end first, which the decoder refuses.
            {
                auto drape = std::make_shared<TerrainDrapeData>();
                drape->lines = {{{{0, 0}, {5, 1}, {9, 0.5}}, false},
                                {{{2, 2}, {6, 2}, {6, 5}, {2, 5}}, true},
                                {{{3, 3}, {3, 4}, {4, 4}, {4, 3}}, true}};
                drape->paint = {{{{{2, 2}, {6, 2}, {6, 5}, {2, 5}}, {{3, 3}, {3, 4}, {4, 4}, {4, 3}}}, 7}};
                drape->runs = {{{0, 0, 10}, {2.5, 0.5, 10.2}, {5, 1, 10.4}, {9, 0.5, 10.1}},
                               {{2, 2, 10.5}, {6, 2, 10.6}, {6, 5, 10.9}, {2, 5, 10.7}, {2, 2, 10.5}},
                               {{3, 3, 10.6}, {3, 4, 10.7}, {4, 4, 10.75}, {4, 3, 10.65}, {3, 3, 10.6}}};
                drape->splits = {{{2, 0}, {3, 1}, {{2.5, 0.5, 10.2}}}};
                auto plain = std::make_shared<TerrainDrapeData>();
                plain->lines = {{{{-4, -4}, {-1, -2}}, false}};
                QJsonArray drapes{encodeTerrainEdit({1, terrainDrapeKind, drape}),
                                  encodeTerrainEdit({2, terrainDrapeKind, plain})};
                publish("terrain-edits", QJsonDocument(drapes).toJson(QJsonDocument::Compact),
                        "drapes");
                auto backwards = drapes[0].toObject();
                auto splits = backwards["splits"].toArray();
                auto split = splits[0].toObject();
                const QJsonValue a = split["a"], b = split["b"];
                split["a"] = b;
                split["b"] = a;
                splits[0] = split;
                backwards["splits"] = splits;
                publishRejected("terrain-edits",
                                QJsonDocument(QJsonArray{backwards}).toJson(QJsonDocument::Compact),
                                "drape-split-backwards");
            }
        }
        // R095.c: terrain sources, the generated fixtures (ADR 0168 section 6). The invalid
        // fixture parses and its import is refused, which the harness checks too.
        publish("terrain-dxf", terrain_fixture::terrainContoursMetres(), "contours-m");
        publish("terrain-dxf", terrain_fixture::terrainUtm(), "utm");
        publish("terrain-dxf", terrain_fixture::terrainInvalid(), "invalid");
        publishRejected("terrain-dxf", terrain_fixture::terrainContoursUsft().replace("AC1015", "AC1001"),
                        "unsupported-version");
        publish("terrain-points", terrain_fixture::terrainXyz(true), "utm-xyz");
        publish("terrain-points", "P,N,E,Z,D\n1,200.5,100.25,10.5,GND\n2,201,103,11,BL\n"
                                  "3,205,101,12,BL\n4,199,104,9.5,GND\n",
                "pnezd");
        publish("terrain-points", "100;200;10\n101;x;11\n102;203;inf\n", "bad-values");
        publishRejected("terrain-points", QByteArray("1 2\n"), "too-few-columns");
        // R097.b: the IFC conversion package decoder (ADR 0171 section 1.6): the sample package of
        // tests/ifc_package_fixtures.hpp, and three packages the decoder must refuse.
        {
            const auto package = ifc_fixture::samplePackage();
            const auto bytes = encodeIfcPackage(package);
            publish("ifc-package", bytes, "sample");
            publish("ifc-package", encodeIfcPackage(IfcPackage{}), "empty");
            // Packages the helper wrote from buildingSMART samples (tests/fixtures/ifc/packages).
            for (const auto &name : {"rv-wall-with-opening-and-window", "rv-tessellated-item", "rv-tessellation-with-individual-colors"}) {
                QFile real(QStringLiteral(SOURCE_DIR "/tests/fixtures/ifc/packages/") + QString::fromLatin1(name) + ".skifc");
                fuzz::require(real.open(QIODevice::ReadOnly), "read a real IFC package seed");
                publish("ifc-package", real.readAll(), QString::fromLatin1(name));
            }
            publishRejected("ifc-package", bytes.left(bytes.size() - 5), "truncated");
            auto badMagic = bytes;
            badMagic[3] = 'X';
            publishRejected("ifc-package", badMagic, "bad-magic");
            auto parent = ifc_fixture::samplePackage();
            parent.nodes[1].parent = 2;
            publishRejected("ifc-package", encodeIfcPackage(parent), "parent-after-child");
        }
        // R099.a: schema 30 camera paths and render presets. A path over a scene that was
        // deleted loads broken, not refused. Rejected: a path of one keyframe, a keyframe of
        // zero, and a preset narrower than 64 pixels.
        {
            Document presentation;
            SceneSnapshot plan;
            plan.camera = SceneCamera{};
            plan.camera->orthographic = true;
            SceneSnapshot side;
            side.camera = SceneCamera{};
            side.camera->yaw = 90;
            const auto planScene = createScene(presentation, "Plan", plan);
            const auto sideScene = createScene(presentation, "Side", side);
            const auto gone = createScene(presentation, "Gone", side);
            createCameraPath(presentation, "Tour", {planScene, sideScene, planScene},
                             CameraPathTiming{30, 12, 6});
            createCameraPath(presentation, "Stale", {planScene, gone});
            eraseScene(presentation, gone);
            createRenderPreset(presentation, "Final",
                               RenderSettings{1920, 1080, 256, 7, RenderEngine::Cycles});
            createRenderPreset(presentation, "Preview",
                               RenderSettings{640, 360, 16, 0, RenderEngine::Eevee});
            const auto encoded = encodePresentation(presentationTablesOf(presentation));
            publish("presentation", QJsonDocument(encoded).toJson(QJsonDocument::Compact),
                    "tour-and-presets");
            publish("presentation",
                    QJsonDocument(QJsonObject{{"cameraPaths", QJsonArray{}},
                                              {"nextCameraPathId", "1"},
                                              {"renderPresets", QJsonArray{}},
                                              {"nextRenderPresetId", "1"}})
                        .toJson(QJsonDocument::Compact),
                    "empty");
            auto oneKeyframe = encoded;
            auto paths = oneKeyframe["cameraPaths"].toArray();
            auto first = paths[0].toObject();
            first["keyframes"] = QJsonArray{first["keyframes"].toArray()[0]};
            paths[0] = first;
            oneKeyframe["cameraPaths"] = paths;
            publishRejected("presentation",
                            QJsonDocument(oneKeyframe).toJson(QJsonDocument::Compact),
                            "one-keyframe");
            auto zeroKeyframe = encoded;
            paths = zeroKeyframe["cameraPaths"].toArray();
            first = paths[0].toObject();
            auto keyframes = first["keyframes"].toArray();
            keyframes[0] = "0";
            first["keyframes"] = keyframes;
            paths[0] = first;
            zeroKeyframe["cameraPaths"] = paths;
            publishRejected("presentation",
                            QJsonDocument(zeroKeyframe).toJson(QJsonDocument::Compact),
                            "zero-keyframe");
            auto narrow = encoded;
            auto presets = narrow["renderPresets"].toArray();
            auto preset = presets[0].toObject();
            auto settings = preset["settings"].toObject();
            settings["width"] = 63;
            preset["settings"] = settings;
            presets[0] = preset;
            narrow["renderPresets"] = presets;
            publishRejected("presentation",
                            QJsonDocument(narrow).toJson(QJsonDocument::Compact), "narrow-preset");
        }
        // R100.a: edge strokes at the bounds, with each named dash, and the rejected bounds.
        {
            const auto stroke = [](double width, double opacity, const char *dash,
                                   const char *texture) {
                return QJsonObject{
                    {"width", width}, {"opacity", opacity}, {"dash", dash}, {"texture", texture}};
            };
            const auto bytes = [](const QJsonObject &object) {
                return QJsonDocument(object).toJson(QJsonDocument::Compact);
            };
            publish("edge-style", bytes(stroke(2, 1, "solid", "0")), "solid-default");
            publish("edge-style", bytes(stroke(8, .01, "dashDot", "42")), "bounds-and-texture");
            publish("edge-style", bytes(stroke(1, .5, "dashed", "1")), "dashed");
            publish("edge-style", bytes(stroke(3, .25, "dotted", "18446744073709551615")),
                    "dotted-max-id");
            publishRejected("edge-style", bytes(stroke(.5, 1, "solid", "0")), "width-below");
            publishRejected("edge-style", bytes(stroke(8.5, 1, "solid", "0")), "width-above");
            publishRejected("edge-style", bytes(stroke(2, 0, "solid", "0")), "opacity-zero");
            publishRejected("edge-style", bytes(stroke(2, 1.5, "solid", "0")), "opacity-above");
            publishRejected("edge-style", bytes(stroke(2, 1, "wavy", "0")), "unknown-dash");
            publishRejected("edge-style", bytes(stroke(2, 1, "solid", "007")), "noncanonical-id");
            publishRejected("edge-style", bytes(stroke(2, 1, "solid", "-1")), "negative-id");
            publishRejected("edge-style",
                            bytes(stroke(2, 1, "solid", "0")).replace("\"width\"", "\"wide\""),
                            "unknown-field");
        }
        // R100.b2: material texture sizes at the bounds, a square and a non-square tile, and the
        // rejected bounds, fields and types.
        // R107.a (ADR 0181 §2.1): geographic records at the bounds with each datum and vertical
        // label, and the rejected inputs (a datum refused by name, a bound, a mismatch).
        {
            const auto sized = [](QJsonValue width, QJsonValue height) {
                return QJsonObject{{"width", width}, {"height", height}};
            };
            const auto geographic = [](double latitude, double longitude, int zone,
                                       const char *datum, const char *vertical, const char *kind,
                                       const char *sha) {
                double convergence = 0;
                try {
                    convergence = utmGridConvergence({latitude, longitude}, zone);
                } catch (const std::exception &) {
                    // Rejected inputs keep a zero convergence, so only their named field is wrong.
                }
                return QJsonObject{
                    {"latitude", latitude},
                    {"longitude", longitude},
                    {"datum", datum},
                    {"utmZone", zone},
                    {"utmHemisphere", latitude < 0 ? "south" : "north"},
                    {"convergence", convergence},
                    {"verticalReference", vertical},
                    {"provenance",
                     QJsonObject{{"kind", kind}, {"source", "seed"}, {"sha256", sha}}}};
            };
            const auto bytes = [](const QJsonObject &object) {
                return QJsonDocument(object).toJson(QJsonDocument::Compact);
            };
            publish("material-texture-size", bytes(sized(minimumTextureSize, maximumTextureSize)),
                    "bounds");
            publish("material-texture-size", bytes(sized(.9, .9)), "brick");
            publish("material-texture-size", bytes(sized(.27, .28)), "wool-non-square");
            publishRejected("material-texture-size", bytes(sized(.009, 1)), "width-below");
            publishRejected("material-texture-size", bytes(sized(1, 100.5)), "height-above");
            publishRejected("material-texture-size", bytes(sized(1, "1")), "string-height");
            publishRejected("material-texture-size", bytes(QJsonObject{{"width", 1}}),
                            "missing-height");
            publishRejected("material-texture-size",
                            bytes(QJsonObject{{"width", 1}, {"height", 1}, {"depth", 1}}),
                            "unknown-field");
            const char *digest = "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef";
            publish("geographic-site",
                    bytes(geographic(51.50735, -0.12776, 30, "WGS84", "unknown", "typed", "")),
                    "london-typed");
            publish("geographic-site",
                    bytes(geographic(84, 3, 31, "NAD83", "EGM2008", "geocoded", "")),
                    "north-bound-approximation");
            publish("geographic-site", bytes(geographic(-80, 3, 31, "WGS84", "EGM96", "typed", "")),
                    "south-bound");
            publish(
                "geographic-site",
                bytes(geographic(-33.8688, 151.2093, 56, "WGS84", "NAVD88", "imported", digest)),
                "southern-imported");
            publishRejected("geographic-site",
                            bytes(geographic(51.5, -0.1, 30, "OSGB36", "unknown", "typed", "")),
                            "datum-osgb36");
            publishRejected("geographic-site",
                            bytes(geographic(51.5, -0.1, 30, "ETRS89", "unknown", "typed", "")),
                            "datum-etrs89");
            publishRejected("geographic-site",
                            bytes(geographic(84.5, 3, 31, "WGS84", "unknown", "typed", "")),
                            "latitude-above-band");
            publishRejected("geographic-site",
                            bytes(geographic(51.5, -0.1, 61, "WGS84", "unknown", "typed", "")),
                            "zone-61");
            publishRejected("geographic-site",
                            bytes(geographic(60, 5, 31, "WGS84", "unknown", "typed", "")),
                            "norway-exception");
            publishRejected("geographic-site",
                            bytes(geographic(51.5, -0.1, 30, "WGS84", "NGVD29", "typed", "")),
                            "vertical-unknown");
            publishRejected("geographic-site",
                            bytes(geographic(51.5, -0.1, 30, "WGS84", "unknown", "typed", digest)),
                            "typed-with-digest");
            publishRejected("geographic-site",
                            bytes(geographic(51.5, -0.1, 30, "WGS84", "unknown", "imported", "")),
                            "imported-without-digest");
            auto mismatch = geographic(51.5, -0.1, 30, "WGS84", "unknown", "typed", "");
            mismatch["utmHemisphere"] = "south";
            publishRejected("geographic-site", bytes(mismatch), "hemisphere-mismatch");
            auto drift = geographic(51.5, -0.1, 30, "WGS84", "unknown", "typed", "");
            drift["convergence"] = drift["convergence"].toDouble() + 1;
            publishRejected("geographic-site", bytes(drift), "convergence-drift");
            publishRejected("geographic-site",
                            bytes(geographic(51.5, -0.1, 30, "WGS84", "unknown", "typed", ""))
                                .replace("\"latitude\"", "\"lat\""),
                            "unknown-field");
        }
        // R093.a: parameter expressions, canonical text, every type and function.
        const char *const expressions[][2] = {
            {"table-width", "width - 60 mm"},
            {"table-shelves", "max(shelves, 1)"},
            {"table-visible", "shelves > 0"},
            {"choice", "if(style == \"oak\", 900 mm, 2 ft)"},
            {"precedence", "-ratio ^ 2 + 3 * (ratio - 1) / 2"},
            {"functions", "clamp(sqrt(floor_area), 400 mm, 2.4 m) + abs(mod(width, 0.3 m))"},
            {"angles", "atan2(width, 1 m) + asin(0.5) + 15 deg - tilt"},
            {"trig", "sin(tilt) ^ 2 + cos(tilt) ^ 2 + tan(0 rad)"},
            {"logic", "not open and (ratio <= 1.5 or shelves != 3)"},
            {"rounding", "round(width / 300 mm) + floor(ratio) + ceil(.5E1)"},
            {"units", "1 in + 1 ft + 1 cm + 1 mm + 1 m"},
            {"powers", "(width ^ -1) * 1 m + pi ^ 3 - 1e-07"},
        };
        for (const auto &[name, text] : expressions)
            publish("parameter-expression", QByteArray(text), QString::fromLatin1(name));
        // R100.c: each catalog fixture decodes, and a tampered one lists its bad entry as not
        // importable rather than failing the decode. A truncated or misfiled catalog is rejected.
        const auto catalogs = catalog_fixture::fixtures();
        for (auto it = catalogs.constBegin(); it != catalogs.constEnd(); ++it)
            publish("catalog", it.value(), it.key() + QStringLiteral(".sketchycat"));
        const auto basic = catalogs.value(QStringLiteral("basic"));
        publishRejected("catalog", basic.left(40), QStringLiteral("truncated.sketchycat"));
        auto misfiled = basic;
        misfiled[0] = char(0x7f);
        publishRejected("catalog", misfiled, QStringLiteral("bad-magic.sketchycat"));
        std::cout << QJsonDocument(QJsonObject{{"validSeeds", rows},
                                               {"rejectedSeeds", rejectedRows},
                                               {"formats", 14},
                                               {"inputLimitBytes", qint64(fuzz::inputLimit)},
                                               {"releaseAcceptance", false}})
                         .toJson(QJsonDocument::Compact)
                         .constData()
                  << '\n';
        return 0;
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
