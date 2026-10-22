#include "io/native_format.hpp"
#include "core/component_parameter_records.hpp"
#include "io/document_io.hpp"
#include "io/native_limits.hpp"
#include "io/new_file.hpp"
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QTemporaryFile>
#include <QtEndian>
#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <unistd.h>
namespace sketchy {
namespace {
constexpr qint64 fileLimit = NativeLimits::fileBytes;
QString hash(const QByteArray &bytes) {
    return QString::fromLatin1(QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex());
}
QByteArray read(const QString &path) {
    QFile file(path);
    if (!QFileInfo(path).isFile() || !file.open(QIODevice::ReadOnly) || file.size() > fileLimit)
        throw std::runtime_error("Native inspection requires a regular file of at most 128 MiB");
    const auto bytes = file.read(fileLimit + 1);
    if (file.error() != QFileDevice::NoError || bytes.size() > fileLimit)
        throw std::runtime_error("Could not read bounded native document");
    return bytes;
}
QJsonObject describe(const QByteArray &bytes, const Document &doc) {
    const bool container = bytes.startsWith(QByteArray("SKUPDOC\0", 8));
    QJsonObject manifest, tree;
    if (container) {
        const auto length = qFromLittleEndian<quint32>(bytes.constData() + 12);
        manifest = QJsonDocument::fromJson(bytes.mid(16, length)).object();
        const auto chunk = manifest["chunks"].toArray().first().toObject();
        tree =
            QJsonDocument::fromJson(bytes.mid(16 + length, chunk["bytes"].toString().toLongLong()))
                .object();
    } else {
        tree = QJsonDocument::fromJson(bytes).object();
    }
    qint64 assetBytes{};
    int missing{};
    int parametric{}, drivers{}, overrides{};
    for (const auto &[id, definition] : doc.definitions())
        if (definition->parameters) {
            ++parametric;
            drivers += int(definition->parameters->drivers.size());
        }
    for (const auto &[root, instance] : doc.instances())
        overrides += instance->overrides.empty() ? 0 : 1;
    for (const auto &[id, asset] : doc.assets()) {
        missing += !asset->payload;
        if (asset->payload)
            assetBytes += qint64(asset->payload->bytes().size());
    }
    return {{"apiVersion", 1},
            {"valid", true},
            {"format", "sketchyup"},
            {"storage", container ? "container" : "json"},
            {"containerVersion", container ? nativeContainerVersion : 0},
            {"documentVersion", tree["version"]},
            {"currentDocumentVersion", nativeDocumentVersion},
            // Sheet-free documents are current at schema 26, parameter-free ones at 27 and
            // terrain-free ones at 28 (ADR 0163 decision 2, ADR 0165 §5, ADR 0168 §2).
            {"migrationNeeded",
             !container || tree["version"].toInt() != nativeDocumentVersionFor(doc)},
            {"documentId", QString::fromStdString(doc.identity())},
            {"revision", QString::number(doc.revision())},
            {"bytes", QString::number(bytes.size())},
            {"sha256", hash(bytes)},
            {"units", "m"},
            {"up", "Z"},
            {"bodies", int(doc.bodies().size())},
            {"scenes", int(doc.scenes().size())},
            {"drawingSheets", int(doc.drawingSheets().size())},
            {"drawingViews", int(doc.drawingViews().size())},
            {"drawingItems", int(doc.drawingItems().size())},
            {"componentParameters", parametric},
            {"componentDrivers", drivers},
            {"componentParameterOverrides", overrides},
            {"assets", int(doc.assets().size())},
            {"assetBytes", QString::number(assetBytes)},
            {"missingAssets", missing},
            {"requiredFeatures", manifest["requiredFeatures"].toArray()},
            {"chunks", manifest["chunks"].toArray()}};
}
} // namespace
QJsonObject inspectNativeFile(const QString &path) {
    const auto bytes = read(path);
    const auto document = decodeContainer(bytes);
    return describe(bytes, document);
}
QJsonObject createNativeFile(const Document &document, const QString &output) {
    const auto encoded = encodeContainer(document);
    if (encodeDocument(decodeContainer(encoded)) != encodeDocument(document))
        throw std::runtime_error("Native round-trip verification failed");
    auto result = describe(encoded, document);
    publishNewFile(output, encoded);
    result["status"] = "created";
    return result;
}
QJsonObject migrateNativeFile(const QString &input, const QString &output) {
    const auto original = read(input);
    const auto document = decodeContainer(original);
    const auto encoded = encodeContainer(document);
    const auto verified = decodeContainer(encoded);
    if (encodeDocument(verified) != encodeDocument(document))
        throw std::runtime_error("Migration round-trip verification failed");
    auto result = describe(original, document);
    publishNewFile(output, encoded);
    result["status"] = "migrated";
    result["outputDocumentVersion"] = nativeDocumentVersionFor(document);
    result["outputContainerVersion"] = nativeContainerVersion;
    result["outputBytes"] = QString::number(encoded.size());
    result["outputSha256"] = hash(encoded);
    result["sourceUnmodified"] = true;
    return result;
}
QJsonObject nativeFormatCapabilities() {
    return {{"apiVersion", 1},
            {"format", "sketchyup"},
            {"publicSchema", "sketchyup-document-v29"},
            {"documentVersion", nativeDocumentVersion},
            {"containerVersion", nativeContainerVersion},
            {"readDocumentVersions", QJsonArray{1,  2,  3,  4,  5,  6,  7,  8,  9,  10, 11, 12,
                                                13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29}},
            {"fileLimitBytes", QString::number(fileLimit)},
            {"migrationReplacesFiles", false},
            {"unknownRequiredRecords", "reject"},
            {"units", "m"},
            {"up", "Z"}};
}
} // namespace sketchy
