#pragma once
#include "core/camera_motion.hpp"
#include "core/render_settings.hpp"
#include "core/scene_records.hpp"
#include <cstdint>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <vector>
namespace sketchy {
// Named camera paths and render presets (ADR 0173 §2, §3). Both are native document
// records (schema 30, feature presentation-v1) validated like the saved scenes.
// A path keyframes saved scenes in presentation order and stores no camera poses; a
// preset carries the render settings only.
struct CameraPathRecord {
    Id id{};
    std::string name;
    std::uint32_t position{};
    std::vector<Id> keyframes;
    CameraPathTiming timing;
    bool operator==(const CameraPathRecord &) const = default;
};
struct RenderPresetRecord {
    Id id{};
    std::string name;
    RenderSettings settings;
    bool operator==(const RenderPresetRecord &) const = default;
};
using CameraPathPtr = std::shared_ptr<const CameraPathRecord>;
using CameraPathRecords = std::map<Id, CameraPathPtr>;
using RenderPresetPtr = std::shared_ptr<const RenderPresetRecord>;
using RenderPresetRecords = std::map<Id, RenderPresetPtr>;
inline constexpr size_t cameraPathCountLimit = 64, cameraPathBytesLimit = 512 * 1024;
inline constexpr size_t cameraPathKeyframeLimit = 32, cameraPathFrameLimit = 240;
inline constexpr size_t renderPresetCountLimit = 16, renderPresetBytesLimit = 256 * 1024;
size_t cameraPathBytes(const CameraPathPtr &path);
size_t renderPresetBytes(const RenderPresetPtr &preset);
// 1 + N·hold + (N−1)·transition: the frames planCameraFrames returns for N keyframes.
size_t cameraPathFrameCount(size_t keyframes, CameraPathTiming timing);
// The one refusal for a path whose planned frames pass the limit, with the formula
// (UX_DESIGN 17.11, gap 3). Every presentation surface names the limit with this text. Empty when
// the plan fits.
std::string cameraPathFrameLimitMessage(size_t keyframes, CameraPathTiming timing);
// Structure only: identities, names, positions, keyframe count, timing, the planned frame
// limit and the record budget. Keyframe scenes are not checked here, so a path whose scene
// was deleted still loads as a diagnosed broken path.
void validateCameraPathRecords(const CameraPathRecords &paths, Id next);
void validateRenderPresetRecords(const RenderPresetRecords &presets, Id next);
// The keyframe references a path cannot use: scenes that no longer exist, and scenes
// that exist but have no camera (such a scene cannot drive a transition).
struct CameraPathReferences {
    std::set<Id> sceneMissing, sceneHasNoCamera;
    bool empty() const { return sceneMissing.empty() && sceneHasNoCamera.empty(); }
    size_t size() const { return sceneMissing.size() + sceneHasNoCamera.size(); }
};
CameraPathReferences cameraPathReferences(const SceneRecords &scenes, const CameraPathRecord &path);
class Document;
// The broken paths of a document, by path ID. A healthy path is absent.
std::map<Id, CameraPathReferences> brokenCameraPaths(const Document &doc);
// Creating a path, or changing its keyframes, requires every keyframe to name a scene
// with a camera. Renaming or reordering a broken path does not.
void validateCameraPathCapture(const SceneRecords &scenes, const CameraPathRecord &path);
} // namespace sketchy
