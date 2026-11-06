#include "core/camera_motion.hpp"
#include <algorithm>
#include <cmath>
#include <iostream>
#include <limits>
#include <sstream>
#include <string>
using namespace sketchy;
void check(bool value, const char *message) {
    if (!value)
        throw std::runtime_error(message);
}
void near(Vec3 a, Vec3 b, const char *message) { check(length(a - b) < 1e-8, message); }
// One transition's midpoint as ADR 0173 defines the motion: the target, pitch and field of view
// move linearly, the yaw turns the short way round, and the distance moves logarithmically (its
// midpoint is the geometric mean).
SceneCamera midpoint(const SceneCamera &from, const SceneCamera &to) {
    double turn = std::fmod(to.yaw - from.yaw, 360.0);
    if (turn > 180)
        turn -= 360;
    else if (turn < -180)
        turn += 360;
    return {(from.target + to.target) * .5,
            from.yaw + turn / 2,
            (from.pitch + to.pitch) / 2,
            std::sqrt(from.distance * to.distance),
            (from.fieldOfView + to.fieldOfView) / 2,
            from.orthographic};
}
// Equal headings may differ by a whole turn (180 and -180).
bool sameYaw(double a, double b) {
    const double difference = std::fmod(std::abs(a - b), 360.0);
    return std::min(difference, 360 - difference) < 1e-9;
}
bool samePose(const SceneCamera &a, const SceneCamera &b) {
    return length(a.target - b.target) < 1e-9 && sameYaw(a.yaw, b.yaw) &&
           std::abs(a.pitch - b.pitch) < 1e-9 && std::abs(a.distance - b.distance) < 1e-9 &&
           std::abs(a.fieldOfView - b.fieldOfView) < 1e-9 && a.orthographic == b.orthographic;
}
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
        // ADR 0173 §6: a four-scene path, each frame against the motion the ADR defines. The second
        // transition turns the yaw across 180 degrees and takes the short way round.
        {
            const std::vector<SceneCamera> fixture{{{0, 0, 0}, -45, 35, 14, 45, false},
                                                   {{10, 0, 0}, 90, 20, 12, 50, false},
                                                   {{10, 10, 2}, 170, 60, 9, 40, false},
                                                   {{0, 10, 0}, -170, 10, 16, 35, false}};
            // Two transition steps and one hold: each scene is its keyframe, then its hold, then
            // the midpoint of the transition to the next scene (none after the last).
            struct Expected {
                SceneCamera camera;
                size_t sceneIndex;
                bool keyframe;
            };
            std::vector<Expected> expected;
            for (size_t scene = 0; scene < fixture.size(); ++scene) {
                expected.push_back({fixture[scene], scene, true});
                expected.push_back({fixture[scene], scene, false});
                if (scene + 1 < fixture.size())
                    expected.push_back({midpoint(fixture[scene], fixture[scene + 1]), scene, false});
            }
            const auto frames = planCameraFrames(fixture, {24, 2, 1});
            check(frames.size() == expected.size(), "The four-scene fixture plans eleven frames");
            for (size_t i = 0; i < frames.size(); ++i) {
                check(frames[i].index == int(i) && frames[i].sceneIndex == expected[i].sceneIndex &&
                          frames[i].keyframe == expected[i].keyframe,
                      "A planned frame keeps its scene and keyframe flag");
                if (expected[i].keyframe)
                    check(frames[i].camera == expected[i].camera,
                          "A keyframe is its scene's camera exactly");
                else
                    check(samePose(frames[i].camera, expected[i].camera),
                          "A planned pose follows the ADR's motion");
            }
            check(sameYaw(frames[8].camera.yaw, 180), "The yaw takes the short way across 180");
        }
        std::cout << "Fixed-eye look, horizontal walking and bounded camera timelines passed\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
