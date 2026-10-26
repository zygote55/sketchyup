#include "integrations/presentation_batch.hpp"
#include "core/presentation_records.hpp"
#include "integrations/render_snapshot.hpp"
#include "io/document_io.hpp"
#include "io/presentation_io.hpp"
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QPointer>
#include <QSaveFile>
#include <QStandardPaths>
#include <QStorageInfo>
#include <QThread>
#include <QTimer>
#include <algorithm>
#include <chrono>
#include <future>
#include <optional>
#include <stdexcept>
namespace sketchy {
namespace {
constexpr qint64 mib = 1024 * 1024, outputLimit = presentationOutputLimit, diskReserve = 256 * mib;
constexpr qint64 frameImageLimit = presentationFrameImageLimit,
                 metadataLimit = presentationMetadataLimit, manifestLimit = mib;
constexpr qint64 videoFileLimit = 512 * mib;
// Encoding is one Blender run per preset; this bound stops a hung encoder, not a normal encode.
constexpr int videoTimeoutMs = 900000;
QString digest(const QByteArray &bytes) {
    return QString::fromLatin1(QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex());
}
QByteArray readFile(const QString &path, qint64 limit) {
    const QFileInfo info(path);
    if (!info.isFile() || info.isSymLink() || info.size() > limit)
        throw std::runtime_error("Batch output file is missing, linked or too large");
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        throw std::runtime_error("Cannot read batch output file");
    return file.readAll();
}
void writeNew(const QString &path, const QByteArray &bytes) {
    if (QFileInfo::exists(path) || QFileInfo(path).isSymLink())
        throw std::runtime_error("Batch output already exists");
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size() || !file.commit())
        throw std::runtime_error("Could not save batch output");
}
void publish(const QString &path, const QByteArray &bytes) {
    if (QFileInfo(path).isSymLink())
        throw std::runtime_error("Batch manifest is a link");
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size() || !file.commit())
        throw std::runtime_error("Could not save batch manifest");
}
QString frameName(size_t index) { return QString("frame-%1").arg(index + 1, 4, 10, QChar('0')); }
// A video is bounded like the frame images it encodes, so the disk check counts it that way.
qint64 plannedBudget(const PresentationPlan &plan, size_t frames) {
    qint64 total = 0;
    for (const auto &preset : plan.presets)
        total += qint64(frames) * presentationFrameBudget(preset.settings) * (plan.video ? 2 : 1);
    return total;
}
qint64 defaultFreeSpace(const QString &path) { return QStorageInfo(path).bytesAvailable(); }
std::vector<CameraFrame> planFrames(const Document &document, const PresentationPlan &plan) {
    const CameraPathRecord path{1, plan.path.toStdString(), 0, plan.keyframes, plan.timing};
    const auto references = cameraPathReferences(document.scenes(), path);
    if (!references.sceneMissing.empty()) {
        QStringList ids;
        for (const auto scene : references.sceneMissing)
            ids.append(QString::number(scene));
        throw std::runtime_error("Camera path '" + plan.path.toStdString() +
                                 "' names saved scenes that no longer exist (" +
                                 ids.join(", ").toStdString() +
                                 "); repair the path before exporting");
    }
    if (!references.sceneHasNoCamera.empty())
        throw std::runtime_error("Camera path '" + plan.path.toStdString() +
                                 "' has a keyframe scene without a camera; give it a camera before "
                                 "exporting");
    std::vector<SceneCamera> cameras;
    for (const auto scene : plan.keyframes)
        cameras.push_back(*document.scenes().at(scene)->snapshot.camera);
    return planCameraFrames(cameras, plan.timing);
}
void requireBackendMatchesPresets(const PresentationPlan &plan,
                                  const BlenderJob::Options &options) {
    for (const auto &preset : plan.presets) {
        const bool eevee = preset.settings.engine == RenderEngine::Eevee;
        if (eevee != (options.backend == "OPENGL"))
            throw std::runtime_error(
                "Presets must match the selected device: Eevee needs an OpenGL device, and Cycles "
                "needs the CPU or a compute device");
    }
}
void requireBlender(const BlenderJob::Options &options) {
    const auto executable = options.executable.isEmpty() ? QStandardPaths::findExecutable("blender")
                                                         : options.executable;
    const QFileInfo file(executable);
    if (executable.isEmpty() || !file.isAbsolute() || !file.isFile() || !file.isExecutable())
        throw std::runtime_error(
            "Presentation batches need an installed Blender 5.2 LTS; install it or choose its "
            "executable");
}
} // namespace
PresentationCapture::PresentationCapture(Document document, QByteArray encoded,
                                         PresentationPlan plan)
    : document_(std::move(document)), encoded_(std::move(encoded)), sha256_(digest(encoded_)),
      documentId_(document_.identity()), revision_(document_.revision()), plan_(std::move(plan)),
      frames_(planFrames(document_, plan_)) {}
PresentationCapture PresentationCapture::capture(const Document &document, PresentationPlan plan) {
    validatePresentationPlan(plan);
    // Refuse a broken path on the live document, before the freeze costs anything.
    planFrames(document, plan);
    RenderOptions options;
    options.settings = plan.presets.front().settings;
    for (const auto scene : plan.keyframes)
        RenderSnapshot::captureSavedScene(document, scene, options);
    auto frozen = document.readSnapshot();
    auto encoded = encodeDocument(frozen);
    return {std::move(frozen), std::move(encoded), std::move(plan)};
}
PresentationCapture PresentationCapture::restore(const QByteArray &encoded, PresentationPlan plan) {
    validatePresentationPlan(plan);
    return {decodeDocument(encoded), encoded, std::move(plan)};
}
RenderSnapshot PresentationCapture::frame(size_t preset, size_t index) const {
    if (preset >= plan_.presets.size() || index >= frames_.size())
        throw std::runtime_error("Batch frame index is out of bounds");
    const auto &pose = frames_[index];
    RenderOptions options;
    options.settings = plan_.presets[preset].settings;
    options.camera = renderSceneCamera(pose.camera);
    return RenderSnapshot::captureSavedScene(document_, plan_.keyframes.at(pose.sceneIndex),
                                             std::move(options));
}
struct PresentationExport::Impl {
    PresentationExport &owner;
    PresentationJobStore &store;
    QString id, output, message;
    std::optional<PresentationCapture> capture;
    BlenderJob::Options options;
    std::vector<QString> presetDirectories;
    // One record per planned frame of each preset. An empty record is a frame not yet verified.
    std::vector<std::vector<QJsonObject>> records;
    size_t preset{}, index{};
    qint64 bytes{};
    bool active{}, canceling{};
    QJsonObject lastAttempt;
    QPointer<BlenderJob> worker;
    std::future<std::shared_ptr<const PreparedRender>> preparing;
    std::shared_ptr<const PreparedRender> input;
    // One verified video record per preset; empty until its encode is verified and written.
    std::vector<QJsonObject> videos;
    size_t videoPreset{};
    bool encoding{};
    QTimer timer;
    std::function<qint64(const QString &)> freeSpace{defaultFreeSpace};
    qint64 limit{outputLimit};

    Impl(PresentationExport &owner, PresentationJobStore &store) : owner(owner), store(store) {
        timer.setInterval(10);
        QObject::connect(&timer, &QTimer::timeout, &owner, [this] { poll(); });
    }
    QString frameDirectory(size_t p) const {
        return output + "/" + presetDirectories[p] + "/frames";
    }
    int completed() const {
        int count = 0;
        for (const auto &preset : records)
            for (const auto &record : preset)
                count += !record.isEmpty();
        return count;
    }
    void setUp(const PresentationCapture &frozen) {
        presetDirectories.clear();
        records.clear();
        preset = index = 0;
        bytes = 0;
        encoding = false;
        lastAttempt = {};
        videos.assign(frozen.plan().presets.size(), QJsonObject{});
        for (size_t p = 0; p < frozen.plan().presets.size(); ++p) {
            presetDirectories.push_back(
                presentationPresetDirectory(p, frozen.plan().presets[p].name));
            records.emplace_back(frozen.frames().size());
        }
    }
    void ensureDirectories() {
        for (size_t p = 0; p < presetDirectories.size(); ++p)
            if (!QDir(frameDirectory(p)).exists() && !QDir().mkpath(frameDirectory(p)))
                throw std::runtime_error("Cannot create batch frame folder");
    }
    void manifest(const QString &status) {
        QJsonArray presets;
        for (size_t p = 0; p < records.size(); ++p) {
            QJsonArray frames;
            QJsonObject device;
            bool complete = true;
            for (size_t i = 0; i < records[p].size(); ++i) {
                const auto &record = records[p][i];
                if (record.isEmpty()) {
                    complete = false;
                    continue;
                }
                frames.append(QJsonObject{{"index", int(i)},
                                          {"file", record.value("file")},
                                          {"sha256", record.value("sha256")},
                                          {"keyframe", record.value("keyframe")}});
                device = record.value("result").toObject().value("device").toObject();
            }
            QString state = "queued";
            if (complete)
                state = "completed";
            else if (p == preset)
                state = status;
            QJsonObject entry{
                {"directory", presetDirectories[p]},
                {"state", state},
                {"settings", describeRenderSettings(capture->plan().presets[p].settings)},
                {"frames", frames}};
            if (!device.isEmpty())
                entry["device"] = device;
            if (!videos[p].isEmpty())
                entry["video"] = videos[p];
            presets.append(entry);
        }
        const auto &plan = capture->plan();
        QJsonArray keyframes;
        for (const auto scene : plan.keyframes)
            keyframes.append(QString::number(scene));
        const QJsonObject value{{"apiVersion", 1},
                                {"adapter", "sketchyup-presentation-v1"},
                                {"jobId", id},
                                {"state", status},
                                {"message", message},
                                {"path", plan.path},
                                {"keyframes", keyframes},
                                {"documentId", QString::fromStdString(capture->documentId())},
                                {"revision", QString::number(capture->revision())},
                                {"captureSha256", capture->sha256()},
                                {"framesPerSecond", plan.timing.framesPerSecond},
                                {"videoRequested", plan.video},
                                {"framesPerPreset", int(capture->frames().size())},
                                {"totalFrames", int(capture->frames().size() * records.size())},
                                {"completedFrames", completed()},
                                {"totalBytes", bytes},
                                {"presets", presets}};
        publish(output + "/manifest.json", QJsonDocument(value).toJson());
    }
    // The record and the bytes of a frame whose image and metadata both agree with its manifest
    // entry; empty otherwise. The files are read again, so a changed file never matches.
    std::optional<std::pair<QJsonObject, qint64>> check(size_t p, size_t i,
                                                        const QJsonObject &entry) const {
        try {
            const auto name = frameName(i);
            if (entry.value("file") != name + ".png" || !entry.value("sha256").isString())
                return std::nullopt;
            const auto png = readFile(frameDirectory(p) + "/" + name + ".png", frameImageLimit);
            if (digest(png) != entry.value("sha256"))
                return std::nullopt;
            const auto metadata = readFile(frameDirectory(p) + "/" + name + ".json", metadataLimit);
            const auto record = QJsonDocument::fromJson(metadata).object();
            if (record.value("file") != name + ".png" ||
                record.value("sha256") != entry.value("sha256") ||
                record.value("bytes").toInteger(-1) != png.size() ||
                record.value("index").toInteger(-1) != qint64(i))
                return std::nullopt;
            return std::pair{record, qint64(png.size() + metadata.size())};
        } catch (const std::exception &) {
            return std::nullopt;
        }
    }
    // Keeps a frame only when check() agrees with its manifest entry, and counts its bytes.
    bool verify(size_t p, size_t i, const QJsonObject &entry) {
        const auto found = check(p, i, entry);
        if (!found)
            return false;
        records[p][i] = found->first;
        bytes += found->second;
        return true;
    }
    void resume() {
        const auto saved =
            QJsonDocument::fromJson(readFile(output + "/manifest.json", manifestLimit)).object();
        if (saved.value("adapter") != "sketchyup-presentation-v1" || saved.value("jobId") != id ||
            saved.value("captureSha256") != capture->sha256())
            throw std::runtime_error(
                "The batch manifest does not belong to this job; the finished frames are kept");
        const auto presets = saved.value("presets").toArray();
        if (presets.size() != qsizetype(records.size()))
            throw std::runtime_error("The batch manifest does not match the batch plan");
        for (size_t p = 0; p < records.size(); ++p) {
            const auto entry = presets[qsizetype(p)].toObject();
            if (entry.value("directory") != presetDirectories[p])
                throw std::runtime_error("The batch manifest does not match the batch plan");
            for (const auto &item : entry.value("frames").toArray()) {
                const auto frame = item.toObject();
                const auto i = frame.value("index").toInteger(-1);
                if (i < 0 || i >= qint64(capture->frames().size()))
                    continue;
                verify(p, size_t(i), frame);
            }
        }
        // A video is kept only for a preset whose frames all verify and whose file still matches.
        for (size_t p = 0; p < records.size() && capture->plan().video; ++p) {
            const auto entry = presets[qsizetype(p)].toObject().value("video").toObject();
            if (entry.isEmpty() || !presetComplete(p))
                continue;
            try {
                const auto mp4 =
                    readFile(output + "/" + presetDirectories[p] + "/video.mp4", videoFileLimit);
                if (entry.value("file") != "video.mp4" || digest(mp4) != entry.value("sha256") ||
                    entry.value("bytes").toInteger(-1) != mp4.size())
                    continue;
                videos[p] = entry;
                bytes += mp4.size();
            } catch (const std::exception &) {
                // An unverified video is encoded again, after its file is removed.
            }
        }
    }
    bool presetComplete(size_t p) const {
        for (const auto &record : records[p])
            if (record.isEmpty())
                return false;
        return true;
    }
    // Removes a video file that has no verified record. A verified video is never reached here.
    void removeStaleVideo(size_t p) {
        const auto path = output + "/" + presetDirectories[p] + "/video.mp4";
        if (QFileInfo(path).isSymLink())
            throw std::runtime_error("A batch video file is a link; remove it to resume");
        if (QFileInfo::exists(path) && !QFile::remove(path))
            throw std::runtime_error("Cannot remove an unverified batch video");
    }
    // Removes only the files of a frame that is not verified; a verified frame is never touched.
    void removeUnverified(size_t p, size_t i) {
        const auto name = frameName(i);
        for (const auto *extension : {".png", ".json"}) {
            const auto path = frameDirectory(p) + "/" + name + extension;
            if (QFileInfo(path).isSymLink())
                throw std::runtime_error("A batch frame file is a link; remove it to resume");
            if (QFileInfo::exists(path) && !QFile::remove(path))
                throw std::runtime_error("Cannot remove an unverified batch frame file");
        }
    }
    void skipVerified() {
        while (preset < records.size()) {
            if (index >= records[preset].size()) {
                ++preset;
                index = 0;
            } else if (records[preset][index].isEmpty()) {
                return;
            } else {
                ++index;
            }
        }
    }
    void begin() {
        options.scratchParent = store.scratchDirectory(id);
        try {
            store.transition(id, PresentationJobState::Running, "Rendering the batch");
            ensureDirectories();
            active = true;
            canceling = false;
            message = "Starting the batch";
            emit owner.changed();
            QTimer::singleShot(0, &owner, [this] { next(); });
        } catch (const std::exception &error) {
            terminal(PresentationJobState::Failed, QString::fromUtf8(error.what()));
        }
    }
    void next() {
        if (!active)
            return;
        if (canceling) {
            terminal(PresentationJobState::Canceled,
                     "Batch canceled; finished frames are retained");
            return;
        }
        skipVerified();
        if (preset == records.size()) {
            if (capture->plan().video)
                encodeNext();
            else
                complete();
            return;
        }
        try {
            removeUnverified(preset, index);
        } catch (const std::exception &error) {
            terminal(PresentationJobState::Failed, QString::fromUtf8(error.what()));
            return;
        }
        const auto p = preset, i = index;
        message = QString("Preparing %1 frame %2 of %3")
                      .arg(presetDirectories[p])
                      .arg(i + 1)
                      .arg(capture->frames().size());
        preparing = std::async(std::launch::async, [this, p, i] {
            return PreparedRender::prepare(capture->frame(p, i));
        });
        timer.start();
        emit owner.changed();
    }
    // Encodes the first preset without a verified video, one Blender run at a time. The batch
    // completes only when every preset has its video.
    void encodeNext() {
        size_t p = 0;
        while (p < videos.size() && !videos[p].isEmpty())
            ++p;
        if (p == videos.size()) {
            complete();
            return;
        }
        const auto &settings = capture->plan().presets[p].settings;
        try {
            removeStaleVideo(p);
            // Every frame is hashed again, on disk, before any of them is encoded.
            for (size_t i = 0; i < records[p].size(); ++i)
                if (!check(p, i, records[p][i]))
                    throw std::runtime_error(
                        QString(
                            "Frame %1 of %2 changed after it was verified; no video was written")
                            .arg(i + 1)
                            .arg(presetDirectories[p])
                            .toStdString());
            BlenderVideoRequest request;
            request.width = settings.width;
            request.height = settings.height;
            request.framesPerSecond = capture->plan().timing.framesPerSecond;
            for (size_t i = 0; i < records[p].size(); ++i) {
                const auto &record = records[p][i];
                request.frames.push_back({frameDirectory(p) + "/" + frameName(i) + ".png",
                                          record.value("sha256").toString(),
                                          record.value("bytes").toInteger()});
            }
            // Encoding reads the verified frames only; it needs no GPU, so it runs on the CPU.
            auto encode = options;
            encode.backend = "CPU";
            encode.deviceId = "CPU";
            encode.allowCpuFallback = false;
            encode.timeoutMs = videoTimeoutMs;
            encoding = true;
            videoPreset = p;
            message = QString("Encoding video for %1").arg(presetDirectories[p]);
            worker = new BlenderJob(&owner);
            QObject::connect(worker, &BlenderJob::changed, &owner, [this] { workerChanged(); });
            worker->encode(std::move(request), std::move(encode));
            emit owner.changed();
        } catch (const std::exception &error) {
            terminal(PresentationJobState::Failed, QString::fromUtf8(error.what()));
        }
    }
    // Writes a verified video, and only then. A refused or failed encode keeps the frames.
    void videoFinished(BlenderJob *finished) {
        encoding = false;
        if (canceling) {
            terminal(PresentationJobState::Canceled,
                     "Batch canceled; finished frames are retained");
            return;
        }
        const auto video = finished->videoResult();
        if (!video) {
            const auto report = finished->report();
            terminal(
                PresentationJobState::Failed,
                QString("Video not written (%1): %2. The finished frames are kept.")
                    .arg(report.value("code").toString("ENCODE_FAILED"))
                    .arg(report.value("message").toString("Blender did not encode the video")));
            return;
        }
        try {
            const auto cost = qint64(video->mp4.size());
            if (cost > limit - bytes)
                throw std::runtime_error(
                    QString("Batch output would exceed its %1 MiB budget; %2 MiB remain")
                        .arg(limit / mib)
                        .arg((limit - bytes) / mib)
                        .toStdString());
            writeNew(output + "/" + presetDirectories[videoPreset] + "/video.mp4", video->mp4);
            bytes += cost;
            videos[videoPreset] = video->manifest;
            manifest("running");
            QTimer::singleShot(0, &owner, [this] { next(); });
            emit owner.changed();
        } catch (const std::exception &error) {
            terminal(PresentationJobState::Failed, QString::fromUtf8(error.what()));
        }
    }
    void poll() {
        if (!preparing.valid() ||
            preparing.wait_for(std::chrono::seconds(0)) != std::future_status::ready)
            return;
        timer.stop();
        try {
            input = preparing.get();
            if (canceling) {
                terminal(PresentationJobState::Canceled,
                         "Batch canceled; finished frames are retained");
                return;
            }
            worker = new BlenderJob(&owner);
            QObject::connect(worker, &BlenderJob::changed, &owner, [this] { workerChanged(); });
            worker->start(input, options);
        } catch (const std::exception &error) {
            if (canceling)
                terminal(PresentationJobState::Canceled,
                         "Batch canceled; finished frames are retained");
            else
                terminal(PresentationJobState::Failed, QString::fromUtf8(error.what()));
        }
    }
    void workerChanged() {
        if (!worker)
            return;
        if (!worker->done()) {
            if (encoding) {
                message = QString("Encoding video for %1 · %2")
                              .arg(presetDirectories[videoPreset])
                              .arg(worker->progress());
                emit owner.changed();
                return;
            }
            message = QString("%1 frame %2 of %3 · %4")
                          .arg(presetDirectories[preset])
                          .arg(index + 1)
                          .arg(capture->frames().size())
                          .arg(worker->progress());
            emit owner.changed();
            return;
        }
        const auto finished = worker;
        worker.clear();
        finished->deleteLater();
        lastAttempt = finished->report();
        if (encoding) {
            videoFinished(finished.data());
            return;
        }
        if (canceling) {
            terminal(PresentationJobState::Canceled,
                     "Batch canceled; finished frames are retained");
            return;
        }
        if (!finished->result()) {
            terminal(PresentationJobState::Failed, QString("Frame %1 of %2 failed: %3")
                                                       .arg(index + 1)
                                                       .arg(capture->frames().size())
                                                       .arg(finished->progress()));
            return;
        }
        try {
            const auto result = finished->result();
            const auto name = frameName(index);
            const auto &pose = capture->frames().at(index);
            const QJsonObject record{
                {"index", int(index)},
                {"file", name + ".png"},
                {"bytes", result->png.size()},
                {"sha256", digest(result->png)},
                {"preset", capture->plan().presets[preset].name},
                {"keyframe", pose.keyframe},
                {"scene", QString::number(capture->plan().keyframes.at(pose.sceneIndex))},
                {"camera", input->manifest().value("camera")},
                {"sourceManifestSha256", input->manifestHash()},
                {"result", result->manifest},
                {"diagnostics", finished->report()}};
            const auto metadata = QJsonDocument(record).toJson();
            const auto cost = qint64(result->png.size()) + metadata.size();
            if (metadata.size() > metadataLimit || cost > limit - bytes)
                throw std::runtime_error(
                    QString("Batch output would exceed its %1 MiB budget; %2 MiB remain")
                        .arg(limit / mib)
                        .arg((limit - bytes) / mib)
                        .toStdString());
            writeNew(frameDirectory(preset) + "/" + name + ".png", result->png);
            writeNew(frameDirectory(preset) + "/" + name + ".json", metadata);
            bytes += cost;
            records[preset][index] = record;
            manifest("running");
            input.reset();
            ++index;
            QTimer::singleShot(0, &owner, [this] { next(); });
            emit owner.changed();
        } catch (const std::exception &error) {
            terminal(PresentationJobState::Failed, QString::fromUtf8(error.what()));
        }
    }
    void complete() {
        timer.stop();
        active = false;
        message = "Batch completed";
        try {
            manifest("completed");
            store.complete(id, QJsonObject{{"completedFrames", completed()}});
        } catch (const std::exception &error) {
            terminal(PresentationJobState::Failed, QString::fromUtf8(error.what()));
            return;
        }
        input.reset();
        emit owner.changed();
    }
    void terminal(PresentationJobState next, QString detail) {
        timer.stop();
        active = false;
        message = std::move(detail);
        input.reset();
        const auto status = presentationJobStateName(next);
        try {
            manifest(status);
        } catch (const std::exception &error) {
            next = PresentationJobState::Failed;
            message = QString::fromUtf8(error.what());
        }
        try {
            store.transition(id, next, message, lastAttempt);
        } catch (const std::exception &error) {
            message = QString::fromUtf8(error.what());
        }
        emit owner.changed();
    }
};
PresentationExport::PresentationExport(PresentationJobStore &store, QObject *parent)
    : QObject(parent), impl_(std::make_unique<Impl>(*this, store)) {}
PresentationExport::~PresentationExport() {
    impl_->timer.stop();
    if (impl_->worker) {
        QObject::disconnect(impl_->worker, nullptr, this, nullptr);
        delete impl_->worker.data();
    }
    // The preparation only owns immutable data; keep that data alive until it ends.
    if (impl_->preparing.valid())
        impl_->preparing.wait();
    if (impl_->active) {
        try {
            impl_->store.transition(impl_->id, PresentationJobState::Interrupted,
                                    "Application closed before this batch finished. Retry to "
                                    "resume from the first unverified frame.");
        } catch (...) { /* Startup reconciliation keeps the stopped batch visible. */
        }
    }
}
QString PresentationExport::start(PresentationCapture capture, QString output,
                                  BlenderJob::Options options) {
    if (QThread::currentThread() != thread())
        throw std::runtime_error("Batch export starts on its owner thread");
    auto &impl = *impl_;
    if (impl.active)
        throw std::runtime_error("A presentation batch is already running");
    for (const auto &[jobId, job] : impl.store.jobs())
        if (job.state == PresentationJobState::Running ||
            job.state == PresentationJobState::Canceling)
            throw std::runtime_error(
                "Another presentation batch is running; wait for it to finish");
    const auto plan = capture.plan();
    requireBackendMatchesPresets(plan, options);
    requireBlender(options);
    const QFileInfo target(output);
    if (output.isEmpty() || !target.isAbsolute() || target.exists() || target.isSymLink())
        throw std::runtime_error("Choose a new batch folder that does not exist yet");
    const QFileInfo parent(target.absolutePath());
    if (!parent.isDir() || parent.isSymLink())
        throw std::runtime_error("Choose a new batch folder within an existing directory");
    // The output budget is enforced per frame, with the remaining budget in the message (§4). The
    // worst-case PNG bound is several times a real frame, so refusing a whole batch on it would
    // refuse 240 frames x 4 presets at 1024x768, the §5 batch. The disk check still uses that
    // bound, capped at the budget the batch can never pass.
    const auto total = std::min(plannedBudget(plan, capture.frames().size()), impl.limit);
    const auto available = impl.freeSpace(parent.absoluteFilePath());
    const auto needed = total + qint64(capture.encoded().size()) + diskReserve;
    if (available < needed)
        throw std::runtime_error(
            QString("Not enough free disk space for this batch: about %1 MiB needed, %2 MiB free")
                .arg(needed / mib)
                .arg(available / mib)
                .toStdString());
    const auto id =
        impl.store.enqueue(capture.encoded(), QString::fromStdString(capture.documentId()),
                           QString::number(capture.revision()), plan, target.absoluteFilePath());
    impl.id = id;
    impl.output = target.absoluteFilePath();
    impl.setUp(capture);
    bool created = false;
    try {
        created = QDir().mkdir(impl.output);
        if (!created)
            throw std::runtime_error("Choose a new batch folder that does not exist yet");
        impl.ensureDirectories();
        impl.capture = std::move(capture);
        impl.options = std::move(options);
        impl.manifest("queued");
    } catch (const std::exception &error) {
        if (created)
            QDir(impl.output).removeRecursively();
        impl.capture.reset();
        impl.store.transition(id, PresentationJobState::Failed, QString::fromUtf8(error.what()));
        impl.store.remove(id);
        throw;
    }
    impl.begin();
    return id;
}
void PresentationExport::retry(const QString &id, BlenderJob::Options options) {
    if (QThread::currentThread() != thread())
        throw std::runtime_error("Batch retry starts on its owner thread");
    auto &impl = *impl_;
    if (impl.active)
        throw std::runtime_error("A presentation batch is already running");
    const auto &job = impl.store.jobs().at(id);
    if (job.state != PresentationJobState::Failed && job.state != PresentationJobState::Canceled &&
        job.state != PresentationJobState::Interrupted)
        throw std::runtime_error("Only an unfinished terminal batch can be retried");
    requireBackendMatchesPresets(job.plan, options);
    requireBlender(options);
    if (!QFileInfo(job.output).isDir() || QFileInfo(job.output).isSymLink())
        throw std::runtime_error("The batch folder is missing; the batch cannot resume");
    auto frozen = PresentationCapture::restore(impl.store.capture(id), job.plan);
    impl.id = id;
    impl.output = job.output;
    impl.setUp(frozen);
    impl.capture = std::move(frozen);
    impl.options = std::move(options);
    impl.resume();
    impl.store.retry(id);
    impl.begin();
}
void PresentationExport::cancel() {
    if (QThread::currentThread() != thread())
        throw std::runtime_error("Batch cancellation requires its owner thread");
    auto &impl = *impl_;
    if (!impl.active || impl.canceling)
        return;
    impl.canceling = true;
    impl.message = "Canceling batch; finished frames are retained";
    impl.store.transition(impl.id, PresentationJobState::Canceling, impl.message);
    if (impl.worker)
        impl.worker->cancel();
    else if (!impl.preparing.valid())
        impl.terminal(PresentationJobState::Canceled, impl.message);
    emit changed();
}
QString PresentationExport::jobId() const { return impl_->id; }
PresentationJobState PresentationExport::state() const {
    if (impl_->id.isEmpty())
        return PresentationJobState::Queued;
    return impl_->store.jobs().at(impl_->id).state;
}
int PresentationExport::completedFrames() const { return impl_->completed(); }
int PresentationExport::totalFrames() const {
    return impl_->capture ? int(impl_->capture->frames().size() * impl_->records.size()) : 0;
}
QString PresentationExport::message() const { return impl_->message; }
QString PresentationExport::directory() const { return impl_->output; }
bool PresentationExport::done() const {
    if (impl_->id.isEmpty())
        return false;
    const auto state = this->state();
    return state == PresentationJobState::Completed || state == PresentationJobState::Failed ||
           state == PresentationJobState::Canceled || state == PresentationJobState::Interrupted;
}
void PresentationExport::setOutputLimit(qint64 bytes) { impl_->limit = bytes; }
void PresentationExport::setFreeSpaceProbe(std::function<qint64(const QString &)> probe) {
    impl_->freeSpace = std::move(probe);
}
} // namespace sketchy
