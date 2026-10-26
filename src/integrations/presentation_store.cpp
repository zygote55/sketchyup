#include "integrations/presentation_store.hpp"
#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QRegularExpression>
#include <QSaveFile>
#include <QUuid>
#include <algorithm>
#include <stdexcept>
namespace sketchy {
namespace {
constexpr qint64 mib = 1024 * 1024, captureLimit = 512 * mib, recordLimit = 512 * 1024;
constexpr int presetLimit = 4;
void require(bool value, const char *message) {
    if (!value)
        throw std::runtime_error(message);
}
bool terminal(PresentationJobState state) {
    return state == PresentationJobState::Completed || state == PresentationJobState::Failed ||
           state == PresentationJobState::Canceled || state == PresentationJobState::Interrupted;
}
bool validId(const QString &id) {
    return QRegularExpression("^[0-9a-f]{8}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{12}$")
        .match(id)
        .hasMatch();
}
QString digest(const QByteArray &bytes) {
    return QString::fromLatin1(QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex());
}
QByteArray read(const QString &path, qint64 limit) {
    const QFileInfo info(path);
    require(info.isFile() && !info.isSymLink() && info.size() <= limit,
            "Invalid presentation job file");
    QFile file(path);
    require(file.open(QIODevice::ReadOnly), "Cannot read presentation job file");
    const auto bytes = file.read(limit + 1);
    require(bytes.size() <= limit && file.error() == QFileDevice::NoError,
            "Presentation job file exceeds limits or cannot be read");
    return bytes;
}
QJsonObject object(const QByteArray &bytes) {
    QJsonParseError error;
    const auto json = QJsonDocument::fromJson(bytes, &error);
    require(error.error == QJsonParseError::NoError && json.isObject(),
            "Invalid presentation job JSON");
    return json.object();
}
void write(const QString &path, const QByteArray &bytes) {
    require(!QFileInfo(path).isSymLink(), "Cannot replace a linked presentation job file");
    QSaveFile file(path);
    require(file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size() && file.commit(),
            "Cannot publish complete presentation job file");
}
QJsonObject encodePlan(const PresentationPlan &plan) {
    QJsonArray keyframes, presets;
    for (const auto scene : plan.keyframes)
        keyframes.append(QString::number(scene));
    for (const auto &preset : plan.presets)
        presets.append(QJsonObject{
            {"name", preset.name},
            {"width", preset.settings.width},
            {"height", preset.settings.height},
            {"samples", preset.settings.samples},
            {"seed", preset.settings.seed},
            {"engine", preset.settings.engine == RenderEngine::Eevee ? "eevee" : "cycles"}});
    return {{"path", plan.path},
            {"keyframes", keyframes},
            {"timing", QJsonObject{{"framesPerSecond", plan.timing.framesPerSecond},
                                   {"transitionSteps", plan.timing.transitionSteps},
                                   {"holdSteps", plan.timing.holdSteps}}},
            {"presets", presets}};
}
PresentationPlan decodePlan(const QJsonObject &value) {
    PresentationPlan plan;
    plan.path = value.value("path").toString();
    for (const auto &item : value.value("keyframes").toArray()) {
        bool ok{};
        const auto scene = item.toString().toULongLong(&ok);
        require(ok && item.isString(), "Invalid presentation keyframe");
        plan.keyframes.push_back(Id(scene));
    }
    const auto timing = value.value("timing").toObject();
    plan.timing.framesPerSecond = timing.value("framesPerSecond").toInt(-1);
    plan.timing.transitionSteps = timing.value("transitionSteps").toInt(-1);
    plan.timing.holdSteps = timing.value("holdSteps").toInt(-1);
    for (const auto &item : value.value("presets").toArray()) {
        const auto preset = item.toObject();
        PresentationPreset decoded;
        decoded.name = preset.value("name").toString();
        decoded.settings.width = preset.value("width").toInt(-1);
        decoded.settings.height = preset.value("height").toInt(-1);
        decoded.settings.samples = preset.value("samples").toInt(-1);
        decoded.settings.seed = preset.value("seed").toInt(-1);
        require(preset.value("engine") == "cycles" || preset.value("engine") == "eevee",
                "Invalid presentation preset engine");
        decoded.settings.engine =
            preset.value("engine") == "eevee" ? RenderEngine::Eevee : RenderEngine::Cycles;
        plan.presets.push_back(std::move(decoded));
    }
    validatePresentationPlan(plan);
    return plan;
}
QJsonObject boundedReport(const QJsonObject &report) {
    const auto bytes = QJsonDocument(report).toJson(QJsonDocument::Compact);
    if (bytes.size() <= 256 * 1024)
        return report;
    return {{"diagnosticsTruncated", true},
            {"phase", report.value("phase").toString().left(64)},
            {"code", report.value("code").toString().left(256)},
            {"message", report.value("message").toString().left(4096)},
            {"logTail", QString::fromUtf8(bytes.right(64 * 1024))}};
}
QJsonObject encode(const StoredPresentationJob &job) {
    return {{"version", 1},
            {"id", job.id},
            {"documentId", job.documentId},
            {"revision", job.revision},
            {"captureSha256", job.captureSha256},
            {"captureBytes", job.captureBytes},
            {"output", job.output},
            {"state", presentationJobStateName(job.state)},
            {"createdMs", job.createdMs},
            {"updatedMs", job.updatedMs},
            {"attempts", job.attempts},
            {"message", job.message},
            {"report", job.report},
            {"plan", encodePlan(job.plan)}};
}
StoredPresentationJob decode(const QJsonObject &value, const QString &id) {
    require(value.value("version") == 1 && value.value("id") == id,
            "Unsupported presentation job record");
    StoredPresentationJob job;
    job.id = id;
    job.documentId = value.value("documentId").toString();
    job.revision = value.value("revision").toString();
    job.captureSha256 = value.value("captureSha256").toString();
    require(!job.documentId.isEmpty() && job.documentId.size() <= 128 &&
                QRegularExpression("^(0|[1-9][0-9]{0,19})$").match(job.revision).hasMatch() &&
                QRegularExpression("^[0-9a-f]{64}$").match(job.captureSha256).hasMatch(),
            "Invalid presentation capture identity");
    job.captureBytes = value.value("captureBytes").toInteger(-1);
    require(job.captureBytes > 0 && job.captureBytes <= captureLimit &&
                value.value("captureBytes").toDouble() == double(job.captureBytes),
            "Invalid presentation capture size");
    job.output = value.value("output").toString();
    require(QFileInfo(job.output).isAbsolute() && job.output.size() <= 4096,
            "Invalid presentation output");
    bool stateFound{};
    for (auto state : {PresentationJobState::Queued, PresentationJobState::Running,
                       PresentationJobState::Canceling, PresentationJobState::Completed,
                       PresentationJobState::Failed, PresentationJobState::Canceled,
                       PresentationJobState::Interrupted}) {
        if (value.value("state") == presentationJobStateName(state)) {
            job.state = state;
            stateFound = true;
            break;
        }
    }
    require(stateFound, "Unknown presentation job state");
    job.createdMs = value.value("createdMs").toInteger(-1);
    job.updatedMs = value.value("updatedMs").toInteger(-1);
    job.attempts = value.value("attempts").toInt(-1);
    require(job.createdMs >= 0 && job.updatedMs >= job.createdMs && job.attempts >= 0 &&
                job.attempts <= 1000 && value.value("attempts").toDouble(-1) == job.attempts,
            "Invalid presentation job counters");
    require(value.value("message").isString() && value.value("message").toString().size() <= 4096 &&
                value.value("report").isObject(),
            "Invalid presentation job diagnostics");
    job.message = value.value("message").toString();
    job.report = value.value("report").toObject();
    require(QJsonDocument(job.report).toJson(QJsonDocument::Compact).size() <= 256 * 1024,
            "Presentation job diagnostics exceed limits");
    job.plan = decodePlan(value.value("plan").toObject());
    return job;
}
} // namespace
void validatePresentationPlan(const PresentationPlan &plan) {
    // The camera path rules (names, two to thirty-two keyframes, timing, the 240-frame plan)
    // and the render settings rules are the R099.a record rules; a transient record applies them.
    const auto path = std::make_shared<const CameraPathRecord>(
        CameraPathRecord{1, plan.path.toStdString(), 0, plan.keyframes, plan.timing});
    validateCameraPathRecords(CameraPathRecords{{1, path}}, 2);
    require(!plan.presets.empty() && int(plan.presets.size()) <= presetLimit,
            "A batch renders one to four presets");
    RenderPresetRecords presets;
    Id next = 1;
    for (const auto &preset : plan.presets) {
        presets.emplace(next, std::make_shared<const RenderPresetRecord>(RenderPresetRecord{
                                  next, preset.name.toStdString(), preset.settings}));
        ++next;
    }
    validateRenderPresetRecords(presets, next);
}
QString presentationJobStateName(PresentationJobState state) {
    switch (state) {
    case PresentationJobState::Queued:
        return "queued";
    case PresentationJobState::Running:
        return "running";
    case PresentationJobState::Canceling:
        return "canceling";
    case PresentationJobState::Completed:
        return "completed";
    case PresentationJobState::Failed:
        return "failed";
    case PresentationJobState::Canceled:
        return "canceled";
    case PresentationJobState::Interrupted:
        return "interrupted";
    }
    throw std::runtime_error("Invalid presentation job state");
}
PresentationJobStore::PresentationJobStore(QString directory)
    : PresentationJobStore(std::move(directory), Limits{}) {}
PresentationJobStore::PresentationJobStore(QString directory, Limits limits)
    : directory_(QFileInfo(directory).absoluteFilePath()), limits_(limits) {
    require(limits.pending >= 1 && limits.pending <= 32 && limits.retained >= limits.pending &&
                limits.retained <= 128 && limits.bytes >= mib && limits.bytes <= 16LL * 1024 * mib,
            "Invalid presentation store limits");
    require(!directory.isEmpty() && !QFileInfo(directory_).isSymLink() &&
                QDir().mkpath(directory_) &&
                QFile::setPermissions(directory_, QFileDevice::ReadOwner | QFileDevice::WriteOwner |
                                                      QFileDevice::ExeOwner),
            "Cannot create private presentation job store");
    lock_ = std::make_unique<QLockFile>(directory_ + "/store.lock");
    lock_->setStaleLockTime(0);
    require(lock_->tryLock(0), "Presentation job store is already open in another process");
    reconcile();
}
QString PresentationJobStore::jobDirectory(const QString &id) const {
    require(validId(id), "Invalid presentation job identity");
    const auto path = directory_ + "/" + id;
    require(!QFileInfo(path).isSymLink(), "Presentation job directory is a link");
    return path;
}
qint64 PresentationJobStore::usedBytes() const {
    qint64 result{};
    QDirIterator files(directory_, QDir::Files | QDir::NoSymLinks | QDir::Hidden,
                       QDirIterator::Subdirectories);
    while (files.hasNext()) {
        files.next();
        const auto size = files.fileInfo().size();
        require(size >= 0 && size <= limits_.bytes - result,
                "Presentation job store exceeds its disk budget");
        result += size;
    }
    return result;
}
void PresentationJobStore::requireCapacity(qint64 additional, int pendingAddition) const {
    int pending = pendingAddition;
    for (const auto &[id, job] : jobs_)
        if (!terminal(job.state))
            ++pending;
    require(pending <= limits_.pending, "A presentation batch is already queued or running");
    const auto used = usedBytes();
    require(additional >= 0 && additional <= limits_.bytes - used,
            "Presentation storage is full; remove finished batches before queuing more");
}
void PresentationJobStore::save(const StoredPresentationJob &job) const {
    require(job.message.size() <= 4096 &&
                QJsonDocument(job.report).toJson(QJsonDocument::Compact).size() <= 256 * 1024,
            "Presentation job diagnostics exceed limits");
    const auto bytes = QJsonDocument(encode(job)).toJson();
    require(bytes.size() <= recordLimit, "Presentation job record exceeds limits");
    write(jobDirectory(job.id) + "/job.json", bytes);
}
void PresentationJobStore::reconcile() {
    const auto entries =
        QDir(directory_)
            .entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot | QDir::NoSymLinks, QDir::Name);
    require(entries.size() <= 256, "Presentation job store contains too many directories");
    for (const auto &entry : entries) {
        const auto id = entry.fileName();
        if (!validId(id))
            continue;
        StoredPresentationJob job;
        job.id = id;
        job.createdMs = job.updatedMs = QDateTime::currentMSecsSinceEpoch();
        try {
            job = decode(object(read(entry.filePath() + "/job.json", recordLimit)), id);
            const QFileInfo capture(entry.filePath() + "/capture.sketchyup");
            require(capture.isFile() && !capture.isSymLink() && capture.size() == job.captureBytes,
                    "Presentation capture is missing or damaged");
            const auto scratch =
                QDir(entry.filePath())
                    .entryInfoList({"scratch-*"},
                                   QDir::Dirs | QDir::NoDotAndDotDot | QDir::NoSymLinks);
            require(scratch.size() <= 16, "Too many interrupted worker directories");
            if (job.state == PresentationJobState::Running ||
                job.state == PresentationJobState::Canceling) {
                QJsonObject logs;
                for (const auto &directory : scratch)
                    for (int attempt = 1; attempt <= 2; ++attempt) {
                        const auto path = directory.filePath() + "/attempt-" +
                                          QString::number(attempt) + "/worker.log";
                        if (QFileInfo::exists(path))
                            logs[directory.fileName() + "/" + QString::number(attempt)] =
                                QString::fromUtf8(read(path, 64 * 1024).right(16 * 1024));
                    }
                if (!logs.isEmpty()) {
                    job.report["interruptedWorkerLogs"] = logs;
                    job.report = boundedReport(job.report);
                }
                job.state = PresentationJobState::Interrupted;
                job.message = "Application stopped before this batch finished. Retry to resume "
                              "from the first unverified frame.";
                job.updatedMs = std::max(job.createdMs, QDateTime::currentMSecsSinceEpoch());
                save(job);
            }
            for (const auto &directory : scratch)
                require(QDir(directory.filePath()).removeRecursively(),
                        "Cannot clean interrupted worker files");
            jobs_.emplace(id, job);
        } catch (const std::exception &error) {
            // Keep the damaged evidence on disk; the job is visible as failed and never resumes.
            job.state = PresentationJobState::Failed;
            job.message = QString::fromUtf8(error.what()).left(4096);
            jobs_[id] = job;
        }
    }
}
QString PresentationJobStore::enqueue(const QByteArray &capture, QString documentId,
                                      QString revision, PresentationPlan plan, QString output) {
    validatePresentationPlan(plan);
    require(!capture.isEmpty() && capture.size() <= captureLimit,
            "Batch capture exceeds the presentation store budget");
    require(!documentId.isEmpty() && documentId.size() <= 128 &&
                QRegularExpression("^(0|[1-9][0-9]{0,19})$").match(revision).hasMatch() &&
                QFileInfo(output).isAbsolute() && output.size() <= 4096,
            "Invalid presentation source identity or output");
    require(jobs_.size() < size_t(limits_.retained),
            "Presentation history is full; remove finished batches");
    requireCapacity(capture.size() + mib, 1);
    StoredPresentationJob job;
    job.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    job.documentId = std::move(documentId);
    job.revision = std::move(revision);
    job.captureSha256 = digest(capture);
    job.captureBytes = capture.size();
    job.output = std::move(output);
    job.plan = std::move(plan);
    job.createdMs = job.updatedMs = QDateTime::currentMSecsSinceEpoch();
    const auto path = jobDirectory(job.id);
    require(QDir().mkdir(path), "Cannot create presentation job directory");
    try {
        require(QFile::setPermissions(path, QFileDevice::ReadOwner | QFileDevice::WriteOwner |
                                                QFileDevice::ExeOwner),
                "Cannot protect presentation job directory");
        write(path + "/capture.sketchyup", capture);
        save(job);
        jobs_.emplace(job.id, job);
    } catch (...) {
        QDir(path).removeRecursively();
        throw;
    }
    return job.id;
}
QByteArray PresentationJobStore::capture(const QString &id) const {
    const auto &job = jobs_.at(id);
    const auto bytes = read(jobDirectory(id) + "/capture.sketchyup", job.captureBytes);
    require(bytes.size() == job.captureBytes && digest(bytes) == job.captureSha256,
            "Stored presentation capture does not match its record");
    return bytes;
}
QString PresentationJobStore::scratchDirectory(const QString &id) const { return jobDirectory(id); }
void PresentationJobStore::transition(const QString &id, PresentationJobState state,
                                      QString message, QJsonObject report) {
    auto job = jobs_.at(id);
    const auto from = job.state;
    const bool allowed =
        (from == PresentationJobState::Queued &&
         (state == PresentationJobState::Running || state == PresentationJobState::Canceled ||
          state == PresentationJobState::Failed)) ||
        (from == PresentationJobState::Running &&
         (state == PresentationJobState::Canceling || state == PresentationJobState::Canceled ||
          state == PresentationJobState::Failed || state == PresentationJobState::Interrupted)) ||
        (from == PresentationJobState::Canceling &&
         (state == PresentationJobState::Canceled || state == PresentationJobState::Failed ||
          state == PresentationJobState::Interrupted));
    require(allowed, "Invalid presentation job state transition");
    if (state == PresentationJobState::Running) {
        require(job.attempts < 1000, "Presentation job retry limit reached");
        for (const auto &[otherId, other] : jobs_)
            require(otherId == id || (other.state != PresentationJobState::Running &&
                                      other.state != PresentationJobState::Canceling),
                    "Only one presentation batch can run at a time");
        ++job.attempts;
    }
    job.state = state;
    job.message = message.left(4096);
    job.report = boundedReport(report);
    job.updatedMs = std::max(job.createdMs, QDateTime::currentMSecsSinceEpoch());
    save(job);
    jobs_[id] = std::move(job);
}
void PresentationJobStore::complete(const QString &id, QJsonObject report) {
    auto job = jobs_.at(id);
    require(job.state == PresentationJobState::Running,
            "Only a running batch can be recorded as complete");
    job.state = PresentationJobState::Completed;
    job.message.clear();
    job.report = boundedReport(report);
    job.updatedMs = std::max(job.createdMs, QDateTime::currentMSecsSinceEpoch());
    save(job);
    jobs_[id] = std::move(job);
}
void PresentationJobStore::retry(const QString &id) {
    auto job = jobs_.at(id);
    require(job.state == PresentationJobState::Failed ||
                job.state == PresentationJobState::Canceled ||
                job.state == PresentationJobState::Interrupted,
            "Only an unfinished terminal batch can be retried");
    require(job.attempts < 1000, "Presentation job retry limit reached");
    capture(id);
    requireCapacity(0, 1);
    job.state = PresentationJobState::Queued;
    job.message.clear();
    job.updatedMs = std::max(job.createdMs, QDateTime::currentMSecsSinceEpoch());
    save(job);
    jobs_[id] = std::move(job);
}
void PresentationJobStore::remove(const QString &id) {
    require(terminal(jobs_.at(id).state), "Cancel a batch before removing its stored files");
    require(QDir(jobDirectory(id)).removeRecursively(), "Cannot remove retained batch files");
    jobs_.erase(id);
}
void PresentationJobStore::clearFinished() {
    QStringList ids;
    for (const auto &[id, job] : jobs_)
        if (terminal(job.state))
            ids.append(id);
    for (const auto &id : ids)
        remove(id);
}
} // namespace sketchy
