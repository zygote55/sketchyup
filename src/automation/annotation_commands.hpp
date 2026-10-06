#pragma once
#include "core/annotations.hpp"
#include <QJsonObject>
namespace sketchy {
bool isAnnotationCommand(const QString &name);
void executeAnnotationCommand(Document &doc, const QJsonObject &command);
QJsonObject annotationPropertiesSchema();
QJsonObject annotationDescription(const Document &doc, Id annotation);
} // namespace sketchy
