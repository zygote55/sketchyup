#include "app/presentation_batches.hpp"
#include "io/presentation_io.hpp"
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSettings>
#include <algorithm>
#include <set>
#include <stdexcept>

namespace sketchy {
namespace {

// The render sheet's Blender executable, shared with the batch dialog.
QString blenderPath() {
    QSettings preferences("SketchyUp", "SketchyUp");
    return preferences.value("render/blenderPath").toString().trimmed();
}

} // namespace

QJsonObject presentationManifest(const QString &outputDirectory) {
    QFile file(outputDirectory + "/manifest.json");
    if (outputDirectory.isEmpty() || !file.open(QIODevice::ReadOnly))
        return {};
    return QJsonDocument::fromJson(file.readAll()).object();
}

struct PresentationBatches::Impl {
    PresentationBatches &owner;
    Document &doc;
    QString directory;
    // The order matters: the batch engine goes first, then the video cache, then the store it uses.
    std::unique_ptr<PresentationJobStore> store;
    std::unique_ptr<PresentationVideoChecks> checks;
    std::unique_ptr<PresentationExport> batch;
    QString last;
    std::set<QString> startedIds;
    // The milestones already announced for the job last announced (UX_DESIGN §17.10).
    QString announcedId, announcedState;
    std::set<QString> announcedPresets, announcedVideos;

    // The members are owned here, so none of them has a QObject parent: a parent would delete one a
    // second time.
    Impl(PresentationBatches &owner, Document &doc, QString directory)
        : owner(owner), doc(doc), directory(std::move(directory)),
          checks(std::make_unique<PresentationVideoChecks>()) {}

    // Opens the store on first use. A second process that has it open makes this throw.
    PresentationJobStore &ensure() {
        if (!store) {
            store = std::make_unique<PresentationJobStore>(directory);
            batch = std::make_unique<PresentationExport>(*store);
            batch->setVideoChecks(checks.get());
            QObject::connect(batch.get(), &PresentationExport::changed, &owner, [this] {
                announce();
                emit owner.changed();
            });
        }
        return *store;
    }
    // The job the engine is on, when the store still holds it. The engine's state() reads the store
    // and throws for a job that was removed, so every check goes through this.
    QString liveId() const {
        if (!batch || batch->jobId().isEmpty() || !store || !store->jobs().contains(batch->jobId()))
            return {};
        return batch->jobId();
    }
    bool live() const {
        const auto id = liveId();
        return !id.isEmpty() && (store->jobs().at(id).state == PresentationJobState::Running ||
                                 store->jobs().at(id).state == PresentationJobState::Canceling);
    }
    // One sentence for each milestone of the job last run: its start or resume, each preset whose
    // frames are finished, each written video, and each final state. Nothing else is announced.
    void announce() {
        const auto id = liveId();
        if (id.isEmpty())
            return;
        const auto job = store->jobs().at(id);
        const auto state = presentationJobStateName(job.state);
        const auto manifest = presentationManifest(job.output);
        const auto presets = manifest.value("presets").toArray();
        if (id != announcedId) {
            announcedId = id;
            announcedState.clear();
            announcedPresets.clear();
            announcedVideos.clear();
        }
        const auto path = job.plan.path;
        if (job.state == PresentationJobState::Running && announcedState != "running") {
            const auto text = QString("Batch %1: %2, %3 %4, %5 frames")
                                  .arg(announcedState.isEmpty() ? "started" : "resumed")
                                  .arg(path)
                                  .arg(job.plan.presets.size())
                                  .arg(job.plan.presets.size() == 1 ? "preset" : "presets")
                                  .arg(manifest.value("totalFrames").toInt());
            emit owner.announced(text);
        }
        // The preset's name, not its folder, is what a screen reader hears.
        for (int p = 0; p < presets.size() && p < int(job.plan.presets.size()); ++p) {
            const auto preset = presets.at(p).toObject();
            const auto name = job.plan.presets[size_t(p)].name;
            if (preset.value("state").toString() == "completed" &&
                !announcedPresets.contains(name)) {
                announcedPresets.insert(name);
                emit owner.announced(QString("Frames finished for %1.").arg(name));
            }
            if (preset.contains("video") && !announcedVideos.contains(name)) {
                announcedVideos.insert(name);
                emit owner.announced(QString("Video written for %1.").arg(name));
            }
        }
        if (state != announcedState && job.state != PresentationJobState::Running &&
            job.state != PresentationJobState::Canceling && job.state != PresentationJobState::Queued) {
            switch (job.state) {
            case PresentationJobState::Completed:
                emit owner.announced(QString("Batch completed: %1.").arg(path));
                break;
            case PresentationJobState::Canceled:
                emit owner.announced("Batch canceled; finished frames are kept.");
                break;
            case PresentationJobState::Failed:
                emit owner.announced("Batch failed: " + job.message);
                break;
            case PresentationJobState::Interrupted:
                emit owner.announced("Batch interrupted; resume it later.");
                break;
            default:
                break;
            }
        }
        announcedState = job.state == PresentationJobState::Running ? QString("running") : state;
    }
};

PresentationBatches::PresentationBatches(Document &document, QString directory, QObject *parent)
    : QObject(parent), impl_(std::make_unique<Impl>(*this, document, std::move(directory))) {}

PresentationBatches::~PresentationBatches() = default;

PresentationPlan PresentationBatches::planFor(Id path, const std::vector<Id> &presets, bool video,
                                              PresentationVideoCodec codec) const {
    const auto &doc = impl_->doc;
    if (!doc.cameraPaths().contains(path))
        throw std::runtime_error("Choose a camera path");
    const auto &record = *doc.cameraPaths().at(path);
    PresentationPlan result;
    result.path = QString::fromStdString(record.name);
    result.keyframes = record.keyframes;
    result.timing = record.timing;
    for (const auto id : presets) {
        if (!doc.renderPresets().contains(id))
            throw std::runtime_error("Check at least one preset");
        const auto &preset = *doc.renderPresets().at(id);
        result.presets.push_back({QString::fromStdString(preset.name), preset.settings});
    }
    result.video = video;
    result.videoCodec = codec;
    return result;
}

QString PresentationBatches::start(const PresentationPlan &plan, const QString &folder,
                                   BlenderJob::Options options) {
    impl_->ensure();
    const auto id = impl_->batch->start(PresentationCapture::capture(impl_->doc, plan), folder,
                                        std::move(options));
    impl_->startedIds.insert(id);
    impl_->last = id;
    return id;
}

BlenderJob::Options PresentationBatches::resumeOptions(const StoredPresentationJob &job,
                                                       QString executable) const {
    BlenderJob::Options options;
    options.executable = std::move(executable);
    options.allowCpuFallback = true;
    // A resumed frame runs on the device the batch's finished frames used, so the frames agree.
    const auto presets = presentationManifest(job.output).value("presets").toArray();
    for (const auto &value : presets) {
        const auto device = value.toObject().value("device").toObject();
        if (device.isEmpty())
            continue;
        options.backend = device.value("backend").toString();
        options.deviceId = device.value("id").toString();
        return options;
    }
    // No frame finished, so no device is recorded. Cycles resumes on the CPU; Eevee needs the
    // OpenGL device's identity, which only a finished frame records.
    for (const auto &preset : job.plan.presets)
        if (preset.settings.engine == RenderEngine::Eevee)
            throw std::runtime_error("This batch's device is unknown because no frame finished on "
                                     "it. Start a new batch instead.");
    options.backend = "CPU";
    options.deviceId = "CPU";
    return options;
}

void PresentationBatches::resume(const QString &id) {
    auto &store = impl_->ensure();
    const auto found = store.jobs().find(id);
    if (found == store.jobs().end())
        throw std::runtime_error("The batch was removed");
    impl_->batch->retry(id, resumeOptions(found->second, blenderPath()));
    impl_->startedIds.insert(id);
    impl_->last = id;
}

void PresentationBatches::cancel() {
    if (impl_->batch)
        impl_->batch->cancel();
}

void PresentationBatches::stop() {
    if (impl_->batch && impl_->live())
        impl_->batch->stop();
}

bool PresentationBatches::running() const { return impl_->live(); }

QString PresentationBatches::lastJob() const { return impl_->last; }

void PresentationBatches::remove(const QString &id) { impl_->ensure().remove(id); }

void PresentationBatches::clearFinished() { impl_->ensure().clearFinished(); }

std::vector<StoredPresentationJob> PresentationBatches::jobs() {
    // Listing a job that was never stored creates nothing: the store's folder is made on first use.
    if (!impl_->store && !QFileInfo::exists(impl_->directory))
        return {};
    std::vector<StoredPresentationJob> result;
    for (const auto &[id, job] : impl_->ensure().jobs())
        result.push_back(job);
    std::sort(result.begin(), result.end(), [](const auto &a, const auto &b) {
        return a.createdMs != b.createdMs ? a.createdMs < b.createdMs : a.id < b.id;
    });
    return result;
}

bool PresentationBatches::started(const QString &id) const {
    return impl_->startedIds.contains(id);
}

bool PresentationBatches::modelUnchanged(const StoredPresentationJob &job) const {
    return job.documentId == QString::fromStdString(impl_->doc.identity()) &&
           job.revision == QString::number(impl_->doc.revision());
}

QString PresentationBatches::sceneName(Id scene) const {
    const auto &scenes = impl_->doc.scenes();
    if (!scenes.contains(scene))
        return QString("Missing scene %1").arg(scene);
    return QString::fromStdString(scenes.at(scene)->name);
}

QString PresentationBatches::summary() {
    if (!impl_->store)
        return {};
    for (const auto &[id, job] : impl_->store->jobs()) {
        if (job.state != PresentationJobState::Running && job.state != PresentationJobState::Canceling)
            continue;
        // Presets render in order; the first one not yet complete is the one rendering (the manifest
        // names it only once one of its frames verifies).
        const auto manifest = presentationManifest(job.output);
        const auto total = manifest.value("framesPerPreset").toInt();
        const auto presets = manifest.value("presets").toArray();
        for (int p = 0; p < presets.size() && p < int(job.plan.presets.size()); ++p) {
            const auto preset = presets.at(p).toObject();
            if (preset.value("state").toString() == "completed")
                continue;
            return QString("Batch %1 · %2 %3 of %4")
                .arg(job.plan.path, job.plan.presets[size_t(p)].name)
                .arg(preset.value("frames").toArray().size())
                .arg(total);
        }
        return QString("Batch %1 · encoding video").arg(job.plan.path);
    }
    return {};
}

PresentationJobStore &PresentationBatches::store() { return impl_->ensure(); }

PresentationVideoChecks &PresentationBatches::videoChecks() { return *impl_->checks; }

} // namespace sketchy
