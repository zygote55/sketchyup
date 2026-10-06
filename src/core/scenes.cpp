#include "core/scenes.hpp"
namespace sketchy {
namespace {
ScenePtr requireScene(const Document &doc, Id id) {
    const auto found = doc.scenes().find(id);
    if (found == doc.scenes().end())
        throw std::runtime_error("Scene does not exist");
    return found->second;
}
} // namespace
Id createScene(Document &doc, std::string name, SceneSnapshot snapshot) {
    validateSceneCapture(doc, snapshot);
    const auto id = doc.nextSceneId();
    auto scene = std::make_shared<SceneRecord>(SceneRecord{
        id, std::move(name), static_cast<std::uint32_t>(doc.scenes().size()), std::move(snapshot)});
    Edit edit{"Create scene", {}};
    edit.scenes.push_back({id, nullptr, scene});
    doc.apply(std::move(edit), doc.revision());
    return id;
}
void renameScene(Document &doc, Id id, std::string name) {
    const auto before = requireScene(doc, id);
    if (before->name == name)
        return;
    auto after = std::make_shared<SceneRecord>(*before);
    after->name = std::move(name);
    Edit edit{"Rename scene", {}};
    edit.scenes.push_back({id, before, after});
    doc.apply(std::move(edit), doc.revision());
}
void updateScene(Document &doc, Id id, SceneSnapshot snapshot) {
    const auto before = requireScene(doc, id);
    validateSceneCapture(doc, snapshot);
    if (before->snapshot == snapshot)
        return;
    auto after = std::make_shared<SceneRecord>(*before);
    after->snapshot = std::move(snapshot);
    Edit edit{"Update scene", {}};
    edit.scenes.push_back({id, before, after});
    doc.apply(std::move(edit), doc.revision());
}
std::vector<Id> orderedScenes(const Document &doc) {
    std::vector<Id> order(doc.scenes().size());
    for (const auto &[id, scene] : doc.scenes())
        order.at(scene->position) = id;
    return order;
}
void reorderScenes(Document &doc, const std::vector<Id> &order) {
    if (order.size() != doc.scenes().size() ||
        std::set<Id>(order.begin(), order.end()).size() != order.size())
        throw std::runtime_error("Scene order must contain every scene exactly once");
    Edit edit{"Reorder scenes", {}};
    for (size_t i = 0; i < order.size(); ++i) {
        const auto before = requireScene(doc, order[i]);
        if (before->position == i)
            continue;
        auto after = std::make_shared<SceneRecord>(*before);
        after->position = static_cast<std::uint32_t>(i);
        edit.scenes.push_back({before->id, before, after});
    }
    if (!edit.scenes.empty())
        doc.apply(std::move(edit), doc.revision());
}
void eraseScene(Document &doc, Id id) {
    const auto before = requireScene(doc, id);
    Edit edit{"Delete scene", {}};
    edit.scenes.push_back({id, before, nullptr});
    for (const auto &[other, scene] : doc.scenes()) {
        if (scene->position <= before->position)
            continue;
        auto after = std::make_shared<SceneRecord>(*scene);
        --after->position;
        edit.scenes.push_back({other, scene, after});
    }
    doc.apply(std::move(edit), doc.revision());
}
Edit sceneRecallEdit(const Document &doc, Id id) {
    const auto scene = requireScene(doc, id);
    Edit edit{"Recall scene", {}};
    const auto &snapshot = scene->snapshot;
    if (snapshot.style && *snapshot.style != doc.style())
        edit.style = std::pair{doc.style(), *snapshot.style};
    if (snapshot.visibility) {
        for (const auto &[body, visible] : snapshot.visibility->bodyVisible) {
            if (!doc.bodies().contains(body) || doc.bodies().at(body)->hidden == !visible)
                continue;
            const auto before = doc.bodies().at(body);
            auto after = std::make_shared<Body>(*before);
            after->hidden = !visible;
            edit.changes.push_back({body, before, after});
        }
        for (const auto &[tag, visible] : snapshot.visibility->tagVisible) {
            if (!doc.tags().contains(tag) || doc.tags().at(tag)->visible == visible)
                continue;
            const auto before = doc.tags().at(tag);
            auto after = std::make_shared<TagRecord>(*before);
            after->visible = visible;
            edit.tags.push_back({tag, before, after});
        }
    }
    return edit;
}
bool sceneRecallChangesModel(const Edit &edit) {
    return !edit.changes.empty() || !edit.tags.empty() || edit.style.has_value();
}
void recallSceneModel(Document &doc, Id id) {
    auto edit = sceneRecallEdit(doc, id);
    if (sceneRecallChangesModel(edit))
        doc.apply(std::move(edit), doc.revision());
}
} // namespace sketchy
