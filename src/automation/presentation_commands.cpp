#include "automation/presentation_commands.hpp"
#include "automation/inspection.hpp"
#include "core/presentation_records.hpp"
#include "core/render_settings.hpp"
#include "io/presentation_io.hpp"
#include <QJsonArray>
#include <QStandardPaths>
#include <QStringList>
#include <algorithm>
#include <optional>
#include <set>
#include <stdexcept>

namespace sketchy {
namespace {

Id identity(const QJsonValue &value) {
    const auto text = value.toString();
    bool ok{};
    const auto parsed = text.toULongLong(&ok);
    if (!value.isString() || !ok || !parsed || text.startsWith('0') || text.size() > 20)
        throw std::runtime_error("Expected a canonical nonzero identity");
    return Id(parsed);
}

std::string name(const QJsonValue &value) {
    if (!value.isString())
        throw std::runtime_error("A name must be text");
    return value.toString().toStdString();
}

int integer(const QJsonValue &value, const char *field) {
    if (!value.isDouble() || value.toDouble() != double(value.toInt()))
        throw std::runtime_error(std::string(field) + " must be a whole number");
    return value.toInt();
}

CameraPathTiming timing(const QJsonValue &value) {
    const auto object = value.toObject();
    CameraPathTiming result;
    result.framesPerSecond = integer(object["framesPerSecond"], "framesPerSecond");
    result.transitionSteps = integer(object["transitionSteps"], "transitionSteps");
    result.holdSteps = integer(object["holdSteps"], "holdSteps");
    return result;
}

std::vector<Id> keyframes(const QJsonValue &value) {
    if (!value.isArray() || value.toArray().size() > int(cameraPathKeyframeLimit))
        throw std::runtime_error("Camera path keyframes are a bounded array of saved scene IDs");
    std::vector<Id> result;
    for (const auto &item : value.toArray())
        result.push_back(identity(item));
    return result;
}

RenderSettings settings(const QJsonObject &object) {
    RenderSettings result;
    result.width = integer(object["width"], "width");
    result.height = integer(object["height"], "height");
    result.samples = integer(object["samples"], "samples");
    result.seed = integer(object["seed"], "seed");
    const auto engine = object["engine"].toString();
    if (engine == "cycles")
        result.engine = RenderEngine::Cycles;
    else if (engine == "eevee")
        result.engine = RenderEngine::Eevee;
    else
        throw std::runtime_error("Render engine must be cycles or eevee");
    return result;
}

QJsonObject settingsObject(const RenderSettings &value) {
    return {{"width", value.width},
            {"height", value.height},
            {"samples", value.samples},
            {"seed", value.seed},
            {"engine", value.engine == RenderEngine::Eevee ? "eevee" : "cycles"}};
}

std::shared_ptr<const CameraPathRecord> requireCameraPath(const Document &doc, Id id) {
    if (!doc.cameraPaths().contains(id))
        throw std::runtime_error("Camera path does not exist");
    return doc.cameraPaths().at(id);
}

std::shared_ptr<const RenderPresetRecord> requireRenderPreset(const Document &doc, Id id) {
    if (!doc.renderPresets().contains(id))
        throw std::runtime_error("Render preset does not exist");
    return doc.renderPresets().at(id);
}

// The describe queries report an unknown identity as NOT_FOUND, not as a command refusal.
std::shared_ptr<const CameraPathRecord> describedCameraPath(const Document &doc, Id id) {
    if (!doc.cameraPaths().contains(id))
        throw InspectionError("NOT_FOUND", "Camera path does not exist");
    return doc.cameraPaths().at(id);
}

std::shared_ptr<const RenderPresetRecord> describedRenderPreset(const Document &doc, Id id) {
    if (!doc.renderPresets().contains(id))
        throw InspectionError("NOT_FOUND", "Render preset does not exist");
    return doc.renderPresets().at(id);
}

void createCameraPath(Document &doc, std::string pathName, std::vector<Id> scenes,
                      CameraPathTiming pathTiming) {
    const auto id = doc.nextCameraPathId();
    const auto record = std::make_shared<const CameraPathRecord>(CameraPathRecord{
        id, std::move(pathName), static_cast<std::uint32_t>(doc.cameraPaths().size()),
        std::move(scenes), pathTiming});
    CameraPathRecords candidate = doc.cameraPaths();
    candidate[id] = record;
    validateCameraPathRecords(candidate, id + 1);
    validateCameraPathCapture(doc.scenes(), *record);
    Edit edit{"Create camera path", {}};
    edit.cameraPaths.push_back({id, nullptr, record});
    doc.apply(std::move(edit), doc.revision());
}

void updateCameraPath(Document &doc, Id id, std::optional<std::string> pathName,
                      std::optional<std::vector<Id>> scenes,
                      std::optional<CameraPathTiming> pathTiming) {
    const auto before = requireCameraPath(doc, id);
    auto after = std::make_shared<CameraPathRecord>(*before);
    if (pathName)
        after->name = std::move(*pathName);
    if (scenes)
        after->keyframes = std::move(*scenes);
    if (pathTiming)
        after->timing = *pathTiming;
    if (*after == *before)
        return;
    CameraPathRecords candidate = doc.cameraPaths();
    candidate[id] = after;
    validateCameraPathRecords(candidate, doc.nextCameraPathId());
    // Renaming or reordering a broken path is allowed; changing its keyframes must name cameras.
    if (after->keyframes != before->keyframes)
        validateCameraPathCapture(doc.scenes(), *after);
    Edit edit{"Update camera path", {}};
    edit.cameraPaths.push_back({id, before, after});
    doc.apply(std::move(edit), doc.revision());
}

void eraseCameraPath(Document &doc, Id id) {
    const auto before = requireCameraPath(doc, id);
    Edit edit{"Delete camera path", {}};
    edit.cameraPaths.push_back({id, before, nullptr});
    for (const auto &[other, path] : doc.cameraPaths()) {
        if (path->position <= before->position)
            continue;
        auto after = std::make_shared<CameraPathRecord>(*path);
        --after->position;
        edit.cameraPaths.push_back({other, path, after});
    }
    doc.apply(std::move(edit), doc.revision());
}

void createRenderPreset(Document &doc, std::string presetName, RenderSettings presetSettings) {
    validateRenderSettings(presetSettings);
    const auto id = doc.nextRenderPresetId();
    const auto record = std::make_shared<const RenderPresetRecord>(
        RenderPresetRecord{id, std::move(presetName), presetSettings});
    RenderPresetRecords candidate = doc.renderPresets();
    candidate[id] = record;
    validateRenderPresetRecords(candidate, id + 1);
    Edit edit{"Create render preset", {}};
    edit.renderPresets.push_back({id, nullptr, record});
    doc.apply(std::move(edit), doc.revision());
}

void updateRenderPreset(Document &doc, Id id, std::optional<std::string> presetName,
                        std::optional<RenderSettings> presetSettings) {
    const auto before = requireRenderPreset(doc, id);
    auto after = std::make_shared<RenderPresetRecord>(*before);
    if (presetName)
        after->name = std::move(*presetName);
    if (presetSettings) {
        validateRenderSettings(*presetSettings);
        after->settings = *presetSettings;
    }
    if (*after == *before)
        return;
    RenderPresetRecords candidate = doc.renderPresets();
    candidate[id] = after;
    validateRenderPresetRecords(candidate, doc.nextRenderPresetId());
    Edit edit{"Update render preset", {}};
    edit.renderPresets.push_back({id, before, after});
    doc.apply(std::move(edit), doc.revision());
}

void eraseRenderPreset(Document &doc, Id id) {
    const auto before = requireRenderPreset(doc, id);
    Edit edit{"Delete render preset", {}};
    edit.renderPresets.push_back({id, before, nullptr});
    doc.apply(std::move(edit), doc.revision());
}

QString scenesText(const std::set<Id> &scenes) {
    QStringList ids;
    for (const auto scene : scenes)
        ids.append(QString::number(scene));
    return ids.join(", ");
}

} // namespace

bool isPresentationCommand(const QString &name) {
    return name == "camera_path.create" || name == "camera_path.update" ||
           name == "camera_path.delete" || name == "render_preset.create" ||
           name == "render_preset.update" || name == "render_preset.delete";
}

void executePresentationCommand(Document &doc, const QJsonObject &command) {
    const auto operation = command["command"].toString();
    if (operation == "camera_path.create")
        createCameraPath(doc, name(command["name"]), keyframes(command["keyframes"]),
                         timing(command["timing"]));
    else if (operation == "camera_path.update") {
        std::optional<std::string> pathName;
        std::optional<std::vector<Id>> scenes;
        std::optional<CameraPathTiming> pathTiming;
        if (command.contains("name"))
            pathName = name(command["name"]);
        if (command.contains("keyframes"))
            scenes = keyframes(command["keyframes"]);
        if (command.contains("timing"))
            pathTiming = timing(command["timing"]);
        if (!pathName && !scenes && !pathTiming)
            throw std::runtime_error("Camera path update names no change");
        updateCameraPath(doc, identity(command["path"]), std::move(pathName), std::move(scenes),
                         pathTiming);
    } else if (operation == "camera_path.delete")
        eraseCameraPath(doc, identity(command["path"]));
    else if (operation == "render_preset.create")
        createRenderPreset(doc, name(command["name"]), settings(command));
    else if (operation == "render_preset.update") {
        std::optional<std::string> presetName;
        std::optional<RenderSettings> presetSettings;
        if (command.contains("name"))
            presetName = name(command["name"]);
        if (command.contains("width") || command.contains("height") ||
            command.contains("samples") || command.contains("seed") || command.contains("engine")) {
            // Unset fields keep the preset's current settings.
            const auto current = requireRenderPreset(doc, identity(command["preset"]))->settings;
            QJsonObject merged = settingsObject(current);
            for (const auto *field : {"width", "height", "samples", "seed", "engine"})
                if (command.contains(field))
                    merged[field] = command[field];
            presetSettings = settings(merged);
        }
        if (!presetName && !presetSettings)
            throw std::runtime_error("Render preset update names no change");
        updateRenderPreset(doc, identity(command["preset"]), std::move(presetName), presetSettings);
    } else if (operation == "render_preset.delete")
        eraseRenderPreset(doc, identity(command["preset"]));
    else
        throw std::runtime_error("Unknown presentation command");
}

QJsonObject cameraPathDescription(const Document &doc, Id path) {
    const auto record = describedCameraPath(doc, path);
    const auto references = cameraPathReferences(doc.scenes(), *record);
    QJsonArray scenes;
    for (const auto scene : record->keyframes) {
        const auto found = doc.scenes().find(scene);
        scenes.append(
            QJsonObject{{"scene", QString::number(scene)},
                        {"name", found == doc.scenes().end()
                                     ? QJsonValue(QJsonValue::Null)
                                     : QJsonValue(QString::fromStdString(found->second->name))},
                        {"missing", references.sceneMissing.contains(scene)},
                        {"noCamera", references.sceneHasNoCamera.contains(scene)}});
    }
    return {{"id", QString::number(path)},
            {"name", QString::fromStdString(record->name)},
            {"position", int(record->position)},
            {"keyframes", scenes},
            {"timing", QJsonObject{{"framesPerSecond", record->timing.framesPerSecond},
                                   {"transitionSteps", record->timing.transitionSteps},
                                   {"holdSteps", record->timing.holdSteps}}},
            {"frames", int(cameraPathFrameCount(record->keyframes.size(), record->timing))},
            {"broken", !references.empty()}};
}

QJsonObject renderPresetDescription(const Document &doc, Id preset) {
    const auto record = describedRenderPreset(doc, preset);
    return {{"id", QString::number(preset)},
            {"name", QString::fromStdString(record->name)},
            {"settings", settingsObject(record->settings)}};
}

namespace {
PresentationVideoProbe presentationVideoProbe;
} // namespace

void setPresentationVideoProbe(PresentationVideoProbe probe) {
    presentationVideoProbe = std::move(probe);
}

QJsonObject presentationDescription(const Document &doc, Id path, const std::vector<Id> &presets,
                                    bool video) {
    const auto record = describedCameraPath(doc, path);
    const auto references = cameraPathReferences(doc.scenes(), *record);
    QJsonArray refusals;
    if (!references.sceneMissing.empty())
        refusals.append(QString("Camera path %1 names saved scenes that no longer exist (%2)")
                            .arg(QString::fromStdString(record->name))
                            .arg(scenesText(references.sceneMissing)));
    if (!references.sceneHasNoCamera.empty())
        refusals.append(QString("Camera path %1 has a keyframe scene without a camera (%2)")
                            .arg(QString::fromStdString(record->name))
                            .arg(scenesText(references.sceneHasNoCamera)));
    const auto frames = cameraPathFrameCount(record->keyframes.size(), record->timing);
    // The record's own refusal, the same text a batch refuses with (UX_DESIGN 17.11, gap 3).
    if (const auto frameLimit =
            cameraPathFrameLimitMessage(record->keyframes.size(), record->timing);
        !frameLimit.empty())
        refusals.append(QString::fromStdString(frameLimit));
    if (presets.empty() || presets.size() > 4)
        refusals.append("A presentation names one to four render presets");

    QJsonArray presetItems;
    qint64 worstCase = 0;
    std::set<Id> seen;
    for (size_t index = 0; index < presets.size(); ++index) {
        const auto id = presets[index];
        if (!seen.insert(id).second)
            refusals.append("Render preset " + QString::number(id) + " is listed more than once");
        const auto preset = describedRenderPreset(doc, id);
        const auto directory =
            presentationPresetDirectory(index, QString::fromStdString(preset->name));
        if (video && (preset->settings.width % 2 || preset->settings.height % 2))
            refusals.append(QString("A video needs even width and height; %1 is %2 by %3")
                                .arg(QString::fromStdString(preset->name))
                                .arg(preset->settings.width)
                                .arg(preset->settings.height));
        const auto budget = presentationFrameBudget(preset->settings);
        worstCase += qint64(frames) * budget * (video ? 2 : 1);
        presetItems.append(QJsonObject{{"id", QString::number(id)},
                                       {"name", QString::fromStdString(preset->name)},
                                       {"directory", directory},
                                       {"settings", settingsObject(preset->settings)},
                                       {"frameBudgetBytes", budget}});
    }
    const bool blender = !QStandardPaths::findExecutable("blender").isEmpty();
    if (!blender)
        refusals.append("Presentation batches need an installed Blender 5.2 LTS");
    // The configured Blender's video encoders (UX_DESIGN 17.5). The check runs only when a Blender
    // is found, and the probe bounds how long it can take.
    QJsonObject encoders{{"checked", false},
                         {"message", "No Blender found, so video cannot be checked"}};
    if (blender)
        encoders = presentationVideoProbe
                       ? presentationVideoProbe()
                       : QJsonObject{{"checked", false},
                                     {"message", "This program does not check video encoders"}};
    if (video && blender && !encoders.value("checked").toBool())
        refusals.append("Video cannot be confirmed in this Blender: " +
                        encoders.value("message").toString() + "; check again");
    if (video && encoders.value("checked").toBool() && !encoders.value("h264").toBool())
        refusals.append("Video is not available in this Blender: its FFmpeg build has no H.264 "
                        "encoder. A request can choose the MPEG-4 Part 2 codec, or clear video");

    return {{"apiVersion", 1},
            {"path", QJsonObject{{"id", QString::number(path)},
                                 {"name", QString::fromStdString(record->name)}}},
            {"framesPerPreset", int(frames)},
            {"totalFrames", int(frames * presets.size())},
            {"framesPerSecond", record->timing.framesPerSecond},
            {"video", video},
            {"presets", presetItems},
            {"budget", QJsonObject{{"worstCaseBytes", std::min(worstCase, presentationOutputLimit)},
                                   {"limitBytes", presentationOutputLimit},
                                   {"exceedsLimit", worstCase > presentationOutputLimit}}},
            {"output", QJsonObject{{"manifest", "manifest.json"},
                                   {"frames", "<directory>/frames/frame-NNNN.png and .json"},
                                   {"video", video ? QJsonValue("<directory>/video.mp4")
                                                   : QJsonValue(QJsonValue::Null)}}},
            {"blender", QJsonObject{{"available", blender}, {"videoEncoders", encoders}}},
            {"refusals", refusals},
            {"ready", refusals.isEmpty()}};
}

} // namespace sketchy
