#include "io/native_format.hpp"
#include "io/document_io.hpp"
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
constexpr qint64 fileLimit = 128LL * 1024 * 1024;
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
            {"migrationNeeded", !container || tree["version"].toInt() != nativeDocumentVersion},
            {"documentId", QString::fromStdString(doc.identity())},
            {"revision", QString::number(doc.revision())},
            {"bytes", QString::number(bytes.size())},
            {"sha256", hash(bytes)},
            {"units", "m"},
            {"up", "Z"},
            {"bodies", int(doc.bodies().size())},
            {"scenes", int(doc.scenes().size())},
            {"assets", int(doc.assets().size())},
            {"assetBytes", QString::number(assetBytes)},
            {"missingAssets", missing},
            {"requiredFeatures", manifest["requiredFeatures"].toArray()},
            {"chunks", manifest["chunks"].toArray()}};
}
void syncFile(int descriptor, bool published) {
    int status;
    do {
        status = ::fsync(descriptor);
    } while (status < 0 && errno == EINTR);
    if (status != 0)
        throw std::runtime_error(
            published ? "Migrated file published, but directory durability is uncertain"
                      : "Could not synchronize migrated file");
}
void publishNew(const QString &path, const QByteArray &bytes) {
    const QFileInfo info(path);
    if (path.isEmpty() || info.exists() || info.isSymLink() || info.fileName().isEmpty())
        throw std::runtime_error(
            "Migration requires a new output path; existing files are never replaced");
    const auto parent = info.dir().canonicalPath();
    if (parent.isEmpty())
        throw std::runtime_error("Migration output parent must already exist");
    const auto target = QDir(parent).filePath(info.fileName());
    QTemporaryFile temporary(QDir(parent).filePath(".sketchyup-migrate-XXXXXX"));
    if (!temporary.open() || temporary.write(bytes) != bytes.size() || !temporary.flush())
        throw std::runtime_error("Could not write migration copy");
    syncFile(temporary.handle(), false);
    // Hard-link publication is atomic and fails on every pre-existing destination,
    // including a racing writer or dangling symbolic link. Both paths share a parent.
    const auto directory =
        ::open(QFile::encodeName(parent).constData(), O_RDONLY | O_DIRECTORY | O_CLOEXEC);
    if (directory < 0)
        throw std::runtime_error("Could not open migration output directory");
    if (::link(QFile::encodeName(temporary.fileName()).constData(),
               QFile::encodeName(target).constData()) != 0) {
        const auto error = errno;
        ::close(directory);
        throw std::runtime_error(std::string("Could not publish migration copy: ") +
                                 std::strerror(error));
    }
    try {
        syncFile(directory, true);
    } catch (...) {
        ::close(directory);
        throw;
    }
    ::close(directory);
}
} // namespace
QJsonObject inspectNativeFile(const QString &path) {
    const auto bytes = read(path);
    const auto document = decodeContainer(bytes);
    return describe(bytes, document);
}
QJsonObject migrateNativeFile(const QString &input, const QString &output) {
    const auto original = read(input);
    const auto document = decodeContainer(original);
    const auto encoded = encodeContainer(document);
    const auto verified = decodeContainer(encoded);
    if (encodeDocument(verified) != encodeDocument(document))
        throw std::runtime_error("Migration round-trip verification failed");
    auto result = describe(original, document);
    publishNew(output, encoded);
    result["status"] = "migrated";
    result["outputDocumentVersion"] = nativeDocumentVersion;
    result["outputContainerVersion"] = nativeContainerVersion;
    result["outputBytes"] = QString::number(encoded.size());
    result["outputSha256"] = hash(encoded);
    result["sourceUnmodified"] = true;
    return result;
}
QJsonObject nativeFormatCapabilities() {
    return {{"apiVersion", 1},
            {"format", "sketchyup"},
            {"publicSchema", "sketchyup-document-v24"},
            {"documentVersion", nativeDocumentVersion},
            {"containerVersion", nativeContainerVersion},
            {"readDocumentVersions", QJsonArray{1,  2,  3,  4,  5,  6,  7,  8,  9,  10, 11, 12,
                                                13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24}},
            {"fileLimitBytes", QString::number(fileLimit)},
            {"migrationReplacesFiles", false},
            {"unknownRequiredRecords", "reject"},
            {"units", "m"},
            {"up", "Z"}};
}
} // namespace sketchy
