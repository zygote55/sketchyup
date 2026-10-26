#include "io/catalog_io.hpp"
#include "io/library_bundle.hpp"
#include "io/library_envelope.hpp"
#include "io/model_style_io.hpp"
#include "io/new_file.hpp"
#include <QCryptographicHash>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QRegularExpression>
#include <QtEndian>
#include <algorithm>
#include <cmath>
#include <limits>
#include <set>
namespace sketchy {
namespace {
using namespace library_envelope;
const QRegularExpression catalogIdPattern("^[A-Za-z0-9._-]{1,64}$");
const QRegularExpression mediaTypePattern("^[a-z0-9][a-z0-9.+-]*/[a-z0-9][a-z0-9.+-]*$");
const QRegularExpression digestPattern("^[0-9a-f]{64}$");
QString nonemptyText(const QString &value, std::size_t maximum) {
    const auto checked = text(QJsonValue(value), qsizetype(maximum));
    require(!checked.trimmed().isEmpty(), "Catalog text must not be empty");
    return checked;
}
void validateMetadata(const CatalogMetadata &m) {
    require(catalogIdPattern.match(m.catalogId).hasMatch(),
            "Catalog id must be 1 to 64 letters, digits, dots, underscores or hyphens");
    nonemptyText(m.name, CatalogLimits::nameBytes);
    text(QJsonValue(m.description), qsizetype(CatalogLimits::descriptionBytes), true);
    text(QJsonValue(m.author), qsizetype(CatalogLimits::authorBytes));
    text(QJsonValue(m.licenseText), qsizetype(CatalogLimits::licenseTextBytes), true);
    nonemptyText(m.licenseIdentifier, CatalogLimits::licenseIdentifierBytes);
    nonemptyText(m.version, CatalogLimits::versionBytes);
    require(m.labels.size() <= qsizetype(CatalogLimits::labels),
            "Catalog has more than 32 search labels");
    std::set<QString> labels;
    for (const auto &label : m.labels)
        require(labels.insert(nonemptyText(label, CatalogLimits::labelBytes).toCaseFolded()).second,
                "Catalog labels must be nonempty and unique");
}
void validateAsset(const QString &name, const QString &mediaType, qsizetype size) {
    nonemptyText(name, CatalogLimits::assetNameBytes);
    require(mediaType.toUtf8().size() <= qsizetype(CatalogLimits::mediaTypeBytes) &&
                mediaTypePattern.match(mediaType).hasMatch(),
            "Catalog asset media type must be lowercase type/subtype");
    require(size >= 1 && std::size_t(size) <= CatalogLimits::assetBytes,
            "Catalog asset must be 1 byte to 16 MiB");
}
void requireUnitInterval(float value, const char *message) {
    require(std::isfinite(value) && value >= 0 && value <= 1, message);
}
qsizetype wholeNumber(const QJsonValue &value, qsizetype maximum, const char *message) {
    require(value.isDouble(), message);
    const auto number = value.toDouble();
    require(number == std::floor(number) && number >= 0 && number <= double(maximum), message);
    return qsizetype(number);
}
std::uint32_t referenceId(const QJsonValue &value) {
    const auto id = wholeNumber(value, std::numeric_limits<std::uint32_t>::max(),
                                "Catalog ids must be positive integers");
    require(id >= 1, "Catalog ids must be positive integers");
    return std::uint32_t(id);
}
QString digestText(const QJsonValue &value) {
    require(value.isString() && digestPattern.match(value.toString()).hasMatch(),
            "Catalog SHA-256 must be 64 lowercase hex digits");
    return value.toString();
}
QString digestOf(QByteArrayView bytes) {
    return QString::fromLatin1(QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex());
}
CatalogEntryKind kindFromName(const QJsonValue &value) {
    require(value.isString(), "Catalog entry kind is missing");
    const auto name = value.toString();
    if (name == "component")
        return CatalogEntryKind::Component;
    if (name == "template")
        return CatalogEntryKind::Template;
    if (name == "style")
        return CatalogEntryKind::Style;
    if (name == "material")
        return CatalogEntryKind::Material;
    throw std::runtime_error("Unsupported catalog entry kind");
}
// A material record must declare exactly the asset it names, and no other dependency.
void requireMaterialRecord(const QByteArray &payload, const std::vector<CatalogDependency> &deps) {
    QJsonParseError error;
    const auto parsed = QJsonDocument::fromJson(payload, &error);
    require(error.error == QJsonParseError::NoError && parsed.isObject(),
            "material record is not a JSON object");
    const auto record = parsed.object();
    fields(record, {"name", "color", "opacity", "asset"});
    nonemptyText(text(record["name"], qsizetype(CatalogLimits::materialNameBytes)),
                 CatalogLimits::materialNameBytes);
    require(record["color"].isArray() && record["color"].toArray().size() == 3,
            "material colour must have three channels");
    for (const auto &channel : record["color"].toArray())
        requireUnitInterval(float(channel.toDouble()), "material colour must be from zero to one");
    requireUnitInterval(float(record["opacity"].toDouble()),
                        "material opacity must be from zero to one");
    const auto asset =
        std::uint32_t(wholeNumber(record["asset"], std::numeric_limits<std::uint32_t>::max(),
                                  "material asset must be an id or zero"));
    std::vector<CatalogDependency> assets;
    for (const auto &dependency : deps)
        if (dependency.kind == CatalogDependency::Kind::Asset)
            assets.push_back(dependency);
    const bool matches =
        asset == 0 ? deps.empty() : deps.size() == 1 && assets.size() == 1 && assets[0].id == asset;
    require(matches, "material dependencies do not match its asset");
}
struct EntryFailure {
    QString reason;
    std::optional<CatalogDependency> missing;
};
// The first reason an entry is not importable, in the order: its payload hash, its declared
// dependencies, then its payload decode. Earlier entries are already checked, so entry
// dependencies form no cycle (a dependency must name an entry with a lower id).
EntryFailure entryFailure(const std::vector<CatalogEntryReport> &entries, std::size_t index,
                          const std::vector<CatalogAssetReport> &assets, QByteArrayView region) {
    const auto &entry = entries[index];
    if (digestOf(region.sliced(entry.offset, entry.bytes)) != entry.sha256)
        return {QStringLiteral("entry hash does not match its bytes"), {}};
    for (const auto &dependency : entry.dependencies) {
        if (dependency.kind == CatalogDependency::Kind::Asset) {
            if (dependency.id > assets.size())
                return {QStringLiteral("missing dependency asset %1").arg(dependency.id),
                        dependency};
            if (!assets[dependency.id - 1].verified)
                return {QStringLiteral("dependency asset %1 does not match its hash")
                            .arg(dependency.id),
                        dependency};
        } else {
            if (dependency.id >= entry.id)
                return {dependency.id > entries.size()
                            ? QStringLiteral("missing dependency entry %1").arg(dependency.id)
                            : QStringLiteral("dependency entry %1 is not earlier in the catalog")
                                  .arg(dependency.id),
                        dependency};
            if (!entries[dependency.id - 1].importable)
                return {QStringLiteral("dependency entry %1 is not importable").arg(dependency.id),
                        dependency};
        }
    }
    try {
        const auto payload = region.sliced(entry.offset, entry.bytes).toByteArray();
        switch (entry.kind) {
        case CatalogEntryKind::Style: {
            QJsonParseError error;
            const auto parsed = QJsonDocument::fromJson(payload, &error);
            require(error.error == QJsonParseError::NoError && parsed.isObject(),
                    "style record is not a JSON object");
            decodeModelStyle(parsed.object());
            break;
        }
        case CatalogEntryKind::Material:
            requireMaterialRecord(payload, entry.dependencies);
            break;
        case CatalogEntryKind::Template:
            (void)decodeContainer(payload);
            break;
        case CatalogEntryKind::Component: {
            bool ok{};
            const auto definition = entry.definition.toULongLong(&ok);
            const auto container = decodeContainer(payload);
            require(ok && definition != 0 && container.definitions().contains(definition),
                    "component definition is absent from its payload");
            break;
        }
        }
    } catch (const std::exception &error) {
        return {QStringLiteral("%1 payload is invalid: %2")
                    .arg(catalogKindName(entry.kind), QString::fromUtf8(error.what())),
                {}};
    }
    return {};
}
QByteArray loadCatalogBytes(const QString &path) {
    QFile file(path);
    require(file.open(QIODevice::ReadOnly) && file.size() <= qint64(libraryBundleLimit),
            "Cannot read bounded catalog");
    const auto bytes = file.read(libraryBundleLimit + 1);
    require(file.error() == QFileDevice::NoError && bytes.size() <= libraryBundleLimit,
            "Catalog read failed");
    return bytes;
}
qsizetype payloadStart(const QByteArray &file) {
    return headerSize + qFromLittleEndian<quint32>(file.constData() + 8);
}
} // namespace
QString catalogKindName(CatalogEntryKind kind) {
    switch (kind) {
    case CatalogEntryKind::Component:
        return QStringLiteral("component");
    case CatalogEntryKind::Template:
        return QStringLiteral("template");
    case CatalogEntryKind::Style:
        return QStringLiteral("style");
    case CatalogEntryKind::Material:
        return QStringLiteral("material");
    }
    throw std::runtime_error("Unknown catalog entry kind");
}
CatalogBuilder::CatalogBuilder(CatalogMetadata metadata) : metadata_(std::move(metadata)) {
    validateMetadata(metadata_);
}
std::uint32_t CatalogBuilder::addAsset(const QString &name, const QString &mediaType,
                                       const QByteArray &bytes) {
    validateAsset(name, mediaType, bytes.size());
    // Sharing comes before the count and budget checks: a reused asset adds nothing to either.
    const auto digest = QCryptographicHash::hash(bytes, QCryptographicHash::Sha256);
    auto &candidates = assetsByHash_[digest];
    for (const auto id : candidates)
        if (assets_[id - 1].mediaType == mediaType && assets_[id - 1].bytes == bytes)
            return id;
    require(assets_.size() < CatalogLimits::assets, "Catalog has more than 256 assets");
    qsizetype total = bytes.size();
    for (const auto &asset : assets_)
        total += asset.bytes.size();
    require(std::size_t(total) <= CatalogLimits::assetTotalBytes,
            "Catalog assets exceed 64 MiB together");
    assets_.push_back({name, mediaType, bytes, digest});
    const auto id = std::uint32_t(assets_.size());
    candidates.push_back(id);
    return id;
}
std::uint32_t CatalogBuilder::addEntry(EntryDraft entry) {
    require(entries_.size() < CatalogLimits::entries, "Catalog has more than 128 entries");
    nonemptyText(entry.name, CatalogLimits::nameBytes);
    text(QJsonValue(entry.description), qsizetype(CatalogLimits::descriptionBytes), true);
    const bool record =
        entry.kind == CatalogEntryKind::Style || entry.kind == CatalogEntryKind::Material;
    require(!entry.payload.isEmpty() &&
                std::size_t(entry.payload.size()) <=
                    (record ? CatalogLimits::recordBytes : CatalogLimits::payloadBytes),
            record ? "Catalog style and material records are limited to 16 KiB"
                   : "Catalog payload is empty or exceeds 128 MiB");
    const auto &deps = entry.dependencies;
    require(deps.size() <= CatalogLimits::dependencies,
            "Catalog entry has more than 64 dependencies");
    for (std::size_t i = 0; i < deps.size(); ++i) {
        if (deps[i].kind == CatalogDependency::Kind::Asset)
            require(deps[i].id >= 1 && deps[i].id <= assets_.size(),
                    "Catalog dependency names an asset that is not in the catalog");
        else
            require(deps[i].id >= 1 && deps[i].id <= entries_.size(),
                    "Catalog dependency names an entry that is not earlier in the catalog");
        require(std::find(deps.begin(), deps.begin() + qsizetype(i), deps[i]) ==
                    deps.begin() + qsizetype(i),
                "Catalog entry repeats a dependency");
    }
    entries_.push_back(std::move(entry));
    return std::uint32_t(entries_.size());
}
std::uint32_t CatalogBuilder::addStyle(const QString &name, const QString &description,
                                       const ModelStyle &style,
                                       const std::vector<CatalogDependency> &dependencies) {
    return addEntry({CatalogEntryKind::Style,
                     name,
                     description,
                     {},
                     dependencies,
                     QJsonDocument(encodeModelStyle(style)).toJson(QJsonDocument::Compact)});
}
std::uint32_t CatalogBuilder::addMaterial(const QString &name, const QString &description,
                                          const CatalogMaterial &material) {
    const auto title =
        nonemptyText(QString::fromStdString(material.name), CatalogLimits::materialNameBytes);
    for (const auto channel : material.color)
        requireUnitInterval(channel, "Catalog material colour channels must be from zero to one");
    requireUnitInterval(material.opacity, "Catalog material opacity must be from zero to one");
    require(material.asset <= assets_.size(),
            "Catalog material names an asset that is not in the catalog");
    const QJsonObject record{
        {"name", title},
        {"color", QJsonArray{material.color[0], material.color[1], material.color[2]}},
        {"opacity", material.opacity},
        {"asset", int(material.asset)}};
    std::vector<CatalogDependency> dependencies;
    if (material.asset)
        dependencies.push_back({CatalogDependency::Kind::Asset, material.asset});
    return addEntry({CatalogEntryKind::Material,
                     name,
                     description,
                     {},
                     dependencies,
                     QJsonDocument(record).toJson(QJsonDocument::Compact)});
}
std::uint32_t CatalogBuilder::addTemplate(const QString &name, const QString &description,
                                          const Document &source) {
    return addEntry(
        {CatalogEntryKind::Template, name, description, {}, {}, encodeContainer(source)});
}
std::uint32_t CatalogBuilder::addComponent(const QString &name, const QString &description,
                                           const Document &source, Id definition,
                                           const std::vector<CatalogDependency> &dependencies,
                                           const std::string &identity) {
    require(definition != 0, "Catalog component definition must be nonzero");
    const auto captured = captureLibraryComponent(source, definition, identity);
    return addEntry({CatalogEntryKind::Component, name, description,
                     QString::number(qulonglong(definition)), dependencies,
                     encodeContainer(captured)});
}
QByteArray CatalogBuilder::encode() const {
    require(!entries_.empty(), "Catalog needs at least one entry");
    // The payload region holds every asset, then every entry, each contiguous and in id order.
    QByteArray region;
    QJsonArray assetRecords;
    for (std::size_t i = 0; i < assets_.size(); ++i) {
        const auto &asset = assets_[i];
        assetRecords.append(QJsonObject{{"id", int(i + 1)},
                                        {"name", asset.name},
                                        {"mediaType", asset.mediaType},
                                        {"offset", qint64(region.size())},
                                        {"bytes", qint64(asset.bytes.size())},
                                        {"sha256", hash(asset.bytes)}});
        region += asset.bytes;
    }
    QJsonArray entryRecords;
    for (std::size_t i = 0; i < entries_.size(); ++i) {
        const auto &entry = entries_[i];
        QJsonArray dependencies;
        for (const auto &dependency : entry.dependencies)
            dependencies.append(QJsonObject{
                {"kind", dependency.kind == CatalogDependency::Kind::Asset ? "asset" : "entry"},
                {"id", int(dependency.id)}});
        QJsonObject record{{"id", int(i + 1)},
                           {"kind", catalogKindName(entry.kind)},
                           {"name", entry.name},
                           {"description", entry.description},
                           {"offset", qint64(region.size())},
                           {"bytes", qint64(entry.payload.size())},
                           {"sha256", hash(entry.payload)},
                           {"dependencies", dependencies}};
        if (entry.kind == CatalogEntryKind::Component)
            record["definition"] = entry.definition;
        entryRecords.append(record);
        region += entry.payload;
    }
    require(std::size_t(region.size()) <= CatalogLimits::payloadBytes,
            "Catalog payload exceeds 128 MiB");
    QJsonArray labels;
    for (const auto &label : metadata_.labels)
        labels.append(label);
    const auto manifest =
        QJsonDocument(
            QJsonObject{{"bundleVersion", 1},
                        {"kind", "catalog"},
                        {"catalogId", metadata_.catalogId},
                        {"name", metadata_.name},
                        {"description", metadata_.description},
                        {"labels", labels},
                        {"author", metadata_.author},
                        {"license", QJsonObject{{"text", metadata_.licenseText},
                                                {"identifier", metadata_.licenseIdentifier}}},
                        {"version", metadata_.version},
                        {"assets", assetRecords},
                        {"entries", entryRecords}})
            .toJson(QJsonDocument::Compact);
    require(manifest.size() <= manifestLimit, "Catalog manifest exceeds 32 KiB");
    require(headerSize + manifest.size() + region.size() <= libraryBundleLimit,
            "Catalog exceeds 130 MiB");
    QByteArray result(headerSize, '\0');
    std::copy(magic.begin(), magic.end(), result.begin());
    qToLittleEndian<quint32>(quint32(manifest.size()), result.data() + 8);
    qToLittleEndian<quint64>(quint64(region.size()), result.data() + 12);
    // A catalog has no thumbnail: the envelope's thumbnail length is zero.
    qToLittleEndian<quint32>(0, result.data() + 20);
    const auto encoded = result + manifest + region;
    // The writer refuses to produce a catalog that its own reader would not list as importable.
    for (const auto &entry : decodeCatalog(encoded).entries)
        if (!entry.importable)
            throw std::runtime_error(entry.reason.toStdString());
    return encoded;
}
CatalogPackage decodeCatalog(const QByteArray &bytes) {
    require(bytes.size() >= headerSize && bytes.size() <= qsizetype(libraryBundleLimit) &&
                bytes.startsWith(magic),
            "Invalid or oversized catalog");
    const auto manifestSize = qFromLittleEndian<quint32>(bytes.constData() + 8);
    const auto payloadSize = qFromLittleEndian<quint64>(bytes.constData() + 12);
    const auto thumbnailSize = qFromLittleEndian<quint32>(bytes.constData() + 20);
    require(manifestSize && manifestSize <= manifestLimit && payloadSize &&
                payloadSize <= CatalogLimits::payloadBytes && thumbnailSize == 0,
            "Catalog envelope lengths exceed limits or carry a thumbnail");
    require(quint64(bytes.size()) == quint64(headerSize) + manifestSize + payloadSize,
            "Catalog length mismatch or trailing data");
    QJsonParseError error;
    const auto parsed = QJsonDocument::fromJson(bytes.mid(headerSize, manifestSize), &error);
    require(error.error == QJsonParseError::NoError && parsed.isObject(),
            "Invalid catalog manifest JSON");
    const auto m = parsed.object();
    fields(m, {"bundleVersion", "kind", "catalogId", "name", "description", "labels", "author",
               "license", "version", "assets", "entries"});
    require(m["bundleVersion"] == 1 && m["kind"] == "catalog",
            "Unsupported catalog version or kind");
    CatalogMetadata metadata;
    metadata.catalogId = text(m["catalogId"], qsizetype(CatalogLimits::catalogIdBytes));
    metadata.name = text(m["name"], qsizetype(CatalogLimits::nameBytes));
    metadata.description = text(m["description"], qsizetype(CatalogLimits::descriptionBytes), true);
    require(m["labels"].isArray() &&
                m["labels"].toArray().size() <= qsizetype(CatalogLimits::labels),
            "Invalid catalog labels");
    for (const auto &label : m["labels"].toArray())
        metadata.labels.append(text(label, qsizetype(CatalogLimits::labelBytes)));
    metadata.author = text(m["author"], qsizetype(CatalogLimits::authorBytes));
    require(m["license"].isObject(), "Catalog license must be an object");
    const auto license = m["license"].toObject();
    fields(license, {"text", "identifier"});
    metadata.licenseText = text(license["text"], qsizetype(CatalogLimits::licenseTextBytes), true);
    metadata.licenseIdentifier =
        text(license["identifier"], qsizetype(CatalogLimits::licenseIdentifierBytes));
    metadata.version = text(m["version"], qsizetype(CatalogLimits::versionBytes));
    validateMetadata(metadata);
    // Each record is checked against the envelope before it is kept: canonical ids, contiguous
    // offsets and in-range sizes. A fault here refuses the whole file, not one entry.
    const auto region =
        QByteArrayView(bytes).sliced(headerSize + manifestSize, qsizetype(payloadSize));
    qsizetype cursor{};
    std::vector<CatalogAssetReport> assets;
    require(m["assets"].isArray() &&
                m["assets"].toArray().size() <= qsizetype(CatalogLimits::assets),
            "Catalog has more than 256 assets");
    qsizetype assetTotal{};
    for (const auto &value : m["assets"].toArray()) {
        require(value.isObject(), "Catalog asset must be an object");
        const auto record = value.toObject();
        fields(record, {"id", "name", "mediaType", "offset", "bytes", "sha256"});
        CatalogAssetReport asset;
        asset.id = referenceId(record["id"]);
        require(asset.id == std::uint32_t(assets.size() + 1),
                "Catalog asset ids must run from 1 in order");
        asset.name = text(record["name"], qsizetype(CatalogLimits::assetNameBytes));
        asset.mediaType = text(record["mediaType"], qsizetype(CatalogLimits::mediaTypeBytes));
        asset.bytes = wholeNumber(record["bytes"], qsizetype(CatalogLimits::assetBytes),
                                  "Catalog asset must be 1 byte to 16 MiB");
        validateAsset(asset.name, asset.mediaType, asset.bytes);
        require(wholeNumber(record["offset"], qsizetype(payloadSize),
                            "Catalog asset offset is out of range") == cursor,
                "Catalog payload must be contiguous in id order");
        asset.offset = cursor;
        asset.sha256 = digestText(record["sha256"]);
        assetTotal += asset.bytes;
        require(std::size_t(assetTotal) <= CatalogLimits::assetTotalBytes,
                "Catalog assets exceed 64 MiB together");
        cursor += asset.bytes;
        assets.push_back(std::move(asset));
    }
    std::vector<CatalogEntryReport> entries;
    require(m["entries"].isArray() && !m["entries"].toArray().isEmpty() &&
                m["entries"].toArray().size() <= qsizetype(CatalogLimits::entries),
            "Catalog must have 1 to 128 entries");
    for (const auto &value : m["entries"].toArray()) {
        require(value.isObject(), "Catalog entry must be an object");
        const auto record = value.toObject();
        const auto kind = kindFromName(record["kind"]);
        QStringList names{"id",     "kind",  "name",   "description",
                          "offset", "bytes", "sha256", "dependencies"};
        if (kind == CatalogEntryKind::Component)
            names.append("definition");
        fields(record, names);
        CatalogEntryReport entry;
        entry.id = referenceId(record["id"]);
        require(entry.id == std::uint32_t(entries.size() + 1),
                "Catalog entry ids must run from 1 in order");
        entry.kind = kind;
        entry.name = nonemptyText(text(record["name"], qsizetype(CatalogLimits::nameBytes)),
                                  CatalogLimits::nameBytes);
        entry.description =
            text(record["description"], qsizetype(CatalogLimits::descriptionBytes), true);
        const bool record_kind =
            kind == CatalogEntryKind::Style || kind == CatalogEntryKind::Material;
        entry.bytes = wholeNumber(record["bytes"], qsizetype(CatalogLimits::payloadBytes),
                                  "Catalog payload size is out of range");
        require(entry.bytes >= 1 &&
                    std::size_t(entry.bytes) <=
                        (record_kind ? CatalogLimits::recordBytes : CatalogLimits::payloadBytes),
                record_kind ? "Catalog style and material records are limited to 16 KiB"
                            : "Catalog payload is empty or exceeds 128 MiB");
        require(wholeNumber(record["offset"], qsizetype(payloadSize),
                            "Catalog entry offset is out of range") == cursor,
                "Catalog payload must be contiguous in id order");
        entry.offset = cursor;
        entry.sha256 = digestText(record["sha256"]);
        if (kind == CatalogEntryKind::Component) {
            require(record["definition"].isString(), "Catalog component needs a definition id");
            entry.definition = record["definition"].toString();
        }
        require(record["dependencies"].isArray() && record["dependencies"].toArray().size() <=
                                                        qsizetype(CatalogLimits::dependencies),
                "Catalog entry has more than 64 dependencies");
        for (const auto &dependencyValue : record["dependencies"].toArray()) {
            require(dependencyValue.isObject(), "Catalog dependency must be an object");
            const auto dependency = dependencyValue.toObject();
            fields(dependency, {"kind", "id"});
            const auto name = dependency["kind"].toString();
            require(dependency["kind"].isString() && (name == "asset" || name == "entry"),
                    "Catalog dependency kind must be asset or entry");
            entry.dependencies.push_back(
                {name == "asset" ? CatalogDependency::Kind::Asset : CatalogDependency::Kind::Entry,
                 referenceId(dependency["id"])});
        }
        cursor += entry.bytes;
        require(cursor <= qsizetype(payloadSize), "Catalog payload exceeds its declared length");
        entries.push_back(std::move(entry));
    }
    require(cursor == qsizetype(payloadSize), "Catalog payload length mismatch");
    for (const auto &asset : assets) {
        const auto referenced = std::any_of(entries.begin(), entries.end(), [&](const auto &entry) {
            return std::any_of(entry.dependencies.begin(), entry.dependencies.end(),
                               [&](const CatalogDependency &dependency) {
                                   return dependency.kind == CatalogDependency::Kind::Asset &&
                                          dependency.id == asset.id;
                               });
        });
        require(referenced, "Catalog asset is not referenced by any entry");
    }
    for (auto &asset : assets)
        asset.verified = digestOf(region.sliced(asset.offset, asset.bytes)) == asset.sha256;
    for (std::size_t index = 0; index < entries.size(); ++index) {
        auto failure = entryFailure(entries, index, assets, region);
        entries[index].importable = failure.reason.isEmpty();
        entries[index].reason = std::move(failure.reason);
        entries[index].missing = failure.missing;
    }
    CatalogPackage package;
    package.metadata = std::move(metadata);
    package.assets = std::move(assets);
    package.entries = std::move(entries);
    package.file = bytes;
    return package;
}
CatalogPackage loadCatalog(const QString &path) { return decodeCatalog(loadCatalogBytes(path)); }
void writeCatalog(const QByteArray &bytes, const QString &newPath) {
    for (const auto &entry : decodeCatalog(bytes).entries)
        require(entry.importable, "Catalog entry is not importable; nothing was written");
    publishNewFile(newPath, bytes);
}
QByteArray catalogAssetBytes(const CatalogPackage &package, const CatalogAssetReport &asset) {
    return package.file.mid(payloadStart(package.file) + asset.offset, asset.bytes);
}
QByteArray catalogEntryPayload(const CatalogPackage &package, const CatalogEntryReport &entry) {
    return package.file.mid(payloadStart(package.file) + entry.offset, entry.bytes);
}
QJsonObject encodeCatalogListing(const CatalogPackage &package) {
    qsizetype missingAssets{}, importable{};
    QJsonArray assets;
    for (const auto &asset : package.assets) {
        if (!asset.verified)
            ++missingAssets;
        assets.append(QJsonObject{{"id", int(asset.id)},
                                  {"name", asset.name},
                                  {"mediaType", asset.mediaType},
                                  {"bytes", qint64(asset.bytes)},
                                  {"assetStatus", asset.verified ? "present" : "missing"}});
    }
    QJsonArray entries;
    for (const auto &entry : package.entries) {
        if (entry.importable)
            ++importable;
        QJsonArray dependencies;
        for (const auto &dependency : entry.dependencies) {
            const bool asset = dependency.kind == CatalogDependency::Kind::Asset;
            const bool present = asset ? dependency.id <= package.assets.size() &&
                                             package.assets[dependency.id - 1].verified
                                       : dependency.id < entry.id;
            dependencies.append(QJsonObject{{"kind", asset ? "asset" : "entry"},
                                            {"id", int(dependency.id)},
                                            {"status", present ? "present" : "missing"}});
        }
        QJsonObject record{{"id", int(entry.id)},
                           {"kind", catalogKindName(entry.kind)},
                           {"name", entry.name},
                           {"description", entry.description},
                           {"importable", entry.importable},
                           {"dependencies", dependencies}};
        if (!entry.importable)
            record["reason"] = entry.reason;
        if (entry.missing)
            record["missing"] = QJsonObject{
                {"kind", entry.missing->kind == CatalogDependency::Kind::Asset ? "asset" : "entry"},
                {"id", int(entry.missing->id)}};
        entries.append(record);
    }
    return {{"name", package.metadata.name},
            {"catalogId", package.metadata.catalogId},
            {"version", package.metadata.version},
            {"entryCount", int(package.entries.size())},
            {"importableCount", int(importable)},
            {"missingAssets", int(missingAssets)},
            {"assets", assets},
            {"entries", entries}};
}
} // namespace sketchy
