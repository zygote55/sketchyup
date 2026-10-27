#pragma once
#include "core/model_style.hpp"
#include <QJsonObject>
namespace sketchy {
QJsonObject encodeModelStyle(const ModelStyle &style);
// `edgeStrokes` admits the schema-31 `edgeStroke` field; a schema-30 or older payload refuses it.
ModelStyle decodeModelStyle(const QJsonValue &value, bool edgeStrokes = false);
QJsonObject encodeEdgeStroke(const EdgeStroke &stroke);
EdgeStroke decodeEdgeStroke(const QJsonValue &value);
} // namespace sketchy
