#pragma once
#include "core/model.hpp"
#include <QJsonObject>
namespace sketchy {
bool isReferenceImageCommand(const QString &name);
ChangeReport executeReferenceImageCommand(Document &doc, const QJsonObject &command);
QJsonObject referenceImageProperties();
QJsonObject referenceImageDescription(const Document &doc, Id body, bool decodePixels = false);
} // namespace sketchy
