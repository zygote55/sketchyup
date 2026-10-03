#pragma once
#include "core/model.hpp"
#include <QJsonArray>
#include <QJsonObject>
namespace sketchy {
QJsonObject capabilities();
QJsonArray commandCatalog();
QJsonObject commandDescription(const QString &name);
QJsonObject describe(const Document &doc);
QJsonObject executeQuery(const Document &doc, const QJsonObject &request);
// A local batch driver, not a provider/MCP implementation. All changes use
// the same core operations as the desktop. No external effects in a batch.
QJsonObject executeAmend(Document &doc, const Document::AmendStamp &stamp,
                         const QJsonObject &request);
QJsonObject previewAmend(const Document &doc, const Document::AmendStamp &stamp,
                         const QJsonObject &request);
QJsonObject previewBatch(const Document &doc, const QJsonObject &request);
QJsonObject executeBatch(Document &doc, const QJsonObject &request);
} // namespace sketchy
