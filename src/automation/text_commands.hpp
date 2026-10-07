#pragma once
#include "core/model.hpp"
#include <QJsonObject>
namespace sketchy {
bool isTextCommand(const QString &name);
ChangeReport executeTextCommand(Document &doc, const QJsonObject &command);
QJsonObject textSettingsProperties();
QJsonObject textDescription(const Document &doc, Id body);
std::string textGeometryDigest(const Body &body);
} // namespace sketchy
