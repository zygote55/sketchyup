#include "integrations/render_store.hpp"
#include <QDateTime>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QRegularExpression>
#include <QSaveFile>
#include <QUuid>
#include <algorithm>
#include <stdexcept>
namespace sketchy {
namespace {
constexpr qint64 mib = 1024 * 1024, outputReserve = 65 * mib;
void require(bool value, const char *message) {
    if (!value)
        throw std::runtime_error(message);
}
bool terminal(RenderJobState state) {
    return state == RenderJobState::Completed || state == RenderJobState::Failed ||
           state == RenderJobState::Canceled || state == RenderJobState::Interrupted;
}
bool validId(const QString &id) {
    return QRegularExpression("^[0-9a-f]{8}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{12}$")
        .match(id)
        .hasMatch();
}
QByteArray read(const QString &path, qint64 limit) {
    const QFileInfo info(path);
    require(info.isFile() && !info.isSymLink() && info.size() <= limit,
            "Invalid retained render artifact");
    QFile file(path);
    require(file.open(QIODevice::ReadOnly), "Cannot read retained render artifact");
    const auto bytes = file.read(limit + 1);
    require(bytes.size() <= limit && file.error() == QFileDevice::NoError,
            "Retained render artifact exceeds limits or cannot be read");
    return bytes;
}
QJsonObject object(const QByteArray &bytes) {
    QJsonParseError error;
    const auto json = QJsonDocument::fromJson(bytes, &error);
    require(error.error == QJsonParseError::NoError && json.isObject(), "Invalid render job JSON");
    return json.object();
}
void write(const QString &path, const QByteArray &bytes) {
    require(!QFileInfo(path).isSymLink(), "Cannot replace a linked render artifact");
    QSaveFile file(path);
    require(file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size() && file.commit(),
            "Cannot publish complete render artifact");
}
void validateOptions(const BlenderJob::Options &options) {
    require(QStringList{"CPU", "CUDA", "OPTIX", "HIP", "ONEAPI", "METAL", "OPENGL"}.contains(
                options.backend) &&
                options.deviceId.size() <= 512 &&
                (options.backend == "CPU" || !options.deviceId.isEmpty()) &&
                options.executable.size() <= 4096 && options.timeoutMs >= 1 &&
                options.timeoutMs <= 3600000,
            "Invalid retained render options");
}
QJsonObject encode(const StoredRenderJob &job) {
    return {{"version", 1},
            {"id", job.id},
            {"documentId", job.documentId},
            {"revision", job.revision},
            {"manifestSha256", job.manifestHash},
            {"state", renderJobStateName(job.state)},
            {"createdMs", job.createdMs},
            {"queueSequence", job.queueSequence},
            {"updatedMs", job.updatedMs},
            {"attempts", job.attempts},
            {"message", job.message},
            {"report", job.report},
            {"options", QJsonObject{{"executable", job.options.executable},
                                    {"backend", job.options.backend},
                                    {"deviceId", job.options.deviceId},
                                    {"allowCpuFallback", job.options.allowCpuFallback},
                                    {"timeoutMs", job.options.timeoutMs}}}};
}
StoredRenderJob decode(const QJsonObject &value, const QString &id) {
    require(value.value("version") == 1 && value.value("id") == id,
            "Unsupported render job record");
    StoredRenderJob result;
    result.id = id;
    result.documentId = value.value("documentId").toString();
    result.revision = value.value("revision").toString();
    result.manifestHash = value.value("manifestSha256").toString();
    require(!result.documentId.isEmpty() && result.documentId.size() <= 128 &&
                QRegularExpression("^(0|[1-9][0-9]{0,19})$").match(result.revision).hasMatch() &&
                QRegularExpression("^[0-9a-f]{64}$").match(result.manifestHash).hasMatch(),
            "Invalid render source identity");
    bool stateFound{};
    for (auto state : {RenderJobState::Queued, RenderJobState::Running, RenderJobState::Canceling,
                       RenderJobState::Completed, RenderJobState::Failed, RenderJobState::Canceled,
                       RenderJobState::Interrupted}) {
        if (value.value("state") == renderJobStateName(state)) {
            result.state = state;
            stateFound = true;
            break;
        }
    }
    require(stateFound, "Unknown render job state");
    result.queueSequence = value.value("queueSequence").toInteger(0);
    require(result.queueSequence >= 1 && result.queueSequence <= 1000000000000LL &&
                value.value("queueSequence").toDouble() == double(result.queueSequence),
            "Invalid persistent render queue order");
    result.createdMs = value.value("createdMs").toInteger(-1);
    result.updatedMs = value.value("updatedMs").toInteger(-1);
    result.attempts = value.value("attempts").toInt(-1);
    require(result.createdMs >= 0 && result.updatedMs >= result.createdMs && result.attempts >= 0 &&
                result.attempts <= 1000 && value.value("attempts").toDouble(-1) == result.attempts,
            "Invalid render job counters");
    require(value.value("message").isString() && value.value("message").toString().size() <= 4096 &&
                value.value("report").isObject(),
            "Invalid render job diagnostics");
    result.message = value.value("message").toString();
    result.report = value.value("report").toObject();
    require(QJsonDocument(result.report).toJson(QJsonDocument::Compact).size() <= 256 * 1024,
            "Render job diagnostics exceed limits");
    const auto options = value.value("options").toObject();
    require(options.value("executable").isString() && options.value("backend").isString() &&
                options.value("deviceId").isString() &&
                options.value("allowCpuFallback").isBool() &&
                options.value("timeoutMs").isDouble() &&
                options.value("timeoutMs").toDouble() == options.value("timeoutMs").toInt(-1),
            "Invalid stored render options");
    result.options.executable = options.value("executable").toString();
    result.options.backend = options.value("backend").toString();
    result.options.deviceId = options.value("deviceId").toString();
    result.options.allowCpuFallback = options.value("allowCpuFallback").toBool();
    result.options.timeoutMs = options.value("timeoutMs").toInt();
    validateOptions(result.options);
    return result;
}
} // namespace
QString renderJobStateName(RenderJobState state) {
    switch (state) {
    case RenderJobState::Queued:
        return "queued";
    case RenderJobState::Running:
        return "running";
    case RenderJobState::Canceling:
        return "canceling";
    case RenderJobState::Completed:
        return "completed";
    case RenderJobState::Failed:
        return "failed";
    case RenderJobState::Canceled:
        return "canceled";
    case RenderJobState::Interrupted:
        return "interrupted";
    }
    throw std::runtime_error("Invalid render job state");
}
RenderJobStore::RenderJobStore(QString directory)
    : RenderJobStore(std::move(directory), Limits{}) {}
RenderJobStore::RenderJobStore(QString directory, Limits limits)
    : directory_(QFileInfo(directory).absoluteFilePath()), limits_(limits) {
    require(limits.pending >= 1 && limits.pending <= 32 && limits.retained >= limits.pending &&
                limits.retained <= 128 && limits.bytes >= outputReserve &&
                limits.bytes <= 16LL * 1024 * mib,
            "Invalid render store limits");
    require(!directory.isEmpty() && !QFileInfo(directory_).isSymLink() &&
                QDir().mkpath(directory_) &&
                QFile::setPermissions(directory_, QFileDevice::ReadOwner | QFileDevice::WriteOwner |
                                                      QFileDevice::ExeOwner),
            "Cannot create private render job store");
    lock_ = std::make_unique<QLockFile>(directory_ + "/store.lock");
    lock_->setStaleLockTime(0);
    require(lock_->tryLock(0), "Render job store is already open in another process");
    reconcile();
}
QString RenderJobStore::jobDirectory(const QString &id) const {
    require(validId(id), "Invalid render job identity");
    const auto path = directory_ + "/" + id;
    require(!QFileInfo(path).isSymLink(), "Render job directory is a link");
    return path;
}
qint64 RenderJobStore::usedBytes() const {
    qint64 result{};
    QDirIterator files(directory_, QDir::Files | QDir::NoSymLinks | QDir::Hidden,
                       QDirIterator::Subdirectories);
    while (files.hasNext()) {
        files.next();
        const auto size = files.fileInfo().size();
        require(size >= 0 && size <= limits_.bytes - result,
                "Render job store exceeds its disk budget");
        result += size;
    }
    return result;
}
void RenderJobStore::requireCapacity(qint64 additional, int pendingAddition) const {
    int pending = pendingAddition;
    for (const auto &[id, job] : jobs_)
        pending += !terminal(job.state);
    require(pending <= limits_.pending, "Render queue is full");
    const auto used = usedBytes();
    require(additional >= 0 && additional <= limits_.bytes - used &&
                qint64(pending) * outputReserve <= limits_.bytes - used - additional,
            "Render storage is full; remove finished jobs before queuing more");
}
void RenderJobStore::save(const StoredRenderJob &job) const {
    require(job.message.size() <= 4096 &&
                QJsonDocument(job.report).toJson(QJsonDocument::Compact).size() <= 256 * 1024,
            "Render job diagnostics exceed limits");
    const auto bytes = QJsonDocument(encode(job)).toJson();
    require(bytes.size() <= 512 * 1024, "Render job record exceeds limits");
    write(jobDirectory(job.id) + "/job.json", bytes);
}
void RenderJobStore::reconcile() {
    const auto entries =
        QDir(directory_)
            .entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot | QDir::NoSymLinks, QDir::Name);
    require(entries.size() <= 256, "Render job store contains too many directories");
    for (const auto &entry : entries) {
        const auto id = entry.fileName();
        if (!validId(id))
            continue;
        StoredRenderJob job;
        job.id = id;
        job.createdMs = job.updatedMs = QDateTime::currentMSecsSinceEpoch();
        try {
            job = decode(object(read(entry.filePath() + "/job.json", 512 * 1024)), id);
            jobs_.emplace(id, job);
            if (job.state == RenderJobState::Running || job.state == RenderJobState::Canceling) {
                job.state = RenderJobState::Interrupted;
                job.message = "Application stopped before this render finished. Retry the captured "
                              "input when ready.";
                job.updatedMs = std::max(job.createdMs, QDateTime::currentMSecsSinceEpoch());
                save(job);
                jobs_[id] = job;
            } else if (job.state == RenderJobState::Completed) {
                result(id);
            } else if (job.state == RenderJobState::Queued) {
                input(id);
            }
        } catch (const std::exception &error) {
            job.state = RenderJobState::Failed;
            job.message = QString::fromUtf8(error.what()).left(4096);
            // Keep corrupt evidence on disk; do not overwrite it with a success-like record.
            jobs_[id] = job;
        }
    }
}
QString RenderJobStore::enqueue(const PreparedRender &input, BlenderJob::Options options) {
    validateOptions(options);
    require(jobs_.size() < size_t(limits_.retained),
            "Render history is full; remove finished jobs");
    const auto manifest = input.manifest();
    require((manifest.value("settings").toObject().value("engine") == "eevee") ==
                (options.backend == "OPENGL"),
            "Render engine and selected device backend do not match");
    qint64 bytes = QJsonDocument(manifest).toJson().size() +
                   manifest.value("scene").toObject().value("bytes").toInteger();
    if (manifest.contains("environment"))
        bytes += manifest.value("environment").toObject().value("bytes").toInteger();
    require(bytes >= 0 && bytes <= 312 * mib, "Invalid render package size");
    requireCapacity(bytes + 512 * 1024, 1);
    StoredRenderJob job;
    for (const auto &[id, existing] : jobs_)
        job.queueSequence = std::max(job.queueSequence, existing.queueSequence + 1);
    require(job.queueSequence <= 1000000000000LL, "Render queue sequence limit reached");
    job.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    job.documentId = manifest.value("documentId").toString();
    job.revision = manifest.value("revision").toString();
    job.manifestHash = input.manifestHash();
    job.createdMs = job.updatedMs = QDateTime::currentMSecsSinceEpoch();
    job.options = std::move(options);
    const auto path = jobDirectory(job.id);
    require(QDir().mkdir(path), "Cannot create render job directory");
    try {
        require(QFile::setPermissions(path, QFileDevice::ReadOwner | QFileDevice::WriteOwner |
                                                QFileDevice::ExeOwner),
                "Cannot protect render job directory");
        input.copyTo(path + "/scene");
        save(job);
        jobs_.emplace(job.id, job);
    } catch (...) {
        QDir(path).removeRecursively();
        throw;
    }
    return job.id;
}
std::shared_ptr<const PreparedRender> RenderJobStore::input(const QString &id) const {
    const auto &job = jobs_.at(id);
    const auto prepared = PreparedRender::open(jobDirectory(id) + "/scene", job.manifestHash);
    require(prepared->manifest().value("documentId") == job.documentId &&
                prepared->manifest().value("revision") == job.revision,
            "Retained source does not match job identity");
    return prepared;
}
std::shared_ptr<const BlenderResult> RenderJobStore::result(const QString &id) const {
    const auto &job = jobs_.at(id);
    require(job.state == RenderJobState::Completed, "Render job has no completed result");
    const auto path = jobDirectory(id);
    return loadBlenderResult(*input(id), path, object(read(path + "/result.json", 64 * 1024)),
                             job.options);
}
void RenderJobStore::transition(const QString &id, RenderJobState state, QString message,
                                QJsonObject report) {
    auto job = jobs_.at(id);
    const auto from = job.state;
    const bool allowed =
        (from == RenderJobState::Queued &&
         (state == RenderJobState::Running || state == RenderJobState::Canceled ||
          state == RenderJobState::Failed)) ||
        (from == RenderJobState::Running &&
         (state == RenderJobState::Canceling || state == RenderJobState::Canceled ||
          state == RenderJobState::Failed || state == RenderJobState::Interrupted)) ||
        (from == RenderJobState::Canceling &&
         (state == RenderJobState::Canceled || state == RenderJobState::Failed ||
          state == RenderJobState::Interrupted));
    require(allowed, "Invalid render job state transition");
    if (state == RenderJobState::Running) {
        require(job.attempts < 1000, "Render job retry limit reached");
        ++job.attempts;
    }
    job.state = state;
    job.message = std::move(message);
    job.report = std::move(report);
    job.updatedMs = std::max(job.createdMs, QDateTime::currentMSecsSinceEpoch());
    save(job);
    jobs_[id] = std::move(job);
}
void RenderJobStore::complete(const QString &id, const BlenderResult &result, QJsonObject report) {
    auto job = jobs_.at(id);
    require(job.state == RenderJobState::Running, "Only a running render can publish a result");
    require(result.png.size() <= 64 * mib, "Retained PNG exceeds limits");
    const auto manifest = QJsonDocument(result.manifest).toJson();
    require(manifest.size() <= 64 * 1024, "Retained result manifest exceeds limits");
    requireCapacity(0);
    const auto path = jobDirectory(id);
    write(path + "/image.png", result.png);
    write(path + "/result.json", manifest);
    loadBlenderResult(*input(id), path, result.manifest, job.options);
    job.state = RenderJobState::Completed;
    job.message.clear();
    job.report = std::move(report);
    job.updatedMs = std::max(job.createdMs, QDateTime::currentMSecsSinceEpoch());
    save(job);
    jobs_[id] = std::move(job);
}
void RenderJobStore::retry(const QString &id) {
    auto job = jobs_.at(id);
    require(job.state == RenderJobState::Failed || job.state == RenderJobState::Canceled ||
                job.state == RenderJobState::Interrupted,
            "Only an unfinished terminal render can be retried");
    input(id);
    requireCapacity(0, 1);
    require(job.attempts < 1000, "Render job retry limit reached");
    for (const auto &[otherId, existing] : jobs_)
        job.queueSequence = std::max(job.queueSequence, existing.queueSequence + 1);
    require(job.queueSequence <= 1000000000000LL, "Render queue sequence limit reached");
    job.state = RenderJobState::Queued;
    job.message.clear();
    job.updatedMs = std::max(job.createdMs, QDateTime::currentMSecsSinceEpoch());
    save(job);
    jobs_[id] = std::move(job);
    QFile::remove(jobDirectory(id) + "/image.png");
    QFile::remove(jobDirectory(id) + "/result.json");
}
void RenderJobStore::remove(const QString &id) {
    require(terminal(jobs_.at(id).state), "Cancel a render before removing its stored files");
    require(QDir(jobDirectory(id)).removeRecursively(), "Cannot remove retained render files");
    jobs_.erase(id);
}
void RenderJobStore::clearFinished() {
    QStringList ids;
    for (const auto &[id, job] : jobs_)
        if (terminal(job.state))
            ids.append(id);
    for (const auto &id : ids)
        remove(id);
}
} // namespace sketchy
