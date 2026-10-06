#include "io/document_io.hpp"
#include "io/model_style_io.hpp"
#include "io/recovery.hpp"
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
    throw std::runtime_error("Expected style persistence rejection");
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
        check(files.isValid(), "Isolated persistence directory");
        for (auto mode :
             {ModelStyleMode::Textured, ModelStyleMode::Shaded, ModelStyleMode::Monochrome,
              ModelStyleMode::Wireframe, ModelStyleMode::XRay}) {
            Document doc;
            doc.addFace({{{0, 0, 0}, {2, 0, 0}, {2, 3, 0}, {0, 3, 0}}});
            auto style = doc.style();
            style.mode = mode;
            style.background = {.01f, .23f, .45f};
            style.ground = {.67f, .89f, .1f};
            style.front = {.2f, .3f, .4f};
            style.back = {.5f, .6f, .7f};
            style.edge = {.8f, .9f, 1};
            style.groundVisible = true;
            style.groundHeight = -123.456789;
            style.gridVisible = false;
            style.axesVisible = false;
            style.edgesVisible = false;
            style.profiles = true;
            style.profileWidth = 3.75;
            style.xrayOpacity = .125;
            doc.setStyle(style);
            const auto raw = encodeDocument(doc), bytes = encodeContainer(doc);
            const auto root = QJsonDocument::fromJson(raw).object();
            check(root["version"] == 19 && decodeDocument(raw).style() == style,
                  "Current schema preserves all style fields and modes");
            auto reopened = decodeContainer(bytes);
            check(reopened.style() == style && encodeContainer(reopened) == bytes &&
                      !reopened.dirty() && !reopened.canUndo() && reopened.worldArea(1, 5) == 6,
                  "Container roundtrip is byte stable without changing metric geometry");
            saveDocument(doc, files.filePath("style.sketchyup"));
            check(loadDocument(files.filePath("style.sketchyup")).style() == style,
                  "Native save/reopen retains style");
            const auto captured = captureSave(doc);
            doc.setStyle(ModelStyle{});
            saveSnapshot(doc, captured, files.filePath("captured.sketchyup"));
            check(doc.dirty() &&
                      loadDocument(files.filePath("captured.sketchyup")).style() == style,
                  "Async save retains captured style while newer style remains dirty");
            QString key;
            {
                RecoveryWriter writer(files.filePath("recovery"),
                                      QString::fromStdString(reopened.identity()));
                key = writer.key();
                writer.write(captureRecovery(reopened, {}));
            }
            const auto recovered = readRecovery(files.filePath("recovery"), key);
            check(recovered.verified && recovered.document &&
                      recovered.document->style() == style && recovered.document->dirty(),
                  "Verified recovery retains style");
            auto missing = root;
            missing.remove("style");
            rejects([&] { decodeDocument(QJsonDocument(missing).toJson()); });
            auto old = root;
            old["version"] = 16;
            rejects([&] { decodeDocument(QJsonDocument(old).toJson()); });
            old.remove("style");
            old.remove("scenes");
            old.remove("sections");
            old.remove("nextSectionId");
            old.remove("activeSections");
            old.remove("nextSceneId");
            check(decodeDocument(QJsonDocument(old).toJson()).style() == ModelStyle{},
                  "Legacy schema gets deterministic default style");
            const auto encodedStyle = root["style"].toObject();
            for (auto it = encodedStyle.begin(); it != encodedStyle.end(); ++it) {
                auto malformed = encodedStyle;
                malformed.remove(it.key());
                rejects([&] { decodeModelStyle(malformed); });
                malformed = encodedStyle;
                malformed[it.key()] = QJsonValue::Null;
                rejects([&] { decodeModelStyle(malformed); });
            }
            for (int variant = 0; variant < 17; ++variant) {
                auto malformed = encodedStyle;
                if (variant == 0)
                    malformed["future"] = true;
                if (variant == 1)
                    malformed["mode"] = "future";
                if (variant == 2)
                    malformed["mode"] = 1;
                if (variant == 3)
                    malformed["background"] = QJsonArray{0, 0};
                if (variant == 4)
                    malformed["ground"] = QJsonArray{0, 0, 0, 0};
                if (variant == 5)
                    malformed["front"] = QJsonArray{0, "0", 0};
                if (variant == 6)
                    malformed["back"] = QJsonArray{0, -1e-12, 0};
                if (variant == 7)
                    malformed["edge"] = QJsonArray{0, 1 + 1e-12, 0};
                if (variant == 8)
                    malformed["groundVisible"] = 1;
                if (variant == 9)
                    malformed["gridVisible"] = "false";
                if (variant == 10)
                    malformed["profileWidth"] = .5;
                if (variant == 11)
                    malformed["profileWidth"] = 9;
                if (variant == 12)
                    malformed["xrayOpacity"] = 0;
                if (variant == 13)
                    malformed["xrayOpacity"] = 1;
                if (variant == 14)
                    malformed["groundHeight"] = 1000001;
                if (variant == 15)
                    malformed["groundHeight"] = "0";
                if (variant == 16) {
                    malformed.remove("profiles");
                    malformed["unknown"] = true;
                }
                auto badRoot = root;
                badRoot["style"] = malformed;
                rejects([&] { decodeDocument(QJsonDocument(badRoot).toJson()); });
            }
            rejects([&] {
                decodeContainer(rewrite(bytes, [](auto &manifest, auto &) {
                    auto features = manifest["requiredFeatures"].toArray();
                    features.removeAt(features.size() - 1);
                    manifest["requiredFeatures"] = features;
                }));
            });
            rejects([&] {
                decodeContainer(rewrite(bytes, [](auto &manifest, auto &) {
                    auto chunks = manifest["chunks"].toArray();
                    auto chunk = chunks[0].toObject();
                    chunk["encoding"] = "json-v16";
                    chunks[0] = chunk;
                    manifest["chunks"] = chunks;
                }));
            });
            rejects([&] {
                decodeContainer(rewrite(bytes, [](auto &, auto &document) {
                    document["version"] = 16;
                    document.remove("style");
                }));
            });
        }
        for (const auto *fixture : {"texture-sides-v16.sketchyup", "texture-mirror-v16.sketchyup",
                                    "texture-jpeg-v16.sketchyup"}) {
            auto doc = loadDocument(QString(SOURCE_DIR "/tests/fixtures/") + fixture);
            check(doc.style() == ModelStyle{} && !doc.assets().empty(),
                  "Actual v16 image fixture migrates with defaults");
            QFile source(QString(SOURCE_DIR "/tests/fixtures/") + fixture);
            check(source.open(QIODevice::ReadOnly), "Read retained writer bytes");
            const auto bytes = source.readAll();
            const auto length = qFromLittleEndian<quint32>(bytes.constData() + 12);
            const auto manifest = QJsonDocument::fromJson(bytes.mid(16, length)).object();
            const auto chunk = manifest["chunks"].toArray()[0].toObject();
            auto original = QJsonDocument::fromJson(
                                bytes.mid(16 + length, chunk["bytes"].toString().toLongLong()))
                                .object();
            original["version"] = 19;
            original["sections"] = QJsonArray{};
            original["nextSectionId"] = "1";
            original["activeSections"] = QJsonArray{};
            original["scenes"] = QJsonArray{};
            original["nextSceneId"] = "1";
            original["style"] = encodeModelStyle(ModelStyle{});
            check(QJsonDocument::fromJson(encodeDocument(doc, AssetStorage::External)).object() ==
                      original,
                  "Migration adds only version, default style and empty scenes, preserving explicit and implicit "
                  "mappings");
            const auto upgraded = encodeContainer(doc);
            check(encodeContainer(decodeContainer(upgraded)) == upgraded,
                  "Migrated texture fixture is byte stable");
        }
        std::cout
            << "Model style storage, recovery, strict validation and legacy migration passed\n";
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
