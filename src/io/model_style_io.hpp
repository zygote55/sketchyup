#pragma once
#include "core/model_style.hpp"
#include <QJsonObject>
namespace sketchy {
QJsonObject encodeModelStyle(const ModelStyle &style);
ModelStyle decodeModelStyle(const QJsonValue &value);
} // namespace sketchy
