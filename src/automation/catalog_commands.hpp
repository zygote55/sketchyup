#pragma once
#include "core/model.hpp"
#include "io/catalog_insert.hpp"
#include "io/catalog_io.hpp"
#include <QJsonObject>
#include <QString>

namespace sketchy {

// The catalog a request names (ADR 0174 section 4, the R100.d reference decision). Exactly one of
// two forms is accepted: `library`, a name in the user's catalog library folder (the recommended
// form, validated by isCatalogLibraryName), or `catalog`, the whole file as unpadded base64url.
// A path is never accepted, so no request reads a file the user did not place in the library.
CatalogPackage catalogReference(const QJsonObject &request);

// `catalog.import` (ADR 0174 section 7): a transaction command. It imports the named entries of
// the referenced catalog into the batch's document, as one edit, and reports the entries it took.
bool isCatalogCommand(const QString &name);
QJsonObject executeCatalogCommand(Document &doc, const QJsonObject &command);
// The report of one import, shared with the CLI's --import-catalog (R100.e).
QJsonObject catalogImportReport(const CatalogPackage &package, const CatalogImportResult &result);

// `catalog.export` is a query: it captures the chosen records as a catalog and changes nothing.
// The catalog is returned as unpadded base64url in `data`, the form `catalog.list` accepts.
QJsonObject catalogExportQuery(const Document &doc, const QJsonObject &request);

} // namespace sketchy
