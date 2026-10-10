#include "io/library_bundle.hpp"
#include "core/components.hpp"
#include "core/scenes.hpp"
#include "io/native_limits.hpp"
#include "io/new_file.hpp"
#include "io/texture_image.hpp"
#include <QCryptographicHash>
#include <QFile>
#include <QJsonDocument>
#include <QtEndian>
#include <set>
namespace sketchy {
namespace {
const QByteArray magic("SKYLIB\0\1", 8);
constexpr qsizetype headerSize = 24, manifestLimit = 32768, modelLimit = NativeLimits::fileBytes,
                    thumbnailLimit = 4 * 1024 * 1024;
void require(bool ok, const char *message) {
    if (!ok)
        throw std::runtime_error(message);
}
QString hash(const QByteArray &bytes) {
    return QString::fromLatin1(QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex());
}
void fields(const QJsonObject &object, const QStringList &names) {
    require(object.size() == names.size(), "Library manifest has missing or extra fields");
    for (const auto &name : names)
        require(object.contains(name), "Library manifest field missing");
}
QString text(const QJsonValue &value, qsizetype maximum, bool multiline = false) {
    require(value.isString(), "Library metadata must be text");
    const auto s = value.toString();
    require(s.toUtf8().size() <= maximum && QString::fromUtf8(s.toUtf8()) == s,
            "Library metadata exceeds UTF-8 limits");
    for (auto c : s)
        require(c.unicode() >= 32 || (multiline && (c == '\n' || c == '\t')),
                "Library metadata contains control characters");
    return s;
}
void validateMetadata(const TemplateMetadata &m, const Document &doc) {
    require(!text(m.name, 256).trimmed().isEmpty(), "Choose a template name");
    text(m.description, 4096, true);
    require(m.labels.size() <= 32, "Template has more than 32 search labels");
    std::set<QString> labels;
    for (const auto &label : m.labels)
        require(!text(label, 64).trimmed().isEmpty() && labels.insert(label.toCaseFolded()).second,
                "Template labels must be nonempty and unique");
    require(!m.defaultScene || doc.scenes().contains(m.defaultScene),
            "Template default scene is missing");
    for (const auto &[id, asset] : doc.assets()) {
        (void)id;
        require(bool(asset->payload),
                "Restore missing embedded assets before saving a template bundle");
    }
    require(doc.readSnapshotBytes() <= 256 * 1024 * 1024, "Template records exceed memory budget");
}
std::pair<int, int> thumbnail(const QByteArray &png) {
    require(png.size() >= 33 && png.size() <= thumbnailLimit &&
                png.startsWith(QByteArray("\x89PNG\r\n\x1a\n", 8)) &&
                qFromBigEndian<quint32>(png.constData() + 8) == 13 && png.mid(12, 4) == "IHDR",
            "Template thumbnail must be a bounded PNG");
    const auto width = qFromBigEndian<quint32>(png.constData() + 16),
               height = qFromBigEndian<quint32>(png.constData() + 20);
    require(width && height && width <= 1024 && height <= 1024,
            "Template thumbnail exceeds 1024 pixels per side");
    AssetRecord asset;
    asset.mediaType = "image/png";
    asset.payload =
        std::make_shared<AssetPayload>(std::vector<std::uint8_t>(png.begin(), png.end()));
    const auto decoded = decodeTextureImage(asset);
    require(decoded.status == TextureImageStatus::Ready,
            "Template thumbnail is not a complete supported static PNG");
    require(decoded.image->width() == int(width) && decoded.image->height() == int(height),
            "Template thumbnail dimensions disagree");
    return {int(width), int(height)};
}
QByteArray encodeBundle(const Document &source, const TemplateMetadata &metadata,
                        const QByteArray &thumbnailPng, Id definition = 0) {
    validateMetadata(metadata, source);
    const auto [width, height] = thumbnail(thumbnailPng);
    const auto model = encodeContainer(source);
    require(model.size() <= modelLimit, "Template native payload exceeds 128 MiB");
    const auto verified = decodeContainer(model);
    validateMetadata(metadata, verified);
    QJsonArray labels;
    for (const auto &label : metadata.labels)
        labels.append(label);
    const auto manifest =
        QJsonDocument(
            QJsonObject{{"bundleVersion", 1},
                        {"kind", definition ? "component" : "template"},
                        {"name", metadata.name},
                        {"description", metadata.description},
                        {"labels", labels},
                        {definition ? "definition" : "defaultScene",
                         QString::number(definition ? definition : metadata.defaultScene)},
                        {"model", QJsonObject{{"bytes", model.size()}, {"sha256", hash(model)}}},
                        {"thumbnail", QJsonObject{{"bytes", thumbnailPng.size()},
                                                  {"sha256", hash(thumbnailPng)},
                                                  {"mediaType", "image/png"},
                                                  {"width", width},
                                                  {"height", height}}}})
            .toJson(QJsonDocument::Compact);
    require(manifest.size() <= manifestLimit, "Template manifest exceeds 32 KiB");
    QByteArray result(headerSize, '\0');
    std::copy(magic.begin(), magic.end(), result.begin());
    qToLittleEndian<quint32>(manifest.size(), result.data() + 8);
    qToLittleEndian<quint64>(model.size(), result.data() + 12);
    qToLittleEndian<quint32>(thumbnailPng.size(), result.data() + 20);
    require(headerSize + manifest.size() + model.size() + thumbnailPng.size() <= libraryBundleLimit,
            "Template bundle exceeds 130 MiB");
    result += manifest;
    result += model;
    result += thumbnailPng;
    return result;
}
TemplateBundle decodeBundle(const QByteArray &bytes, Id *definition = nullptr) {
    require(bytes.size() >= headerSize && bytes.size() <= libraryBundleLimit &&
                bytes.startsWith(magic),
            "Invalid or oversized library bundle");
    const auto manifestSize = qFromLittleEndian<quint32>(bytes.constData() + 8);
    const auto modelSize = qFromLittleEndian<quint64>(bytes.constData() + 12);
    const auto imageSize = qFromLittleEndian<quint32>(bytes.constData() + 20);
    require(manifestSize && manifestSize <= manifestLimit && modelSize && modelSize <= modelLimit &&
                imageSize && imageSize <= thumbnailLimit,
            "Library payload lengths exceed limits");
    require(quint64(bytes.size()) == quint64(headerSize) + manifestSize + modelSize + imageSize,
            "Library bundle length mismatch or trailing data");
    QJsonParseError error;
    const auto parsed = QJsonDocument::fromJson(bytes.mid(headerSize, manifestSize), &error);
    require(error.error == QJsonParseError::NoError && parsed.isObject(),
            "Invalid library manifest JSON");
    const auto m = parsed.object();
    fields(m, {"bundleVersion", "kind", "name", "description", "labels",
               definition ? "definition" : "defaultScene", "model", "thumbnail"});
    require(m["bundleVersion"] == 1 && m["kind"] == (definition ? "component" : "template"),
            "Unsupported library bundle version or kind");
    TemplateMetadata metadata;
    metadata.name = text(m["name"], 256);
    metadata.description = text(m["description"], 4096, true);
    require(m["labels"].isArray() && m["labels"].toArray().size() <= 32, "Invalid template labels");
    for (const auto &label : m["labels"].toArray())
        metadata.labels.append(text(label, 64));
    bool idOk{};
    const auto scene = text(m[definition ? "definition" : "defaultScene"], 20);
    metadata.defaultScene = scene.toULongLong(&idOk);
    require(idOk && QString::number(metadata.defaultScene) == scene,
            "Invalid library scene or component identity");
    if (definition) {
        *definition = metadata.defaultScene;
        metadata.defaultScene = 0;
        require(*definition != 0, "Component definition must be nonzero");
    }
    require(m["model"].isObject() && m["thumbnail"].isObject(),
            "Library payload descriptors must be objects");
    const auto modelInfo = m["model"].toObject(), imageInfo = m["thumbnail"].toObject();
    fields(modelInfo, {"bytes", "sha256"});
    fields(imageInfo, {"bytes", "sha256", "mediaType", "width", "height"});
    const auto model = bytes.mid(headerSize + manifestSize, qsizetype(modelSize));
    const auto png = bytes.mid(headerSize + manifestSize + qsizetype(modelSize), imageSize);
    require(modelInfo["bytes"] == double(modelSize) && modelInfo["sha256"] == hash(model) &&
                imageInfo["bytes"] == double(imageSize) && imageInfo["sha256"] == hash(png) &&
                imageInfo["mediaType"] == "image/png",
            "Library payload hash, size or media type mismatch");
    const auto [width, height] = thumbnail(png);
    require(imageInfo["width"] == width && imageInfo["height"] == height,
            "Library thumbnail dimensions mismatch");
    auto document = decodeContainer(model);
    validateMetadata(metadata, document);
    return {std::move(metadata), std::move(document), png};
}
QByteArray loadBundleBytes(const QString &path) {
    QFile file(path);
    require(file.open(QIODevice::ReadOnly) && file.size() <= libraryBundleLimit,
            "Cannot read bounded library bundle");
    const auto bytes = file.read(libraryBundleLimit + 1);
    require(file.error() == QFileDevice::NoError, "Library bundle read failed");
    return bytes;
}
} // namespace
QByteArray encodeTemplateBundle(const Document &source, const TemplateMetadata &metadata,
                                const QByteArray &thumbnailPng) {
    return encodeBundle(source, metadata, thumbnailPng);
}
TemplateBundle decodeTemplateBundle(const QByteArray &bytes) { return decodeBundle(bytes); }
TemplateBundle loadTemplateBundle(const QString &path) {
    return decodeTemplateBundle(loadBundleBytes(path));
}
void writeTemplateBundle(const QByteArray &bytes, const QString &newPath) {
    (void)decodeTemplateBundle(bytes);
    publishNewFile(newPath, bytes);
}
Document instantiateTemplate(const TemplateBundle &bundle) {
    validateMetadata(bundle.metadata, bundle.document);
    auto defaults = bundle.document.readSnapshot();
    if (bundle.metadata.defaultScene)
        recallSceneModel(defaults, bundle.metadata.defaultScene);
    const auto &d = defaults;
    Document result;
    result.restore(result.identity(), d.nextId(), d.bodies(), 0, d.definitions(), d.instances(),
                   d.nextDefinitionId(), d.tags(), d.nextTagId(), d.materials(), d.nextMaterialId(),
                   d.assets(), d.nextAssetId(), d.displayUnits(), d.hostedRecords(), d.style(),
                   d.scenes(), d.nextSceneId(), d.sections(), d.nextSectionId(), d.activeSections(),
                   d.annotations(), d.nextAnnotationId(), d.solar(), d.displayPrecision());
    result.markRecovered();
    return result;
}
QByteArray encodeComponentBundle(const Document &source, Id definition,
                                 const TemplateMetadata &metadata, const QByteArray &thumbnailPng) {
    require(!metadata.defaultScene, "Component bundles do not contain default scenes");
    const auto captured = captureLibraryComponent(source, definition);
    return encodeBundle(captured, metadata, thumbnailPng, definition);
}
ComponentBundle decodeComponentBundle(const QByteArray &bytes) {
    Id definition{};
    auto bundle = decodeBundle(bytes, &definition);
    const auto &d = bundle.document;
    const auto captured = captureLibraryComponent(d, definition);
    require(d.definitions().size() == captured.definitions().size() &&
                d.tags().size() == captured.tags().size() &&
                d.materials().size() == captured.materials().size() &&
                d.assets().size() == captured.assets().size() &&
                d.bodies().size() == captured.bodies().size() &&
                d.instances().size() == captured.instances().size() && d.scenes().empty() &&
                d.sections().empty() && d.annotations().empty() &&
                d.hostedComponents().hosts.empty() && d.hostedComponents().attachments.empty(),
            "Component bundle contains unrelated records");
    size_t roots{};
    for (const auto &[id, body] : d.bodies()) {
        if (body->parent)
            continue;
        ++roots;
        require(d.instances().contains(id) && d.instances().at(id)->definition == definition &&
                    body->transform == Transform{},
                "Component bundle root placement is invalid");
    }
    require(roots == 1, "Component bundle must contain one root instance");
    return {std::move(bundle.metadata), std::move(bundle.document), definition,
            std::move(bundle.thumbnailPng)};
}
ComponentBundle loadComponentBundle(const QString &path) {
    return decodeComponentBundle(loadBundleBytes(path));
}
void writeComponentBundle(const QByteArray &bytes, const QString &newPath) {
    (void)decodeComponentBundle(bytes);
    publishNewFile(newPath, bytes);
}
} // namespace sketchy
