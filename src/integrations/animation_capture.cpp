#include "integrations/animation_capture.hpp"
namespace sketchy {
AnimationCapture::AnimationCapture(Document document, std::vector<Id> scenes,
                                   CameraPathTiming timing, RenderOptions options,
                                   std::vector<CameraFrame> frames)
    : document_(std::move(document)), scenes_(std::move(scenes)), timing_(timing),
      options_(std::move(options)), frames_(std::move(frames)) {}
AnimationCapture AnimationCapture::capture(const Document &document, std::vector<Id> scenes,
                                           CameraPathTiming timing, RenderOptions options) {
    if (scenes.size() < 2 || scenes.size() > 32)
        throw std::runtime_error("Animation requires 2–32 saved camera scenes");
    if (options.camera)
        throw std::runtime_error("Animation cameras come from saved scenes");
    std::vector<SceneCamera> cameras;
    for (const auto scene : scenes) {
        const auto found = document.scenes().find(scene);
        if (found == document.scenes().end() || !found->second->snapshot.camera)
            throw std::runtime_error("Every animation scene requires a saved camera");
        cameras.push_back(*found->second->snapshot.camera);
    }
    auto frames = planCameraFrames(cameras, timing);
    // Validate all scene state and resource limits before accepting the batch.
    for (const auto scene : scenes)
        RenderSnapshot::captureSavedScene(document, scene, options);
    return {document.readSnapshot(), std::move(scenes), timing, std::move(options),
            std::move(frames)};
}
RenderSnapshot AnimationCapture::frame(size_t index) const {
    if (index >= frames_.size())
        throw std::runtime_error("Animation frame index is out of bounds");
    const auto &pose = frames_[index];
    auto options = options_;
    options.camera = renderSceneCamera(pose.camera);
    return RenderSnapshot::captureSavedScene(document_, scenes_.at(pose.sceneIndex),
                                             std::move(options));
}
} // namespace sketchy
