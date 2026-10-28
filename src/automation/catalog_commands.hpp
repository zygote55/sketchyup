#pragma once
#include "core/model.hpp"
#include "io/catalog_insert.hpp"
#include "io/catalog_io.hpp"
#include <QJsonObject>
#include <QString>
#include <map>

namespace sketchy {

// Catalogs the desktop dialog has already decoded, by the key a batch names each one with. A
// command names one as `"opened": <key>`. The dialog's dry runs and insert pass the map, so the
// file's bytes are neither encoded into the request nor decoded again on every selection. Only a
// caller that holds the map can name a catalog this way: MCP, the CLI and scripts pass none.
using OpenedCatalogs = std::map<QString, CatalogPackage>;

// The catalog a request names (ADR 0174 section 4, the R100.d reference decision). Exactly one of
// three forms is accepted: `library`, a name in the user's catalog library folder (the recommended
// form, validated by isCatalogLibraryName), `catalog`, the whole file as unpadded base64url, or
// `opened`, a key of `opened`. A path is never accepted, so no request reads a file the user did
// not place in the library.
CatalogPackage catalogReference(const QJsonObject &request, const OpenedCatalogs *opened = nullptr);

// `catalog.import` (ADR 0174 section 7): a transaction command. It imports the named entries of
// the referenced catalog into the batch's document, as one edit, and reports the entries it took.
bool isCatalogCommand(const QString &name);
QJsonObject executeCatalogCommand(Document &doc, const QJsonObject &command,
                                  const OpenedCatalogs *opened = nullptr);
// The report of one import, shared with the CLI's --import-catalog (R100.e).
QJsonObject catalogImportReport(const CatalogPackage &package, const CatalogImportResult &result);

// `catalog.export` is a query: it captures the chosen records as a catalog and changes nothing.
// The catalog is returned as unpadded base64url in `data`, the form `catalog.list` accepts.
QJsonObject catalogExportQuery(const Document &doc, const QJsonObject &request);

} // namespace sketchy
