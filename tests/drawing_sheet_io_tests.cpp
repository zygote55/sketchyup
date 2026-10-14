// R090.b: schema-27 drawing-sheet persistence, migration and the downgrade window
// of ADR 0163 decision 2.
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
// The retained drawing-sheets-v27 fixture. It exercises every stored field, both
// content modes, a null and a present acceptance stamp, nondefault order, retired
// sheet and view IDs, and a view whose scene was deleted (a diagnosed broken link).
Document sheetFixture() {
    Document doc;
    doc.addFace({{{0, 0, 0}, {2, 0, 0}, {2, 3, 0}, {0, 3, 0}}});
    const auto plan = createScene(doc, "Plan", orthographicScene(-90));
    const auto front = createScene(doc, "Front", orthographicScene(0));
    const auto detail = createScene(doc, "Detail", orthographicScene(45));
    const auto cover = createDrawingSheet(doc, "Cover", {420, 297});
    const auto plans = createDrawingSheet(doc, "Plans", {297, 420.5});
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
    createDrawingView(doc, frontView);
    auto detailView = viewRecord("Detail A", cover, detail);
    detailView.frame = {300, 180, 100, 100};
    detailView.scaleDenominator = 2.5;
    createDrawingView(doc, detailView);
    acceptDrawingViews(doc, {{planId, {doc.revision(), fingerprintA}}});
    eraseScene(doc, detail);
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
              a.nextDrawingSheetId() == b.nextDrawingSheetId() &&
              a.nextDrawingViewId() == b.nextDrawingViewId(),
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

        // The policy constants: the reader accepts 27, and sheet-free content stays 26.
        static_assert(nativeDocumentVersion == 27 && nativeSheetFreeDocumentVersion == 26);
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
                                       {"paper", QJsonObject{{"widthMm", 297}, {"heightMm", 420.5}}}},
                  "Sheet record stores id, name, position and paper");
        }
        const auto reopened = decodeContainer(container);
        sameSheets(doc, reopened, "container");
        check(encodeContainer(reopened) == container, "Container re-encodes byte-identically");
        sameSheets(doc, decodeDocument(encodeDocument(doc)), "raw document");
        check(drawingViewsWithMissingSource(reopened) == std::set<Id>{4} &&
                  reopened.drawingViews().at(4)->source.scene == 3 && !reopened.scenes().contains(3),
              "A broken scene link survives the round trip");
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
            // Allocator disagreement: below a live ID, or not matching the manifest floor.
            rawRejects([](QJsonObject &root) { root["nextDrawingSheetId"] = "2"; },
                       "sheet allocator at a live ID");
            rawRejects([](QJsonObject &root) { root["nextDrawingViewId"] = "4"; },
                       "view allocator at a live ID");
            for (const auto &floor : {"nextDrawingSheetId", "nextDrawingViewId"}) {
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
            // 3. Deleting every sheet writes 26 again. The allocators are not stored at
            // schema 26, so a reopened sheet-free file starts them at 1 again.
            eraseDrawingSheet(plain, sheet);
            check(plain.nextDrawingSheetId() == 2 && plain.nextDrawingViewId() == 2,
                  "In-memory allocators never rewind");
            saveDocument(plain, path);
            const auto again = read(path);
            matchesRelease(again, release, "after deleting every sheet");
            const auto reloaded = loadDocument(path);
            check(reloaded.nextDrawingSheetId() == 1 && reloaded.nextDrawingViewId() == 1,
                  "Schema 26 does not store drawing-sheet allocators");
            check(QFileInfo::exists(path + ".bak") &&
                      documentTree(read(path + ".bak"))["version"] == 27,
                  "The previous schema-27 save is kept as the backup");
            // Undo brings the sheet back, and the next save is schema 27 again.
            plain.undo();
            check(plain.drawingSheets().size() == 1, "Undo restores the sheet");
            saveDocument(plain, path);
            check(documentTree(read(path))["version"] == 27, "Undo re-enters schema 27");

            // The opt-in release check: SKETCHYUP_PRIOR_CLI names a previous release's
            // sketchyup-cli (the R090.b scripted check uses it). SketchyUp 1.0.0 reports
            // "Unsupported container field": its manifest check runs before the
            // required-feature check, so it never reaches the features message.
            const auto prior = qEnvironmentVariable("SKETCHYUP_PRIOR_CLI");
            if (!prior.isEmpty()) {
                QByteArray error;
                const auto freePath = files.filePath("prior-free.sketchyup");
                write(freePath, again);
                check(runPriorCli(prior, freePath, error) == 0,
                      "The prior release accepts a sheet-free document");
                const auto sheetPath = files.filePath("prior-sheet.sketchyup");
                const auto sheetBytes = read(path);
                write(sheetPath, sheetBytes);
                check(runPriorCli(prior, sheetPath, error) != 0 &&
                          error.contains("\"status\":\"failed\"") &&
                          error.contains("Unsupported") &&
                          read(sheetPath) == sheetBytes,
                      "The prior release rejects schema 27 and leaves the file unchanged");
                std::cout << "prior release CLI checked: " << prior.toStdString() << '\n';
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
            const auto snapshot = captureSave(edited);
            moveDrawingView(edited, 3, 1);
            saveSnapshot(edited, snapshot, path);
            check(loadDocument(path).drawingViews().at(3)->sheet == 2 && edited.dirty(),
                  "A captured snapshot saves the captured sheets, not later edits");

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
        }

        // Migration: every v1-26 fixture opens with empty sheet tables, its original
        // bytes unchanged, and a sheet-free migration writes schema 26.
        {
            const auto names = fixtures.entryList({"*.sketchyup", "raw-*.json"}, QDir::Files,
                                                  QDir::Name);
            int historical{};
            for (const auto &name : names) {
                if (name == "drawing-sheets-v27.sketchyup")
                    continue;
                const auto source = fixtures.filePath(name);
                const auto original = read(source);
                const auto loaded = loadDocument(source);
                check(loaded.drawingSheets().empty() && loaded.drawingViews().empty() &&
                          loaded.nextDrawingSheetId() == 1 && loaded.nextDrawingViewId() == 1,
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
