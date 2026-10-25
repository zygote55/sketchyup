#include "core/presentation_records.hpp"
#include "core/model.hpp"
#include <algorithm>
namespace sketchy {
namespace {
void validateName(const std::string &name, const char *kind) {
    if (name.empty() || name.size() > 1024 || name.front() == ' ' || name.back() == ' ' ||
        std::any_of(name.begin(), name.end(),
                    [](unsigned char c) { return c < 32 || c == 127; }))
        throw std::runtime_error(std::string("Invalid ") + kind + " identity or name");
}
void validateTiming(CameraPathTiming timing) {
    if (timing.framesPerSecond < 1 || timing.framesPerSecond > 60 || timing.transitionSteps < 1 ||
        timing.transitionSteps > 600 || timing.holdSteps < 0 || timing.holdSteps > 600)
        throw std::runtime_error("Camera path timing exceeds limits");
}
} // namespace
size_t cameraPathBytes(const CameraPathPtr &path) {
    if (!path)
        return 0;
    return sizeof(CameraPathRecord) + path->name.size() + path->keyframes.size() * sizeof(Id) + 64;
}
size_t renderPresetBytes(const RenderPresetPtr &preset) {
    if (!preset)
        return 0;
    return sizeof(RenderPresetRecord) + preset->name.size() + 64;
}
size_t cameraPathFrameCount(size_t keyframes, CameraPathTiming timing) {
    if (!keyframes)
        return 0;
    return 1 + keyframes * size_t(timing.holdSteps) + (keyframes - 1) * size_t(timing.transitionSteps);
}
void validateCameraPathRecords(const CameraPathRecords &paths, Id next) {
    if (!next || paths.size() > cameraPathCountLimit)
        throw std::runtime_error("Invalid camera path allocator or count");
    std::set<std::string> names;
    std::set<std::uint32_t> positions;
    size_t bytes = 0;
    for (const auto &[id, path] : paths) {
        if (!path || !id || id >= next || path->id != id)
            throw std::runtime_error("Invalid camera path identity");
        validateName(path->name, "camera path");
        if (!names.insert(path->name).second)
            throw std::runtime_error("Camera path names must be unique");
        if (path->position >= paths.size() || !positions.insert(path->position).second)
            throw std::runtime_error("Camera path order must contain each position exactly once");
        validateTiming(path->timing);
        const auto keyframes = path->keyframes.size();
        if (keyframes < 2 || keyframes > cameraPathKeyframeLimit)
            throw std::runtime_error("Camera paths need two to thirty-two keyframes");
        for (const auto scene : path->keyframes)
            if (!scene)
                throw std::runtime_error("Camera path keyframes must name saved scenes");
        const auto frames = cameraPathFrameCount(keyframes, path->timing);
        if (frames > cameraPathFrameLimit)
            throw std::runtime_error(
                "Camera path would plan " + std::to_string(frames) + " frames (1 + " +
                std::to_string(keyframes) + " x " + std::to_string(path->timing.holdSteps) +
                " + " + std::to_string(keyframes - 1) + " x " +
                std::to_string(path->timing.transitionSteps) + "); the limit is 240");
        bytes += cameraPathBytes(path);
        if (bytes > cameraPathBytesLimit)
            throw std::runtime_error("Camera paths exceed the 512 KiB record budget");
    }
}
void validateRenderPresetRecords(const RenderPresetRecords &presets, Id next) {
    if (!next || presets.size() > renderPresetCountLimit)
        throw std::runtime_error("Invalid render preset allocator or count");
    std::set<std::string> names;
    size_t bytes = 0;
    for (const auto &[id, preset] : presets) {
        if (!preset || !id || id >= next || preset->id != id)
            throw std::runtime_error("Invalid render preset identity");
        validateName(preset->name, "render preset");
        if (!names.insert(preset->name).second)
            throw std::runtime_error("Render preset names must be unique");
        validateRenderSettings(preset->settings);
        bytes += renderPresetBytes(preset);
        if (bytes > renderPresetBytesLimit)
            throw std::runtime_error("Render presets exceed the 256 KiB record budget");
    }
}
CameraPathReferences cameraPathReferences(const SceneRecords &scenes, const CameraPathRecord &path) {
    CameraPathReferences result;
    for (const auto scene : path.keyframes) {
        const auto found = scenes.find(scene);
        if (found == scenes.end())
            result.sceneMissing.insert(scene);
        else if (!found->second->snapshot.camera)
            result.sceneHasNoCamera.insert(scene);
    }
    return result;
}
std::map<Id, CameraPathReferences> brokenCameraPaths(const Document &doc) {
    std::map<Id, CameraPathReferences> result;
    for (const auto &[id, path] : doc.cameraPaths()) {
        auto references = cameraPathReferences(doc.scenes(), *path);
        if (!references.empty())
            result.emplace(id, std::move(references));
    }
    return result;
}
void validateCameraPathCapture(const SceneRecords &scenes, const CameraPathRecord &path) {
    if (!cameraPathReferences(scenes, path).empty())
        throw std::runtime_error(
            "New camera path keyframes require existing saved scenes with cameras");
}
} // namespace sketchy
