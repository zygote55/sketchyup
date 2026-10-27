#include "automation/catalog_commands.hpp"
#include "io/catalog_insert.hpp"
#include <QByteArray>
#include <QJsonArray>
#include <stdexcept>

namespace sketchy {
namespace {

// The inline export limit: the query response is 256 KiB, and base64url adds a third.
constexpr qsizetype inlineCatalogBytes = 192 * 1024;

// A stable id as the request writes it: decimal text, nonzero unless `zeroAllowed`.
Id stableIdText(const QJsonValue &value, bool zeroAllowed = false) {
    bool ok{};
    const auto text = value.toString();
    const auto id = value.isString() ? text.toULongLong(&ok) : 0;
    if (!value.isString() || !ok || (!id && !zeroAllowed) || text != QString::number(id))
        throw std::runtime_error("Ids must be decimal text");
    return Id(id);
}

QStringList textList(const QJsonValue &value) {
    QStringList result;
    for (const auto &item : value.toArray()) {
        if (!item.isString())
            throw std::runtime_error("Catalog entry names must be text");
        result.append(item.toString());
    }
    return result;
}

std::vector<Id> idList(const QJsonValue &value) {
    std::vector<Id> result;
    for (const auto &item : value.toArray())
        result.push_back(stableIdText(item));
    return result;
}

} // namespace

CatalogPackage catalogReference(const QJsonObject &request) {
    const bool inlineForm = request.contains("catalog");
    const bool libraryForm = request.contains("library");
    if (inlineForm == libraryForm)
        throw std::runtime_error("Name exactly one catalog: library or catalog");
    if (libraryForm) {
        if (!request["library"].isString())
            throw std::runtime_error("Catalog library name must be text");
        return loadCatalogLibraryEntry(catalogLibraryFolder(), request["library"].toString());
    }
    if (!request["catalog"].isString())
        throw std::runtime_error("catalog must be unpadded base64url text");
    const auto bytes = request["catalog"].toString().toLatin1();
    const auto decoded = QByteArray::fromBase64(bytes, QByteArray::Base64UrlEncoding);
    if (decoded.toBase64(QByteArray::Base64UrlEncoding | QByteArray::OmitTrailingEquals) != bytes)
        throw std::runtime_error("catalog must be unpadded base64url");
    return decodeCatalog(decoded);
}

bool isCatalogCommand(const QString &name) { return name == QStringLiteral("catalog.import"); }

QJsonObject executeCatalogCommand(Document &doc, const QJsonObject &command) {
    if (command["command"].toString() != QStringLiteral("catalog.import"))
        throw std::runtime_error("Unknown catalog command");
    const auto package = catalogReference(command);
    const auto names = textList(command["entries"]);
    const Id parent = command.contains("parent") ? stableIdText(command["parent"], true) : 0;
    const auto result = importCatalogEntries(doc, package, names, parent);
    QJsonArray entries;
    for (const auto &entry : result.entries) {
        QJsonObject record{{"name", entry.name},
                           {"kind", catalogKindName(entry.kind)},
                           {"requested", entry.requested}};
        if (entry.definition)
            record["definition"] = QString::number(entry.definition);
        if (entry.instance)
            record["instance"] = QString::number(entry.instance);
        if (entry.material)
            record["material"] = QString::number(entry.material);
        if (entry.kind == CatalogEntryKind::Style)
            record["styleChanged"] = entry.styleChanged;
        entries.append(record);
    }
    return {{"command", "catalog.import"},
            {"name", package.metadata.name},
            {"catalogId", package.metadata.catalogId},
            {"entries", entries},
            {"reusedAssets", double(result.reusedAssets)},
            {"reusedMaterials", double(result.reusedMaterials)},
            {"renamedResources", double(result.renamedResources)}};
}

QJsonObject catalogExportQuery(const Document &doc, const QJsonObject &request) {
    CatalogExportRequest chosen;
    chosen.name = request["name"].toString();
    if (chosen.name.trimmed().isEmpty())
        throw std::runtime_error("A catalog needs a name");
    chosen.description = request["description"].toString();
    chosen.version =
        request.contains("version") ? request["version"].toString() : QStringLiteral("1.0.0");
    chosen.licenseIdentifier = request.contains("license")
                                   ? request["license"].toString()
                                   : QStringLiteral("LicenseRef-Unspecified");
    chosen.labels = textList(request["labels"]);
    chosen.components = idList(request["components"]);
    chosen.materials = idList(request["materials"]);
    chosen.style = request["style"].toBool(false);
    const auto bytes = exportCatalog(doc, chosen);
    if (bytes.size() > inlineCatalogBytes)
        throw std::runtime_error("Catalog export exceeds 192 KiB; export fewer records");
    const auto data =
        bytes.toBase64(QByteArray::Base64UrlEncoding | QByteArray::OmitTrailingEquals);
    return {{"command", "catalog.export"},
            {"name", chosen.name},
            {"bytes", double(bytes.size())},
            {"data", QString::fromLatin1(data)}};
}

} // namespace sketchy
