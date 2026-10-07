#include "core/camera_motion.hpp"
#include <algorithm>
#include <cmath>
#include <numbers>
#include <stdexcept>
namespace sketchy {
namespace {
void require(bool value, const char *message) {
    if (!value)
        throw std::runtime_error(message);
}
Vec3 direction(const SceneCamera &camera) {
    const auto yaw = camera.yaw * std::numbers::pi / 180,
               pitch = camera.pitch * std::numbers::pi / 180;
    return {std::cos(pitch) * std::cos(yaw), std::cos(pitch) * std::sin(yaw), std::sin(pitch)};
}
} // namespace
Vec3 sceneCameraEye(const SceneCamera &camera) {
    camera.validate();
    return camera.target + direction(camera) * camera.distance;
}
SceneCamera lookAround(SceneCamera camera, double yawDegrees, double pitchDegrees) {
    require(std::isfinite(yawDegrees) && std::isfinite(pitchDegrees) &&
                std::abs(yawDegrees) <= 360 && std::abs(pitchDegrees) <= 180,
            "Look delta exceeds limits");
    const auto eye = sceneCameraEye(camera);
    if (yawDegrees == 0 && pitchDegrees == 0)
        return camera;
    camera.yaw = std::remainder(camera.yaw + yawDegrees, 360.);
    camera.pitch = std::clamp(camera.pitch + pitchDegrees, -89., 89.);
    camera.target = eye - direction(camera) * camera.distance;
    camera.validate();
    return camera;
}
SceneCamera walkCamera(SceneCamera camera, double forward, double right, double vertical) {
    camera.validate();
    require(std::isfinite(forward) && std::isfinite(right) && std::isfinite(vertical) &&
                std::max({std::abs(forward), std::abs(right), std::abs(vertical)}) <= 10000,
            "Walk step exceeds limits");
    const auto yaw = camera.yaw * std::numbers::pi / 180;
    camera.target =
        camera.target + Vec3{-std::cos(yaw) * forward - std::sin(yaw) * right,
                             -std::sin(yaw) * forward + std::cos(yaw) * right, vertical};
    camera.validate();
    return camera;
}
double sceneTransitionFraction(double t) {
    require(std::isfinite(t) && t >= 0 && t <= 1, "Transition fraction must be from zero to one");
    return t < .5 ? 4 * t * t * t : 1 - std::pow(-2 * t + 2, 3) / 2;
}
SceneCamera interpolateSceneCamera(const SceneCamera &from, const SceneCamera &to,
                                   double fraction) {
    from.validate();
    to.validate();
    const auto t = sceneTransitionFraction(fraction);
    if (fraction == 0)
        return from;
    if (fraction == 1)
        return to;
    // Different projections have no shared physical interpolation; cut at the keyframe.
    if (from.orthographic != to.orthographic)
        return from;
    auto camera = to;
    camera.target = from.target * (1 - t) + to.target * t;
    camera.yaw = std::remainder(from.yaw + std::remainder(to.yaw - from.yaw, 360.) * t, 360.);
    camera.pitch = from.pitch * (1 - t) + to.pitch * t;
    camera.distance = std::exp(std::log(from.distance) * (1 - t) + std::log(to.distance) * t);
    camera.fieldOfView = from.fieldOfView * (1 - t) + to.fieldOfView * t;
    camera.validate();
    return camera;
}
std::vector<CameraFrame> planCameraFrames(const std::vector<SceneCamera> &scenes,
                                          CameraPathTiming timing) {
    require(scenes.size() >= 2 && scenes.size() <= 32,
            "Camera paths require two to thirty-two scenes");
    require(timing.framesPerSecond >= 1 && timing.framesPerSecond <= 60 &&
                timing.transitionSteps >= 1 && timing.transitionSteps <= 600 &&
                timing.holdSteps >= 0 && timing.holdSteps <= 600,
            "Camera path timing exceeds limits");
    const auto count = 1 + scenes.size() * size_t(timing.holdSteps) +
                       (scenes.size() - 1) * size_t(timing.transitionSteps);
    require(count <= 240, "Camera path exceeds 240 captured frames");
    for (const auto &camera : scenes)
        camera.validate();
    std::vector<CameraFrame> result;
    result.reserve(count);
    auto append = [&](SceneCamera camera, size_t scene, bool keyframe) {
        result.push_back({camera, scene, int(result.size()), keyframe});
    };
    append(scenes.front(), 0, true);
    for (size_t i = 0; i < scenes.size(); ++i) {
        for (int hold = 0; hold < timing.holdSteps; ++hold)
            append(scenes[i], i, false);
        if (i + 1 == scenes.size())
            break;
        for (int step = 1; step <= timing.transitionSteps; ++step) {
            const auto endpoint = step == timing.transitionSteps;
            append(interpolateSceneCamera(scenes[i], scenes[i + 1],
                                          double(step) / timing.transitionSteps),
                   endpoint ? i + 1 : i, endpoint);
        }
    }
    return result;
}
} // namespace sketchy
