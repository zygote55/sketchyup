#pragma once
#include "core/scene_records.hpp"
#include <vector>
namespace sketchy {
Vec3 sceneCameraEye(const SceneCamera &camera);
// Look preserves the eye; walk uses horizontal metres in the view's yaw frame.
SceneCamera lookAround(SceneCamera camera, double yawDegrees, double pitchDegrees);
SceneCamera walkCamera(SceneCamera camera, double forward, double right, double vertical);
double sceneTransitionFraction(double fraction);
SceneCamera interpolateSceneCamera(const SceneCamera &from, const SceneCamera &to, double fraction);
struct CameraFrame {
    SceneCamera camera;
    size_t sceneIndex{}; // Visibility/state remains at this saved scene until the next keyframe.
    int index{};
    bool keyframe{};
};
struct CameraPathTiming {
    int framesPerSecond{24}, transitionSteps{24}, holdSteps{12};
};
// Includes the first pose, every transition endpoint and each requested hold step.
std::vector<CameraFrame> planCameraFrames(const std::vector<SceneCamera> &scenes,
                                          CameraPathTiming timing = {});
} // namespace sketchy
