#include "core/camera_motion.hpp"
#include <iostream>
#include <limits>
using namespace sketchy;
void check(bool value, const char *message) {
    if (!value)
        throw std::runtime_error(message);
}
void near(Vec3 a, Vec3 b, const char *message) { check(length(a - b) < 1e-8, message); }
template <class F> void rejects(F f) {
    try {
        f();
    } catch (const std::exception &) {
        return;
    }
    throw std::runtime_error("Expected rejected camera input");
}
int main() {
    try {
        SceneCamera camera{{3, 4, 2}, 0, 30, 10, 45, false};
        const auto eye = sceneCameraEye(camera);
        const auto turned = lookAround(camera, 90, 20);
        near(sceneCameraEye(turned), eye, "Look-around keeps eye fixed");
        check(turned.yaw == 90 && turned.pitch == 50, "Look changes heading and pitch");
        const auto walked = walkCamera(camera, 2, 3, 1);
        near(sceneCameraEye(walked), eye + Vec3{-2, 3, 1},
             "Walking follows yaw while preserving horizontal eye height");
        check(walked.distance == camera.distance && walked.pitch == camera.pitch,
              "Walking preserves lens and orientation");
        auto from = camera, to = camera;
        from.yaw = 170;
        to.yaw = -170;
        from.distance = 2;
        to.distance = 8;
        to.target = {9, 8, 6};
        const auto middle = interpolateSceneCamera(from, to, .5);
        check(std::abs(std::abs(middle.yaw) - 180) < 1e-10 && std::abs(middle.distance - 4) < 1e-10,
              "Shortest yaw and logarithmic distance interpolation");
        near(middle.target, (from.target + to.target) * .5, "Transition midpoint");
        check(interpolateSceneCamera(from, to, 0) == from &&
                  interpolateSceneCamera(from, to, 1) == to,
              "Exact camera endpoints");
        to.orthographic = true;
        check(interpolateSceneCamera(from, to, .9) == from &&
                  interpolateSceneCamera(from, to, 1) == to,
              "Projection changes cut exactly at keyframe");
        const auto frames = planCameraFrames({from, to, camera}, {24, 3, 2});
        check(frames.size() == 13 && frames.front().camera == from &&
                  frames.back().camera == camera,
              "Path sample budget includes holds and endpoints");
        check(frames[5].camera == to && frames[5].sceneIndex == 1 && frames[5].keyframe,
              "Scene visibility changes only at exact camera keyframe");
        check(frames[4].sceneIndex == 0 && !frames[4].keyframe,
              "Transition retains outgoing scene visibility");
        for (size_t i = 0; i < frames.size(); ++i)
            check(frames[i].index == int(i), "Stable frame indices");
        rejects([&] { planCameraFrames({from}); });
        rejects([&] { planCameraFrames({from, to}, {24, 240, 0}); });
        rejects([&] { lookAround(camera, std::numeric_limits<double>::infinity(), 0); });
        rejects([&] { walkCamera(camera, 10001, 0, 0); });
        rejects([&] { interpolateSceneCamera(from, to, -.1); });
        std::cout << "Fixed-eye look, horizontal walking and bounded camera timelines passed\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
