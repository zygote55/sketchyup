#pragma once
#include "core/sections.hpp"
#include <QJsonObject>
namespace sketchy {
bool isSectionCommand(const QString &name);
void executeSectionCommand(Document &doc, const QJsonObject &command);
QJsonObject sectionPlaneSchema();
QJsonObject sectionDescription(const Document &doc, Id section);
} // namespace sketchy
