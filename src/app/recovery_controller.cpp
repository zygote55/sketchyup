#include "app/recovery_controller.hpp"
#include <QDir>
#include <QFileInfo>
#include <chrono>
namespace sketchy {
struct RecoveryController::Worker {
    std::unique_ptr<RecoveryWriter> writer;
    QStringList adopted;
};
RecoveryController::RecoveryController(Document &doc, QString root,
                                       std::function<RecoveryContext()> context, QObject *parent)
    : QObject(parent), document_(doc), root_(std::move(root)), context_(std::move(context)),
      worker_(std::make_shared<Worker>()) {
    connect(&timer_, &QTimer::timeout, this, &RecoveryController::tick);
    connect(&poll_, &QTimer::timeout, this, [this] {
        if (future_.valid() &&
            future_.wait_for(std::chrono::seconds(0)) == std::future_status::ready)
            finish();
    });
    poll_.start(50);
    setInterval(30);
}
RecoveryController::~RecoveryController() {
    // A normal close explicitly discards after Save/Discard. Destruction alone retains data.
    timer_.stop();
    poll_.stop();
    if (future_.valid()) {
        try {
            (void)future_.get();
        } catch (...) {
        }
    }
}
void RecoveryController::setInterval(int seconds) {
    interval_ = seconds == 0 ? 0 : std::clamp(seconds, 5, 3600);
    timer_.stop();
    if (interval_)
        timer_.start(interval_ * 1000);
    emit changed();
}
void RecoveryController::finish() {
    if (!future_.valid())
        return;
    const auto result = future_.get();
    if (document_.owns(pendingStamp_)) {
        if (result.info)
            durable_ = result.info;
        error_ = result.error;
    }
    emit changed();
}
void RecoveryController::tick() {
    // A storage failure pauses automatic retries to avoid accumulating uncertain writes.
    // The user can retry explicitly after correcting the cause.
    if (busy())
        return;
    if (session_ && !document_.owns(*session_)) {
        worker_ = std::make_shared<Worker>();
        durable_.reset();
        error_.clear();
        session_.reset();
        emit changed();
    }
    if (!error_.isEmpty())
        return;
    if (!document_.dirty()) {
        if (worker_->writer || !worker_->adopted.isEmpty())
            (void)discard();
        return;
    }
    checkpoint();
}
void RecoveryController::checkpoint() {
    if (busy())
        return;
    if (session_ && !document_.owns(*session_)) {
        // A caller replaced the document without an explicit discard: retain the old session.
        worker_ = std::make_shared<Worker>();
        durable_.reset();
        error_.clear();
        session_.reset();
    }
    if (worker_->adopted.isEmpty() && durable_ && durable_->revision == document_.revision() &&
        error_.isEmpty())
        return;
    try {
        auto snapshot = captureRecovery(document_, context_());
        pendingStamp_ = document_.saveStamp();
        session_ = pendingStamp_;
        error_.clear();
        const auto worker = worker_;
        const auto root = root_;
        future_ = std::async(std::launch::async, [worker, root, snapshot = std::move(snapshot)] {
            Outcome result;
            try {
                if (!worker->writer)
                    worker->writer =
                        std::make_unique<RecoveryWriter>(root, snapshot.info().documentId);
                result.info = worker->writer->write(snapshot);
                // Keep the recovered source until its replacement is durably protected.
                while (!worker->adopted.isEmpty()) {
                    if (QFileInfo::exists(QDir(root).filePath(worker->adopted.front())))
                        discardRecovery(root, worker->adopted.front());
                    worker->adopted.removeFirst();
                }
            } catch (const std::exception &error) {
                result.error = QString::fromUtf8(error.what());
            }
            return result;
        });
    } catch (const std::exception &error) {
        error_ = QString::fromUtf8(error.what());
    }
    emit changed();
}
QString RecoveryController::discard() {
    finish(); // Serialize cleanup after an in-flight write; never race its publication.
    QString failure;
    try {
        if (worker_->writer)
            worker_->writer->discard();
        for (const auto &key : worker_->adopted)
            discardRecovery(root_, key);
    } catch (const std::exception &error) {
        failure = QString::fromUtf8(error.what());
    }
    worker_ = std::make_shared<Worker>();
    durable_.reset();
    session_.reset();
    error_ = failure;
    emit changed();
    return failure;
}
void RecoveryController::adopt(const RecoveryInfo &info) {
    finish();
    if (info.documentId != QString::fromStdString(document_.identity()) ||
        info.revision != document_.revision())
        throw std::runtime_error("Recovery acknowledgement does not match the opened document");
    if (!worker_->adopted.contains(info.key))
        worker_->adopted.push_back(info.key);
    durable_ = info;
    session_ = document_.saveStamp();
    emit changed();
}
} // namespace sketchy
