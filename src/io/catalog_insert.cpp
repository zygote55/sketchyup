#include "io/catalog_insert.hpp"
#include "core/assets.hpp"
#include "core/component_records.hpp"
#include "core/materials.hpp"
#include "io/assets.hpp"
#include "io/component_library.hpp"
#include "io/document_io.hpp"
#include "io/library_bundle.hpp"
#include "io/model_style_io.hpp"
#include <QJsonDocument>
#include <QJsonObject>
#include <array>
#include <map>
#include <set>
#include <stdexcept>

namespace sketchy {
namespace {

const QString styleEntryName = QStringLiteral("Document style");

bool sameBytes(const std::vector<std::uint8_t> &bytes, const QByteArray &other) {
    return QByteArray(reinterpret_cast<const char *>(bytes.data()), qsizetype(bytes.size())) ==
           other;
}

QJsonObject recordObject(const QByteArray &payload) {
    QJsonParseError error;
    const auto parsed = QJsonDocument::fromJson(payload, &error);
    if (error.error != QJsonParseError::NoError || !parsed.isObject())
        throw std::runtime_error("Catalog record is not a JSON object");
    return parsed.object();
}

std::string quoted(const QString &name) { return ("'" + name + "'").toStdString(); }

// The destination asset for one catalog asset: an equal asset already there, or a new one. The
// catalog asset was verified by its hash when the catalog was read, and its bytes are compared
// here too, because a hash is only an index (ADR 0131).
Id destinationAsset(Document &doc, const CatalogPackage &package, std::uint32_t catalogAsset,
                    std::map<std::uint32_t, Id> &mapped, CatalogImportResult &result) {
    if (const auto found = mapped.find(catalogAsset); found != mapped.end())
        return found->second;
    const auto &asset = package.assets.at(catalogAsset - 1);
    const auto bytes = catalogAssetBytes(package, asset);
    const auto mediaType = asset.mediaType.toStdString();
    for (const auto &[id, record] : doc.assets())
        if (record->payload && record->mediaType == mediaType &&
            sameBytes(record->payload->bytes(), bytes)) {
            mapped[catalogAsset] = id;
            ++result.reusedAssets;
            return id;
        }
    std::set<std::string> names;
    for (const auto &[id, record] : doc.assets()) {
        (void)id;
        names.insert(record->name);
    }
    const auto name = uniqueLibraryName(asset.name.toStdString(), names, result.renamedResources);
    const auto id = createAsset(doc, name, mediaType, assetPayload(bytes));
    mapped[catalogAsset] = id;
    return id;
}

void importComponent(Document &doc, const CatalogPackage &package, const CatalogEntryReport &entry,
                     Id parent, CatalogImportResult &result, CatalogImportEntry &out) {
    ComponentBundle bundle;
    bundle.metadata.name = entry.name;
    bundle.metadata.description = entry.description;
    bundle.document = decodeContainer(catalogEntryPayload(package, entry));
    bundle.definition = Id(entry.definition.toULongLong());
    const auto inserted = insertLibraryComponent(doc, bundle, Transform{}, parent);
    out.definition = inserted.component.definition;
    out.instance = inserted.component.instance;
    result.reusedAssets += inserted.reusedAssets;
    result.reusedMaterials += inserted.reusedMaterials;
    result.renamedResources += inserted.renamedResources;
}

void importMaterial(Document &doc, const CatalogPackage &package, const CatalogEntryReport &entry,
                    std::map<std::uint32_t, Id> &mapped, CatalogImportResult &result,
                    CatalogImportEntry &out) {
    const auto record = recordObject(catalogEntryPayload(package, entry));
    const auto name = record["name"].toString().toStdString();
    std::array<float, 3> color{};
    const auto channels = record["color"].toArray();
    for (int i = 0; i < 3; ++i)
        color[size_t(i)] = float(channels[i].toDouble());
    const auto opacity = float(record["opacity"].toDouble());
    const auto catalogAsset = std::uint32_t(record["asset"].toDouble());
    const Id asset =
        catalogAsset ? destinationAsset(doc, package, catalogAsset, mapped, result) : 0;
    // A material is reused only for the same name, colour, opacity and asset (ADR 0131).
    for (const auto &[id, material] : doc.materials())
        if (material->name == name && material->color == color && material->opacity == opacity &&
            material->asset == asset) {
            out.material = id;
            ++result.reusedMaterials;
            return;
        }
    std::set<std::string> names;
    for (const auto &[id, material] : doc.materials()) {
        (void)id;
        names.insert(material->name);
    }
    out.material = createMaterial(doc, uniqueLibraryName(name, names, result.renamedResources),
                                  color, opacity, asset);
}

void applyStyle(Document &doc, const CatalogPackage &package, const CatalogEntryReport &entry,
                std::map<std::uint32_t, Id> &mapped, CatalogImportResult &result,
                CatalogImportEntry &out) {
    auto style = decodeModelStyle(recordObject(catalogEntryPayload(package, entry)), true);
    if (style.edgeStroke && style.edgeStroke->texture)
        style.edgeStroke->texture = destinationAsset(
            doc, package, std::uint32_t(style.edgeStroke->texture), mapped, result);
    // Applying a style never changes anything else in the document (ADR 0068).
    out.styleChanged = !(style == doc.style());
    if (out.styleChanged)
        doc.setStyle(style);
}

// The composed edit for a whole import: every body and resource change in one undo step.
void publish(Document &doc, const Document &draft) {
    Edit edit{"Import catalog entries", {}};
    for (const auto &[id, body] : draft.bodies()) {
        const auto old = doc.bodies().contains(id) ? doc.bodies().at(id) : nullptr;
        if (!old || *old != *body)
            edit.changes.push_back({id, old, body, {}, {}, {}, true});
    }
    for (const auto &[id, body] : doc.bodies())
        if (!draft.bodies().contains(id))
            edit.changes.push_back({id, body, nullptr, {}, {}, {}, true});
    edit.nextIdFloor = draft.nextId();
    edit.nextAssetFloor = draft.nextAssetId();
    edit.nextMaterialFloor = draft.nextMaterialId();
    edit.nextTagFloor = draft.nextTagId();
    edit.nextDefinitionFloor = draft.nextDefinitionId();
    edit.nextComponentDriverFloor = draft.nextComponentDriverId();
    appendSceneMetadataChanges(edit, doc, draft);
    doc.apply(std::move(edit), doc.revision());
}

QString catalogIdFor(const QString &name) {
    QString id;
    for (const auto c : name) {
        const bool ascii = c.unicode() < 128 && c.isLetterOrNumber();
        id += ascii || c == QLatin1Char('.') || c == QLatin1Char('_') || c == QLatin1Char('-')
                  ? c
                  : QLatin1Char('-');
    }
    id = id.left(int(CatalogLimits::catalogIdBytes));
    return id.isEmpty() ? QStringLiteral("catalog") : id;
}

// The catalog asset for one document asset, reusing it when another record already took it.
std::uint32_t exportedAsset(CatalogBuilder &builder, const Document &doc, Id asset,
                            std::map<Id, std::uint32_t> &mapped) {
    if (const auto found = mapped.find(asset); found != mapped.end())
        return found->second;
    const auto found = doc.assets().find(asset);
    if (found == doc.assets().end() || !found->second->payload)
        throw std::runtime_error("Asset " + std::to_string(asset) +
                                 " has no bytes; restore it before exporting");
    const auto &record = *found->second;
    const auto id =
        builder.addAsset(QString::fromStdString(record.name),
                         QString::fromStdString(record.mediaType), assetByteArray(record.payload));
    mapped[asset] = id;
    return id;
}

} // namespace

CatalogImportResult importCatalogEntries(Document &doc, const CatalogPackage &package,
                                         const QStringList &names, Id parent) {
    if (names.isEmpty())
        throw std::runtime_error("Choose at least one catalog entry to import");
    // Each name must match exactly one entry; a name that matches several is refused, not chosen.
    std::set<std::size_t> requested;
    for (const auto &name : names) {
        std::vector<std::size_t> matches;
        for (std::size_t i = 0; i < package.entries.size(); ++i)
            if (package.entries[i].name == name)
                matches.push_back(i);
        if (matches.empty())
            throw std::runtime_error("No catalog entry is named " + quoted(name));
        if (matches.size() > 1)
            throw std::runtime_error("Catalog entry name " + quoted(name) + " is ambiguous: " +
                                     std::to_string(matches.size()) + " entries share it");
        requested.insert(matches.front());
    }
    // An entry's entry dependencies come with it. A dependency always names an earlier entry, so
    // this walk ends and the ascending order below imports every dependency first.
    std::set<std::size_t> closure;
    std::vector<std::size_t> pending(requested.begin(), requested.end());
    while (!pending.empty()) {
        const auto index = pending.back();
        pending.pop_back();
        if (!closure.insert(index).second)
            continue;
        for (const auto &dependency : package.entries[index].dependencies)
            if (dependency.kind == CatalogDependency::Kind::Entry)
                pending.push_back(dependency.id - 1);
    }
    // Every entry is checked before the first change, so one refusal leaves the document alone.
    for (const auto index : closure) {
        const auto &entry = package.entries[index];
        if (!entry.importable)
            throw std::runtime_error("Catalog entry " + quoted(entry.name) +
                                     " is not importable: " + entry.reason.toStdString());
        if (entry.kind == CatalogEntryKind::Template)
            throw std::runtime_error("Catalog entry " + quoted(entry.name) +
                                     " is a template: a template opens as a new model and is not "
                                     "inserted into an open one");
    }
    // The work runs on a snapshot and publishes one edit, so a failure part-way changes nothing.
    auto draft = doc.readSnapshot();
    CatalogImportResult result;
    std::map<std::uint32_t, Id> mapped;
    for (const auto index : closure) {
        const auto &entry = package.entries[index];
        CatalogImportEntry out;
        out.name = entry.name;
        out.kind = entry.kind;
        out.requested = requested.contains(index);
        switch (entry.kind) {
        case CatalogEntryKind::Component:
            importComponent(draft, package, entry, parent, result, out);
            break;
        case CatalogEntryKind::Style:
            applyStyle(draft, package, entry, mapped, result, out);
            break;
        case CatalogEntryKind::Material:
            importMaterial(draft, package, entry, mapped, result, out);
            break;
        case CatalogEntryKind::Template:
            throw std::runtime_error("A template is not inserted into an open model");
        }
        result.entries.push_back(std::move(out));
    }
    publish(doc, draft);
    return result;
}

QByteArray exportCatalog(const Document &doc, const CatalogExportRequest &request) {
    if (request.components.empty() && request.materials.empty() && !request.style)
        throw std::runtime_error(
            "Choose at least one component, material or the document style to export");
    CatalogMetadata metadata;
    metadata.catalogId = catalogIdFor(request.name);
    metadata.name = request.name;
    metadata.description = request.description;
    metadata.labels = request.labels;
    metadata.licenseIdentifier = request.licenseIdentifier;
    metadata.version = request.version;
    CatalogBuilder builder(metadata);
    std::set<QString> names;
    auto claim = [&](const QString &name) {
        if (!names.insert(name).second)
            throw std::runtime_error("Catalog entry names must be unique: " + quoted(name) +
                                     " is used twice");
    };
    std::map<Id, std::uint32_t> mapped;
    for (const auto definition : request.components) {
        const auto found = doc.definitions().find(definition);
        if (found == doc.definitions().end())
            throw std::runtime_error("Component definition " + std::to_string(definition) +
                                     " does not exist");
        const auto name = QString::fromStdString(found->second->name);
        claim(name);
        builder.addComponent(name, {}, doc, definition);
    }
    for (const auto material : request.materials) {
        const auto found = doc.materials().find(material);
        if (found == doc.materials().end())
            throw std::runtime_error("Material " + std::to_string(material) + " does not exist");
        const auto &record = *found->second;
        const auto name = QString::fromStdString(record.name);
        claim(name);
        CatalogMaterial entry{record.name, record.color, record.opacity, 0};
        if (record.asset)
            entry.asset = exportedAsset(builder, doc, record.asset, mapped);
        builder.addMaterial(name, {}, entry);
    }
    if (request.style) {
        claim(styleEntryName);
        auto style = doc.style();
        std::vector<CatalogDependency> dependencies;
        if (style.edgeStroke && style.edgeStroke->texture) {
            const auto asset = exportedAsset(builder, doc, style.edgeStroke->texture, mapped);
            style.edgeStroke->texture = asset;
            dependencies.push_back({CatalogDependency::Kind::Asset, asset});
        }
        builder.addStyle(styleEntryName, {}, style, dependencies);
    }
    return builder.encode();
}

} // namespace sketchy
