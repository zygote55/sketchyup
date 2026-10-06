#pragma once
#include "core/scene_records.hpp"
#include <QJsonArray>
#include <QJsonObject>
namespace sketchy {
QJsonObject encodeSceneSnapshot(const SceneSnapshot &snapshot);
SceneSnapshot decodeSceneSnapshot(const QJsonValue &value, bool namedSections = true, bool solar = true);
QJsonArray encodeScenes(const SceneRecords &scenes);
SceneRecords decodeScenes(const QJsonValue &value, Id nextSceneId, bool namedSections = true, bool solar = true);
QJsonObject encodeMissingSceneReferences(const MissingSceneReferences &missing);
} // namespace sketchy
