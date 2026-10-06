#include "core/scene_records.hpp"
#include "core/model.hpp"
#include <algorithm>
namespace sketchy {
void SceneCamera::validate() const {
    for (const auto value : {target.x, target.y, target.z})
        if (!std::isfinite(value) || std::abs(value) > 1e9)
            throw std::runtime_error("Scene camera target must be within one billion metres");
    if (!std::isfinite(yaw) || yaw < -180 || yaw > 180 || !std::isfinite(pitch) || pitch < -90 ||
        pitch > 90)
        throw std::runtime_error("Scene camera requires canonical yaw and pitch in degrees");
    if (!std::isfinite(distance) || distance < .05 || distance > 1e7)
        throw std::runtime_error("Scene camera distance must be from 0.05 to ten million metres");
    if (!std::isfinite(fieldOfView) || fieldOfView < 5 || fieldOfView > 120)
        throw std::runtime_error("Scene field of view must be from 5 to 120 degrees");
}
void SceneEntity::validate() const {
    if (!body || (kind == SceneEntityKind::Body ? entity != 0 : entity == 0))
        throw std::runtime_error("Scene visibility requires typed nonzero entity identities");
    switch (kind) {
    case SceneEntityKind::Body:
    case SceneEntityKind::Face:
    case SceneEntityKind::Edge:
    case SceneEntityKind::Guide:
        return;
    }
    throw std::runtime_error("Unknown scene visibility entity kind");
}
void SceneVisibility::validate() const {
    if (bodyVisible.size() > 10000 || tagVisible.size() > 1024 || hiddenEntities.size() > 50000)
        throw std::runtime_error("Scene visibility exceeds its reference budget");
    for (const auto &[id, visible] : bodyVisible)
        if (!id)
            throw std::runtime_error("Scene body identity must be nonzero");
    for (const auto &[id, visible] : tagVisible)
        if (!id)
            throw std::runtime_error("Untagged visibility cannot be overridden by a scene");
    for (const auto &entity : hiddenEntities) {
        entity.validate();
        if (!bodyVisible.contains(entity.body))
            throw std::runtime_error(
                "Hidden scene entities require a captured body visibility scope");
    }
}
void SceneSection::validate() const {
    if (active.size() > sectionRecordLimit)
        throw std::runtime_error("Scene section activation exceeds its reference budget");
    std::set<Id> ids;
    for (const auto &[context, id] : active)
        if (!id || !ids.insert(id).second)
            throw std::runtime_error("Scene section activation requires unique nonzero plane IDs");
    if (!plane)
        return;
    for (const auto value : *plane)
        if (!std::isfinite(value) || std::abs(value) > 1e9)
            throw std::runtime_error("Scene section coefficients must be finite and bounded");
    const auto &p = *plane;
    const auto squared = p[0] * p[0] + p[1] * p[1] + p[2] * p[2];
    if (std::abs(squared - 1) > 1e-8)
        throw std::runtime_error("Scene section plane requires a unit normal");
}
void SceneSnapshot::validate() const {
    if (!camera && !visibility && !style && !section)
        throw std::runtime_error("A scene must control at least one view property");
    if (camera)
        camera->validate();
    if (visibility)
        visibility->validate();
    if (style)
        style->validate();
    if (section)
        section->validate();
}
size_t sceneBytes(const ScenePtr &scene) {
    if (!scene)
        return 0;
    const auto &visibility = scene->snapshot.visibility;
    return sizeof(SceneRecord) + scene->name.size() + 64 +
           (scene->snapshot.section ? scene->snapshot.section->active.size() * 64 : 0) +
           (visibility ? (visibility->bodyVisible.size() + visibility->tagVisible.size()) * 64 +
                             visibility->hiddenEntities.size() * 80
                       : 0);
}
void validateSceneRecords(const SceneRecords &scenes, Id next) {
    if (!next || scenes.size() > sceneCountLimit)
        throw std::runtime_error("Invalid scene allocator or count");
    std::set<std::string> names;
    std::set<std::uint32_t> positions;
    size_t bytes = 0;
    for (const auto &[id, scene] : scenes) {
        if (!scene || !id || id >= next || scene->id != id || scene->name.empty() ||
            scene->name.size() > 1024 || scene->name.front() == ' ' || scene->name.back() == ' ' ||
            std::any_of(scene->name.begin(), scene->name.end(),
                        [](unsigned char c) { return c < 32 || c == 127; }))
            throw std::runtime_error("Invalid scene identity or name");
        if (!names.insert(scene->name).second)
            throw std::runtime_error("Scene names must be unique");
        if (scene->position >= scenes.size() || !positions.insert(scene->position).second)
            throw std::runtime_error("Scene order must contain each position exactly once");
        scene->snapshot.validate();
        bytes += sceneBytes(scene);
        if (bytes > sceneBytesLimit)
            throw std::runtime_error("Scenes exceed the eight MiB record budget");
    }
}
MissingSceneReferences missingSceneReferences(const Document &doc, const SceneSnapshot &snapshot) {
    MissingSceneReferences missing;
    if (snapshot.section)
        for (const auto &[context, id] : snapshot.section->active)
            if (!doc.sections().contains(id) || doc.sections().at(id)->context != context ||
                (context && !doc.bodies().contains(context)))
                missing.sections.insert(id);
    if (!snapshot.visibility)
        return missing;
    const auto &visibility = *snapshot.visibility;
    for (const auto &[id, visible] : visibility.bodyVisible)
        if (!doc.bodies().contains(id))
            missing.bodies.insert(id);
    for (const auto &[id, visible] : visibility.tagVisible)
        if (!doc.tags().contains(id))
            missing.tags.insert(id);
    for (const auto &entity : visibility.hiddenEntities) {
        const auto found = doc.bodies().find(entity.body);
        bool exists = found != doc.bodies().end();
        if (exists) {
            const auto &body = *found->second;
            switch (entity.kind) {
            case SceneEntityKind::Body:
                break;
            case SceneEntityKind::Face:
                exists = body.surface.faces.contains(entity.entity);
                break;
            case SceneEntityKind::Edge:
                exists = body.topology.edges.contains(entity.entity);
                break;
            case SceneEntityKind::Guide:
                exists = body.guides.contains(entity.entity);
                break;
            default:
                exists = false;
            }
        }
        if (!exists)
            missing.entities.insert(entity);
    }
    return missing;
}
void validateSceneCapture(const Document &doc, const SceneSnapshot &snapshot) {
    snapshot.validate();
    if (!missingSceneReferences(doc, snapshot).empty())
        throw std::runtime_error(
            "New scene snapshots require existing body, tag, geometry and section references");
    if (snapshot.section)
        validateSectionDepth(doc, &snapshot.section->active);
}
} // namespace sketchy
