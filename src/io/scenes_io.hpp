#pragma once
#include "core/scene_records.hpp"
#include <QJsonArray>
#include <QJsonObject>
namespace sketchy {
QJsonObject encodeSceneSnapshot(const SceneSnapshot &snapshot);
// `edgeStrokes` admits the schema-31 style sub-record (edge-styles-v1).
SceneSnapshot decodeSceneSnapshot(const QJsonValue &value, bool namedSections = true,
                                  bool solar = true, bool edgeStrokes = false);
QJsonArray encodeScenes(const SceneRecords &scenes);
SceneRecords decodeScenes(const QJsonValue &value, Id nextSceneId, bool namedSections = true,
                          bool solar = true, bool edgeStrokes = false);
QJsonObject encodeMissingSceneReferences(const MissingSceneReferences &missing);
} // namespace sketchy
