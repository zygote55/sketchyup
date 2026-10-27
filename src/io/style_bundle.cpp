#include "io/style_bundle.hpp"
#include "io/assets.hpp"
#include "io/model_style_io.hpp"
#include "io/new_file.hpp"
#include "io/texture_image.hpp"
#include <QCryptographicHash>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QStandardPaths>
#include <QtEndian>
#include <algorithm>
#include <set>

namespace sketchy {
namespace {

const QByteArray magic("SKYLIB\0\1", 8);
constexpr qsizetype headerSize = 24;
constexpr qsizetype scanByteLimit = 256 * 1024 * 1024;

void require(bool ok, const char *message) {
    if (!ok)
        throw std::runtime_error(message);
}

QString hash(const QByteArray &bytes) {
    return QString::fromLatin1(QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex());
}

// The text rules of the library manifest (ADR 0129): valid UTF-8 within a byte limit, and no
// control characters except tab and newline when the text is multiline.
QString text(const QJsonValue &value, qsizetype maximum, bool multiline = false) {
    require(value.isString(), "Style metadata must be text");
    const auto s = value.toString();
    require(s.toUtf8().size() <= maximum && QString::fromUtf8(s.toUtf8()) == s,
            "Style metadata exceeds UTF-8 limits");
    for (auto c : s)
        require(c.unicode() >= 32 || (multiline && (c == '\n' || c == '\t')),
                "Style metadata contains control characters");
    return s;
}

void validateMetadata(const StyleMetadata &metadata) {
    require(!text(metadata.name, 256).trimmed().isEmpty(), "Choose a style name");
    text(metadata.description, 4096, true);
    require(metadata.labels.size() <= 32, "Style has more than 32 search labels");
    std::set<QString> labels;
    for (const auto &label : metadata.labels)
        require(!text(label, 64).trimmed().isEmpty() && labels.insert(label.toCaseFolded()).second,
                "Style labels must be nonempty and unique");
}

void requireFields(const QJsonObject &object, const QStringList &names, const char *what) {
    require(object.size() == names.size(), what);
    for (const auto &name : names)
        require(object.contains(name), what);
}

// The envelope accepts an edge texture only as one of these two, the formats the texture decoder
// reads. Anything else is refused before it can be stored.
void requireTextureMediaType(const std::string &mediaType) {
    require(mediaType == "image/png" || mediaType == "image/jpeg",
            "Style edge texture must be a PNG or JPEG image");
}

// A texture must decode as a complete supported image within the texture bounds. Decoding is the
// only way to know the bytes are usable, so a style is never stored with an undecodable texture.
void requireDecodableTexture(const AssetRecord &texture) {
    requireTextureMediaType(texture.mediaType);
    const auto decoded = decodeTextureImage(texture);
    require(decoded.status == TextureImageStatus::Ready,
            "Style edge texture is not a complete supported PNG or JPEG within the texture limits");
}

QByteArray fileBytes(const QString &path, qsizetype limit) {
    QFile file(path);
    require(file.open(QIODevice::ReadOnly) && file.size() <= limit,
            "Cannot read bounded style file");
    const auto bytes = file.read(limit + 1);
    require(file.error() == QFileDevice::NoError && bytes.size() <= limit,
            "Style file read failed or exceeded its limit");
    return bytes;
}

} // namespace

std::string_view styleTextureStatusCode(StyleTextureStatus status) {
    switch (status) {
    case StyleTextureStatus::None:
        return "none";
    case StyleTextureStatus::Present:
        return "present";
    case StyleTextureStatus::Missing:
        return "missing";
    case StyleTextureStatus::HashMismatch:
        return "hashMismatch";
    }
    throw std::runtime_error("Unknown style texture status");
}

QByteArray encodeStyleBundle(const StyleBundle &bundle) {
    validateMetadata(bundle.metadata);
    bundle.style.validate();
    const auto &texture = bundle.texture;
    const auto referenced = bundle.style.edgeStroke ? bundle.style.edgeStroke->texture : Id{0};
    QJsonValue descriptor = QJsonValue::Null;
    QByteArray payload;
    if (referenced) {
        require(texture.id == referenced && texture.payload &&
                    bundle.textureStatus == StyleTextureStatus::Present,
                "Style edge texture must be present to write the style file");
        requireDecodableTexture(texture);
        payload = assetByteArray(texture.payload);
        descriptor = QJsonObject{{"id", QString::number(texture.id)},
                                 {"name", QString::fromStdString(texture.name)},
                                 {"mediaType", QString::fromStdString(texture.mediaType)},
                                 {"missing", false},
                                 {"bytes", payload.size()},
                                 {"sha256", hash(payload)}};
    } else
        require(texture.id == 0 && bundle.textureStatus == StyleTextureStatus::None,
                "A style without an edge texture cannot carry one");
    QJsonArray labels;
    for (const auto &label : bundle.metadata.labels)
        labels.append(label);
    const auto manifest = QJsonDocument(QJsonObject{{"bundleVersion", 1},
                                                    {"kind", "style"},
                                                    {"name", bundle.metadata.name},
                                                    {"description", bundle.metadata.description},
                                                    {"labels", labels},
                                                    {"style", encodeModelStyle(bundle.style)},
                                                    {"texture", descriptor}})
                              .toJson(QJsonDocument::Compact);
    require(manifest.size() <= styleManifestLimit, "Style manifest exceeds 32 KiB");
    QByteArray result(headerSize, '\0');
    std::copy(magic.begin(), magic.end(), result.begin());
    qToLittleEndian<quint32>(manifest.size(), result.data() + 8);
    qToLittleEndian<quint64>(payload.size(), result.data() + 12);
    qToLittleEndian<quint32>(0, result.data() + 20);
    result += manifest;
    result += payload;
    require(result.size() <= styleFileLimit, "Style file exceeds its size limit");
    return result;
}

StyleBundle decodeStyleBundle(const QByteArray &bytes) {
    require(bytes.size() >= headerSize && bytes.size() <= styleFileLimit && bytes.startsWith(magic),
            "Invalid or oversized style file");
    const auto manifestSize = qFromLittleEndian<quint32>(bytes.constData() + 8);
    const auto payloadSize = qFromLittleEndian<quint64>(bytes.constData() + 12);
    const auto thumbnailSize = qFromLittleEndian<quint32>(bytes.constData() + 20);
    require(manifestSize && manifestSize <= styleManifestLimit &&
                payloadSize <= AssetPayload::limit && thumbnailSize == 0,
            "Style payload lengths exceed limits or carry a thumbnail");
    require(quint64(bytes.size()) == quint64(headerSize) + manifestSize + payloadSize,
            "Style file length mismatch or trailing data");
    QJsonParseError error;
    const auto parsed = QJsonDocument::fromJson(bytes.mid(headerSize, manifestSize), &error);
    require(error.error == QJsonParseError::NoError && parsed.isObject(),
            "Invalid style manifest JSON");
    const auto m = parsed.object();
    requireFields(m, {"bundleVersion", "kind", "name", "description", "labels", "style", "texture"},
                  "Style manifest has missing or extra fields");
    require(m["bundleVersion"] == 1 && m["kind"] == "style",
            "Unsupported style bundle version or kind");
    StyleBundle bundle;
    bundle.metadata.name = text(m["name"], 256);
    bundle.metadata.description = text(m["description"], 4096, true);
    require(m["labels"].isArray() && m["labels"].toArray().size() <= 32, "Invalid style labels");
    for (const auto &label : m["labels"].toArray())
        bundle.metadata.labels.append(text(label, 64));
    validateMetadata(bundle.metadata);
    bundle.style = decodeModelStyle(m["style"], true);
    const auto payload = bytes.mid(headerSize + manifestSize, qsizetype(payloadSize));
    const auto referenced = bundle.style.edgeStroke ? bundle.style.edgeStroke->texture : Id{0};
    if (m["texture"].isNull()) {
        require(!referenced && payloadSize == 0,
                "Style names an edge texture that the file does not describe");
        return bundle;
    }
    require(m["texture"].isObject(), "Style texture descriptor must be an object");
    const auto descriptor = m["texture"].toObject();
    requireFields(descriptor, {"id", "name", "mediaType", "missing", "bytes", "sha256"},
                  "Style texture descriptor has missing or extra fields");
    bool idOk{};
    const auto idText = text(descriptor["id"], 20);
    const auto id = idText.toULongLong(&idOk);
    require(idOk && id && QString::number(id) == idText && id == referenced,
            "Style texture identity does not match the style");
    bundle.texture.id = id;
    bundle.texture.name = text(descriptor["name"], 1024).toStdString();
    bundle.texture.mediaType = text(descriptor["mediaType"], 128).toStdString();
    requireTextureMediaType(bundle.texture.mediaType);
    require(!bundle.texture.name.empty() && descriptor["missing"].isBool(),
            "Style texture descriptor is invalid");
    if (descriptor["missing"].toBool()) {
        require(payloadSize == 0 && descriptor["bytes"] == 0 && descriptor["sha256"].isNull(),
                "A missing style texture carries no bytes");
        bundle.textureStatus = StyleTextureStatus::Missing;
        return bundle;
    }
    require(descriptor["bytes"] == double(payloadSize) && payloadSize > 0 &&
                descriptor["sha256"].isString(),
            "Style texture size or hash descriptor is invalid");
    if (descriptor["sha256"] != hash(payload)) {
        bundle.textureStatus = StyleTextureStatus::HashMismatch;
        return bundle;
    }
    bundle.texture.payload = assetPayload(payload);
    requireDecodableTexture(bundle.texture);
    bundle.textureStatus = StyleTextureStatus::Present;
    return bundle;
}

StyleBundle loadStyleBundle(const QString &path) {
    return decodeStyleBundle(fileBytes(path, styleFileLimit));
}

void writeStyleBundle(const QByteArray &bytes, const QString &newPath) {
    (void)decodeStyleBundle(bytes);
    publishNewFile(newPath, bytes);
}

QByteArray exportStyleBundle(const Document &doc, const StyleMetadata &metadata) {
    StyleBundle bundle{metadata, doc.style(), {}, StyleTextureStatus::None};
    if (doc.style().edgeStroke && doc.style().edgeStroke->texture) {
        const auto id = doc.style().edgeStroke->texture;
        const auto &asset = doc.assets().at(id);
        require(bool(asset->payload),
                "Edge texture is missing; the style cannot be exported without it");
        bundle.texture = *asset;
        bundle.textureStatus = StyleTextureStatus::Present;
    }
    const auto bytes = encodeStyleBundle(bundle);
    const auto verified = decodeStyleBundle(bytes);
    require(verified.textureStatus == bundle.textureStatus && verified.style == bundle.style &&
                encodeStyleBundle(verified) == bytes,
            "Style round-trip verification failed");
    return bytes;
}

QString styleLibraryFolder() {
    const auto root = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    return root.isEmpty() ? QString() : QDir(root).filePath(QStringLiteral("styles"));
}

QString sketchyLibraryKind(const QByteArray &bytes) {
    if (bytes.size() < headerSize || !bytes.startsWith(magic))
        return {};
    const auto manifestSize = qFromLittleEndian<quint32>(bytes.constData() + 8);
    if (!manifestSize || manifestSize > quint64(bytes.size() - headerSize))
        return {};
    const auto parsed = QJsonDocument::fromJson(bytes.mid(headerSize, manifestSize));
    return parsed.isObject() ? parsed.object()["kind"].toString() : QString{};
}

StyleLibrary scanStyleDirectory(const QString &directory) {
    const QFileInfo info(directory);
    require(info.isDir() && info.isReadable(), "Choose a readable local style folder");
    StyleLibrary result;
    qsizetype readBytes{};
    size_t visited{};
    QDirIterator files(info.absoluteFilePath(), QDir::Files | QDir::NoDotAndDotDot);
    while (files.hasNext()) {
        files.next();
        if (++visited > styleScanEntryLimit) {
            result.stoppedAtEntries = true;
            break;
        }
        const auto fileInfo = files.fileInfo();
        if (fileInfo.suffix().compare("sketchystyle", Qt::CaseInsensitive) != 0) {
            ++result.skippedNotStyles;
            continue;
        }
        if (result.entries.size() == styleScanCandidateLimit) {
            ++result.skippedPastCandidates;
            continue;
        }
        StyleLibraryEntry entry;
        entry.path = fileInfo.absoluteFilePath();
        entry.metadata.name = fileInfo.completeBaseName().left(256);
        try {
            require(!fileInfo.isSymLink() && fileInfo.isFile(),
                    "Style entries must be regular files, not links or folders");
            require(fileInfo.size() >= headerSize && fileInfo.size() <= styleFileLimit,
                    "Style file exceeds size limits");
            require(fileInfo.size() <= scanByteLimit - readBytes,
                    "Style scan reached its 256 MiB read budget. Use a smaller folder.");
            const auto bytes = fileBytes(entry.path, styleFileLimit);
            readBytes += bytes.size();
            const auto bundle = decodeStyleBundle(bytes);
            entry.metadata = bundle.metadata;
            entry.style = bundle.style;
            entry.textureStatus = bundle.textureStatus;
        } catch (const std::exception &error) {
            entry.error = QString::fromUtf8(error.what()).left(512);
        }
        result.entries.push_back(std::move(entry));
    }
    auto count = [](size_t n, const char *one, const char *many) {
        return QString::number(qulonglong(n)) + " " + (n == 1 ? one : many);
    };
    if (result.stoppedAtEntries)
        result.notices.append("Stopped after 4,096 folder entries. Files after them were not "
                              "read. Use a smaller style folder.");
    if (result.skippedNotStyles)
        result.notices.append("Skipped " +
                              count(result.skippedNotStyles, "file that is not a style file",
                                    "files that are not style files") +
                              ".");
    if (result.skippedPastCandidates)
        result.notices.append("Skipped " +
                              count(result.skippedPastCandidates, "style past the 128 listed",
                                    "styles past the 128 listed") +
                              ". Split this folder into smaller ones.");
    std::sort(result.entries.begin(), result.entries.end(), [](const auto &a, const auto &b) {
        const auto comparison =
            QString::compare(a.metadata.name, b.metadata.name, Qt::CaseInsensitive);
        return comparison ? comparison < 0 : a.path < b.path;
    });
    return result;
}

} // namespace sketchy
