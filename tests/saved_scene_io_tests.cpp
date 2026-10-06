#include "core/scenes.hpp"
#include "core/tags.hpp"
#include "io/document_io.hpp"
#include "io/recovery.hpp"
#include "io/scenes_io.hpp"
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
    throw std::runtime_error("Expected saved scene persistence rejection");
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
        check(files.isValid(), "Isolated scene persistence files");
        Document doc;
        const auto body = doc.addFace({{{0, 0, 0}, {2, 0, 0}, {2, 3, 0}, {0, 3, 0}}});
        const auto face = doc.bodies().at(body)->surface.faces.begin()->first;
        const auto edge = doc.bodies().at(body)->topology.edges.begin()->first;
        const auto tag = createTag(doc, "Optional tag");
        SceneSnapshot all;
        all.camera = SceneCamera{{1.25, -2.5, 3.75}, 179.5, -89.5, .0625, 119.5, true};
        all.visibility = SceneVisibility{{{body, false}},
                                         {{tag, false}},
                                         {{body, SceneEntityKind::Body, 0},
                                          {body, SceneEntityKind::Face, face},
                                          {body, SceneEntityKind::Edge, edge}},
                                         true};
        all.style = ModelStyle{};
        all.style->mode = ModelStyleMode::XRay;
        all.style->groundVisible = true;
        all.section = SceneSection{{{0, 0, 1, -3.25}}};
        for (int mask = 1; mask < 16; ++mask) {
            auto snapshot = all;
            if (!(mask & 1))
                snapshot.camera.reset();
            if (!(mask & 2))
                snapshot.visibility.reset();
            if (!(mask & 4))
                snapshot.style.reset();
            if (!(mask & 8))
                snapshot.section.reset();
            const auto encoded = encodeSceneSnapshot(snapshot);
            check(decodeSceneSnapshot(encoded) == snapshot,
                  "Every selective property mask roundtrips");
            createScene(doc, "View " + std::to_string(mask), snapshot);
        }
        SceneSnapshot off;
        off.section = SceneSection{};
        const auto offId = createScene(doc, "Section off", off);
        check(decodeSceneSnapshot(encodeSceneSnapshot(off)) == off,
              "Section OFF differs from unowned section state");
        eraseScene(doc, offId);
        auto order = orderedScenes(doc);
        std::reverse(order.begin(), order.end());
        reorderScenes(doc, order);
        auto raw = encodeDocument(doc), bytes = encodeContainer(doc);
        check(QJsonDocument::fromJson(raw).object()["version"] == 21, "Current schema explicit");
        auto reopened = decodeContainer(bytes);
        check(encodeContainer(reopened) == bytes && encodeDocument(decodeDocument(raw)) == raw &&
                  orderedScenes(reopened) == order && reopened.nextSceneId() > offId &&
                  !reopened.canUndo(),
              "Scene bytes, order and retired ID floor preserved");
        const auto saved = captureSave(doc);
        renameScene(doc, order.front(), "Newer name");
        saveSnapshot(doc, saved, files.filePath("captured.sketchyup"));
        check(encodeContainer(loadDocument(files.filePath("captured.sketchyup"))) == bytes &&
                  doc.dirty(),
              "Async save owns immutable scene records");
        eraseTag(reopened, tag);
        reopened.erase(body);
        const auto &snapshot = reopened.scenes().at(15)->snapshot;
        const auto missing = missingSceneReferences(reopened, snapshot);
        check(missing.bodies.contains(body) && missing.tags.contains(tag) &&
                  missing.entities.size() == 3,
              "Deleted geometry and tags diagnosed");
        const auto missingBytes = encodeContainer(reopened);
        auto restored = decodeContainer(missingBytes);
        check(encodeContainer(restored) == missingBytes &&
                  restored.scenes().at(15)->snapshot == snapshot &&
                  missingSceneReferences(restored, snapshot).size() == missing.size(),
              "Missing references survive save/reopen without rewriting snapshot");
        QString key;
        {
            RecoveryWriter writer(files.filePath("recovery"),
                                  QString::fromStdString(restored.identity()));
            key = writer.key();
            writer.write(captureRecovery(restored, {}));
        }
        const auto recovered = readRecovery(files.filePath("recovery"), key);
        check(recovered.verified && recovered.document && recovered.document->dirty() &&
                  encodeScenes(recovered.document->scenes()) == encodeScenes(restored.scenes()),
              "Recovery preserves selective snapshots and diagnostics");
        const auto encoded = encodeSceneSnapshot(all);
        for (const auto &key : encoded.keys()) {
            auto bad = encoded;
            bad[key] = QJsonValue::Null;
            rejects([&] { decodeSceneSnapshot(bad); });
            auto record = encoded[key].toObject();
            for (const auto &field : record.keys()) {
                auto missing = record;
                missing.remove(field);
                bad = encoded;
                bad[key] = missing;
                rejects([&] { decodeSceneSnapshot(bad); });
                auto mistyped = record;
                mistyped[field] = "wrong";
                bad = encoded;
                bad[key] = mistyped;
                rejects([&] { decodeSceneSnapshot(bad); });
            }
        }
        rejects([&] { decodeSceneSnapshot(QJsonObject{}); });
        for (int variant = 0; variant < 12; ++variant) {
            auto root = QJsonDocument::fromJson(raw).object();
            auto scenes = root["scenes"].toArray();
            auto record = scenes[0].toObject();
            if (variant == 0)
                root.remove("scenes");
            if (variant == 1)
                root.remove("nextSceneId");
            if (variant == 2)
                root["nextSceneId"] = "1";
            if (variant == 3)
                record["position"] = .5;
            if (variant == 4)
                record["position"] = 15;
            if (variant == 5)
                record["position"] = scenes[1].toObject()["position"];
            if (variant == 6)
                record["id"] = 1;
            if (variant == 7)
                record["id"] = "01";
            if (variant == 8)
                record["name"] = scenes[1].toObject()["name"];
            if (variant == 9)
                record["snapshot"] = QJsonObject{};
            if (variant == 10)
                record["future"] = true;
            if (variant == 11)
                root["nextSceneId"] = 1;
            if (variant >= 3 && variant <= 10) {
                scenes[0] = record;
                root["scenes"] = scenes;
            }
            rejects([&] { decodeDocument(QJsonDocument(root).toJson()); });
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
                        auto c = chunks[0].toObject();
                        c["encoding"] = "json-v17";
                        chunks[0] = c;
                        manifest["chunks"] = chunks;
                    }
                    if (variant == 2) {
                        document["version"] = 17;
                        document.remove("scenes");
                        document.remove("nextSceneId");
                    }
                    if (variant == 3) {
                        auto floors = manifest["allocatorFloors"].toObject();
                        floors["nextSceneId"] = "999";
                        manifest["allocatorFloors"] = floors;
                    }
                }));
            });
        const auto historical =
            loadDocument(QStringLiteral(SOURCE_DIR "/tests/fixtures/model-style-v17.sketchyup"));
        check(historical.scenes().empty() && historical.nextSceneId() == 1 &&
                  historical.style().mode == ModelStyleMode::Monochrome &&
                  !historical.bodies().empty(),
              "Retained v17 styled model migrates with empty scenes");
        QFile source(QStringLiteral(SOURCE_DIR "/tests/fixtures/model-style-v17.sketchyup"));
        check(source.open(QIODevice::ReadOnly), "Read actual v17 writer bytes");
        const auto oldBytes = source.readAll();
        const auto oldLength = qFromLittleEndian<quint32>(oldBytes.constData() + 12);
        auto old = QJsonDocument::fromJson(oldBytes.mid(16 + oldLength)).object();
        old["version"] = 21;
        old["annotations"] = QJsonArray{};
        old["nextAnnotationId"] = "1";
        old["sections"] = QJsonArray{};
        old["nextSectionId"] = "1";
        old["activeSections"] = QJsonArray{};
        old["scenes"] = QJsonArray{};
        old["nextSceneId"] = "1";
        check(
            QJsonDocument::fromJson(encodeDocument(historical, AssetStorage::External)).object() ==
                old,
            "Migration changes only schema and empty scene fields");
        std::cout << "Selective scenes, strict storage, missing refs, recovery and retained v17 "
                     "migration passed\n";
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
