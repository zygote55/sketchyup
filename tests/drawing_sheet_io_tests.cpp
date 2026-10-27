// R090.b: schema-27 drawing-sheet persistence, migration and the downgrade window
// of ADR 0163 decision 2.
#include "core/annotation_anchors.hpp"
#include "core/drawing_sheets.hpp"
#include "core/scenes.hpp"
#include "io/document_io.hpp"
#include "io/drawing_sheets_io.hpp"
#include "io/native_format.hpp"
#include "io/recovery.hpp"
#include "sketchyup/version.hpp"
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QProcess>
#include <QTemporaryDir>
#include <QtEndian>
#include <functional>
#include <iostream>
#include <set>
#include <string>
using namespace sketchy;
namespace {
void check(bool value, const std::string &message) {
    if (!value)
        throw std::runtime_error(message);
}
template <class F> void rejects(F operation, const std::string &what) {
    try {
        operation();
    } catch (const std::exception &) {
        return;
    }
    throw std::runtime_error("Expected rejection: " + what);
}
QByteArray read(const QString &path) {
    QFile file(path);
    check(file.open(QIODevice::ReadOnly), "Read " + path.toStdString());
    return file.readAll();
}
void write(const QString &path, const QByteArray &bytes) {
    QFile file(path);
    check(file.open(QIODevice::WriteOnly | QIODevice::Truncate) &&
              file.write(bytes) == bytes.size(),
          "Write " + path.toStdString());
}
QString sha256(const QByteArray &bytes) {
    return QString::fromLatin1(QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex());
}
QJsonObject manifest(const QByteArray &container) {
    const auto length = qFromLittleEndian<quint32>(container.constData() + 12);
    return QJsonDocument::fromJson(container.mid(16, length)).object();
}
QByteArray documentChunk(const QByteArray &container) {
    const auto length = qFromLittleEndian<quint32>(container.constData() + 12);
    const auto bytes =
        manifest(container)["chunks"].toArray()[0].toObject()["bytes"].toString().toLongLong();
    return container.mid(16 + length, bytes);
}
QJsonObject documentTree(const QByteArray &container) {
    return QJsonDocument::fromJson(documentChunk(container)).object();
}
// Rebuilds a container without assets around a replacement manifest and document chunk.
QByteArray repack(const QByteArray &container, QJsonObject head, const QByteArray &document) {
    auto chunks = head["chunks"].toArray();
    auto chunk = chunks[0].toObject();
    chunk["bytes"] = QString::number(document.size());
    chunk["sha256"] = sha256(document);
    chunks[0] = chunk;
    head["chunks"] = chunks;
    const auto encoded = QJsonDocument(head).toJson(QJsonDocument::Compact);
    auto header = container.first(16);
    qToLittleEndian<quint32>(encoded.size(), header.data() + 12);
    return header + encoded + document;
}
QByteArray compact(const QJsonObject &object) {
    return QJsonDocument(object).toJson(QJsonDocument::Compact);
}
std::set<QString> keys(const QJsonObject &object) {
    const auto list = object.keys();
    return {list.begin(), list.end()};
}
std::set<QString> strings(const QJsonArray &array) {
    std::set<QString> result;
    for (const auto &value : array)
        result.insert(value.toString());
    return result;
}
const std::string fingerprintA =
    "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef";
SceneSnapshot orthographicScene(double yaw) {
    SceneSnapshot snapshot;
    snapshot.camera = SceneCamera{};
    snapshot.camera->yaw = yaw;
    snapshot.camera->orthographic = true;
    return snapshot;
}
DrawingViewRecord viewRecord(std::string name, Id sheet, Id scene) {
    DrawingViewRecord record;
    record.name = std::move(name);
    record.sheet = sheet;
    record.source = {DrawingViewSourceKind::Scene, scene};
    record.frame = {20, 20, 200, 150};
    record.offsetMm = {5, -5};
    record.scaleDenominator = 50;
    record.hiddenLines = true;
    return record;
}
DrawingFont font(std::string family, double sizePt) {
    DrawingFont result;
    result.family = std::move(family);
    result.sizePt = sizePt;
    return result;
}
// The retained drawing-sheets-v27 fixture. It exercises every stored field, both
// content modes, a null and a present acceptance stamp, nondefault order, retired
// sheet, view and item IDs, a view whose scene was deleted (a diagnosed broken link),
// a sheet number and nondefault margin, every item kind (R091.b) in a nondefault z
// order, and a dimension whose anchor body was deleted.
Document sheetFixture() {
    Document doc;
    const auto body = doc.addFace({{{0, 0, 0}, {2, 0, 0}, {2, 3, 0}, {0, 3, 0}}});
    const auto removed = doc.addFace({{{5, 0, 0}, {6, 0, 0}, {6, 1, 0}}}, "Removed");
    const auto plan = createScene(doc, "Plan", orthographicScene(-90));
    const auto front = createScene(doc, "Front", orthographicScene(0));
    const auto detail = createScene(doc, "Detail", orthographicScene(45));
    const auto cover = createDrawingSheet(doc, "Cover", {420, 297});
    const auto plans = createDrawingSheet(doc, "Plans", {297, 420.5}, "A-101", 12.5);
    // R091.g: a nondefault dimension format.
    updateDrawingSheetPage(doc, plans, "A-101", 12.5,
                           DrawingDimensionFormat{DrawingDimensionUnit::FeetInches, 2, true});
    const auto scratch = createDrawingSheet(doc, "Scratch", {210, 297});
    auto temporary = viewRecord("Temporary", scratch, plan);
    temporary.frame = {10, 10, 100, 100};
    createDrawingView(doc, temporary);
    eraseDrawingSheet(doc, scratch);
    reorderDrawingSheets(doc, {plans, cover});
    auto planView = viewRecord("Plan view", plans, plan);
    planView.frame = {10.25, 12.5, 250, 180.75};
    planView.offsetMm = {-3.125, 7.5};
    planView.scaleDenominator = 100;
    const auto planId = createDrawingView(doc, planView);
    auto frontView = viewRecord("Front elevation", plans, front);
    frontView.frame = {10, 200, 120, 90};
    frontView.content = DrawingViewContent::RasterAppearance;
    frontView.hiddenLines = false;
    frontView.offsetMm = {0, 0};
    frontView.scaleDenominator = 0.5;
    const auto frontId = createDrawingView(doc, frontView);
    auto detailView = viewRecord("Detail A", cover, detail);
    detailView.frame = {300, 180, 100, 100};
    detailView.scaleDenominator = 2.5;
    const auto detailId = createDrawingView(doc, detailView);
    acceptDrawingViews(doc, {{planId, {doc.revision(), fingerprintA}}});

    // Page items: one of each kind, with every enumeration value used at least once.
    const auto &face = *doc.bodies().at(body);
    const auto vertex = vertexAnchor(doc, body, face.surface.vertices.begin()->first);
    const auto edge = edgeAnchor(doc, body, face.topology.edges.begin()->first, .375);
    const auto &gone = *doc.bodies().at(removed);
    const auto goneVertex = vertexAnchor(doc, removed, gone.surface.vertices.begin()->first);
    const auto goneEdge = edgeAnchor(doc, removed, gone.topology.edges.rbegin()->first, .25);
    DrawingTextItem text;
    text.frame = {15, 15, 120.5, 30.25};
    text.text = "General notes:\n\t1. Dimensions in mm — verify on site";
    text.font = font("Liberation Serif", 12.5);
    text.align = DrawingTextAlign::Center;
    const auto textId = createDrawingItem(doc, plans, text);
    DrawingLineItem line;
    line.from = {12.5, 400};
    line.to = {284.5, 400};
    line.strokeMm = .25;
    const auto lineId = createDrawingItem(doc, plans, line);
    // A retired item ID: the allocator stays above it.
    const auto retired = createDrawingItem(doc, plans, line);
    eraseDrawingItem(doc, retired);
    DrawingRectangleItem rectangle;
    rectangle.frame = {12.5, 12.5, 272, 395.5};
    rectangle.strokeMm = 2;
    rectangle.dash = DrawingStrokeDash::Dashed;
    const auto rectangleId = createDrawingItem(doc, plans, rectangle);
    DrawingTitleBlockItem block;
    block.frame = {137, 360, 147.5, 47.5};
    block.font = font("DejaVu Sans", 8);
    block.fields = {"House \"Larch\"", "Ground floor plan", "2026-10-14", ""};
    const auto blockId = createDrawingItem(doc, plans, block);
    DrawingDimensionItem width;
    width.view = planId;
    width.anchors = {vertex, edge};
    width.orientation = DrawingDimensionOrientation::Horizontal;
    width.offsetMm = -7.5;
    width.prefix = "≈ ";
    width.font = font("DejaVu Sans Mono", 9);
    width.lastMm = {DrawingPointMm{60.125, 120}, DrawingPointMm{100, -3.5}};
    const auto widthId = createDrawingItem(doc, plans, width);
    DrawingLabelItem tag;
    tag.text = "W2";
    tag.textAt = {200, 250};
    tag.leader = false;
    tag.target.kind = DrawingLabelTargetKind::View;
    tag.target.view = frontId;
    tag.target.anchor = edge;
    tag.target.lastMm = {80.5, 260.25};
    const auto tagId = createDrawingItem(doc, plans, tag);
    DrawingLabelItem north;
    north.text = "North";
    north.textAt = {380, 30};
    north.target.pointMm = {400, 45.5};
    createDrawingItem(doc, cover, north);
    // Bound to the broken detail view, with anchors whose body is deleted below.
    DrawingDimensionItem lost;
    lost.view = detailId;
    lost.anchors = {goneVertex, goneEdge};
    lost.orientation = DrawingDimensionOrientation::Vertical;
    lost.offsetMm = 12;
    lost.lastMm = {DrawingPointMm{320, 200}, DrawingPointMm{320, 260}};
    createDrawingItem(doc, cover, lost);
    DrawingDimensionItem aligned = width;
    aligned.orientation = DrawingDimensionOrientation::Aligned;
    aligned.prefix.clear();
    aligned.anchors = {edge, vertex};
    aligned.offsetMm = 0;
    const auto alignedId = createDrawingItem(doc, plans, aligned);
    // A nondefault stacking order on the plans sheet.
    reorderDrawingItems(doc, plans,
                        {rectangleId, blockId, alignedId, lineId, widthId, textId, tagId});
    eraseScene(doc, detail);
    doc.erase(removed);
    return doc;
}
void sameSheets(const Document &a, const Document &b, const std::string &label) {
    auto equal = [](const auto &x, const auto &y) {
        if (x.size() != y.size())
            return false;
        for (const auto &[id, record] : x)
            if (!y.contains(id) || !(*y.at(id) == *record))
                return false;
        return true;
    };
    check(equal(a.drawingSheets(), b.drawingSheets()) &&
              equal(a.drawingViews(), b.drawingViews()) &&
              equal(a.drawingItems(), b.drawingItems()) &&
              a.nextDrawingSheetId() == b.nextDrawingSheetId() &&
              a.nextDrawingViewId() == b.nextDrawingViewId() &&
              a.nextDrawingItemId() == b.nextDrawingItemId(),
          label + ": drawing-sheet tables and allocators round-trip exactly");
}
// Root fields, required features, chunk encoding and allocator floors match the
// 1.0.0 writer exactly, so the 1.0.0 reader's checks see a file of its own kind.
void matchesRelease(const QByteArray &container, const QByteArray &release,
                    const std::string &label) {
    const auto head = manifest(container), releaseHead = manifest(release);
    check(head["requiredFeatures"] == releaseHead["requiredFeatures"],
          label + ": required features equal the 1.0.0 writer's");
    check(head["chunks"].toArray()[0].toObject()["encoding"] == "json-v26" &&
              documentTree(container)["version"] == nativeSheetFreeDocumentVersion,
          label + ": document chunk is json-v26");
    check(keys(head) == keys(releaseHead), label + ": manifest fields equal 1.0.0's");
    check(keys(head["allocatorFloors"].toObject()) ==
              keys(releaseHead["allocatorFloors"].toObject()),
          label + ": allocator floors equal 1.0.0's");
    check(keys(documentTree(container)) == keys(documentTree(release)),
          label + ": document root fields equal 1.0.0's");
}
int runPriorCli(const QString &cli, const QString &path, QByteArray &stderrText) {
    QProcess process;
    process.start(cli, {"--validate-native", path});
    check(process.waitForFinished(30000) && process.exitStatus() == QProcess::NormalExit,
          "Prior release CLI runs");
    stderrText = process.readAllStandardError();
    return process.exitCode();
}
// The newest document schema a prior release reads (its --format-capabilities).
int priorReads(const QString &cli) {
    QProcess process;
    process.start(cli, {"--format-capabilities"});
    check(process.waitForFinished(30000) && process.exitStatus() == QProcess::NormalExit &&
              process.exitCode() == 0,
          "The prior release describes its formats");
    int reads{};
    for (const auto &value : QJsonDocument::fromJson(process.readAllStandardOutput())
                                 .object()["readDocumentVersions"]
                                 .toArray())
        reads = std::max(reads, value.toInt());
    check(reads >= 26, "The prior release reads schema 26 or later");
    return reads;
}
} // namespace
int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    try {
        const auto args = app.arguments();
        if (args.size() == 3 && args[1] == "--write-fixture") {
            auto doc = sheetFixture();
            saveDocument(doc, args[2]);
            return 0;
        }
        check(args.size() == 1, "Usage: drawing_sheet_io_tests [--write-fixture PATH]");
        QTemporaryDir files;
        check(files.isValid(), "Temporary directory");
        const QDir fixtures(QString(SOURCE_DIR) + "/tests/fixtures");
        const auto releasePath = fixtures.filePath("release-1.0.0-v26.sketchyup");
        const auto release = read(releasePath);

        // The policy constants: sheets need 27, and sheet-free content stays 26. Schema 28
        // (R093.c) is only for component parameters, which these documents do not have.
        static_assert(nativeParameterFreeDocumentVersion == 27 &&
                      nativeSheetFreeDocumentVersion == 26);
        check(nativeDocumentVersionFor(Document{}) == 26, "Empty document needs schema 26");

        // Every field round-trips through raw JSON and the container.
        const auto doc = sheetFixture();
        check(doc.drawingSheets().size() == 2 && doc.drawingViews().size() == 3 &&
                  doc.nextDrawingSheetId() == 4 && doc.nextDrawingViewId() == 5,
              "Fixture has retired sheet and view IDs");
        check(drawingViewsWithMissingSource(doc) == std::set<Id>{4},
              "Fixture keeps a view whose scene was deleted");
        check(nativeDocumentVersionFor(doc) == 27, "Sheets need schema 27");
        const auto container = encodeContainer(doc);
        {
            const auto head = manifest(container);
            const auto features = head["requiredFeatures"].toArray();
            check(features.last() == "drawing-sheets-v1" &&
                      strings(features).size() ==
                          strings(manifest(release)["requiredFeatures"].toArray()).size() + 1,
                  "Schema 27 appends drawing-sheets-v1 to the 1.0.0 features");
            check(head["chunks"].toArray()[0].toObject()["encoding"] == "json-v27" &&
                      documentTree(container)["version"] == 27,
                  "Schema 27 chunk encoding and version");
            const auto floors = head["allocatorFloors"].toObject();
            check(floors["nextDrawingSheetId"] == "4" && floors["nextDrawingViewId"] == "5",
                  "Container records drawing-sheet allocator floors");
            const auto tree = documentTree(container);
            check(tree["nextDrawingSheetId"] == "4" && tree["nextDrawingViewId"] == "5" &&
                      tree["drawingSheets"].toArray().size() == 2 &&
                      tree["drawingViews"].toArray().size() == 3,
                  "Document root stores both tables and allocators");
            const auto plan = tree["drawingViews"].toArray()[0].toObject();
            check(plan["id"] == "2" && plan["sheet"] == "2" &&
                      plan["source"] == QJsonObject{{"kind", "scene"}, {"scene", "1"}} &&
                      plan["frame"] == QJsonObject{{"xMm", 10.25},
                                                   {"yMm", 12.5},
                                                   {"widthMm", 250},
                                                   {"heightMm", 180.75}} &&
                      plan["offsetMm"] == QJsonArray{-3.125, 7.5} &&
                      plan["scaleDenominator"] == 100 && plan["content"] == "technicalLines" &&
                      plan["hiddenLines"] == true &&
                      plan["accepted"].toObject()["fingerprint"] ==
                          QString::fromStdString(fingerprintA),
                  "View record stores every field in its documented shape");
            const auto front = tree["drawingViews"].toArray()[1].toObject();
            check(front["content"] == "rasterAppearance" && front["accepted"].isNull() &&
                      front["hiddenLines"] == false,
                  "Raster view without acceptance");
            const auto sheet = tree["drawingSheets"].toArray()[1].toObject();
            check(sheet == QJsonObject{{"id", "2"},
                                       {"name", "Plans"},
                                       {"position", 0},
                                       {"paper", QJsonObject{{"widthMm", 297}, {"heightMm", 420.5}}},
                                       {"number", "A-101"},
                                       {"marginMm", 12.5},
                                       {"dimensionFormat", QJsonObject{{"unit", "ftIn"},
                                                                       {"decimals", 2},
                                                                       {"showUnit", true}}}},
                  "Sheet record stores id, name, position, paper, number, margin and "
                  "dimension format");
            check(tree["drawingSheets"].toArray()[0].toObject()["number"] == "" &&
                      tree["drawingSheets"].toArray()[0].toObject()["marginMm"] == 10 &&
                      tree["drawingSheets"].toArray()[0].toObject()["dimensionFormat"] ==
                          QJsonObject{{"unit", "mm"}, {"decimals", 0}, {"showUnit", false}},
                  "An empty sheet number, the default margin and the default dimension format "
                  "are stored explicitly");

            // R091.b: the item table, its allocator and its floor.
            check(floors["nextDrawingItemId"] == "11" && tree["nextDrawingItemId"] == "11",
                  "Container and root record the drawing-item allocator");
            const auto items = tree["drawingItems"].toArray();
            check(items.size() == 9, "Document root stores the item table");
            QStringList ids, kinds;
            for (const auto &entry : items) {
                const auto item = entry.toObject();
                check(keys(item) == std::set<QString>{"id", "sheet", "z", "kind", "data"},
                      "Item record stores id, sheet, z, kind and data");
                ids.append(item["id"].toString());
                kinds.append(item["kind"].toString());
            }
            check(ids == QStringList{"1", "2", "4", "5", "6", "7", "8", "9", "10"},
                  "Items are stored once each in ascending ID order");
            check(kinds == QStringList{"text", "line", "rectangle", "titleBlock", "dimension",
                                       "label", "label", "dimension", "dimension"},
                  "Item kinds use their documented codes");
            check(items[0].toObject() ==
                      QJsonObject{{"id", "1"},
                                  {"sheet", "2"},
                                  {"z", 5},
                                  {"kind", "text"},
                                  {"data",
                                   QJsonObject{{"frame", QJsonObject{{"xMm", 15},
                                                                     {"yMm", 15},
                                                                     {"widthMm", 120.5},
                                                                     {"heightMm", 30.25}}},
                                               {"text", QString::fromUtf8(
                                                            "General notes:\n\t1. Dimensions in "
                                                            "mm — verify on site")},
                                               {"font", QJsonObject{{"family", "Liberation Serif"},
                                                                    {"sizePt", 12.5}}},
                                               {"align", "center"}}}},
                  "Text item stores every field in its documented shape");
            check(items[1].toObject()["data"] ==
                      QJsonObject{{"from", QJsonArray{12.5, 400}},
                                  {"to", QJsonArray{284.5, 400}},
                                  {"strokeMm", .25},
                                  {"dash", "solid"}},
                  "Line item shape");
            check(items[2].toObject()["data"] ==
                      QJsonObject{{"frame", QJsonObject{{"xMm", 12.5},
                                                        {"yMm", 12.5},
                                                        {"widthMm", 272},
                                                        {"heightMm", 395.5}}},
                                  {"strokeMm", 2},
                                  {"dash", "dashed"}},
                  "Rectangle item shape");
            check(items[3].toObject()["data"].toObject()["fields"] ==
                      QJsonObject{{"project", "House \"Larch\""},
                                  {"drawingTitle", "Ground floor plan"},
                                  {"date", "2026-10-14"},
                                  {"revision", ""}} &&
                      keys(items[3].toObject()["data"].toObject()) ==
                          std::set<QString>{"frame", "font", "fields"},
                  "Title block stores its four fields, not the derived ones");
            const auto width = items[4].toObject()["data"].toObject();
            check(keys(width) == std::set<QString>{"view", "anchors", "orientation", "offsetMm",
                                                   "prefix", "font", "lastMm"} &&
                      width["view"] == "2" && width["orientation"] == "horizontal" &&
                      width["offsetMm"] == -7.5 &&
                      width["prefix"] == QString::fromUtf8("≈ ") &&
                      width["lastMm"] ==
                          QJsonArray{QJsonArray{60.125, 120}, QJsonArray{100, -3.5}} &&
                      width["anchors"].toArray().size() == 2 &&
                      width["anchors"].toArray()[0].toObject()["kind"] == "vertex" &&
                      width["anchors"].toArray()[1].toObject()["kind"] == "edge" &&
                      width["anchors"].toArray()[1].toObject()["parameter"] == .375,
                  "Dimension item stores its view, anchors and placement");
            const auto tagData = items[5].toObject()["data"].toObject();
            check(keys(tagData) ==
                          std::set<QString>{"text", "font", "textAt", "leader", "target"} &&
                      tagData["leader"] == false &&
                      tagData["textAt"] == QJsonObject{{"xMm", 200}, {"yMm", 250}} &&
                      keys(tagData["target"].toObject()) ==
                          std::set<QString>{"kind", "view", "anchor", "lastMm"} &&
                      tagData["target"].toObject()["view"] == "3" &&
                      tagData["target"].toObject().value("lastMm") == QJsonArray{80.5, 260.25},
                  "View-target label shape");
            check(items[6].toObject()["data"].toObject()["target"] ==
                          QJsonObject{{"kind", "paper"}, {"xMm", 400}, {"yMm", 45.5}} &&
                      items[6].toObject()["data"].toObject()["leader"] == true,
                  "Paper-target label shape");
            check(items[7].toObject()["data"].toObject()["orientation"] == "vertical" &&
                      items[8].toObject()["data"].toObject()["orientation"] == "aligned",
                  "Every dimension orientation is stored");
            QStringList zs;
            for (const auto &entry : items)
                zs.append(QString::number(entry.toObject()["z"].toInt()));
            check(zs == QStringList{"5", "3", "0", "1", "4", "6", "0", "1", "2"},
                  "Items store their per-sheet z order");
        }
        const auto reopened = decodeContainer(container);
        sameSheets(doc, reopened, "container");
        check(encodeContainer(reopened) == container, "Container re-encodes byte-identically");
        sameSheets(doc, decodeDocument(encodeDocument(doc)), "raw document");
        check(drawingViewsWithMissingSource(reopened) == std::set<Id>{4} &&
                  reopened.drawingViews().at(4)->source.scene == 3 && !reopened.scenes().contains(3),
              "A broken scene link survives the round trip");
        {
            // A dimension whose anchor body was deleted is kept as stored; it is broken
            // only when evaluated (ADR 0164 §3).
            const auto &lost = std::get<DrawingDimensionItem>(reopened.drawingItems().at(9)->data);
            check(!reopened.bodies().contains(lost.anchors[0].body) &&
                      !reopened.bodies().contains(lost.anchors[1].body) &&
                      lost.anchors[0].state == AnchorState::Resolved && lost.view == 4,
                  "A dimension whose anchor target is missing survives the round trip");
            check(reopened.drawingItems().at(9)->sheet == 1 &&
                      drawingItemsOnSheet(reopened, 2) ==
                          std::vector<Id>{4, 5, 10, 2, 6, 1, 7},
                  "Sheets and z order survive the round trip");
            // Every kind round-trips through a second, independent document.
            std::set<DrawingItemKind> seen;
            for (const auto &[id, item] : reopened.drawingItems())
                seen.insert(item->kind());
            check(seen.size() == 6, "The fixture covers every item kind");
        }
        {
            const auto path = files.filePath("created.sketchyup");
            const auto created = createNativeFile(doc, path);
            check(created["documentVersion"] == 27 && !created["migrationNeeded"].toBool(),
                  "createNativeFile verifies schema 27");
            const auto inspected = inspectNativeFile(path);
            check(inspected["documentVersion"] == 27 && inspected["valid"] == true,
                  "Inspection reports schema 27");
        }

        // Unknown, missing and noncanonical fields, and allocator disagreement, reject.
        {
            const auto tree = QJsonDocument::fromJson(encodeDocument(doc)).object();
            const auto head = manifest(container);
            auto rawRejects = [&](const std::function<void(QJsonObject &)> &mutate,
                                  const std::string &what) {
                auto changed = tree;
                mutate(changed);
                rejects([&] { (void)decodeDocument(compact(changed)); }, "raw " + what);
                auto packed = documentTree(container);
                mutate(packed);
                rejects([&] { (void)decodeContainer(repack(container, head, compact(packed))); },
                        "container " + what);
            };
            auto onSheet = [&](int index, const std::function<void(QJsonObject &)> &mutate) {
                return [=](QJsonObject &root) {
                    auto sheets = root["drawingSheets"].toArray();
                    auto sheet = sheets[index].toObject();
                    mutate(sheet);
                    sheets[index] = sheet;
                    root["drawingSheets"] = sheets;
                };
            };
            auto onView = [&](int index, const std::function<void(QJsonObject &)> &mutate) {
                return [=](QJsonObject &root) {
                    auto views = root["drawingViews"].toArray();
                    auto view = views[index].toObject();
                    mutate(view);
                    views[index] = view;
                    root["drawingViews"] = views;
                };
            };
            for (const auto &field :
                 {"drawingSheets", "nextDrawingSheetId", "drawingViews", "nextDrawingViewId"})
                rawRejects([&](QJsonObject &root) { root.remove(field); },
                           std::string("missing root ") + field);
            rawRejects([](QJsonObject &root) { root["drawingPages"] = QJsonArray{}; },
                       "unknown root field");
            for (const auto &field : {"id", "name", "position", "paper"})
                rawRejects(onSheet(0, [&](QJsonObject &sheet) { sheet.remove(field); }),
                           std::string("missing sheet ") + field);
            for (const auto &field :
                 {"id", "name", "sheet", "source", "frame", "offsetMm", "scaleDenominator",
                  "content", "hiddenLines", "accepted"})
                rawRejects(onView(0, [&](QJsonObject &view) { view.remove(field); }),
                           std::string("missing view ") + field);
            rawRejects(onSheet(0, [](QJsonObject &sheet) { sheet["titleBlock"] = true; }),
                       "unknown sheet field");
            rawRejects(onView(0, [](QJsonObject &view) { view["camera"] = QJsonObject{}; }),
                       "unknown view field");
            rawRejects(onSheet(0,
                               [](QJsonObject &sheet) {
                                   auto paper = sheet["paper"].toObject();
                                   paper["name"] = "A3";
                                   sheet["paper"] = paper;
                               }),
                       "unknown paper field");
            rawRejects(onSheet(0,
                               [](QJsonObject &sheet) {
                                   auto paper = sheet["paper"].toObject();
                                   paper.remove("heightMm");
                                   sheet["paper"] = paper;
                               }),
                       "missing paper height");
            rawRejects(onView(0,
                              [](QJsonObject &view) {
                                  auto frame = view["frame"].toObject();
                                  frame["rotation"] = 0;
                                  view["frame"] = frame;
                              }),
                       "unknown frame field");
            rawRejects(onView(0,
                              [](QJsonObject &view) {
                                  auto frame = view["frame"].toObject();
                                  frame.remove("yMm");
                                  view["frame"] = frame;
                              }),
                       "missing frame field");
            rawRejects(onView(0,
                              [](QJsonObject &view) {
                                  view["source"] = QJsonObject{
                                      {"kind", "scene"}, {"scene", "1"}, {"model", "x"}};
                              }),
                       "unknown source field");
            rawRejects(onView(0,
                              [](QJsonObject &view) {
                                  view["source"] = QJsonObject{{"kind", "external"},
                                                               {"scene", "1"}};
                              }),
                       "unknown source kind");
            rawRejects(onView(0,
                              [](QJsonObject &view) {
                                  auto accepted = view["accepted"].toObject();
                                  accepted["by"] = "me";
                                  view["accepted"] = accepted;
                              }),
                       "unknown acceptance field");
            rawRejects(onView(0,
                              [](QJsonObject &view) {
                                  auto accepted = view["accepted"].toObject();
                                  accepted.remove("revision");
                                  view["accepted"] = accepted;
                              }),
                       "missing acceptance revision");
            // Noncanonical identities, numbers and enumerations.
            for (const auto &bad : {QJsonValue("02"), QJsonValue("0"), QJsonValue(2),
                                    QJsonValue("+2"), QJsonValue(" 2")}) {
                rawRejects(onSheet(1, [&](QJsonObject &sheet) { sheet["id"] = bad; }),
                           "noncanonical sheet id");
                rawRejects(onView(0, [&](QJsonObject &view) { view["id"] = bad; }),
                           "noncanonical view id");
                rawRejects(onView(0, [&](QJsonObject &view) { view["sheet"] = bad; }),
                           "noncanonical view sheet");
                rawRejects(onView(0,
                                  [&](QJsonObject &view) {
                                      view["source"] =
                                          QJsonObject{{"kind", "scene"}, {"scene", bad}};
                                  }),
                           "noncanonical scene link");
                rawRejects([&](QJsonObject &root) { root["nextDrawingSheetId"] = bad; },
                           "noncanonical sheet allocator");
            }
            rawRejects(onView(0,
                              [](QJsonObject &view) {
                                  auto accepted = view["accepted"].toObject();
                                  accepted["revision"] = "007";
                                  view["accepted"] = accepted;
                              }),
                       "noncanonical acceptance revision");
            rawRejects(onView(0,
                              [](QJsonObject &view) {
                                  auto accepted = view["accepted"].toObject();
                                  accepted["fingerprint"] =
                                      QString::fromStdString(fingerprintA).toUpper();
                                  view["accepted"] = accepted;
                              }),
                       "uppercase fingerprint");
            rawRejects(onView(0, [](QJsonObject &view) { view["accepted"] = false; }),
                       "non-object acceptance");
            rawRejects(onView(0, [](QJsonObject &view) { view["content"] = "TechnicalLines"; }),
                       "noncanonical content mode");
            rawRejects(onView(0, [](QJsonObject &view) { view["hiddenLines"] = 1; }),
                       "numeric hidden-line flag");
            rawRejects(onView(0, [](QJsonObject &view) { view["scaleDenominator"] = "100"; }),
                       "string scale");
            rawRejects(onView(0, [](QJsonObject &view) { view["scaleDenominator"] = 0; }),
                       "zero scale");
            rawRejects(onView(0, [](QJsonObject &view) { view["offsetMm"] = QJsonArray{1}; }),
                       "short offset");
            rawRejects(onView(0, [](QJsonObject &view) { view["offsetMm"] = QJsonArray{1, 2, 3}; }),
                       "long offset");
            rawRejects(onSheet(0, [](QJsonObject &sheet) { sheet["position"] = 0.5; }),
                       "fractional position");
            rawRejects(onSheet(0, [](QJsonObject &sheet) { sheet["position"] = "1"; }),
                       "string position");
            rawRejects(onSheet(0, [](QJsonObject &sheet) { sheet["position"] = 0; }),
                       "repeated position");
            rawRejects(onSheet(0, [](QJsonObject &sheet) { sheet["name"] = "Plans"; }),
                       "duplicate sheet name");
            rawRejects(onSheet(0, [](QJsonObject &sheet) { sheet["name"] = " Cover"; }),
                       "padded sheet name");
            rawRejects(onSheet(0,
                               [](QJsonObject &sheet) {
                                   sheet["paper"] =
                                       QJsonObject{{"widthMm", 5}, {"heightMm", 297}};
                               }),
                       "paper below 10 mm");
            rawRejects(onView(0,
                              [](QJsonObject &view) {
                                  auto frame = view["frame"].toObject();
                                  frame["xMm"] = 100;
                                  view["frame"] = frame;
                              }),
                       "frame outside paper");
            rawRejects(onView(0, [](QJsonObject &view) { view["sheet"] = "3"; }),
                       "view on a retired sheet");
            rawRejects(
                [](QJsonObject &root) {
                    auto sheets = root["drawingSheets"].toArray();
                    const auto first = sheets[0];
                    sheets[0] = sheets[1];
                    sheets[1] = first;
                    root["drawingSheets"] = sheets;
                },
                "sheets out of ID order");
            rawRejects(
                [](QJsonObject &root) {
                    auto views = root["drawingViews"].toArray();
                    views.append(views[0]);
                    root["drawingViews"] = views;
                },
                "repeated view");
            // R091.b: the sheet number and margin are required fields; R091.g: and the
            // dimension format, every field of it.
            for (const auto &field : {"number", "marginMm", "dimensionFormat"})
                rawRejects(onSheet(1, [&](QJsonObject &sheet) { sheet.remove(field); }),
                           std::string("missing sheet ") + field);
            rawRejects(onSheet(1, [](QJsonObject &sheet) { sheet["number"] = 101; }),
                       "numeric sheet number");
            rawRejects(onSheet(1, [](QJsonObject &sheet) { sheet["number"] = QJsonValue(); }),
                       "null sheet number");
            rawRejects(onSheet(0, [](QJsonObject &sheet) { sheet["number"] = "A-101"; }),
                       "duplicate sheet number");
            rawRejects(onSheet(1, [](QJsonObject &sheet) { sheet["number"] = "A\n101"; }),
                       "control character in sheet number");
            rawRejects(onSheet(1,
                               [](QJsonObject &sheet) {
                                   sheet["number"] = QString(33, QChar('A'));
                               }),
                       "sheet number over 32 bytes");
            rawRejects(onSheet(1, [](QJsonObject &sheet) { sheet["marginMm"] = "10"; }),
                       "string margin");
            rawRejects(onSheet(1, [](QJsonObject &sheet) { sheet["marginMm"] = -1; }),
                       "negative margin");
            rawRejects(onSheet(1, [](QJsonObject &sheet) { sheet["marginMm"] = 50.5; }),
                       "margin over 50 mm");
            auto onFormat = [&](const std::function<void(QJsonObject &)> &mutate) {
                return onSheet(1, [=](QJsonObject &sheet) {
                    auto format = sheet["dimensionFormat"].toObject();
                    mutate(format);
                    sheet["dimensionFormat"] = format;
                });
            };
            for (const auto &field : {"unit", "decimals", "showUnit"})
                rawRejects(onFormat([&](QJsonObject &format) { format.remove(field); }),
                           std::string("missing dimension format ") + field);
            rawRejects(onFormat([](QJsonObject &format) { format["extra"] = 1; }),
                       "unknown dimension format field");
            rawRejects(onFormat([](QJsonObject &format) { format["unit"] = "in"; }),
                       "unknown dimension unit");
            rawRejects(onFormat([](QJsonObject &format) { format["decimals"] = 5; }),
                       "dimension decimals over 4");
            rawRejects(onFormat([](QJsonObject &format) { format["decimals"] = 1.5; }),
                       "fractional dimension decimals");
            rawRejects(onFormat([](QJsonObject &format) { format["showUnit"] = "yes"; }),
                       "text showUnit");
            rawRejects(onSheet(1, [](QJsonObject &sheet) { sheet["dimensionFormat"] = "mm"; }),
                       "dimension format not an object");

            // R091.b: the item table. Every field at every level is required and unknown
            // fields are rejected.
            auto onItem = [&](int index, const std::function<void(QJsonObject &)> &mutate) {
                return [=](QJsonObject &root) {
                    auto items = root["drawingItems"].toArray();
                    auto item = items[index].toObject();
                    mutate(item);
                    items[index] = item;
                    root["drawingItems"] = items;
                };
            };
            // Applies mutate to the object at a path of keys and array indices in an item.
            auto onItemPath = [&](int index, const QStringList &path,
                                  const std::function<void(QJsonObject &)> &mutate) {
                return onItem(index, [=](QJsonObject &item) {
                    std::function<QJsonValue(QJsonValue, int)> descend =
                        [&](QJsonValue value, int depth) -> QJsonValue {
                        if (depth == path.size()) {
                            auto object = value.toObject();
                            mutate(object);
                            return object;
                        }
                        bool numeric{};
                        const auto position = path[depth].toInt(&numeric);
                        if (numeric) {
                            auto array = value.toArray();
                            array[position] = descend(array[position], depth + 1);
                            return array;
                        }
                        auto object = value.toObject();
                        object[path[depth]] = descend(object[path[depth]], depth + 1);
                        return object;
                    };
                    item = descend(item, 0).toObject();
                });
            };
            for (const auto &field : {"drawingItems", "nextDrawingItemId"})
                rawRejects([&](QJsonObject &root) { root.remove(field); },
                           std::string("missing root ") + field);
            rawRejects([](QJsonObject &root) { root["drawingItems"] = QJsonObject{}; },
                       "non-array item table");
            // Every object in every item, by path: each field missing, and one unknown.
            const std::vector<std::pair<int, QStringList>> paths{
                {0, {}},
                {0, {"data"}},
                {0, {"data", "frame"}},
                {0, {"data", "font"}},
                {1, {"data"}},
                {2, {"data"}},
                {2, {"data", "frame"}},
                {3, {"data"}},
                {3, {"data", "fields"}},
                {3, {"data", "font"}},
                {4, {"data"}},
                {4, {"data", "font"}},
                {4, {"data", "anchors", "0"}},
                {4, {"data", "anchors", "1"}},
                {5, {"data"}},
                {5, {"data", "textAt"}},
                {5, {"data", "target"}},
                {5, {"data", "target", "anchor"}},
                {6, {"data", "target"}},
            };
            int fieldCases{};
            for (const auto &[index, path] : paths) {
                auto probe = documentTree(container);
                QJsonValue node = probe["drawingItems"].toArray()[index];
                for (const auto &step : path) {
                    bool numeric{};
                    const auto position = step.toInt(&numeric);
                    node = numeric ? node.toArray()[position] : node.toObject()[step];
                }
                check(node.isObject(), "Item path names an object");
                const auto label = QString("item %1 /%2").arg(index).arg(path.join('/'));
                for (const auto &field : node.toObject().keys()) {
                    rawRejects(onItemPath(index, path,
                                          [&](QJsonObject &object) { object.remove(field); }),
                               "missing field " + field.toStdString() + " at " +
                                   label.toStdString());
                    ++fieldCases;
                }
                rawRejects(onItemPath(index, path,
                                      [](QJsonObject &object) { object["extra"] = true; }),
                           "unknown field at " + label.toStdString());
            }
            check(fieldCases >= 60, "Every stored item field has a missing-field case");
            // Kinds, enumerations and value shapes.
            rawRejects(onItem(0, [](QJsonObject &item) { item["kind"] = "Text"; }),
                       "noncanonical item kind");
            rawRejects(onItem(0, [](QJsonObject &item) { item["kind"] = "image"; }),
                       "unknown item kind");
            rawRejects(onItem(1, [](QJsonObject &item) { item["kind"] = "rectangle"; }),
                       "kind that does not match its data");
            rawRejects(onItemPath(0, {"data"}, [](QJsonObject &d) { d["align"] = "justify"; }),
                       "unknown alignment");
            rawRejects(onItemPath(1, {"data"}, [](QJsonObject &d) { d["dash"] = "dotted"; }),
                       "unknown dash");
            rawRejects(onItemPath(4, {"data"},
                                  [](QJsonObject &d) { d["orientation"] = "Horizontal"; }),
                       "noncanonical orientation");
            rawRejects(onItemPath(5, {"data", "target"},
                                  [](QJsonObject &t) { t["kind"] = "model"; }),
                       "unknown label target kind");
            rawRejects(onItemPath(6, {"data", "target"},
                                  [](QJsonObject &t) {
                                      t["kind"] = "view";
                                  }),
                       "paper target fields under a view kind");
            rawRejects(onItemPath(5, {"data"}, [](QJsonObject &d) { d["leader"] = 0; }),
                       "numeric leader flag");
            rawRejects(onItemPath(1, {"data"}, [](QJsonObject &d) { d["from"] = QJsonArray{1}; }),
                       "short point");
            rawRejects(onItemPath(1, {"data"},
                                  [](QJsonObject &d) { d["to"] = QJsonArray{1, 2, 3}; }),
                       "long point");
            rawRejects(onItemPath(4, {"data"},
                                  [](QJsonObject &d) {
                                      auto anchors = d["anchors"].toArray();
                                      anchors.removeLast();
                                      d["anchors"] = anchors;
                                  }),
                       "one dimension anchor");
            rawRejects(onItemPath(4, {"data"},
                                  [](QJsonObject &d) {
                                      auto last = d["lastMm"].toArray();
                                      last.append(QJsonArray{0, 0});
                                      d["lastMm"] = last;
                                  }),
                       "three dimension positions");
            rawRejects(onItemPath(4, {"data", "anchors", "0"},
                                  [](QJsonObject &a) { a["kind"] = "face"; }),
                       "face anchor on a paper dimension");
            rawRejects(onItemPath(4, {"data", "anchors", "0"},
                                  [](QJsonObject &a) { a["state"] = "missing"; }),
                       "stored broken anchor state");
            rawRejects(onItemPath(0, {"data"}, [](QJsonObject &d) { d["text"] = ""; }),
                       "empty text");
            rawRejects(onItemPath(0, {"data"}, [](QJsonObject &d) { d["text"] = 7; }),
                       "numeric text");
            rawRejects(onItemPath(0, {"data", "font"},
                                  [](QJsonObject &f) { f["family"] = "/usr/share/fonts/x.ttf"; }),
                       "font path");
            rawRejects(onItemPath(0, {"data", "font"}, [](QJsonObject &f) { f["sizePt"] = 3; }),
                       "font below 4 pt");
            rawRejects(onItemPath(2, {"data"}, [](QJsonObject &d) { d["strokeMm"] = 2.5; }),
                       "stroke over 2 mm");
            rawRejects(onItemPath(2, {"data", "frame"}, [](QJsonObject &f) { f["xMm"] = 30; }),
                       "frame outside paper");
            rawRejects(onItemPath(3, {"data", "fields"},
                                  [](QJsonObject &f) { f["revision"] = QString(257, QChar('B')); }),
                       "title-block field over 256 bytes");
            rawRejects(onItemPath(3, {"data", "fields"},
                                  [](QJsonObject &f) { f["sheetNumber"] = "A-101"; }),
                       "stored derived title-block field");
            // Identities, z order and canonical ID order.
            for (const auto &bad : {QJsonValue("01"), QJsonValue("0"), QJsonValue(1),
                                    QJsonValue("+1"), QJsonValue(" 1")}) {
                rawRejects(onItem(0, [&](QJsonObject &item) { item["id"] = bad; }),
                           "noncanonical item id");
                rawRejects(onItem(0, [&](QJsonObject &item) { item["sheet"] = bad; }),
                           "noncanonical item sheet");
                rawRejects(onItemPath(4, {"data"}, [&](QJsonObject &d) { d["view"] = bad; }),
                           "noncanonical dimension view");
                rawRejects([&](QJsonObject &root) { root["nextDrawingItemId"] = bad; },
                           "noncanonical item allocator");
            }
            rawRejects(onItem(0, [](QJsonObject &item) { item["z"] = 1.5; }), "fractional z");
            rawRejects(onItem(0, [](QJsonObject &item) { item["z"] = "5"; }), "string z");
            rawRejects(onItem(0, [](QJsonObject &item) { item["z"] = -1; }), "negative z");
            rawRejects(onItem(0, [](QJsonObject &item) { item["z"] = 3; }), "repeated z");
            rawRejects(onItem(0, [](QJsonObject &item) { item["z"] = 7; }), "z gap");
            rawRejects(
                [](QJsonObject &root) {
                    auto items = root["drawingItems"].toArray();
                    const auto first = items[0];
                    items[0] = items[1];
                    items[1] = first;
                    root["drawingItems"] = items;
                },
                "items out of ID order");
            rawRejects(
                [](QJsonObject &root) {
                    auto items = root["drawingItems"].toArray();
                    items.append(items[8]);
                    root["drawingItems"] = items;
                },
                "repeated item");
            rawRejects(onItem(6, [](QJsonObject &item) { item["sheet"] = "3"; }),
                       "item on a retired sheet");
            rawRejects(onItem(6, [](QJsonObject &item) { item["sheet"] = "2"; }),
                       "item moved without its z order");
            rawRejects(onItemPath(4, {"data"}, [](QJsonObject &d) { d["view"] = "4"; }),
                       "dimension bound to a view on another sheet");
            rawRejects(onItemPath(4, {"data"}, [](QJsonObject &d) { d["view"] = "1"; }),
                       "dimension bound to a retired view");
            rawRejects(onItemPath(5, {"data", "target"},
                                  [](QJsonObject &t) { t["view"] = "9"; }),
                       "label bound to a missing view");
            rawRejects([](QJsonObject &root) { root["nextDrawingItemId"] = "10"; },
                       "item allocator at a live ID");
            {
                // Removing every item leaves a valid table; the allocator is kept.
                auto empty = documentTree(container);
                empty["drawingItems"] = QJsonArray{};
                const auto decoded = decodeDocument(compact(empty));
                check(decoded.drawingItems().empty() && decoded.nextDrawingItemId() == 11,
                      "An empty item table with a raised allocator decodes");
            }
            // Allocator disagreement: below a live ID, or not matching the manifest floor.
            rawRejects([](QJsonObject &root) { root["nextDrawingSheetId"] = "2"; },
                       "sheet allocator at a live ID");
            rawRejects([](QJsonObject &root) { root["nextDrawingViewId"] = "4"; },
                       "view allocator at a live ID");
            for (const auto &floor :
                 {"nextDrawingSheetId", "nextDrawingViewId", "nextDrawingItemId"}) {
                auto changed = head;
                auto floors = changed["allocatorFloors"].toObject();
                floors[floor] = "9";
                changed["allocatorFloors"] = floors;
                rejects([&] { (void)decodeContainer(repack(container, changed,
                                                           documentChunk(container))); },
                        std::string("manifest floor disagrees: ") + floor);
                floors.remove(floor);
                changed["allocatorFloors"] = floors;
                rejects([&] { (void)decodeContainer(repack(container, changed,
                                                           documentChunk(container))); },
                        std::string("manifest floor missing: ") + floor);
            }
            // Raising the root allocators consistently in both places is valid.
            {
                auto raised = documentTree(container);
                raised["nextDrawingSheetId"] = "9";
                auto changed = head;
                auto floors = changed["allocatorFloors"].toObject();
                floors["nextDrawingSheetId"] = "9";
                changed["allocatorFloors"] = floors;
                check(decodeContainer(repack(container, changed, compact(raised)))
                              .nextDrawingSheetId() == 9,
                      "Consistent raised sheet allocator decodes");
            }
            {
                auto raised = documentTree(container);
                raised["nextDrawingItemId"] = "40";
                auto changed = head;
                auto floors = changed["allocatorFloors"].toObject();
                floors["nextDrawingItemId"] = "40";
                changed["allocatorFloors"] = floors;
                check(decodeContainer(repack(container, changed, compact(raised)))
                              .nextDrawingItemId() == 40,
                      "Consistent raised item allocator decodes");
            }
            // The feature, the encoding and the payload version must agree.
            auto withoutFeature = head;
            auto features = withoutFeature["requiredFeatures"].toArray();
            features.removeLast();
            withoutFeature["requiredFeatures"] = features;
            rejects([&] { (void)decodeContainer(repack(container, withoutFeature,
                                                       documentChunk(container))); },
                    "schema 27 payload without drawing-sheets-v1");
            auto encoding26 = head;
            auto chunks = encoding26["chunks"].toArray();
            auto chunk = chunks[0].toObject();
            chunk["encoding"] = "json-v26";
            chunks[0] = chunk;
            encoding26["chunks"] = chunks;
            rejects([&] { (void)decodeContainer(repack(container, encoding26,
                                                       documentChunk(container))); },
                    "drawing-sheets-v1 with json-v26 encoding");
            auto version26 = documentTree(container);
            version26["version"] = 26;
            rejects([&] { (void)decodeDocument(compact(version26)); },
                    "schema 26 payload with drawing-sheet fields");
            rejects([&] { (void)decodeContainer(repack(container, head, compact(version26))); },
                    "schema 26 payload under drawing-sheets-v1");
            auto duplicateFeature = head;
            features = duplicateFeature["requiredFeatures"].toArray();
            features.append("drawing-sheets-v1");
            duplicateFeature["requiredFeatures"] = features;
            rejects([&] { (void)decodeContainer(repack(container, duplicateFeature,
                                                       documentChunk(container))); },
                    "repeated drawing-sheets-v1");
        }

        // Decision 2: the downgrade window.
        {
            // 1. A sheet-free document saved by this build matches the 1.0.0 writer.
            auto plain = loadDocument(releasePath);
            check(plain.drawingSheets().empty() && plain.drawingViews().empty() &&
                      plain.nextDrawingSheetId() == 1 && plain.nextDrawingViewId() == 1,
                  "1.0.0 document migrates with empty drawing-sheet tables");
            const auto path = files.filePath("window.sketchyup");
            saveDocument(plain, path);
            const auto sheetFree = read(path);
            matchesRelease(sheetFree, release, "sheet-free save");
            check(documentChunk(sheetFree) == documentChunk(release),
                  "Re-saving the 1.0.0 file reproduces its document chunk exactly");
            auto releaseHead = manifest(release), head = manifest(sheetFree);
            releaseHead.remove("writer");
            head.remove("writer");
            check(head == releaseHead && manifest(sheetFree)["writer"] == SKETCHYUP_WRITER,
                  "Only the writer string may differ from the 1.0.0 manifest");
            // 2. Adding a sheet writes 27.
            const auto sheet = createDrawingSheet(plain, "Sheet 1", {420, 297});
            saveDocument(plain, path);
            check(documentTree(read(path))["version"] == 27 &&
                      manifest(read(path))["requiredFeatures"].toArray().last() ==
                          "drawing-sheets-v1",
                  "Adding a sheet writes schema 27");
            const auto view = createDrawingView(plain, viewRecord("View 1", sheet, 1));
            eraseDrawingView(plain, view);
            saveDocument(plain, path);
            check(documentTree(read(path))["version"] == 27, "A sheet alone keeps schema 27");
            // R091.b: sheets with items write 27 and store the items.
            DrawingTextItem note;
            note.frame = {20, 20, 100, 20};
            note.text = "Window schedule";
            createDrawingItem(plain, sheet, note);
            DrawingTitleBlockItem block;
            block.frame = {250, 240, 160, 47};
            block.fields = {"Box", "Plan", "2026-10-14", "A"};
            createDrawingItem(plain, sheet, block);
            saveDocument(plain, path);
            check(documentTree(read(path))["version"] == 27 &&
                      documentTree(read(path))["drawingItems"].toArray().size() == 2 &&
                      manifest(read(path))["allocatorFloors"].toObject()["nextDrawingItemId"] ==
                          "3",
                  "Sheets with items write schema 27");
            check(loadDocument(path).drawingItems().size() == 2, "Saved items reopen");
            // 3. Deleting every sheet, and with it every item, writes 26 again. The
            // allocators are not stored at schema 26, so a reopened sheet-free file
            // starts them at 1 again.
            eraseDrawingSheet(plain, sheet);
            check(plain.drawingItems().empty(), "Deleting the sheet deletes its items");
            check(plain.nextDrawingSheetId() == 2 && plain.nextDrawingViewId() == 2 &&
                      plain.nextDrawingItemId() == 3,
                  "In-memory allocators never rewind");
            saveDocument(plain, path);
            const auto again = read(path);
            matchesRelease(again, release, "after deleting every sheet");
            const auto reloaded = loadDocument(path);
            check(reloaded.nextDrawingSheetId() == 1 && reloaded.nextDrawingViewId() == 1 &&
                      reloaded.nextDrawingItemId() == 1 && reloaded.drawingItems().empty(),
                  "Schema 26 does not store drawing-sheet allocators");
            check(QFileInfo::exists(path + ".bak") &&
                      documentTree(read(path + ".bak"))["version"] == 27,
                  "The previous schema-27 save is kept as the backup");
            // Undo brings the sheet back, and the next save is schema 27 again.
            plain.undo();
            check(plain.drawingSheets().size() == 1 && plain.drawingItems().size() == 2,
                  "Undo restores the sheet and its items");
            saveDocument(plain, path);
            check(documentTree(read(path))["version"] == 27 &&
                      documentTree(read(path))["drawingItems"].toArray().size() == 2,
                  "Undo re-enters schema 27 with the items");

            // The opt-in release check: SKETCHYUP_PRIOR_CLI names a previous release's
            // sketchyup-cli (the R090.b scripted check uses it). SketchyUp 1.0.0 reports
            // "Unsupported container field": its manifest check runs before the
            // required-feature check, so it never reaches the features message. A prior
            // release that reads schema 27 (1.1.x, the 1.2.0 package's upgrade baseline)
            // opens the sheet documents instead, unchanged.
            const auto prior = qEnvironmentVariable("SKETCHYUP_PRIOR_CLI");
            if (!prior.isEmpty()) {
                QByteArray error;
                const auto reads = priorReads(prior);
                const auto freePath = files.filePath("prior-free.sketchyup");
                write(freePath, again);
                check(runPriorCli(prior, freePath, error) == 0,
                      "The prior release accepts a sheet-free document");
                const auto sheetPath = files.filePath("prior-sheet.sketchyup");
                const auto sheetBytes = read(path);
                write(sheetPath, sheetBytes);
                const auto fixturePath = files.filePath("prior-fixture.sketchyup");
                const auto fixtureBytes =
                    read(fixtures.filePath("drawing-sheets-v27.sketchyup"));
                write(fixturePath, fixtureBytes);
                if (reads >= 27) {
                    check(runPriorCli(prior, sheetPath, error) == 0 &&
                              read(sheetPath) == sheetBytes,
                          "The prior release opens schema 27 with items unchanged: " +
                              error.toStdString());
                    check(runPriorCli(prior, fixturePath, error) == 0 &&
                              read(fixturePath) == fixtureBytes,
                          "The prior release opens the v27 item fixture unchanged: " +
                              error.toStdString());
                } else {
                    check(runPriorCli(prior, sheetPath, error) != 0 &&
                              error.contains("\"status\":\"failed\"") &&
                              error.contains("Unsupported") &&
                              read(sheetPath) == sheetBytes,
                          "The prior release rejects schema 27 with items and leaves the file "
                          "unchanged");
                    check(runPriorCli(prior, fixturePath, error) != 0 &&
                              error.contains("\"status\":\"failed\"") &&
                              read(fixturePath) == fixtureBytes,
                          "The prior release rejects the v27 item fixture unchanged");
                }
                std::cout << "prior release CLI checked: " << prior.toStdString()
                          << " (reads up to " << reads << ")\n";
            }
        }

        // Save, backup and recovery round-trip with sheets.
        {
            auto edited = sheetFixture();
            const auto path = files.filePath("saved.sketchyup");
            saveDocument(edited, path);
            check(!edited.dirty(), "Save marks the document clean");
            sameSheets(edited, loadDocument(path), "save");
            const auto first = read(path);
            renameDrawingSheet(edited, 1, "Title");
            saveDocument(edited, path);
            check(read(path + ".bak") == first, "Backup keeps the previous container");
            sameSheets(sheetFixture(), loadDocument(path + ".bak"), "backup");
            const auto saved = loadDocument(path);
            check(saved.drawingSheets().at(1)->name == "Title", "Second save has the rename");
            sameSheets(edited, saved, "second save");
            // R091.b: an item edit saves, and the backup keeps the previous items.
            const auto beforeItem = read(path);
            auto text = std::get<DrawingTextItem>(edited.drawingItems().at(1)->data);
            text.text = "Revised notes";
            updateDrawingItem(edited, 1, text);
            reorderDrawingItems(edited, 1, {9, 8});
            saveDocument(edited, path);
            check(read(path + ".bak") == beforeItem, "Backup keeps the items before the edit");
            sameSheets(edited, loadDocument(path), "item edit save");
            check(std::get<DrawingTextItem>(loadDocument(path + ".bak").drawingItems().at(1)->data)
                          .text != "Revised notes",
                  "The backup has the previous item text");
            const auto snapshot = captureSave(edited);
            moveDrawingView(edited, 3, 1);
            saveSnapshot(edited, snapshot, path);
            check(loadDocument(path).drawingViews().at(3)->sheet == 2 && edited.dirty(),
                  "A captured snapshot saves the captured sheets, not later edits");
            check(loadDocument(path).drawingItems().at(7)->sheet == 2 &&
                      edited.drawingItems().at(7)->sheet == 1,
                  "The snapshot keeps the view's label on its captured sheet");
            eraseDrawingItem(edited, 2);
            DrawingRectangleItem border;
            border.frame = {10, 10, 400, 277};
            const auto border11 = createDrawingItem(edited, 1, border);
            check(border11 == 11, "New items use the never-rewinding allocator");

            const auto root = files.filePath("recovery");
            QString key;
            {
                // Destroying the writer releases the session lock and keeps its data.
                RecoveryWriter writer(root, QString::fromStdString(edited.identity()));
                writer.write(captureRecovery(edited, {path, std::nullopt, {}}));
                key = writer.key();
            }
            const auto recovered = readRecovery(root, key);
            check(recovered.verified && recovered.document.has_value() && !recovered.busy,
                  "Recovery journal verifies: " + recovered.issue.toStdString());
            sameSheets(edited, *recovered.document, "recovery");
            check(recovered.document->drawingViews().at(3)->sheet == 1,
                  "Recovery keeps the unsaved view move");
            check(recovered.document->drawingItems().at(7)->sheet == 1 &&
                      !recovered.document->drawingItems().contains(2) &&
                      recovered.document->drawingItems().contains(11) &&
                      recovered.document->nextDrawingItemId() == 12,
                  "Recovery keeps the unsaved item edits");
        }

        // Migration: every v1-26 fixture opens with empty sheet tables, its original
        // bytes unchanged, and a sheet-free migration writes schema 26.
        {
            const auto names = fixtures.entryList({"*.sketchyup", "raw-*.json"}, QDir::Files,
                                                  QDir::Name);
            int historical{};
            for (const auto &name : names) {
                // Newer schemas keep their own tests: release-1.1.0-v27 and the schema-28
                // fixture (R093.c, component_parameter_io), the schema-30 fixture
                // (R099.a, presentation_io), the schema-31 fixture (R100.a, style_bundle) and
                // the schema-32 fixture (R100.b2, material_texture_size).
                if (name == "drawing-sheets-v27.sketchyup" ||
                    name == "release-1.1.0-v27.sketchyup" ||
                    name == "component-parameters-v28.sketchyup" ||
                    name == "presentation-v30.sketchyup" || name == "edge-styles-v31.sketchyup" ||
                    name == "material-texture-size-v32.sketchyup")
                    continue;
                const auto source = fixtures.filePath(name);
                const auto original = read(source);
                const auto loaded = loadDocument(source);
                check(loaded.drawingSheets().empty() && loaded.drawingViews().empty() &&
                          loaded.drawingItems().empty() && loaded.nextDrawingSheetId() == 1 &&
                          loaded.nextDrawingViewId() == 1 && loaded.nextDrawingItemId() == 1,
                      name.toStdString() + " migrates with empty drawing-sheet tables");
                const auto output = files.filePath("migrated-" + name + ".sketchyup");
                const auto report = migrateNativeFile(source, output);
                check(report["outputDocumentVersion"] == 26 && read(source) == original &&
                          documentTree(read(output))["version"] == 26,
                      name.toStdString() + " migrates to schema 26 without changing the source");
                ++historical;
            }
            check(historical >= 29, "Historical fixture corpus present");
            std::cout << historical << " historical fixtures migrate with empty sheet tables\n";
        }

        // The retained writer fixture decodes and re-encodes exactly.
        {
            const auto path = fixtures.filePath("drawing-sheets-v27.sketchyup");
            const auto original = read(path);
            const auto loaded = loadDocument(path);
            sameSheets(sheetFixture(), loaded, "retained v27 fixture");
            const auto encoded = encodeContainer(loaded);
            check(documentChunk(encoded) == documentChunk(original),
                  "Retained v27 fixture re-encodes its document chunk exactly");
            auto head = manifest(encoded), originalHead = manifest(original);
            head.remove("writer");
            originalHead.remove("writer");
            check(head == originalHead, "Retained v27 manifest re-encodes exactly");
            const auto report = migrateNativeFile(path, files.filePath("fixture27.sketchyup"));
            check(report["outputDocumentVersion"] == 27 && !report["migrationNeeded"].toBool() &&
                      read(path) == original,
                  "The v27 fixture is current and unchanged");
        }
        std::cout << "drawing-sheet persistence passed\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
