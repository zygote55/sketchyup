#pragma once
#include "core/scene_records.hpp"
#include <QJsonArray>
#include <QJsonObject>
namespace sketchy {
QJsonObject encodeSceneSnapshot(const SceneSnapshot &snapshot);
SceneSnapshot decodeSceneSnapshot(const QJsonValue &value);
QJsonArray encodeScenes(const SceneRecords &scenes);
SceneRecords decodeScenes(const QJsonValue &value, Id nextSceneId);
QJsonObject encodeMissingSceneReferences(const MissingSceneReferences &missing);
} // namespace sketchy
