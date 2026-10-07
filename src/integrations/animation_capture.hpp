#pragma once
#include "core/camera_motion.hpp"
#include "integrations/render_snapshot.hpp"
namespace sketchy {
// Freeze once on the document owner thread. Prepare individual frames on workers.
class AnimationCapture {
  public:
    static AnimationCapture capture(const Document &document, std::vector<Id> scenes,
                                    CameraPathTiming timing = {}, RenderOptions options = {});
    RenderSnapshot frame(size_t index) const;
    const std::vector<CameraFrame> &frames() const { return frames_; }
    const std::vector<Id> &scenes() const { return scenes_; }
    int framesPerSecond() const { return timing_.framesPerSecond; }
    const std::string &sourceIdentity() const { return document_.identity(); }
    std::uint64_t sourceRevision() const { return document_.revision(); }

  private:
    AnimationCapture(Document document, std::vector<Id> scenes, CameraPathTiming timing,
                     RenderOptions options, std::vector<CameraFrame> frames);
    Document document_;
    std::vector<Id> scenes_;
    CameraPathTiming timing_;
    RenderOptions options_;
    std::vector<CameraFrame> frames_;
};
} // namespace sketchy
