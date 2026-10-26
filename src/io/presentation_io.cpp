#include "io/presentation_io.hpp"
#include "core/model.hpp"
#include <QJsonArray>
#include <QJsonValue>
#include <QStringList>
#include <algorithm>
#include <cmath>
#include <limits>
namespace sketchy {
namespace {
QJsonObject object(const QJsonValue &value, const QStringList &fields, const char *what) {
    if (!value.isObject())
        throw std::runtime_error(std::string(what) + " requires an object");
    const auto result = value.toObject();
    if (result.size() != fields.size())
        throw std::runtime_error(std::string(what) + " has missing or unknown fields");
    for (const auto &field : fields)
        if (!result.contains(field))
            throw std::runtime_error(std::string(what) + " is missing field " + field.toStdString());
    return result;
}
QJsonArray array(const QJsonValue &value, qsizetype maximum, const char *what) {
    if (!value.isArray() || value.toArray().size() > maximum)
        throw std::runtime_error(std::string(what) + " exceed their shape or record budget");
    return value.toArray();
}
// A canonical nonzero decimal string: the form every stable identity takes in the file.
Id identity(const QJsonValue &value) {
    bool ok{};
    const auto text = value.toString();
    const auto result = text.toULongLong(&ok);
    if (!value.isString() || !ok || !result || QString::number(result) != text)
        throw std::runtime_error("Presentation identity requires a canonical nonzero decimal string");
    return result;
}
// An integer within the int range; the semantic bounds are checked by the record validators.
long long integer(const QJsonValue &value) {
    if (!value.isDouble())
        throw std::runtime_error("Presentation values require integers");
    const auto number = value.toDouble();
    if (!std::isfinite(number) || number != std::floor(number) ||
        number < std::numeric_limits<int>::min() || number > std::numeric_limits<int>::max())
        throw std::runtime_error("Presentation integer is out of range");
    return static_cast<long long>(number);
}
std::string name(const QJsonValue &value) {
    if (!value.isString())
        throw std::runtime_error("Presentation names require text");
    return value.toString().toStdString();
}
RenderEngine engine(const QJsonValue &value) {
    if (value == "cycles")
        return RenderEngine::Cycles;
    if (value == "eevee")
        return RenderEngine::Eevee;
    throw std::runtime_error("Unknown render engine");
}
} // namespace
PresentationTables presentationTablesOf(const Document &doc) {
    return {doc.cameraPaths(), doc.nextCameraPathId(), doc.renderPresets(), doc.nextRenderPresetId()};
}
QJsonObject encodePresentation(const PresentationTables &tables) {
    QJsonArray paths;
    for (const auto &[id, path] : tables.cameraPaths) {
        QJsonArray keyframes;
        for (const auto scene : path->keyframes)
            keyframes.append(QString::number(scene));
        const auto &t = path->timing;
        paths.append(QJsonObject{
            {"id", QString::number(id)},
            {"name", QString::fromStdString(path->name)},
            {"position", int(path->position)},
            {"keyframes", keyframes},
            {"timing", QJsonObject{{"framesPerSecond", t.framesPerSecond},
                                   {"transitionSteps", t.transitionSteps},
                                   {"holdSteps", t.holdSteps}}}});
    }
    QJsonArray presets;
    for (const auto &[id, preset] : tables.renderPresets) {
        const auto &s = preset->settings;
        presets.append(QJsonObject{
            {"id", QString::number(id)},
            {"name", QString::fromStdString(preset->name)},
            {"settings", QJsonObject{{"width", s.width},
                                     {"height", s.height},
                                     {"samples", s.samples},
                                     {"seed", s.seed},
                                     {"engine", s.engine == RenderEngine::Eevee ? "eevee" : "cycles"}}}});
    }
    return {{"cameraPaths", paths},
            {"nextCameraPathId", QString::number(tables.nextCameraPathId)},
            {"renderPresets", presets},
            {"nextRenderPresetId", QString::number(tables.nextRenderPresetId)}};
}
PresentationTables decodePresentation(const QJsonValue &value) {
    const auto fields = object(value,
                               {"cameraPaths", "nextCameraPathId", "renderPresets", "nextRenderPresetId"},
                               "Presentation");
    PresentationTables result;
    result.nextCameraPathId = identity(fields["nextCameraPathId"]);
    result.nextRenderPresetId = identity(fields["nextRenderPresetId"]);
    for (const auto &entry :
         array(fields["cameraPaths"], qsizetype(cameraPathCountLimit), "Camera paths")) {
        const auto record = object(entry, {"id", "name", "position", "keyframes", "timing"}, "Camera path");
        const auto timing = object(record["timing"], {"framesPerSecond", "transitionSteps", "holdSteps"},
                                   "Camera path timing");
        CameraPathRecord path{identity(record["id"]), name(record["name"]),
                              0, {}, CameraPathTiming{int(integer(timing["framesPerSecond"])),
                                                      int(integer(timing["transitionSteps"])),
                                                      int(integer(timing["holdSteps"]))}};
        const auto position = integer(record["position"]);
        if (position < 0)
            throw std::runtime_error("Camera path order must be nonnegative");
        path.position = static_cast<std::uint32_t>(position);
        for (const auto &scene : array(record["keyframes"], qsizetype(cameraPathKeyframeLimit),
                                       "Camera path keyframes"))
            path.keyframes.push_back(identity(scene));
        const auto id = path.id;
        if (!result.cameraPaths.emplace(id, std::make_shared<CameraPathRecord>(std::move(path))).second)
            throw std::runtime_error("Repeated camera path identity");
    }
    validateCameraPathRecords(result.cameraPaths, result.nextCameraPathId);
    for (const auto &entry :
         array(fields["renderPresets"], qsizetype(renderPresetCountLimit), "Render presets")) {
        const auto record = object(entry, {"id", "name", "settings"}, "Render preset");
        const auto s = object(record["settings"], {"width", "height", "samples", "seed", "engine"},
                              "Render preset settings");
        RenderPresetRecord preset{identity(record["id"]), name(record["name"]),
                                  RenderSettings{int(integer(s["width"])), int(integer(s["height"])),
                                                 int(integer(s["samples"])), int(integer(s["seed"])),
                                                 engine(s["engine"])}};
        const auto id = preset.id;
        if (!result.renderPresets.emplace(id, std::make_shared<RenderPresetRecord>(std::move(preset))).second)
            throw std::runtime_error("Repeated render preset identity");
    }
    validateRenderPresetRecords(result.renderPresets, result.nextRenderPresetId);
    return result;
}
qint64 presentationFrameBudget(const RenderSettings &settings) {
    return std::min<qint64>(presentationFrameImageLimit,
                            qint64(settings.width) * settings.height * 5 + 1024 * 1024) +
           presentationMetadataLimit;
}

QString presentationPresetDirectory(size_t index, const QString &name) {
    QString safe;
    for (const auto character : name.left(48)) {
        const bool keep =
            (character >= 'a' && character <= 'z') || (character >= 'A' && character <= 'Z') ||
            (character >= '0' && character <= '9') || character == '-' || character == '_';
        safe += keep ? character : QChar('-');
    }
    if (safe.isEmpty())
        safe = "preset";
    return QString("%1-%2").arg(index + 1, 2, 10, QChar('0')).arg(safe);
}
} // namespace sketchy
