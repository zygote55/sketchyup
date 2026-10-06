#include "automation/commands.hpp"
#include "automation/measurements.hpp"
#include "io/document_io.hpp"
#include "io/recovery.hpp"
#include <QCoreApplication>
#include <QJsonDocument>
#include <QTemporaryDir>
#include <iostream>
using namespace sketchy;
void check(bool value, const char *message) {
    if (!value)
        throw std::runtime_error(message);
}
template <class F> void rejects(F operation) {
    bool rejected = false;
    try {
        operation();
    } catch (const std::exception &) {
        rejected = true;
    }
    check(rejected, "Expected unit storage/API rejection");
}
QJsonObject request(const Document &doc, QJsonArray commands) {
    return {{"apiVersion", 1},
            {"documentId", QString::fromStdString(doc.identity())},
            {"expectedRevision", QString::number(doc.revision())},
            {"commands", commands}};
}
int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    try {
        QTemporaryDir files;
        for (auto unit : {DisplayUnit::Meters, DisplayUnit::Millimeters, DisplayUnit::FeetInches}) {
            Document doc(unit);
            doc.addFace({{{0, 0, 0}, {2, 0, 0}, {2, 3, 0}, {0, 3, 0}}});
            const auto bytes = encodeContainer(doc);
            auto reopened = decodeContainer(bytes);
            check(reopened.displayUnits() == unit && encodeContainer(reopened) == bytes &&
                      reopened.worldArea(1, 5) == 6 && !reopened.dirty() && !reopened.canUndo(),
                  "All display units roundtrip with identity and metric geometry intact");
            check(decodeDocument(encodeDocument(doc)).displayUnits() == unit,
                  "Raw v12 preserves document units");
            saveDocument(doc, files.filePath("units.sketchyup"));
            const auto snapshot = captureSave(doc);
            doc.setDisplayUnits(unit == DisplayUnit::Meters ? DisplayUnit::Millimeters
                                                            : DisplayUnit::Meters);
            saveSnapshot(doc, snapshot, files.filePath("captured.sketchyup"));
            check(doc.dirty() &&
                      loadDocument(files.filePath("captured.sketchyup")).displayUnits() == unit,
                  "Immutable save preserves captured units and newer edits remain dirty");
            QString key;
            {
                RecoveryWriter writer(files.filePath("recovery"),
                                      QString::fromStdString(reopened.identity()));
                key = writer.key();
                writer.write(captureRecovery(reopened, {}));
            }
            const auto recovered = readRecovery(files.filePath("recovery"), key);
            check(recovered.verified && recovered.document &&
                      recovered.document->displayUnits() == unit && recovered.document->dirty(),
                  "Verified crash recovery preserves preferred units");
        }
        auto legacy =
            loadDocument(QString(SOURCE_DIR) + "/tests/fixtures/m4-complete-v11.sketchyup");
        check(legacy.displayUnits() == DisplayUnit::Meters && legacy.assets().size() > 0 &&
                  legacy.definitions().size() > 0,
              "Complete v11 fixture migrates with meters and all records");
        Document doc;
        const auto original = encodeContainer(doc);
        const QJsonObject units{{"command", "document.units"}, {"units", "ft-in"}};
        const auto change = request(doc, {units});
        previewBatch(doc, change);
        check(encodeContainer(doc) == original, "Unit preview leaves authoritative state alone");
        executeBatch(doc, change);
        check(doc.displayUnits() == DisplayUnit::FeetInches && doc.history().total == 1 &&
                  describe(doc)["units"] == "m" && describe(doc)["displayUnits"] == "ft-in",
              "Public units command/query distinguishes geometry and display units");
        rejects([&] { executeBatch(doc, change); });
        const auto before = encodeContainer(doc);
        for (QJsonValue invalid : {QJsonValue("in"), QJsonValue(42), QJsonValue(QJsonValue::Null)})
            rejects([&] {
                executeBatch(doc, request(doc, {QJsonObject{{"command", "document.units"},
                                                            {"units", invalid}}}));
            });
        rejects([&] {
            executeBatch(doc,
                         request(doc, {QJsonObject{{"command", "document.units"}, {"units", "mm"}},
                                       QJsonObject{{"command", "unknown"}}}));
        });
        check(encodeContainer(doc) == before, "Invalid batches roll back unit preferences");
        auto root = QJsonDocument::fromJson(encodeDocument(doc)).object();
        for (QJsonValue invalid :
             {QJsonValue("yards"), QJsonValue(42), QJsonValue(QJsonValue::Null)}) {
            auto bad = root;
            bad["displayUnits"] = invalid;
            rejects([&] { decodeDocument(QJsonDocument(bad).toJson()); });
        }
        root.remove("displayUnits");
        rejects([&] { decodeDocument(QJsonDocument(root).toJson()); });
        root["version"] = 11;
        root.remove("style");
        root.remove("scenes");
        root.remove("nextSceneId");
        root.remove("hosted");
        auto legacyBodies = root["bodies"].toArray();
        for (qsizetype i = 0; i < legacyBodies.size(); ++i) {
            auto body = legacyBodies[i].toObject();
            body.remove("edgeAppearances");
            body.remove("faceTextureMappings");
            legacyBodies[i] = body;
        }
        root["bodies"] = legacyBodies;
        check(decodeDocument(QJsonDocument(root).toJson()).displayUnits() == DisplayUnit::Meters,
              "Legacy raw schema defaults to meters");
        check(
            parseLength("1000", QString::fromLatin1(
                                    defaultLengthUnit(DisplayUnit::Millimeters).data())) == 1 &&
                std::abs(parseLength("2", QString::fromLatin1(
                                              defaultLengthUnit(DisplayUnit::FeetInches).data())) -
                         .6096) < 1e-12 &&
                parseLength("2m", "mm") == 2,
            "Bare values use preferred units; explicit suffix wins");
        std::cout << "Document units storage, immutable save, recovery, migration and API passed\n";
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
