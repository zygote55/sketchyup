// R099.a (ADR 0173 §2, §3): camera path and render preset records. Validation bounds, the
// missing-reference and no-camera reports, allocators, undo and redo, and the history charge.
// Each scenario builds its own document, so a later check never depends on an earlier one.
#include "core/presentation.hpp"
#include "core/scenes.hpp"
#include <iostream>
using namespace sketchy;
namespace {
void check(bool value, const char *message) {
    if (!value)
        throw std::runtime_error(message);
}
// The refusal message, or empty when the operation succeeds.
template <class F> std::string rejection(F operation) {
    try {
        operation();
    } catch (const std::exception &error) {
        return error.what();
    }
    return {};
}
template <class F> void rejects(F operation, const char *message) {
    check(!rejection(operation).empty(), message);
}
SceneSnapshot camera(double yaw = -45) {
    SceneSnapshot snapshot;
    snapshot.camera = SceneCamera{};
    snapshot.camera->yaw = yaw;
    return snapshot;
}
SceneSnapshot cameraless() {
    SceneSnapshot snapshot;
    snapshot.visibility = SceneVisibility{};
    return snapshot;
}
CameraPathTiming timing(int fps, int transition, int hold) { return {fps, transition, hold}; }
// A document with a plan view, a side view and a scene that has no camera.
struct Scenes {
    Document doc;
    Id plan{}, side{}, bare{};
    Scenes() {
        plan = createScene(doc, "Plan", camera(0));
        side = createScene(doc, "Side", camera(90));
        bare = createScene(doc, "Bare", cameraless());
    }
};
void creation() {
    Scenes s;
    auto &doc = s.doc;
    const auto path = createCameraPath(doc, "Tour", {s.plan, s.side});
    check(doc.cameraPaths().size() == 1 && doc.cameraPaths().at(path)->position == 0,
          "A path is created at the next position");
    check(doc.cameraPaths().at(path)->keyframes == std::vector<Id>{s.plan, s.side},
          "Keyframes keep their presentation order");
    check(doc.cameraPaths().at(path)->timing == CameraPathTiming{},
          "Default timing is 24 frames a second, 24 transition and 12 hold steps");
    check(cameraPathFrameCount(2, CameraPathTiming{}) == 49,
          "The planned count is 1 + N x hold + (N - 1) x transition");
    check(brokenCameraPaths(doc).empty(), "A healthy path reports no references");

    // Keyframe counts: two to thirty-two.
    rejects([&] { createCameraPath(doc, "One", {s.plan}); }, "One keyframe is refused");
    rejects([&] { createCameraPath(doc, "Empty", {}); }, "No keyframes are refused");
    rejects([&] { createCameraPath(doc, "Long", std::vector<Id>(33, s.plan)); },
            "Thirty-three keyframes are refused");
    const auto long32 = createCameraPath(doc, "Long", std::vector<Id>(32, s.plan), timing(1, 1, 0));
    check(doc.cameraPaths().at(long32)->keyframes.size() == 32,
          "Thirty-two keyframes are accepted when their plan fits");
    eraseCameraPath(doc, long32);
    check(doc.cameraPaths().size() == 1, "Refused paths change nothing");
    // A repeated keyframe is a valid presentation (a loop back to a view).
    const auto loop = createCameraPath(doc, "Loop", {s.plan, s.side, s.plan});
    check(doc.cameraPaths().at(loop)->keyframes.size() == 3, "A repeated keyframe is kept");

    // Timing ranges: 1 to 60 frames a second, 1 to 600 transition steps, 0 to 600 holds.
    rejects([&] { createCameraPath(doc, "Fps", {s.plan, s.side}, timing(0, 24, 12)); },
            "Zero frames a second is refused");
    rejects([&] { createCameraPath(doc, "Fps", {s.plan, s.side}, timing(61, 24, 12)); },
            "Sixty-one frames a second is refused");
    rejects([&] { createCameraPath(doc, "Transition", {s.plan, s.side}, timing(24, 0, 12)); },
            "Zero transition steps are refused");
    rejects([&] { createCameraPath(doc, "Transition", {s.plan, s.side}, timing(24, 601, 12)); },
            "601 transition steps are refused");
    rejects([&] { createCameraPath(doc, "Hold", {s.plan, s.side}, timing(24, 24, -1)); },
            "Negative hold steps are refused");
    check(createCameraPath(doc, "Fast", {s.plan, s.side}, timing(60, 1, 0)) != 0,
          "Sixty frames a second is accepted");

    // The 240-frame plan limit names its formula: 8 keyframes at 24 and 12 plan 265 frames.
    const std::vector<Id> eight{s.plan, s.side, s.plan, s.side, s.plan, s.side, s.plan, s.side};
    const auto overPlan = rejection([&] { createCameraPath(doc, "Over", eight); });
    check(overPlan.find("265") != std::string::npos && overPlan.find("240") != std::string::npos,
          "An over-budget plan is refused with its frame count and limit");
    check(cameraPathFrameCount(2, timing(24, 239, 0)) == 240 &&
              cameraPathFrameCount(2, timing(24, 240, 0)) == 241,
          "The plan limit is inclusive at 240 frames");
    check(createCameraPath(doc, "Exact", {s.plan, s.side}, timing(24, 239, 0)) != 0,
          "A plan of exactly 240 frames is accepted");

    // Names: one to 1024 bytes, no edge spaces, no control characters, unique.
    rejects([&] { createCameraPath(doc, "", {s.plan, s.side}); }, "An empty name is refused");
    rejects([&] { createCameraPath(doc, " Tour", {s.plan, s.side}); },
            "A leading space is refused");
    rejects([&] { createCameraPath(doc, "Tour ", {s.plan, s.side}); },
            "A trailing space is refused");
    rejects([&] { createCameraPath(doc, "A\tB", {s.plan, s.side}); },
            "A control character is refused");
    rejects([&] { createCameraPath(doc, std::string(1025, 'x'), {s.plan, s.side}); },
            "A name over 1024 bytes is refused");
    rejects([&] { createCameraPath(doc, "Tour", {s.plan, s.side}); },
            "Duplicate names are refused");
    check(createCameraPath(doc, std::string(1024, 'x'), {s.plan, s.side}) != 0,
          "A 1024-byte name is accepted");

    // Missing and camera-less keyframes are refused at creation and at keyframe change.
    rejects([&] { createCameraPath(doc, "Ghost", {s.plan, 999}); },
            "A missing scene is refused at creation");
    rejects([&] { createCameraPath(doc, "Bareness", {s.plan, s.bare}); },
            "A scene without a camera is refused at creation");
    rejects([&] { updateCameraPath(doc, path, "Tour", {s.plan, s.bare}, CameraPathTiming{}); },
            "A camera-less keyframe is refused at keyframe change");
    check(doc.cameraPaths().at(path)->keyframes == std::vector<Id>{s.plan, s.side},
          "Refused keyframe changes leave the path as it was");
}
void references() {
    Scenes s;
    auto &doc = s.doc;
    const auto path = createCameraPath(doc, "Tour", {s.plan, s.side}, timing(30, 12, 6));
    // Missing-reference report: deleting a keyframe's scene leaves the path loaded and broken.
    eraseScene(doc, s.side);
    check(doc.cameraPaths().count(path) == 1 &&
              doc.cameraPaths().at(path)->keyframes == std::vector<Id>{s.plan, s.side},
          "Deleting a scene leaves the path and its keyframes");
    const auto broken = brokenCameraPaths(doc);
    check(broken.size() == 1 && broken.at(path).sceneMissing == std::set<Id>{s.side} &&
              broken.at(path).sceneHasNoCamera.empty() && broken.at(path).size() == 1,
          "The missing-reference report names the deleted scene");
    check(cameraPathReferences(doc.scenes(), *doc.cameraPaths().at(path)).sceneMissing ==
              std::set<Id>{s.side},
          "The per-path report agrees with the document report");
    // A broken path can be renamed or retimed, keeping its diagnosed keyframes.
    updateCameraPath(doc, path, "Tour renamed", {s.plan, s.side}, timing(24, 24, 12));
    check(brokenCameraPaths(doc).size() == 1 && doc.cameraPaths().at(path)->name == "Tour renamed",
          "A broken path can be renamed without naming the missing scene again");
    // Replacing the broken keyframes repairs the path.
    updateCameraPath(doc, path, "Tour renamed", {s.plan, s.plan}, timing(24, 24, 12));
    check(brokenCameraPaths(doc).empty(), "Replacing the broken keyframes repairs the path");
    // A keyframe change to a missing scene is refused.
    rejects([&] { updateCameraPath(doc, path, "Tour renamed", {s.plan, 999}, timing(24, 24, 12)); },
            "A keyframe change to a missing scene is refused");
    // Undo reverts the repair: the path is broken again, with its diagnosed keyframes.
    doc.undo();
    check(brokenCameraPaths(doc).size() == 1 &&
              brokenCameraPaths(doc).at(path).sceneMissing == std::set<Id>{s.side},
          "Undo restores the broken path and its diagnosed keyframe");

    // The no-camera report: a scene that loses its camera keeps its path, reported.
    updateScene(doc, s.plan, cameraless());
    const auto noCamera = brokenCameraPaths(doc);
    // The path still holds the deleted side view, so both reasons are reported, each apart.
    check(noCamera.size() == 1 && noCamera.at(path).sceneHasNoCamera == std::set<Id>{s.plan} &&
              noCamera.at(path).sceneMissing == std::set<Id>{s.side},
          "The no-camera report names the scene that has no camera, apart from missing scenes");
    rejects([&] { createCameraPath(doc, "Plan again", {s.plan, s.bare}); },
            "A new path cannot use a camera-less scene");
    updateScene(doc, s.plan, camera(0));
    check(brokenCameraPaths(doc).empty() == false, "The deleted scene stays reported");

    // Deleting a broken path clears its report.
    eraseCameraPath(doc, path);
    check(brokenCameraPaths(doc).empty() && doc.cameraPaths().empty(),
          "Deleting the broken path clears the report");
}
void allocators() {
    Scenes s;
    auto &doc = s.doc;
    const auto first = createCameraPath(doc, "First", {s.plan, s.side});
    const auto second = createCameraPath(doc, "Second", {s.plan, s.side});
    check(second == first + 1 && doc.nextCameraPathId() == second + 1,
          "Path identities ascend from the allocator");
    eraseCameraPath(doc, first);
    check(doc.nextCameraPathId() == second + 1, "Deleting a path does not lower the allocator");
    const auto third = createCameraPath(doc, "Third", {s.plan, s.side});
    check(third == second + 1, "A deleted identity is not reused");
    rejects(
        [&] {
            Edit edit{"Reuse", {}};
            edit.cameraPaths.push_back(
                {first, nullptr,
                 std::make_shared<CameraPathRecord>(
                     CameraPathRecord{first, "Reused", 0, {s.plan, s.side}, {}})});
            doc.apply(std::move(edit), doc.revision());
        },
        "A retired camera path identity cannot be reused");
    check(doc.cameraPaths().size() == 2 && doc.cameraPaths().at(second)->position == 0 &&
              doc.cameraPaths().at(third)->position == 1,
          "Deleting a path compacts the order");

    const auto preset = createRenderPreset(doc, "Final");
    check(preset == 1 && doc.nextRenderPresetId() == 2, "Presets have their own allocator");
    eraseRenderPreset(doc, preset);
    check(doc.nextRenderPresetId() == 2, "Deleting a preset does not lower its allocator");
    const auto again = createRenderPreset(doc, "Final");
    check(again == 2, "A deleted preset identity is not reused");
    rejects(
        [&] {
            Edit edit{"Reuse", {}};
            edit.renderPresets.push_back(
                {preset, nullptr, std::make_shared<RenderPresetRecord>(RenderPresetRecord{preset, "Reused", {}})});
            doc.apply(std::move(edit), doc.revision());
        },
        "A retired render preset identity cannot be reused");
}
void history() {
    Scenes s;
    auto &doc = s.doc;
    doc.markSaved();
    const auto saved = doc.saveStamp();
    const auto path = createCameraPath(doc, "Second", {s.plan, s.side});
    const auto beforeEdit = *doc.cameraPaths().at(path);
    // One undoable entry per edit; undo restores the exact record.
    const auto entries = doc.history().total;
    updateCameraPath(doc, path, "Second edited", {s.side, s.plan}, timing(30, 12, 6));
    check(doc.history().total == entries + 1, "A path edit is one history entry");
    check(*doc.cameraPaths().at(path) != beforeEdit, "The edit changes the path");
    doc.undo();
    check(*doc.cameraPaths().at(path) == beforeEdit, "Undo restores the exact record");
    doc.redo();
    check(doc.cameraPaths().at(path)->name == "Second edited" &&
              doc.cameraPaths().at(path)->timing == timing(30, 12, 6),
          "Redo restores the edit");
    doc.undo();
    // Undo of a delete restores the table, its order and the allocator.
    const auto snapshot = doc.readSnapshot();
    eraseCameraPath(doc, path);
    check(doc.cameraPaths().empty(), "The delete removes the path");
    doc.undo();
    check(doc.cameraPaths().size() == 1 && doc.cameraPaths().at(path)->position == 0 &&
              snapshot.cameraPaths() == doc.cameraPaths() &&
              doc.nextCameraPathId() == path + 1,
          "Undo of a delete restores the path, its order and the allocator");
    // Undoing back to the saved state clears the dirty flag.
    doc.undo();
    check(doc.isCurrentSnapshot(saved) && !doc.dirty(), "Undo to the saved state is clean");
    // The history charge: a path edit is charged its records, as scene edits are.
    const auto historyBefore = doc.historyBytes();
    const auto created = createCameraPath(doc, "Charged", {s.plan, s.side});
    check(doc.historyBytes() - historyBefore >=
              sizeof(CameraPathChange) + cameraPathBytes(doc.cameraPaths().at(created)),
          "A path creation is charged its record");
    const auto presetBefore = doc.historyBytes();
    const auto preset = createRenderPreset(doc, "Final");
    check(doc.historyBytes() - presetBefore >=
              sizeof(RenderPresetChange) + renderPresetBytes(doc.renderPresets().at(preset)),
          "A preset creation is charged its record");
    check(doc.readSnapshotBytes() > snapshot.readSnapshotBytes(),
          "Retained bytes count the presentation tables");
}
void presets() {
    Document doc;
    const auto final = createRenderPreset(
        doc, "Final", RenderSettings{3840, 2160, 256, 0, RenderEngine::Cycles});
    check(doc.renderPresets().at(final)->settings.width == 3840 &&
              doc.renderPresets().at(final)->settings.engine == RenderEngine::Cycles,
          "A preset keeps its settings");
    const auto preview = createRenderPreset(
        doc, "Preview", RenderSettings{64, 64, 1, 1000000, RenderEngine::Eevee});
    check(doc.renderPresets().at(preview)->settings.seed == 1000000,
          "The seed limit is inclusive");
    rejects([&] { createRenderPreset(doc, "Narrow", RenderSettings{63, 768}); },
            "A width under 64 is refused");
    rejects([&] { createRenderPreset(doc, "Wide", RenderSettings{4097, 768}); },
            "A width over 4096 is refused");
    rejects([&] { createRenderPreset(doc, "Tall", RenderSettings{1024, 4097}); },
            "A height over 4096 is refused");
    rejects([&] { createRenderPreset(doc, "Samples", RenderSettings{1024, 768, 0}); },
            "Zero samples are refused");
    rejects([&] { createRenderPreset(doc, "Samples", RenderSettings{1024, 768, 1025}); },
            "More than 1024 samples are refused");
    rejects([&] { createRenderPreset(doc, "Seed", RenderSettings{1024, 768, 32, -1}); },
            "A negative seed is refused");
    rejects([&] { createRenderPreset(doc, "Seed", RenderSettings{1024, 768, 32, 1000001}); },
            "A seed over 1,000,000 is refused");
    rejects([&] { createRenderPreset(doc, "Final"); }, "Preset names are unique");
    rejects([&] { createRenderPreset(doc, " "); }, "A blank preset name is refused");
    check(doc.renderPresets().size() == 2, "Refused presets change nothing");
    updateRenderPreset(doc, final, "Final", RenderSettings{1920, 1080, 128, 7, RenderEngine::Eevee});
    check(doc.renderPresets().at(final)->settings.seed == 7, "A preset update applies");
    doc.undo();
    check(doc.renderPresets().at(final)->settings.width == 3840, "Undo restores the preset");
    eraseRenderPreset(doc, preview);
    check(doc.renderPresets().size() == 1, "Deleting a preset");
    doc.undo();
    check(doc.renderPresets().size() == 2, "Undo restores a deleted preset");
}
void limits() {
    Scenes s;
    auto &doc = s.doc;
    for (size_t i = 0; i < cameraPathCountLimit; ++i)
        createCameraPath(doc, "Path " + std::to_string(i), {s.plan, s.side});
    rejects([&] { createCameraPath(doc, "Path 64", {s.plan, s.side}); },
            "A 65th camera path is refused");
    check(doc.cameraPaths().size() == cameraPathCountLimit, "The 64-path limit holds");
    for (size_t i = 0; i < renderPresetCountLimit; ++i)
        createRenderPreset(doc, "Preset " + std::to_string(i));
    rejects([&] { createRenderPreset(doc, "Preset 16"); }, "A 17th preset is refused");
    check(doc.renderPresets().size() == renderPresetCountLimit, "The 16-preset limit holds");
    // Snapshots share the tables and their allocators.
    const auto shared = doc.readSnapshot();
    check(shared.cameraPaths() == doc.cameraPaths() &&
              shared.renderPresets() == doc.renderPresets() &&
              shared.nextCameraPathId() == doc.nextCameraPathId() &&
              shared.nextRenderPresetId() == doc.nextRenderPresetId(),
          "A snapshot shares the presentation tables and allocators");
    check(cameraPathFrameCount(32, CameraPathTiming{}) == 1 + 32 * 12 + 31 * 24,
          "The frame formula covers the largest path");
}
} // namespace
int main() {
    try {
        creation();
        references();
        allocators();
        history();
        presets();
        limits();
        std::cout << "presentation records: all checks passed\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << "presentation record test failed: " << error.what() << '\n';
        return 1;
    }
}
