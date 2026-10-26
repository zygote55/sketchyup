#include "automation/presentation_export.hpp"
#include "automation/inspection.hpp"
#include <QJsonArray>
#include <QJsonDocument>
#include <QSet>
namespace sketchy {
namespace {
// The timing bounds are the camera path record's; the batch engine names the plan limits.
int integerField(const QJsonObject &object, const char *key, int fallback) {
    const auto value = object.value(key);
    if (!value.isUndefined() && (!value.isDouble() || value.toDouble() != double(value.toInt())))
        throw InspectionError("INVALID_REQUEST", std::string("Presentation timing ") + key +
                                                     " must be a whole number");
    return value.isUndefined() ? fallback : value.toInt();
}
void requireFields(const QJsonObject &object, const QStringList &allowed, const char *where) {
    for (const auto &key : object.keys())
        if (!allowed.contains(key))
            throw InspectionError("INVALID_REQUEST", std::string("Unknown ") + where + " field: " +
                                                         key.toStdString());
}
QJsonObject timingSchema() {
    return {{"framesPerSecond", "integer 1 to 60, default 24"},
            {"transitionSteps", "integer 1 to 600, default 24"},
            {"holdSteps", "integer 0 to 600, default 12"}};
}
} // namespace
PresentationRequest parsePresentationRequest(const QByteArray &bytes) {
    if (bytes.size() > presentationRequestBytes)
        throw InspectionError("LIMIT_EXCEEDED", "Presentation request exceeds 16 KiB");
    QJsonParseError error;
    const auto json = QJsonDocument::fromJson(bytes, &error);
    if (error.error != QJsonParseError::NoError || !json.isObject())
        throw InspectionError("INVALID_REQUEST", "Expected one JSON presentation request object");
    const auto value = json.object();
    requireFields(value, {"apiVersion", "path", "name", "keyframes", "timing", "presets", "output",
                          "video"},
                  "presentation request");
    if (value.value("apiVersion") != 1)
        throw InspectionError("INVALID_REQUEST", "Presentation requests require apiVersion 1");
    if (value.contains("video")) {
        if (!value.value("video").isBool())
            throw InspectionError("INVALID_REQUEST", "Presentation video must be true or false");
        if (value.value("video").toBool())
            throw InspectionError("INVALID_REQUEST",
                                  "Presentation video encoding is not available in this build; "
                                  "remove video or set it to false");
    }
    PresentationRequest request;
    const bool byPath = value.contains("path"), byKeyframes = value.contains("keyframes");
    if (byPath == byKeyframes)
        throw InspectionError("INVALID_REQUEST",
                              "Name one camera path or give an explicit keyframe list, not both");
    if (byPath) {
        if (!value.value("path").isString() || value.value("path").toString().isEmpty() ||
            value.contains("timing") || value.contains("name"))
            throw InspectionError("INVALID_REQUEST",
                                  "A camera path is named by a non-empty path string; its timing "
                                  "and name come from the path");
        request.path = value.value("path").toString();
    } else {
        const auto keyframes = value.value("keyframes").toArray();
        if (!value.value("keyframes").isArray() || keyframes.size() < 2 || keyframes.size() > 32)
            throw InspectionError("INVALID_REQUEST",
                                  "An explicit keyframe list names two to thirty-two saved scenes");
        for (const auto &item : keyframes) {
            if (!item.isString() || item.toString().isEmpty())
                throw InspectionError("INVALID_REQUEST", "Keyframes are saved scene names");
            request.keyframes.push_back(item.toString());
        }
        request.explicitKeyframes = true;
        request.name = value.value("name").toString("Keyframes");
        if (value.contains("name") && (!value.value("name").isString() || request.name.isEmpty()))
            throw InspectionError("INVALID_REQUEST", "A presentation name must be a non-empty string");
        if (value.contains("timing")) {
            if (!value.value("timing").isObject())
                throw InspectionError("INVALID_REQUEST", "Presentation timing must be an object");
            const auto timing = value.value("timing").toObject();
            requireFields(timing, {"framesPerSecond", "transitionSteps", "holdSteps"},
                          "presentation timing");
            request.timing.framesPerSecond =
                integerField(timing, "framesPerSecond", request.timing.framesPerSecond);
            request.timing.transitionSteps =
                integerField(timing, "transitionSteps", request.timing.transitionSteps);
            request.timing.holdSteps = integerField(timing, "holdSteps", request.timing.holdSteps);
        }
    }
    const auto presets = value.value("presets").toArray();
    if (!value.value("presets").isArray() || presets.isEmpty() || presets.size() > 4)
        throw InspectionError("INVALID_REQUEST", "A presentation names one to four render presets");
    QSet<QString> seen;
    for (const auto &item : presets) {
        if (!item.isString() || item.toString().isEmpty())
            throw InspectionError("INVALID_REQUEST", "Presets are render preset names");
        if (seen.contains(item.toString()))
            throw InspectionError("INVALID_REQUEST",
                                  "Render preset " + item.toString().toStdString() +
                                      " is listed more than once");
        seen.insert(item.toString());
        request.presets.push_back(item.toString());
    }
    if (!value.value("output").isString() || value.value("output").toString().isEmpty() ||
        value.value("output").toString().size() > 4096)
        throw InspectionError("INVALID_REQUEST", "A presentation names a new output folder");
    request.output = value.value("output").toString();
    return request;
}
QJsonObject presentationExportCapabilities() {
    return {{"operation", "export-presentation"},
            {"requestVersion", 1},
            {"requestBytes", presentationRequestBytes},
            {"manifestAdapter", "sketchyup-presentation-v1"},
            {"presets", QJsonObject{{"min", 1}, {"max", 4}}},
            {"keyframes", QJsonObject{{"min", 2}, {"max", 32}}},
            {"plannedFramesPerPreset", QJsonObject{{"max", 240}}},
            {"outputBudgetBytes", 2LL * 1024 * 1024 * 1024},
            {"frameDeadlineMs", 180000},
            {"store", "AppLocalDataLocation/presentation-jobs"},
            {"cancel", "SIGINT stops at the next frame boundary"},
            {"exitCodes", QJsonObject{{"0", "completed"}, {"1", "failed or interrupted"},
                                      {"130", "canceled"}}},
            {"video", "unavailable until R099.c"}};
}
QJsonObject presentationExportDescription() {
    return {{"name", "export-presentation"},
            {"usage", "sketchyup-cli --input MODEL --export-presentation REQUEST.json"},
            {"summary", "Render a camera path through one or more render presets to a new folder "
                        "with a batch manifest. Finished frames survive cancellation."},
            {"request",
             QJsonObject{
                 {"apiVersion", 1},
                 {"path", "camera path name (or use keyframes with timing)"},
                 {"keyframes", "saved scene names, two to thirty-two, in playback order"},
                 {"name", "name for an explicit keyframe list, default Keyframes"},
                 {"timing", timingSchema()},
                 {"presets", "one to four render preset names"},
                 {"output", "new folder; its parent must exist"},
                 {"video", "must be absent or false in this release"}}},
            {"limits", presentationExportCapabilities()}};
}
} // namespace sketchy
