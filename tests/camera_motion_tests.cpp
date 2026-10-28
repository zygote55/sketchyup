#include "core/camera_motion.hpp"
#include <QByteArray>
#include <QCryptographicHash>
#include <QString>
#include <array>
#include <iomanip>
#include <iostream>
#include <limits>
#include <sstream>
using namespace sketchy;
void check(bool value, const char *message) {
    if (!value)
        throw std::runtime_error(message);
}
void near(Vec3 a, Vec3 b, const char *message) { check(length(a - b) < 1e-8, message); }
// The SHA-256 of one planned frame's index, scene, keyframe flag and camera, at 17 significant digits.
QString poseDigest(const CameraFrame &frame) {
    std::ostringstream text;
    text << std::setprecision(17) << frame.index << ' ' << frame.sceneIndex << ' '
         << frame.keyframe << ' ' << frame.camera.target.x << ' ' << frame.camera.target.y << ' '
         << frame.camera.target.z << ' ' << frame.camera.yaw << ' ' << frame.camera.pitch << ' '
         << frame.camera.distance << ' ' << frame.camera.fieldOfView << ' '
         << frame.camera.orthographic;
    return QString::fromLatin1(QCryptographicHash::hash(QByteArray::fromStdString(text.str()),
                                                        QCryptographicHash::Sha256)
                                   .toHex());
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
        // ADR 0173 §6: a four-scene path with pinned per-frame pose digests. The second transition
        // turns the yaw across 180 degrees and takes the short way round.
        {
            const std::vector<SceneCamera> fixture{{{0, 0, 0}, -45, 35, 14, 45, false},
                                                   {{10, 0, 0}, 90, 20, 12, 50, false},
                                                   {{10, 10, 2}, 170, 60, 9, 40, false},
                                                   {{0, 10, 0}, -170, 10, 16, 35, false}};
            const std::array<const char *, 11> pinned{
                "801ca383a2bb2e6cf7e29e0954d3e93007cd5eb4480a40d876970608cc581ebe",
                "93ca9389e343e7500b9406cf363f0909690f0f8bb809b986f7df491a9af52e9b",
                "8f78fb6b065a6e12063295598ecfbba3b447bc78b7198eae2137d2754b1056d5",
                "901a03d6a1c9b764e3a11272f9a60bb5ee263c2cecc47dafb894ad5625cc9edc",
                "aa8bebd12070a2c1064340b3bc0e47a3da887f8a487ee25477a8024951f2bb8d",
                "17082672c888789c75124f0e60576d46737258ed9388225309b3374981461b08",
                "a37fd5a97f479944584bf4b97666cd47188992ad04aefdeb3e42befafe0dd88c",
                "74f6e1863e8d84d08ef0d3f295bed310bd49d78704bc140a339a26cd7cd1623a",
                "4c51446a2358fcfb5971ae66158ddaf26450de74b3d7184dcf111247ef532dd8",
                "c7d0f581f0151d85034cc1b313b4340cb2dd4e35c7f19e0161ec112de9e4940a",
                "70ddb0556f4fdb36e30aa7b57802a1e84ef4784a5042397dcb08d60e9ab9a125"
            };
            const auto frames = planCameraFrames(fixture, {24, 2, 1});
            check(frames.size() == pinned.size(), "The four-scene fixture plans eleven frames");
            for (size_t i = 0; i < frames.size(); ++i)
                check(poseDigest(frames[i]) == QString::fromLatin1(pinned[i]),
                      "A planned pose matches its pinned digest");
        }
        std::cout << "Fixed-eye look, horizontal walking and bounded camera timelines passed\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
