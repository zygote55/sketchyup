#include "integrations/render_queue.hpp"
#include <QJsonArray>
#include <QThread>
#include <QTimer>
#include <algorithm>
#include <stdexcept>
namespace sketchy {
namespace {
void require(bool value, const char *message) {
    if (!value)
        throw std::runtime_error(message);
}
bool terminal(RenderJobState state) {
    return state == RenderJobState::Completed || state == RenderJobState::Failed ||
           state == RenderJobState::Canceled || state == RenderJobState::Interrupted;
}
QJsonObject history(const StoredRenderJob &record, const QJsonObject &report) {
    auto runs = record.report.value("runs").toArray();
    while (runs.size() >= 8)
        runs.removeFirst();
    runs.append(report);
    return {{"phase", report.value("phase")},
            {"code", report.value("code")},
            {"message", report.value("message")},
            {"runs", runs}};
}
} // namespace
RenderQueue::RenderQueue(QString directory, QObject *parent)
    : RenderQueue(std::move(directory), RenderJobStore::Limits{}, 2, parent) {}
RenderQueue::RenderQueue(QString directory, RenderJobStore::Limits limits, int concurrency,
                         QObject *parent)
    : QObject(parent), store_(std::move(directory), limits), concurrency_(concurrency) {
    require(concurrency >= 1 && concurrency <= 2, "Render concurrency must be one or two");
    schedule();
}
RenderQueue::~RenderQueue() {
    shuttingDown_ = true;
    for (const auto &[id, job] : active_) {
        auto report = store_.jobs().at(id).report;
        if (job) {
            job->cancel();
            report = history(store_.jobs().at(id), job->report());
            delete job.data();
        }
        try {
            store_.transition(id, RenderJobState::Interrupted,
                              "Application closed before this render finished.", report);
        } catch (...) { /* Startup reconciliation retains an interrupted or failed record. */
        }
    }
}
void RenderQueue::checkOwner() const {
    require(QThread::currentThread() == thread(), "Render queue belongs to its owner thread");
}
std::vector<StoredRenderJob> RenderQueue::jobs() const {
    checkOwner();
    std::vector<StoredRenderJob> result;
    for (const auto &[id, record] : store_.jobs()) {
        result.push_back(record);
        if (const auto error = errors_.find(id); error != errors_.end()) {
            result.back().state = RenderJobState::Failed;
            result.back().message = error->second;
        }
    }
    std::sort(result.begin(), result.end(),
              [](const auto &a, const auto &b) { return a.queueSequence < b.queueSequence; });
    return result;
}
QString RenderQueue::enqueue(std::shared_ptr<const PreparedRender> input,
                             BlenderJob::Options options) {
    checkOwner();
    require(bool(input), "Render queue requires captured input");
    const auto id = store_.enqueue(*input, std::move(options));
    emit changed();
    schedule();
    return id;
}
void RenderQueue::schedule() {
    QTimer::singleShot(0, this, [this] { pump(); });
}
void RenderQueue::failed(const QString &id, const QString &message, const QJsonObject &report) {
    try {
        store_.transition(id, RenderJobState::Failed, message, report);
    } catch (const std::exception &error) {
        errors_[id] = message + " · " + QString::fromUtf8(error.what());
    }
    emit this->error(errors_.contains(id) ? errors_.at(id) : message);
    emit changed();
}
void RenderQueue::pump() {
    checkOwner();
    if (shuttingDown_)
        return;
    while (active_.size() < size_t(concurrency_)) {
        const auto records = jobs();
        const auto next = std::find_if(records.begin(), records.end(), [](const auto &record) {
            return record.state == RenderJobState::Queued;
        });
        if (next == records.end())
            return;
        const auto id = next->id;
        try {
            auto input = store_.input(id);
            store_.transition(id, RenderJobState::Running, {}, next->report);
            auto *job = new BlenderJob(this);
            active_[id] = job;
            connect(job, &BlenderJob::changed, this, [this, id, job] {
                if (shuttingDown_)
                    return;
                emit changed();
                if (job->done() && finishing_.insert(id).second)
                    QTimer::singleShot(0, this, [this, id] { finish(id); });
            });
            auto options = next->options;
            options.scratchParent = store_.directory() + "/" + id;
            job->start(std::move(input), std::move(options));
        } catch (const std::exception &error) {
            if (const auto found = active_.find(id); found != active_.end()) {
                if (found->second)
                    found->second->deleteLater();
                active_.erase(found);
            }
            finishing_.erase(id);
            failed(id, QString::fromUtf8(error.what()));
        }
    }
}
void RenderQueue::finish(const QString &id) {
    checkOwner();
    const auto found = active_.find(id);
    if (found == active_.end() || !found->second)
        return;
    auto *job = found->second.data();
    const auto report = history(store_.jobs().at(id), job->report());
    bool ready{};
    try {
        if (store_.jobs().at(id).state == RenderJobState::Canceling ||
            job->phase() == BlenderJob::Phase::Canceled)
            store_.transition(id, RenderJobState::Canceled, "Render canceled.", report);
        else if (job->result()) {
            store_.complete(id, *job->result(), report);
            ready = true;
        } else
            store_.transition(id, RenderJobState::Failed,
                              job->report().value("message").toString("Render failed."), report);
    } catch (const std::exception &error) {
        failed(id, QString::fromUtf8(error.what()), report);
    }
    active_.erase(found);
    finishing_.erase(id);
    job->deleteLater();
    emit changed();
    if (ready)
        emit resultReady(id);
    schedule();
}
void RenderQueue::cancel(const QString &id) {
    checkOwner();
    const auto &record = store_.jobs().at(id);
    if (record.state == RenderJobState::Queued)
        store_.transition(id, RenderJobState::Canceled, "Queued render canceled.", record.report);
    else {
        require(record.state == RenderJobState::Running ||
                    record.state == RenderJobState::Canceling,
                "Only pending work can be canceled");
        if (record.state == RenderJobState::Running)
            store_.transition(id, RenderJobState::Canceling, "Canceling render.", record.report);
        if (const auto found = active_.find(id); found != active_.end() && found->second)
            found->second->cancel();
    }
    emit changed();
    schedule();
}
void RenderQueue::retry(const QString &id) {
    checkOwner();
    require(!active_.contains(id), "Cannot retry an active render");
    if (errors_.contains(id) && !terminal(store_.jobs().at(id).state))
        store_.transition(id, RenderJobState::Failed, errors_.at(id));
    store_.retry(id);
    errors_.erase(id);
    emit changed();
    schedule();
}
void RenderQueue::remove(const QString &id) {
    checkOwner();
    require(!active_.contains(id), "Cancel an active render before removing it");
    if (errors_.contains(id) && !terminal(store_.jobs().at(id).state))
        store_.transition(id, RenderJobState::Failed, errors_.at(id));
    store_.remove(id);
    errors_.erase(id);
    emit changed();
}
void RenderQueue::clearFinished() {
    checkOwner();
    const auto records = jobs();
    for (const auto &record : records)
        if (terminal(record.state) && !active_.contains(record.id))
            remove(record.id);
}
std::shared_ptr<const BlenderResult> RenderQueue::result(const QString &id) {
    checkOwner();
    try {
        return store_.result(id);
    } catch (const std::exception &error) {
        errors_[id] = QString::fromUtf8(error.what());
        emit changed();
        throw;
    }
}
std::shared_ptr<const PreparedRender> RenderQueue::input(const QString &id) const {
    checkOwner();
    return store_.input(id);
}
QJsonObject RenderQueue::diagnostics(const QString &id) const {
    checkOwner();
    const auto found = active_.find(id);
    return found != active_.end() && found->second
               ? history(store_.jobs().at(id), found->second->report())
               : store_.jobs().at(id).report;
}
QString RenderQueue::progress(const QString &id) const {
    checkOwner();
    const auto found = active_.find(id);
    return found != active_.end() && found->second ? found->second->progress()
                                                   : renderJobStateName(store_.jobs().at(id).state);
}
qint64 RenderQueue::workerProcessId(const QString &id) const {
    checkOwner();
    const auto found = active_.find(id);
    return found != active_.end() && found->second ? found->second->processId() : 0;
}
} // namespace sketchy
