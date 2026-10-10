#include "automation/commands.hpp"
#include "io/document_io.hpp"
#include "io/native_format.hpp"
#include "io/recovery.hpp"
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QTemporaryDir>
#include <QtEndian>
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
    check(rejected, "Expected display precision storage/API rejection");
}
QJsonObject request(const Document &doc, QJsonArray commands) {
    return {{"apiVersion", 1},
            {"documentId", QString::fromStdString(doc.identity())},
            {"expectedRevision", QString::number(doc.revision())},
            {"commands", commands}};
}
QJsonObject manifest(const QByteArray &bytes) {
    const auto length = qFromLittleEndian<quint32>(bytes.constData() + 12);
    return QJsonDocument::fromJson(bytes.mid(16, length)).object();
}
QByteArray withManifest(const QByteArray &bytes, const QJsonObject &replacement) {
    const auto length = qFromLittleEndian<quint32>(bytes.constData() + 12);
    const auto json = QJsonDocument(replacement).toJson(QJsonDocument::Compact);
    QByteArray header = bytes.left(16);
    qToLittleEndian<quint32>(json.size(), header.data() + 12);
    return header + json + bytes.mid(16 + length);
}
int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    try {
        const QString fixture = QString(SOURCE_DIR) + "/tests/fixtures/display-precision-v25.sketchyup";
        if (argc == 3 && QString(argv[1]) == "--write-fixture") {
            Document doc(DisplayUnit::FeetInches);
            doc.addFace({{{0, 0, 0}, {.3048, 0, 0}, {.3048, .6096, 0}, {0, .6096, 0}}});
            doc.setDisplayPrecision(2);
            doc.markSaved();
            QFile file(argv[2]);
            check(file.open(QIODevice::WriteOnly) && file.write(encodeContainer(doc)) > 0,
                  "Fixture written");
            return 0;
        }
        QTemporaryDir files;
        for (auto unit : {DisplayUnit::Meters, DisplayUnit::Millimeters, DisplayUnit::FeetInches}) {
            for (int precision : {fullDisplayPrecision, 0, maxDisplayPrecision(unit)}) {
                Document doc(unit, precision);
                doc.addFace({{{0, 0, 0}, {2, 0, 0}, {2, 3, 0}, {0, 3, 0}}});
                const auto bytes = encodeContainer(doc);
                auto reopened = decodeContainer(bytes);
                check(reopened.displayUnits() == unit && reopened.displayPrecision() == precision &&
                          encodeContainer(reopened) == bytes && !reopened.dirty() &&
                          !reopened.canUndo(),
                      "Every unit roundtrips at Full, minimum and maximum precision");
                const auto raw = QJsonDocument::fromJson(encodeDocument(doc)).object();
                check(raw["version"] == nativeDocumentVersion &&
                          raw["displayPrecision"] == precision &&
                          decodeDocument(encodeDocument(doc)).displayPrecision() == precision,
                      "Raw schema stores the integer precision (-1 is Full)");
            }
            Document doc(unit, 1);
            doc.addFace({{{0, 0, 0}, {2, 0, 0}, {2, 3, 0}, {0, 3, 0}}});
            const auto snapshot = captureSave(doc);
            doc.setDisplayPrecision(0);
            saveSnapshot(doc, snapshot, files.filePath("captured.sketchyup"));
            check(doc.dirty() &&
                      loadDocument(files.filePath("captured.sketchyup")).displayPrecision() == 1,
                  "Immutable save keeps captured precision; later edit stays dirty");
            QString key;
            {
                RecoveryWriter writer(files.filePath("recovery"),
                                      QString::fromStdString(doc.identity()));
                key = writer.key();
                writer.write(captureRecovery(doc, {}));
            }
            const auto recovered = readRecovery(files.filePath("recovery"), key);
            check(recovered.verified && recovered.document &&
                      recovered.document->displayPrecision() == 0 && recovered.document->dirty(),
                  "Recovery checkpoint carries precision");
        }

        // Required container feature: older readers reject; this reader requires it.
        Document doc(DisplayUnit::Millimeters, 2);
        const auto bytes = encodeContainer(doc);
        auto head = manifest(bytes);
        const auto features = head["requiredFeatures"].toArray();
        check(features.contains("display-precision-v1") &&
                  head["chunks"].toArray()[0].toObject()["encoding"] ==
                      QString("json-v%1").arg(nativeDocumentVersion),
              "Container requires display-precision-v1 with the current encoding");
        auto withoutFeature = head;
        QJsonArray reduced;
        for (const auto &feature : features)
            if (feature != "display-precision-v1")
                reduced.append(feature);
        withoutFeature["requiredFeatures"] = reduced;
        rejects([&] { decodeContainer(withManifest(bytes, withoutFeature)); });
        auto unknown = head;
        auto extra = features;
        extra.append("display-precision-v2");
        unknown["requiredFeatures"] = extra;
        rejects([&] { decodeContainer(withManifest(bytes, unknown)); });

        // Invalid stored values reject; a v25 document without the field rejects.
        auto root = QJsonDocument::fromJson(encodeDocument(doc)).object();
        for (QJsonValue invalid : {QJsonValue(4), QJsonValue(-2), QJsonValue(1.5),
                                   QJsonValue("full"), QJsonValue(QJsonValue::Null)}) {
            auto bad = root;
            bad["displayPrecision"] = invalid;
            rejects([&] { decodeDocument(QJsonDocument(bad).toJson()); });
        }
        auto missing = root;
        missing.remove("displayPrecision");
        rejects([&] { decodeDocument(QJsonDocument(missing).toJson()); });
        auto older = root;
        older["version"] = 24;
        rejects([&] { decodeDocument(QJsonDocument(older).toJson()); });
        older.remove("displayPrecision");
        const auto migrated = decodeDocument(QJsonDocument(older).toJson());
        check(migrated.displayUnits() == DisplayUnit::Millimeters &&
                  migrated.displayPrecision() == fullDisplayPrecision,
              "Schema 24 migrates to Full precision");

        // Every historical fixture migrates to Full.
        const QDir fixtures(QString(SOURCE_DIR) + "/tests/fixtures");
        int historical = 0;
        for (const auto &name : fixtures.entryList({"*.sketchyup", "raw-*.json"}, QDir::Files)) {
            if (name == "display-precision-v25.sketchyup")
                continue;
            check(loadDocument(fixtures.filePath(name)).displayPrecision() == fullDisplayPrecision,
                  "Historical fixture migrates to Full precision");
            ++historical;
        }
        check(historical >= 20, "Historical fixture corpus covered");
        const auto golden = loadDocument(fixture);
        check(golden.displayUnits() == DisplayUnit::FeetInches && golden.displayPrecision() == 2 &&
                  golden.bodies().size() == 1 && !golden.dirty(),
              "v25 golden fixture keeps ft-in at two decimals");
        QFile goldenFile(fixture);
        check(goldenFile.open(QIODevice::ReadOnly), "Read v25 golden fixture");
        const auto goldenBytes = goldenFile.readAll();
        check(manifest(goldenBytes)["chunks"].toArray()[0].toObject()["encoding"] == "json-v25",
              "Retained fixture is schema-25 writer output");
        // Schema 26 (R082.cc) only changes placement storage; this fixture has no
        // placements, so its migrated container reopens byte-for-byte.
        const auto upgraded = encodeContainer(golden);
        const auto reopenedGolden = decodeContainer(upgraded);
        check(encodeContainer(reopenedGolden) == upgraded &&
                  reopenedGolden.displayPrecision() == 2 &&
                  encodeBodies(reopenedGolden.bodies()) == encodeBodies(golden.bodies()),
              "v25 golden fixture migrates to the current schema exactly");

        // Public API: document.units precision and document.describe.
        Document api;
        const auto original = encodeContainer(api);
        const auto change = request(
            api, {QJsonObject{{"command", "document.units"}, {"units", "mm"}, {"precision", 2}}});
        previewBatch(api, change);
        check(encodeContainer(api) == original, "Precision preview leaves authoritative state alone");
        executeBatch(api, change);
        check(api.displayUnits() == DisplayUnit::Millimeters && api.displayPrecision() == 2 &&
                  api.history().total == 1 && describe(api)["displayPrecision"] == 2,
              "document.units applies precision and describe reports it");
        executeBatch(api, request(api, {QJsonObject{{"command", "document.units"},
                                                    {"units", "mm"},
                                                    {"precision", "full"}}}));
        check(api.displayPrecision() == fullDisplayPrecision &&
                  describe(api)["displayPrecision"] == "full" && api.history().total == 2,
              "Full precision uses the \"full\" wire form");
        executeBatch(api, request(api, {QJsonObject{{"command", "document.units"},
                                                    {"units", "m"},
                                                    {"precision", 6}}}));
        executeBatch(api, request(api, {QJsonObject{{"command", "document.units"},
                                                    {"units", "ft-in"}}}));
        check(api.displayUnits() == DisplayUnit::FeetInches &&
                  api.displayPrecision() == fullDisplayPrecision,
              "Unit change without precision resets to Full");
        const auto before = encodeContainer(api);
        for (QJsonValue invalid : {QJsonValue(4), QJsonValue(-1), QJsonValue(1.5), QJsonValue("2"),
                                   QJsonValue("Full"), QJsonValue(QJsonValue::Null)})
            rejects([&] {
                executeBatch(api, request(api, {QJsonObject{{"command", "document.units"},
                                                            {"units", "ft-in"},
                                                            {"precision", invalid}}}));
            });
        rejects([&] {
            executeBatch(api, request(api, {QJsonObject{{"command", "document.units"},
                                                        {"units", "mm"},
                                                        {"precision", 5}}}));
        });
        rejects([&] {
            executeBatch(api,
                         request(api, {QJsonObject{{"command", "document.units"},
                                                   {"units", "ft-in"},
                                                   {"precision", 1}},
                                       QJsonObject{{"command", "unknown"}}}));
        });
        check(encodeContainer(api) == before && api.revision() == 4,
              "Rejected precision changes are atomic before publication");
        std::cout << "Display precision storage, migration, container feature, recovery and API "
                     "passed\n";
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
