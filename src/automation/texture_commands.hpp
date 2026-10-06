#pragma once
#include "core/model.hpp"
#include <QJsonObject>

namespace sketchy {
// Projection descriptions always report stored/effective body-local covectors.
QJsonObject faceTextureDescription(const Body &body, Id face);
ChangeReport executeTextureMappingCommand(Document &doc, const QJsonObject &command);
} // namespace sketchy
