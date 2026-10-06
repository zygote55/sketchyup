#include "automation/transaction_coordinator.hpp"
#include "core/component_glue.hpp"
#include "core/components.hpp"
#include "io/document_io.hpp"
#include "io/recovery.hpp"
#include <QCoreApplication>
#include <QCryptographicHash>
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
    throw std::runtime_error("Expected glue persistence rejection");
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
        Document doc;
        const auto body = doc.addFace({{{0, 0, 0}, {4, 0, 0}, {4, 4, 0}, {0, 4, 0}},
                                       {{1, 1, 0}, {1, 3, 0}, {3, 3, 0}, {3, 1, 0}}});
        const auto face = doc.bodies().at(body)->surface.faces.begin()->first;
        const auto made = createComponent(doc, body, "Stored window");
        const auto member = made.movedGeometry.at(body);
        const ComponentGlue glue{member, face, {2, 2, 0}, {1, 0, 0}, true};
        setComponentGlue(doc, made.definition, glue);
        placeComponent(doc, made.definition,
                       Transform::translation({10, 0, 0}) * Transform::scaling({-2, 1, .5}));
        const auto raw = encodeDocument(doc), bytes = encodeContainer(doc);
        auto reopened = decodeContainer(bytes);
        check(encodeDocument(reopened) == raw && encodeContainer(reopened) == bytes &&
                  reopened.definitions().at(made.definition)->glue == glue && !reopened.dirty() &&
                  !reopened.canUndo(),
              "Canonical glue, references and mirrored placements roundtrip exactly");
        check(resolveComponentGlue(*reopened.definitions().at(made.definition)).profile.size() == 4,
              "Reopened glue resolves against authoritative geometry");
        const auto json = QJsonDocument::fromJson(raw).object();
        check(json["version"] == 14, "Glue schema version is explicit");
        auto missing = json;
        auto definitions = missing["definitions"].toArray();
        auto definition = definitions[0].toObject();
        const auto goodGlue = definition["glue"].toObject();
        definition.remove("glue");
        definitions[0] = definition;
        missing["definitions"] = definitions;
        rejects([&] { decodeDocument(QJsonDocument(missing).toJson()); });
        missing["version"] = 13;
        auto migrated = decodeDocument(QJsonDocument(missing).toJson());
        check(!migrated.definitions().at(made.definition)->glue &&
                  migrated.instances().size() == doc.instances().size(),
              "Schema 13 migration never invents placement behavior");
        for (int variant = 0; variant < 9; ++variant) {
            auto bad = json;
            auto records = bad["definitions"].toArray();
            auto record = records[0].toObject();
            auto malformed = goodGlue;
            if (variant == 0)
                malformed["face"] = "999";
            if (variant == 1)
                malformed["member"] = 2;
            if (variant == 2)
                malformed["anchor"] = QJsonArray{1, 2};
            if (variant == 3)
                malformed["anchor"] = QJsonArray{1, 2, 1};
            if (variant == 4)
                malformed["tangent"] = QJsonArray{0, 0, 1};
            if (variant == 5)
                malformed["cutsOpening"] = 1;
            if (variant == 6)
                malformed.remove("cutsOpening");
            if (variant == 7)
                malformed["future"] = true;
            record["glue"] = variant == 8 ? QJsonValue(false) : QJsonValue(malformed);
            records[0] = record;
            bad["definitions"] = records;
            rejects([&] { decodeDocument(QJsonDocument(bad).toJson()); });
        }
        const auto oldContainer = rewrite(bytes, [](auto &manifest, auto &document) {
            auto features = manifest["requiredFeatures"].toArray();
            features.removeAt(features.size() - 1);
            manifest["requiredFeatures"] = features;
            auto chunks = manifest["chunks"].toArray();
            auto chunk = chunks[0].toObject();
            chunk["encoding"] = "json-v13";
            chunks[0] = chunk;
            manifest["chunks"] = chunks;
            document["version"] = 13;
            auto records = document["definitions"].toArray();
            for (qsizetype i = 0; i < records.size(); ++i) {
                auto record = records[i].toObject();
                record.remove("glue");
                records[i] = record;
            }
            document["definitions"] = records;
        });
        check(!decodeContainer(oldContainer).definitions().at(made.definition)->glue,
              "Historical container feature/encoding pairing migrates safely");
        rejects([&] {
            decodeContainer(rewrite(bytes, [](auto &manifest, auto &) {
                auto features = manifest["requiredFeatures"].toArray();
                features.removeAt(features.size() - 1);
                manifest["requiredFeatures"] = features;
            }));
        });
        rejects([&] {
            decodeContainer(rewrite(bytes, [](auto &manifest, auto &) {
                auto features = manifest["requiredFeatures"].toArray();
                features.append("future-glue-v99");
                manifest["requiredFeatures"] = features;
            }));
        });
        QTemporaryDir files;
        check(files.isValid(), "Isolated persistence directory");
        const auto save = captureSave(doc);
        const auto recovery = captureRecovery(doc);
        setComponentGlue(doc, made.definition, {});
        saveSnapshot(doc, save, files.filePath("window.sketchyup"));
        check(doc.dirty() && loadDocument(files.filePath("window.sketchyup"))
                                     .definitions()
                                     .at(made.definition)
                                     ->glue == glue,
              "Immutable save retains captured glue and does not mark changed document saved");
        QString key;
        {
            RecoveryWriter writer(files.filePath("recovery"),
                                  QString::fromStdString(doc.identity()));
            key = writer.key();
            writer.write(recovery);
        }
        const auto recovered = readRecovery(files.filePath("recovery"), key);
        check(recovered.verified && recovered.document && recovered.document->dirty() &&
                  recovered.document->definitions().at(made.definition)->glue == glue,
              "Verified recovery retains captured glue behavior");
        // Exercise durable delta reconstruction when the only change is glue
        // metadata: it must not be pruned as an otherwise equal definition.
        auto after = doc.readSnapshot();
        setComponentGlue(after, made.definition, glue);
        const auto transactionRoot = files.filePath("transactions");
        {
            OutcomeStore store(transactionRoot, QString::fromStdString(doc.identity()));
            const auto hash = QString(64, 'a');
            const auto pending = store.begin(doc, hash);
            const auto entry = after.history().entries.back();
            store.commit(
                pending["requestId"].toString(), hash, doc, after,
                {{"transactionApiVersion", 1},
                 {"revision", QString::number(after.revision())},
                 {"undo", QJsonObject{{"label", QString::fromStdString(entry.label)},
                                      {"taskId", ""},
                                      {"request", ""},
                                      {"assistant", false},
                                      {"commitRevision", QString::number(after.revision())}}}});
        }
        TransactionCoordinator::Options options;
        options.mode = TransactionCoordinator::OpenMode::RecoverLatest;
        TransactionCoordinator coordinator(doc, transactionRoot, options);
        check(encodeContainer(coordinator.document()) == encodeContainer(after) &&
                  coordinator.document().history().total == 1,
              "Durable recovery reconstructs a glue-only edit and one Undo item");
        coordinator.edit([](Document &document) { document.undo(); });
        check(!coordinator.document().definitions().at(made.definition)->glue,
              "Recovered glue-only Undo restores the previous behavior");
        std::cout << "Glue schema 14, strict records, legacy migration, immutable save and "
                     "recovery passed\n";
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
