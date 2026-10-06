#pragma once
#include "core/scenes.hpp"
#include <QJsonObject>
namespace sketchy {
bool isSavedSceneCommand(const QString &name);
void executeSavedSceneCommand(Document &doc, const QJsonObject &command);
QJsonObject savedSceneSnapshotSchema(const QJsonObject &styleSchema);
QJsonObject savedSceneSummary(const Document &doc, Id scene);
QJsonObject savedSceneDescription(const Document &doc, Id scene);
} // namespace sketchy
