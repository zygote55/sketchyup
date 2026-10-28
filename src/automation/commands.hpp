#pragma once
#include "automation/catalog_commands.hpp"
#include "core/model.hpp"
#include <QJsonArray>
#include <QJsonObject>
namespace sketchy {
QJsonObject capabilities();
QJsonArray commandCatalog();
QJsonObject commandDescription(const QString &name);
QJsonObject describe(const Document &doc);
QJsonObject executeQuery(const Document &doc, const QJsonObject &request);
QJsonObject describeHistory(const Document &doc, size_t offset = 0, size_t limit = 200);
// History navigation controls the existing cursor; it cannot be composed into a modeling batch.
QJsonObject executeHistory(Document &doc, const QJsonObject &request);
// A local batch driver, not a provider/MCP implementation. All changes use
// the same core operations as the desktop. No external effects in a batch.
enum class BatchResponse { Full, Changes, CreatedIds };
// R096.f1: `response` as for executeBatch (Full describes the whole document; the terrain tools
// ask for Changes, since describing a 250,000-triangle terrain costs most of a commit).
QJsonObject executeAmend(Document &doc, const Document::AmendStamp &stamp,
                         const QJsonObject &request, BatchResponse response = BatchResponse::Full);
QJsonObject previewAmend(const Document &doc, const Document::AmendStamp &stamp,
                         const QJsonObject &request);
// `opened` names the catalogs a `catalog.import` command may use by key (see OpenedCatalogs). Only
// the desktop dialog passes it.
QJsonObject previewBatch(const Document &doc, const QJsonObject &request,
                         const OpenedCatalogs *opened = nullptr);
QJsonObject executeBatch(Document &doc, const QJsonObject &request,
                         BatchResponse response = BatchResponse::Full,
                         const OpenedCatalogs *opened = nullptr);
} // namespace sketchy
