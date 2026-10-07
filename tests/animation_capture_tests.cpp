#include "core/scenes.hpp"
#include "core/sections.hpp"
#include "integrations/animation_capture.hpp"
#include "integrations/glb_export.hpp"
#include "io/document_io.hpp"
#include <QCoreApplication>
#include <QJsonDocument>
#include <future>
#include <iostream>
using namespace sketchy;
void check(bool value, const char *message) {
    if (!value)
        throw std::runtime_error(message);
}
template <class F> void rejects(F operation) {
    try {
        operation();
    } catch (const std::exception &) {
        return;
    }
    throw std::runtime_error("Expected rejected animation capture");
}
int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    try {
        Document doc;
        const auto body = doc.addFace({{{0, 0, 0}, {2, 0, 0}, {2, 2, 0}, {0, 2, 0}}});
        SceneSnapshot a, b;
        a.camera = SceneCamera{};
        a.visibility = SceneVisibility{{{body, true}}, {}, {}, false};
        a.solar = doc.solar();
        b = a;
        b.camera->target = {4, 2, 0};
        b.camera->yaw = 90;
        b.visibility->bodyVisible[body] = false;
        const auto first = createScene(doc, "First", a), second = createScene(doc, "Second", b);
        const auto section = createSection(doc, "Saved cut", 0, {{1, 0, 0}, -1});
        a.section = SceneSection{std::nullopt, {{0, section}}};
        a.solar->enabled = true;
        a.solar->latitude = 40;
        a.solar->time.hour = 9;
        updateScene(doc, first, a);
        const auto revision = doc.revision();
        const auto stamp = doc.saveStamp();
        const auto history = doc.canUndo();
        auto animation = AnimationCapture::capture(doc, {first, second}, {24, 2, 0});
        check(animation.frames().size() == 3 && animation.framesPerSecond() == 24,
              "Bounded immutable frame plan");
        check(animation.frame(0).visible(body) && animation.frame(1).visible(body) &&
                  !animation.frame(2).visible(body),
              "Visibility switches at saved keyframe");
        for (size_t i = 0; i < 3; ++i) {
            const auto frame = animation.frame(i);
            check(length(frame.camera().position - sceneCameraEye(animation.frames()[i].camera)) <
                      1e-8,
                  "Frame camera matches timeline");
            check(frame.sourceRevision() == revision && frame.sourceIdentity() == doc.identity(),
                  "Derived scene view retains original source provenance");
        }
        check(animation.frame(0).document().activeSections() == a.section->active &&
                  animation.frame(0).document().solar() == *a.solar &&
                  animation.frame(2).document().solar() == *b.solar,
              "Saved section activation and sun study apply only to private frame documents");
        const auto exported = exportGlb(animation.frame(0));
        check(exported.manifest["revision"].toString() == QString::number(revision) &&
                  exported.manifest["sceneView"].toString() == QString::number(first),
              "GLB manifest identifies captured revision and scene");
        check(doc.isCurrentSnapshot(stamp) && doc.canUndo() == history &&
                  doc.revision() == revision,
              "Animation capture never recalls scenes into live document");
        b.camera->yaw = -90;
        updateScene(doc, second, b);
        eraseScene(doc, first);
        const auto frozen =
            std::async(std::launch::async, [&] { return animation.frame(2); }).get();
        check(frozen.sourceRevision() == revision && !frozen.visible(body) &&
                  length(frozen.camera().position - sceneCameraEye(animation.frames()[2].camera)) <
                      1e-8,
              "Worker frames survive edits and removal of original scenes");
        rejects([&] { animation.frame(3); });
        rejects([&] { AnimationCapture::capture(doc, {first, second}); });
        b.visibility->showHidden = true;
        updateScene(doc, second, b);
        rejects([&] { AnimationCapture::capture(doc, {second, second}); });
        b.visibility->showHidden = false;
        b.section = SceneSection{std::array<double, 4>{0, 0, 1, 0}, {}};
        updateScene(doc, second, b);
        rejects([&] { AnimationCapture::capture(doc, {second, second}); });
        std::cout
            << "Immutable animation cameras, visibility, provenance and worker capture passed\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
