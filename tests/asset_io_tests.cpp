#include "core/materials.hpp"
#include "io/assets.hpp"
#include "io/document_io.hpp"
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>
#include <QtEndian>
#include <iostream>
using namespace sketchy;
void check(bool value, const char *message) {
    if (!value)
        throw std::runtime_error(message);
}
template <class F> void rejects(F operation, const char *message) {
    bool rejected = false;
    try {
        operation();
    } catch (const std::exception &) {
        rejected = true;
    }
    check(rejected, message);
}
QByteArray changedManifest(const QByteArray &bytes,
                           const std::function<void(QJsonObject &)> &edit) {
    const auto length = qFromLittleEndian<quint32>(bytes.constData() + 12);
    auto object = QJsonDocument::fromJson(bytes.mid(16, length)).object();
    edit(object);
    const auto json = QJsonDocument(object).toJson(QJsonDocument::Compact);
    auto result = bytes.first(16);
    qToLittleEndian<quint32>(json.size(), result.data() + 12);
    return result + json + bytes.mid(16 + length);
}
int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    try {
        QTemporaryDir files;
        const auto source = files.filePath("source.png");
        const auto png = QByteArray::fromBase64("iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAQAAAC1HAwCAAAAC0"
                                                "lEQVR42mP8/x8AAwMCAO+jH1sAAAAASUVORK5CYII=");
        QFile file(source);
        check(file.open(QIODevice::WriteOnly) && file.write(png) == png.size(),
              "Write source fixture");
        file.close();
        const auto imported = readAssetFile(source);
        Document doc;
        const auto asset = createAsset(doc, imported.name, imported.mediaType, imported.payload);
        const auto missing = createAsset(doc, "../missing.png", "image/png");
        createMaterial(doc, "Attached image", {1, 1, 1}, 1, asset);
        createMaterial(doc, "Missing image", {1, 1, 1}, 1, missing);
        const auto path = files.filePath("model.sketchyup");
        saveDocument(doc, path);
        QDir(files.path()).mkdir("relocated");
        const auto moved = files.filePath("relocated/copy.sketchyup");
        check(QFile::copy(path, moved) && QFile::remove(source) && QFile::remove(path),
              "Relocate document and remove originals");
        const auto reopened = loadDocument(moved);
        check(assetByteArray(reopened.assets().at(asset)->payload) == png &&
                  !reopened.assets().at(missing)->payload &&
                  reopened.materials().at(2)->asset == missing,
              "Relocated copy owns bytes and preserves missing resource references");
        const auto raw = encodeDocument(doc), packed = encodeContainer(doc);
        check(encodeDocument(decodeDocument(raw)) == raw &&
                  encodeContainer(decodeContainer(packed)) == packed,
              "Inline JSON and separate binary chunks roundtrip exactly");
        const auto manifest = assetManifest(doc);
        check(manifest.size() == 2 && manifest[1].toObject()["path"] == "assets/2.bin" &&
                  manifest[1].toObject()["sha256"].isNull(),
              "Display names never become extraction paths; missing hashes remain null");
        const auto length = qFromLittleEndian<quint32>(packed.constData() + 12);
        const auto root = QJsonDocument::fromJson(packed.mid(16, length)).object();
        check(root["chunks"].toArray().size() == 2,
              "Missing asset has no fabricated payload chunk");
        for (const auto &path :
             {"../escape.bin", "/tmp/escape.bin", "assets/../escape.bin", "C:\\escape.bin"})
            rejects(
                [&] {
                    decodeContainer(changedManifest(packed, [&](auto &manifest) {
                        auto chunks = manifest["chunks"].toArray();
                        auto chunk = chunks[1].toObject();
                        chunk["path"] = path;
                        chunks[1] = chunk;
                        manifest["chunks"] = chunks;
                    }));
                },
                "Imported chunk paths cannot escape their logical namespace");
        for (const auto &key : {"offset", "bytes", "id"})
            rejects(
                [&] {
                    decodeContainer(changedManifest(packed, [&](auto &manifest) {
                        auto chunks = manifest["chunks"].toArray();
                        auto chunk = chunks[1].toObject();
                        chunk[key] = "0";
                        chunks[1] = chunk;
                        manifest["chunks"] = chunks;
                    }));
                },
                "Invalid asset range or identity rejects");
        auto corrupted = packed;
        corrupted.back() ^= 1;
        rejects([&] { decodeContainer(corrupted); }, "Payload checksum detects corruption");
        rejects([&] { decodeContainer(packed + "extra"); }, "Trailing bytes reject");
        rejects([&] { decodeContainer(packed.first(packed.size() - 1)); },
                "Truncated asset rejects");
        rejects(
            [&] {
                decodeContainer(changedManifest(packed, [&](auto &manifest) {
                    auto assets = manifest["assets"].toArray();
                    auto first = assets[0].toObject();
                    first["name"] = "Mismatched";
                    assets[0] = first;
                    manifest["assets"] = assets;
                }));
            },
            "Manifest metadata must agree with document");
        rejects(
            [&] {
                decodeContainer(changedManifest(packed, [&](auto &manifest) {
                    auto chunks = manifest["chunks"].toArray();
                    chunks.append(chunks[1]);
                    manifest["chunks"] = chunks;
                }));
            },
            "Duplicate chunks reject");
        auto malformed = [&](const std::function<void(QJsonObject &)> &edit) {
            auto object = QJsonDocument::fromJson(raw).object();
            edit(object);
            rejects([&] { decodeDocument(QJsonDocument(object).toJson()); },
                    "Malformed inline asset rejected");
        };
        malformed([](auto &root) { root["nextAssetId"] = "1"; });
        malformed([](auto &root) {
            auto assets = root["assets"].toArray();
            auto first = assets[0].toObject();
            first["data"] = "not base64!";
            assets[0] = first;
            root["assets"] = assets;
        });
        malformed([](auto &root) {
            auto assets = root["assets"].toArray();
            auto first = assets[0].toObject();
            first["bytes"] = "1";
            assets[0] = first;
            root["assets"] = assets;
        });
        malformed([](auto &root) { root["assetStorage"] = "external"; });
        const auto external = encodeDocument(Document{}, AssetStorage::External);
        rejects([&] { decodeDocument(external + QByteArray(32 * 1024 * 1024, ' ')); },
                "External model budget applies to direct decoding as well as containers");
        for (const auto &invalid : {"", "AA", "AA==\n", "AA=", "%%%%"})
            rejects([&] { decodeAssetPayload(invalid); },
                    "Only nonempty canonical bounded base64 accepted");
        const auto snapshot = captureSave(doc);
        replaceAsset(doc, asset, assetPayload(QByteArray("new bytes")));
        saveSnapshot(doc, snapshot, files.filePath("snapshot.sketchyup"));
        check(doc.dirty() && assetByteArray(loadDocument(files.filePath("snapshot.sketchyup"))
                                                .assets()
                                                .at(asset)
                                                ->payload) == png,
              "Captured save owns old immutable bytes while document edits continue");
        replaceAsset(doc, missing, imported.payload);
        const auto resolved = decodeContainer(encodeContainer(doc));
        check(assetByteArray(resolved.assets().at(missing)->payload) == png,
              "Resolved missing resource survives packaging with the same ID");
        QFile oversized(files.filePath("oversized.bin"));
        check(oversized.open(QIODevice::WriteOnly) && oversized.resize(AssetPayload::limit + 1),
              "Oversized fixture");
        oversized.close();
        rejects([&] { readAssetFile(oversized.fileName()); },
                "Oversized local import rejects before reading bytes");
        std::cout << "Packaged assets, relocation, missing resources, checksums, safe paths and "
                     "snapshots passed\n";
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
