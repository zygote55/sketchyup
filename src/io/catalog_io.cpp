#include "io/catalog_io.hpp"
#include "io/library_bundle.hpp"
#include "io/library_envelope.hpp"
#include "io/model_style_io.hpp"
#include <QCryptographicHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QRegularExpression>
#include <QtEndian>
#include <algorithm>
#include <cmath>
#include <set>
namespace sketchy {
namespace {
using namespace library_envelope;
const QRegularExpression catalogIdPattern("^[A-Za-z0-9._-]{1,64}$");
const QRegularExpression mediaTypePattern("^[a-z0-9][a-z0-9.+-]*/[a-z0-9][a-z0-9.+-]*$");
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
void validateAsset(const QString &name, const QString &mediaType, const QByteArray &bytes) {
    nonemptyText(name, CatalogLimits::assetNameBytes);
    require(mediaType.toUtf8().size() <= qsizetype(CatalogLimits::mediaTypeBytes) &&
                mediaTypePattern.match(mediaType).hasMatch(),
            "Catalog asset media type must be lowercase type/subtype");
    require(!bytes.isEmpty() && std::size_t(bytes.size()) <= CatalogLimits::assetBytes,
            "Catalog asset must be 1 byte to 16 MiB");
}
void requireUnitInterval(float value, const char *message) {
    require(std::isfinite(value) && value >= 0 && value <= 1, message);
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
    validateAsset(name, mediaType, bytes);
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
    return result + manifest + region;
}
} // namespace sketchy
