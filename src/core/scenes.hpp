#pragma once
#include "core/model.hpp"
namespace sketchy {
Id createScene(Document &doc, std::string name, SceneSnapshot snapshot);
void renameScene(Document &doc, Id scene, std::string name);
void updateScene(Document &doc, Id scene, SceneSnapshot snapshot);
void reorderScenes(Document &doc, const std::vector<Id> &order);
void eraseScene(Document &doc, Id scene);
std::vector<Id> orderedScenes(const Document &doc);
} // namespace sketchy
