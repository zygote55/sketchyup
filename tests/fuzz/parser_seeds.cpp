#include "../component_parameter_fixture.hpp"
#include "core/annotation_anchors.hpp"
#include "core/components.hpp"
#include "core/drawing_sheets.hpp"
#include "core/scenes.hpp"
#include "core/terrain.hpp"
#include "integrations/glb_export.hpp"
#include "io/dxf_export.hpp"
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
        std::cout << QJsonDocument(QJsonObject{{"validSeeds", rows},
                                               {"rejectedSeeds", rejectedRows},
                                               {"formats", 12},
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
