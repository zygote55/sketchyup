#include "automation/commands.hpp"
#include "automation/inspection.hpp"
#include "automation/presentation_commands.hpp"
#include "automation/staging.hpp"
#include "core/presentation_records.hpp"
#include "core/scenes.hpp"
#include "io/document_io.hpp"
#include <QCoreApplication>
#include <QJsonArray>
#include <QStringList>
#include <iostream>

using namespace sketchy;

namespace {

void check(bool value, const char *message) {
    if (!value)
        throw std::runtime_error(message);
}

template <class F> void rejects(F operation, const char *message) {
    try {
        operation();
    } catch (const std::exception &) {
        return;
    }
    throw std::runtime_error(message);
}

QJsonObject batch(const Document &doc, QJsonArray commands) {
    return {{"apiVersion", 1},
            {"documentId", QString::fromStdString(doc.identity())},
            {"expectedRevision", QString::number(doc.revision())},
            {"commands", commands}};
}

QJsonObject query(const Document &doc, const char *name, QJsonObject extra = {}) {
    extra["apiVersion"] = 1;
    extra["documentId"] = QString::fromStdString(doc.identity());
    extra["expectedRevision"] = QString::number(doc.revision());
    extra["query"] = name;
    return extra;
}

QJsonObject timing(int framesPerSecond, int transition, int hold) {
    return {
        {"framesPerSecond", framesPerSecond}, {"transitionSteps", transition}, {"holdSteps", hold}};
}

QJsonArray scenes(std::initializer_list<Id> ids) {
    QJsonArray result;
    for (const auto id : ids)
        result.append(QString::number(id));
    return result;
}

QJsonObject createPath(const char *name, QJsonArray keyframes, QJsonObject pathTiming) {
    return {{"command", "camera_path.create"},
            {"name", name},
            {"keyframes", keyframes},
            {"timing", pathTiming}};
}

QJsonObject createPreset(const char *name, int width, int height) {
    return {{"command", "render_preset.create"},
            {"name", name},
            {"width", width},
            {"height", height},
            {"samples", 1},
            {"seed", 0},
            {"engine", "cycles"}};
}

QJsonObject describe(const Document &doc, const char *name, QJsonObject extra) {
    return inspectDocument(doc, query(doc, name, extra))["data"].toObject();
}

} // namespace

int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    try {
        Document doc;
        doc.addFace({{{0, 0, 0}, {1, 0, 0}, {1, 1, 0}, {0, 1, 0}}});
        SceneSnapshot view;
        view.camera = SceneCamera{};
        const auto front = createScene(doc, "Front", view);
        view.camera->yaw = 90;
        const auto side = createScene(doc, "Side", view);
        view.camera->yaw = 180;
        const auto back = createScene(doc, "Back", view);
        SceneSnapshot bare;
        bare.style = doc.style(); // A scene needs one view property; it has no camera.
        const auto bareScene = createScene(doc, "Bare", bare);
        doc.markSaved();

        // Create, describe, undo and redo a camera path. Eight frames: 1 + 3 x 1 + 2 x 2.
        executeBatch(
            doc, batch(doc, {createPath("Orbit", scenes({front, side, back}), timing(24, 2, 1))}));
        check(doc.cameraPaths().size() == 1, "Create adds one camera path");
        const auto orbit = doc.cameraPaths().begin()->first;
        const auto info = describe(doc, "camera_path.describe", {{"path", QString::number(orbit)}});
        check(info["name"] == "Orbit" && info["frames"] == 8 && info["broken"] == false &&
                  info["keyframes"].toArray().size() == 3,
              "camera_path.describe reports the planned frame count and keyframes");
        check(info["keyframes"].toArray()[0].toObject()["name"] == "Front",
              "camera_path.describe names each keyframe scene");
        doc.undo();
        check(doc.cameraPaths().empty(), "Undo removes the created camera path");
        doc.redo();
        check(doc.cameraPaths().size() == 1, "Redo restores the camera path");

        // Staged preview and apply carry a camera path change too (the batch path, not only
        // executeBatch).
        StagingSession staging;
        const auto stage = staging
                               .prepare(doc, batch(doc, {createPath("Staged", scenes({front, back}),
                                                                    timing(24, 1, 0))}))["stageId"]
                               .toString();
        const auto changes = staging.changes(doc, stage)["changes"].toArray();
        check(changes.size() == 1 && changes[0].toObject()["kind"] == "camera_path",
              "A staged camera path names its resource");
        doc.applyPrepared(*staging.proposal(doc, stage));
        check(doc.cameraPaths().size() == 2, "A staged camera path applies");
        doc.undo();

        // Update: a rename keeps the keyframes; a broken keyframe scene is refused.
        executeBatch(doc, batch(doc, {QJsonObject{{"command", "camera_path.update"},
                                                  {"path", QString::number(orbit)},
                                                  {"name", "Orbit A"}}}));
        check(doc.cameraPaths().at(orbit)->name == "Orbit A", "Update renames a camera path");
        rejects(
            [&] {
                executeBatch(doc,
                             batch(doc, {QJsonObject{{"command", "camera_path.update"},
                                                     {"path", QString::number(orbit)},
                                                     {"keyframes", scenes({front, bareScene})}}}));
            },
            "A keyframe scene without a camera is refused");
        rejects(
            [&] {
                executeBatch(doc, batch(doc, {QJsonObject{{"command", "camera_path.update"},
                                                          {"path", QString::number(orbit)}}}));
            },
            "A camera path update must name a change");

        // Validation refusals match the R099.a bounds.
        rejects(
            [&] {
                executeBatch(doc,
                             batch(doc, {createPath("One", scenes({front}), timing(24, 2, 1))}));
            },
            "A camera path needs two keyframes");
        rejects(
            [&] {
                executeBatch(doc, batch(doc, {createPath("Missing", scenes({front, 999}),
                                                         timing(24, 2, 1))}));
            },
            "A keyframe must name an existing scene");
        rejects(
            [&] {
                executeBatch(doc, batch(doc, {createPath("Bare", scenes({front, bareScene}),
                                                         timing(24, 2, 1))}));
            },
            "A keyframe scene must have a camera");
        rejects(
            [&] {
                executeBatch(
                    doc, batch(doc, {createPath("Fast", scenes({front, side}), timing(61, 2, 1))}));
            },
            "Frames per second are limited to sixty");
        rejects(
            [&] {
                executeBatch(doc, batch(doc, {createPath("Long", scenes({front, side, back}),
                                                         timing(24, 2, 100))}));
            },
            "A path over 240 planned frames is refused");
        rejects(
            [&] {
                executeBatch(doc, batch(doc, {createPath("Orbit A", scenes({front, side}),
                                                         timing(24, 2, 1))}));
            },
            "Camera path names are unique");

        // Delete renumbers the order so the remaining positions stay a permutation.
        executeBatch(doc,
                     batch(doc, {createPath("Second", scenes({side, back}), timing(24, 1, 0))}));
        const auto second = doc.cameraPaths().rbegin()->first;
        executeBatch(doc, batch(doc, {QJsonObject{{"command", "camera_path.delete"},
                                                  {"path", QString::number(orbit)}}}));
        check(doc.cameraPaths().size() == 1 && doc.cameraPaths().begin()->second->position == 0,
              "Delete keeps the remaining order contiguous");
        doc.undo();
        check(doc.cameraPaths().size() == 2, "Undo restores a deleted camera path");

        // Render presets: create, update the settings, describe, delete, and refuse bad settings.
        executeBatch(doc, batch(doc, {createPreset("Draft", 64, 64)}));
        const auto draft = doc.renderPresets().begin()->first;
        const auto presetInfo =
            describe(doc, "render_preset.describe", {{"preset", QString::number(draft)}});
        check(presetInfo["settings"].toObject()["width"] == 64 &&
                  presetInfo["settings"].toObject()["engine"] == "cycles",
              "render_preset.describe reports the settings");
        executeBatch(doc, batch(doc, {QJsonObject{{"command", "render_preset.update"},
                                                  {"preset", QString::number(draft)},
                                                  {"width", 80}}}));
        check(doc.renderPresets().at(draft)->settings.width == 80 &&
                  doc.renderPresets().at(draft)->settings.height == 64,
              "Update changes only the named render setting");
        doc.undo();
        check(doc.renderPresets().at(draft)->settings.width == 64, "Undo restores the preset");
        doc.redo();
        rejects([&] { executeBatch(doc, batch(doc, {createPreset("Narrow", 63, 64)})); },
                "A preset below 64 pixels is refused");
        rejects(
            [&] {
                executeBatch(doc, batch(doc, {QJsonObject{{"command", "render_preset.create"},
                                                          {"name", "Engine"},
                                                          {"width", 64},
                                                          {"height", 64},
                                                          {"samples", 1},
                                                          {"seed", 0},
                                                          {"engine", "gpu"}}}));
            },
            "An unknown render engine is refused");
        executeBatch(doc, batch(doc, {QJsonObject{{"command", "render_preset.delete"},
                                                  {"preset", QString::number(draft)}}}));
        check(doc.renderPresets().empty(), "Delete removes the render preset");
        doc.undo();
        check(doc.renderPresets().size() == 1, "Undo restores the deleted render preset");

        // A mixed batch keeps both kinds of change.
        executeBatch(doc,
                     batch(doc, {createPreset("Check", 64, 80),
                                 createPath("Mixed", scenes({front, back}), timing(12, 1, 0))}));
        check(doc.renderPresets().size() == 2 && doc.cameraPaths().size() == 3,
              "A mixed batch keeps the camera path and the preset");

        // Presentation descriptions: a plan is ready, or every refusal is named.
        const auto mixed = doc.cameraPaths().rbegin()->first;
        const auto check64 = doc.renderPresets().rbegin()->first;
        const auto ready = describe(
            doc, "presentation.describe",
            {{"path", QString::number(mixed)}, {"presets", scenes({check64})}, {"video", false}});
        check(ready["framesPerPreset"] == 2 && ready["totalFrames"] == 2 &&
                  ready["presets"].toArray()[0].toObject()["directory"] == "01-Check",
              "presentation.describe plans frames and the first preset folder");
        const auto blender = ready["blender"].toObject()["available"].toBool();
        check((ready["refusals"].toArray().isEmpty()) == (blender && ready["ready"].toBool()),
              "A presentation plan is ready only when it has no refusals");
        check(ready["budget"].toObject()["limitBytes"] == 2147483648LL &&
                  ready["budget"].toObject()["exceedsLimit"] == false,
              "presentation.describe reports the 2 GiB budget");

        const auto odd = [&] {
            executeBatch(doc, batch(doc, {createPreset("Odd", 81, 64)}));
            return doc.renderPresets().rbegin()->first;
        }();
        const auto videoPlan = presentationDescription(doc, mixed, {odd}, true);
        check(videoPlan["refusals"].toArray().size() >= 1 && videoPlan["ready"] == false,
              "A video names the even-size rule for an odd preset");
        const auto tooMany =
            presentationDescription(doc, mixed, {check64, check64, check64, check64, odd}, false);
        check(tooMany["refusals"].toArray().size() >= 1,
              "A presentation with more than four presets names the limit");

        // A broken path is reported, not thrown, and the refusal names the missing scene.
        executeBatch(doc, batch(doc, {QJsonObject{{"command", "saved_scene.delete"},
                                                  {"scene", QString::number(side)}}}));
        const auto broken =
            describe(doc, "camera_path.describe", {{"path", QString::number(second)}});
        check(broken["broken"] == true, "A path whose scene is gone reports broken");
        check(presentationDescription(doc, second, {check64}, false)["refusals"].toArray().size() >=
                  1,
              "A broken path names a refusal in presentation.describe");

        // Unknown identities are not found; the describe queries never create anything.
        rejects([&] { describe(doc, "camera_path.describe", {{"path", "999"}}); },
                "An unknown camera path is not found");
        rejects([&] { describe(doc, "render_preset.describe", {{"preset", "999"}}); },
                "An unknown render preset is not found");

        // Exposure: the six commands are catalogue commands; the three describes are queries; no
        // presentation execution exists over any surface (ADR 0173 decision 12).
        QStringList commandNames;
        for (const auto &entry : commandCatalog())
            commandNames.append(entry.toObject()["name"].toString());
        for (const auto *name :
             {"camera_path.create", "camera_path.update", "camera_path.delete",
              "render_preset.create", "render_preset.update", "render_preset.delete"})
            check(commandNames.contains(name),
                  "Every camera path and render preset command is in the catalogue");
        QStringList queryNames;
        for (const auto &entry : inspectionCatalog())
            queryNames.append(entry.toObject()["name"].toString());
        for (const auto *name :
             {"camera_path.describe", "render_preset.describe", "presentation.describe"})
            check(queryNames.contains(name), "Every presentation description is a query");
        for (const auto &name : commandNames + queryNames)
            check(!name.startsWith("presentation.") || name == "presentation.describe",
                  "No presentation execution is exposed as a command or query");

        std::cout
            << "Camera path and render preset commands, describes and exposure checks passed\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
