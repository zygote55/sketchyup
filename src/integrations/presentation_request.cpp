#include "integrations/presentation_request.hpp"
#include "automation/inspection.hpp"
#include "automation/presentation_commands.hpp"
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QTimer>
namespace sketchy {
PresentationPlan resolvePresentationRequest(const Document &doc,
                                            const PresentationRequest &request) {
    PresentationPlan plan;
    plan.video = request.video;
    plan.videoCodec = presentationVideoCodecFromName(request.videoCodec);
    if (request.explicitKeyframes) {
        plan.path = request.name;
        plan.timing = request.timing;
        for (const auto &name : request.keyframes) {
            Id found{};
            for (const auto &[id, scene] : doc.scenes())
                if (scene->name == name.toStdString())
                    found = id;
            if (!found)
                throw InspectionError("INVALID_REQUEST",
                                      "Saved scene " + name.toStdString() + " does not exist");
            plan.keyframes.push_back(found);
        }
    } else {
        const CameraPathRecord *named = nullptr;
        for (const auto &[id, path] : doc.cameraPaths())
            if (path->name == request.path.toStdString())
                named = path.get();
        if (!named)
            throw InspectionError("INVALID_REQUEST",
                                  "Camera path " + request.path.toStdString() + " does not exist");
        plan.path = request.path;
        plan.keyframes = named->keyframes;
        plan.timing = named->timing;
    }
    for (const auto &name : request.presets) {
        const RenderPresetRecord *named = nullptr;
        for (const auto &[id, preset] : doc.renderPresets())
            if (preset->name == name.toStdString())
                named = preset.get();
        if (!named)
            throw InspectionError("INVALID_REQUEST",
                                  "Render preset " + name.toStdString() + " does not exist");
        plan.presets.push_back({name, named->settings});
    }
    return plan;
}
PresentationRun runPresentationRequest(const Document &doc, const PresentationRequest &request,
                                       PresentationJobStore &store,
                                       const std::function<bool()> &interrupted,
                                       const std::function<bool()> &stopped,
                                       BlenderJob::Options options) {
    auto plan = resolvePresentationRequest(doc, request);
    PresentationExport batch(store);
    batch.start(PresentationCapture::capture(doc, std::move(plan)),
                QFileInfo(request.output).absoluteFilePath(), std::move(options));
    QEventLoop loop;
    QTimer watch;
    watch.setInterval(20);
    QObject::connect(&watch, &QTimer::timeout, &batch, [&] {
        if (stopped())
            batch.stop();
        else if (interrupted())
            batch.cancel();
    });
    QObject::connect(&batch, &PresentationExport::changed, &loop, [&] {
        if (batch.done())
            loop.quit();
    });
    if (!batch.done()) {
        watch.start();
        loop.exec();
    }
    watch.stop();
    QFile file(batch.directory() + "/manifest.json");
    if (!file.open(QIODevice::ReadOnly))
        throw std::runtime_error("The batch manifest could not be read");
    const auto manifest = QJsonDocument::fromJson(file.readAll()).object();
    return {batch.state(), manifest};
}

namespace {
// The describe query's report of one check (UX_DESIGN 17.5).
QJsonObject encodersReport(const PresentationVideoCheck &check) {
    if (!check.checked)
        return {{"checked", false}, {"message", check.message}};
    return {{"checked", true},
            {"h264", check.codecs.contains("H264")},
            {"mpeg4", check.codecs.contains("MPEG4")}};
}
} // namespace

void installPresentationVideoProbe(PresentationVideoChecks &checks) {
    setPresentationVideoProbe([&checks]() -> QJsonObject {
        if (const auto answer = checks.cached(BlenderJob::Options{}))
            return encodersReport(*answer);
        checks.refresh(BlenderJob::Options{});
        return {{"checked", false}, {"pending", true}, {"message", "Checking the video encoder…"}};
    });
}

void installBlockingPresentationVideoProbe(PresentationVideoChecks &checks) {
    setPresentationVideoProbe(
        [&checks]() -> QJsonObject { return encodersReport(checks.check(BlenderJob::Options{})); });
}
} // namespace sketchy
