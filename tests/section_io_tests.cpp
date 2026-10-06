#include "core/sections.hpp"
#include "core/scenes.hpp"
#include "io/scenes_io.hpp"
#include "core/tags.hpp"
#include "io/document_io.hpp"
#include "io/recovery.hpp"
#include "io/sections_io.hpp"
#include <QCoreApplication>
#include <QCryptographicHash>
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
template <class F> void rejects(F operation) {
    try {
        operation();
    } catch (const std::exception &) {
        return;
    }
    throw std::runtime_error("Expected section persistence rejection");
}
QByteArray rewrite(const QByteArray &source,
                   const std::function<void(QJsonObject &, QJsonObject &)> &operation) {
    const auto length = qFromLittleEndian<quint32>(source.constData() + 12);
    auto manifest = QJsonDocument::fromJson(source.mid(16, length)).object();
    auto document = QJsonDocument::fromJson(source.mid(16 + length)).object();
    operation(manifest, document);
    const auto payload = QJsonDocument(document).toJson(QJsonDocument::Compact);
    auto chunks = manifest["chunks"].toArray();
    auto chunk = chunks[0].toObject();
    chunk["bytes"] = QString::number(payload.size());
    chunk["sha256"] =
        QString::fromLatin1(QCryptographicHash::hash(payload, QCryptographicHash::Sha256).toHex());
    chunks[0] = chunk;
    manifest["chunks"] = chunks;
    const auto metadata = QJsonDocument(manifest).toJson(QJsonDocument::Compact);
    auto header = source.left(16);
    qToLittleEndian<quint32>(metadata.size(), header.data() + 12);
    return header + metadata + payload;
}
} // namespace
int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    try {
        QTemporaryDir files;
        check(files.isValid(), "Isolated section files");
        Document doc;
        const auto body = doc.addFace({{{0, 0, 0}, {2, 0, 0}, {2, 3, 0}}});
        const auto root = createSection(doc, "Horizontal", 0, {{0, 0, 1}, -.5});
        const auto scoped = createSection(doc, "Scoped", body, {{1, 0, 0}, -1});
        auto section = *doc.sections().at(scoped);
        section.fill = false;
        section.edges = false;
        section.color = {.125F, .25F, .375F};
        updateSection(doc, scoped, section);
        setActiveSection(doc, 0, root);
        setActiveSection(doc, body, scoped);
        const auto retired = createSection(doc, "Retired", 0, {});
        doc.undo();
        const auto raw = encodeDocument(doc), bytes = encodeContainer(doc);
        check(QJsonDocument::fromJson(raw).object()["version"] == 21, "Schema 21 explicit");
        check(encodeDocument(decodeDocument(raw)) == raw &&
                  encodeContainer(decodeContainer(bytes)) == bytes,
              "Exact raw/container section round trips");
        auto reopened = decodeContainer(bytes);
        check(*reopened.sections().at(scoped) == section &&
                  reopened.activeSections() == doc.activeSections() &&
                  reopened.nextSectionId() > retired,
              "Scope, plane, display flags, colors, activation and retired floor persist");
        const auto saved = captureSave(doc);
        eraseSection(doc, scoped);
        saveSnapshot(doc, saved, files.filePath("captured.sketchyup"));
        check(encodeContainer(loadDocument(files.filePath("captured.sketchyup"))) == bytes &&
                  doc.dirty(),
              "Async save owns immutable section state");
        reopened.erase(body);
        auto missing = decodeContainer(encodeContainer(reopened));
        check(missingSectionContexts(missing) == std::set<Id>{scoped} &&
                  missing.activeSections().at(body) == scoped,
              "Missing context and inactive reference persist");
        QString recoveryKey;
        {
            RecoveryWriter writer(files.filePath("recovery"),
                                  QString::fromStdString(missing.identity()));
            recoveryKey = writer.key();
            writer.write(captureRecovery(missing, {}));
        }
        auto recovered = readRecovery(files.filePath("recovery"), recoveryKey);
        check(recovered.verified && recovered.document && recovered.document->dirty() &&
                  encodeSections(recovered.document->sections()) ==
                      encodeSections(missing.sections()) &&
                  recovered.document->activeSections() == missing.activeSections(),
              "Verified recovery preserves section records and activation");
        const auto good = QJsonDocument::fromJson(raw).object();
        const auto rows = good["sections"].toArray();
        const auto row = rows[0].toObject();
        for (const auto &field : row.keys()) {
            auto invalid = row;
            invalid.remove(field);
            rejects([&] { decodeSections(QJsonArray{invalid}, 100); });
            invalid = row;
            invalid[field] = QJsonValue::Null;
            rejects([&] { decodeSections(QJsonArray{invalid}, 100); });
        }
        for (int variant = 0; variant < 13; ++variant) {
            auto invalid = good;
            auto records = rows;
            auto first = row;
            if (variant == 0)
                first["plane"] = QJsonArray{0, 0, 2, 0};
            if (variant == 1)
                first["plane"] = QJsonArray{0, 0, 1};
            if (variant == 2)
                first["plane"] = QJsonArray{0, 0, 1, 1e9};
            if (variant == 3)
                first["id"] = "01";
            if (variant == 4)
                first["context"] = 0;
            if (variant == 5)
                first["color"] = QJsonArray{1, -0.1, 0};
            if (variant == 6)
                first["fill"] = 1;
            if (variant == 7)
                first["extra"] = true;
            records[0] = first;
            invalid["sections"] = records;
            if (variant == 8)
                invalid["sections"] = QJsonArray{row, row};
            if (variant == 9)
                invalid["nextSectionId"] = "1";
            if (variant == 10)
                invalid["activeSections"] =
                    QJsonArray{QJsonObject{{"context", "0"}, {"section", "999"}}};
            if (variant == 11)
                invalid["activeSections"] =
                    QJsonArray{QJsonObject{{"context", "0"}, {"section", QString::number(scoped)}}};
            if (variant == 12)
                invalid.remove("activeSections");
            rejects([&] { decodeDocument(QJsonDocument(invalid).toJson()); });
        }
        for (int variant = 0; variant < 4; ++variant)
            rejects([&] {
                decodeContainer(rewrite(bytes, [&](auto &manifest, auto &document) {
                    if (variant == 0) {
                        auto features = manifest["requiredFeatures"].toArray();
                        features.removeAt(features.size() - 1);
                        manifest["requiredFeatures"] = features;
                    }
                    if (variant == 1) {
                        auto chunks = manifest["chunks"].toArray();
                        auto chunk = chunks[0].toObject();
                        chunk["encoding"] = "json-v18";
                        chunks[0] = chunk;
                        manifest["chunks"] = chunks;
                    }
                    if (variant == 2)
                        document["version"] = 18;
                    if (variant == 3) {
                        auto floors = manifest["allocatorFloors"].toObject();
                        floors["nextSectionId"] = "999";
                        manifest["allocatorFloors"] = floors;
                    }
                }));
            });
        QFile source(QStringLiteral(SOURCE_DIR "/tests/fixtures/saved-scenes-v18.sketchyup"));
        check(source.open(QIODevice::ReadOnly), "Read actual prior writer fixture");
        const auto oldBytes = source.readAll();
        const auto old = decodeContainer(oldBytes);
        check(old.sections().empty() && old.activeSections().empty() && old.nextSectionId() == 1 &&
                  old.scenes().size() == 2,
              "Version 18 retains saved scenes with no invented sections");
        const auto length = qFromLittleEndian<quint32>(oldBytes.constData() + 12);
        auto expected = QJsonDocument::fromJson(oldBytes.mid(16 + length)).object();
        expected["version"] = 21;
        expected["annotations"] = QJsonArray{};
        expected["nextAnnotationId"] = "1";
        expected["sections"] = QJsonArray{};
        expected["nextSectionId"] = "1";
        expected["activeSections"] = QJsonArray{};
        check(QJsonDocument::fromJson(encodeDocument(old, AssetStorage::External)).object() ==
                  expected,
              "Actual historical fixture migration changes only schema and empty section fields");
        SceneSnapshot captured;
        captured.section = SceneSection{std::nullopt, doc.activeSections()};
        const auto scene = createScene(doc, "Named section view", captured);
        auto sectionRoundTrip = decodeContainer(encodeContainer(doc));
        check(sectionRoundTrip.scenes().at(scene)->snapshot == captured,
              "Named scene section identities round trip exactly");
        const auto encodedScene = encodeSceneSnapshot(captured);
        rejects([&] { decodeSceneSnapshot(encodedScene, false); });
        auto malformedScene = encodedScene;
        auto malformedSection = malformedScene["section"].toObject();
        malformedSection["active"] = QJsonArray{QJsonObject{{"context", "0"}, {"section", "0"}}};
        malformedScene["section"] = malformedSection;
        rejects([&] { decodeSceneSnapshot(malformedScene); });
        malformedSection["active"] = QJsonArray{QJsonObject{{"context", "0"}, {"section", "1"}},
                                                QJsonObject{{"context", "0"}, {"section", "2"}}};
        malformedScene["section"] = malformedSection;
        rejects([&] { decodeSceneSnapshot(malformedScene); });
        QFile prior(QStringLiteral(SOURCE_DIR "/tests/fixtures/section-planes-v19.sketchyup"));
        check(prior.open(QIODevice::ReadOnly), "Read actual schema-19 section fixture");
        const auto priorBytes = prior.readAll();
        const auto priorDoc = decodeContainer(priorBytes);
        const auto priorLength = qFromLittleEndian<quint32>(priorBytes.constData() + 12);
        auto priorExpected = QJsonDocument::fromJson(priorBytes.mid(16 + priorLength)).object();
        priorExpected["version"] = 21;
        priorExpected["annotations"] = QJsonArray{};
        priorExpected["nextAnnotationId"] = "1";
        check(QJsonDocument::fromJson(encodeDocument(priorDoc, AssetStorage::External)).object() ==
                  priorExpected && priorDoc.activeSections().size() == 2,
              "Schema-19 migration preserves geometry, sections and active state exactly");
        auto wide = good;
        auto wideRecords = rows;
        auto wideRecord = row;
        constexpr Id wideId = 9007199254740993ULL;
        wideRecord["id"] = QString::number(wideId);
        wideRecords[0] = wideRecord;
        wide["sections"] = wideRecords;
        wide["nextSectionId"] = QString::number(wideId + 100);
        wide["activeSections"] =
            QJsonArray{QJsonObject{{"context", "0"}, {"section", QString::number(wideId)}}};
        auto wideDocument = decodeDocument(QJsonDocument(wide).toJson());
        check(wideDocument.sections().contains(wideId) &&
                  wideDocument.nextSectionId() == wideId + 100 &&
                  decodeContainer(encodeContainer(wideDocument)).activeSections().at(0) == wideId,
              "Section identity and allocator above 2^53 remain exact");
        std::cout << "Strict section storage, immutable saves, recovery and v18 migration passed\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
