#pragma once
#include "core/model.hpp"
#include <QJsonArray>
#include <QJsonObject>
namespace sketchy {
QJsonObject capabilities();
QJsonObject describe(const Document &doc);
// A local batch driver, not a provider/MCP implementation. All changes use
// the same core operations as the desktop. No external effects in a batch.
QJsonObject executeBatch(Document &doc, const QJsonObject &request);
} // namespace sketchy
