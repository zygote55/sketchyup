#include "app/terrain_preview_worker.hpp"
#include <QCoreApplication>
#include <QDeadlineTimer>
#include <QMetaObject>
#include <QPointer>
#include <QThread>
namespace sketchy {
TerrainPreviewWorker::TerrainPreviewWorker(QObject *parent) : QObject(parent) {
    pool_.setMaxThreadCount(1);
    pool_.setObjectName("terrainPreviewWorker");
}
TerrainPreviewWorker::~TerrainPreviewWorker() {
    pending_.reset();
    if (cancel_)
        cancel_->store(true);
    pool_.waitForDone();
}
std::uint64_t TerrainPreviewWorker::request(std::shared_ptr<const TerrainEditPreviewRequest> request) {
    pending_ = std::move(request);
    pendingGeneration_ = ++generation_;
    if (!running_)
        startNext();
    return pendingGeneration_;
}
void TerrainPreviewWorker::cancel() {
    pending_.reset();
    floor_ = generation_; // nothing requested so far is delivered
    if (cancel_)
        cancel_->store(true);
}
void TerrainPreviewWorker::startNext() {
    if (!pending_)
        return;
    running_ = true;
    ++started_;
    auto request = std::move(pending_);
    const auto generation = pendingGeneration_;
    cancel_ = std::make_shared<std::atomic<bool>>(false);
    QPointer<TerrainPreviewWorker> guard(this);
    // The task owns its request (the record, mesh and sources it plans against are immutable)
    // and its cancel flag; it reads no document.
    pool_.start([request = std::move(request), generation, cancel = cancel_, guard] {
        std::shared_ptr<const TerrainCommandPreview> preview;
        try {
            TerrainProgress progress([cancel] { return cancel->load(std::memory_order_relaxed); });
            preview = std::make_shared<const TerrainCommandPreview>(
                runTerrainEditPreview(*request, &progress));
        } catch (...) {
            // runTerrainEditPreview returns its refusals; anything else is dropped.
        }
        QMetaObject::invokeMethod(
            qApp,
            [guard, generation, preview = std::move(preview)]() mutable {
                if (guard)
                    guard->done(generation, std::move(preview));
            },
            Qt::QueuedConnection);
    });
}
void TerrainPreviewWorker::done(std::uint64_t generation,
                                std::shared_ptr<const TerrainCommandPreview> preview) {
    running_ = false;
    cancel_.reset();
    // Delivered when newer than every result delivered and than the last cancel.
    if (preview && generation > floor_ && generation > delivered_ && preview->code != "CANCELLED") {
        delivered_ = generation;
        emit finished(qulonglong(generation), std::move(preview));
    }
    startNext();
}
bool TerrainPreviewWorker::waitForIdle(int timeoutMs) {
    QDeadlineTimer deadline(timeoutMs);
    while (busy() && !deadline.hasExpired()) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 5);
        if (busy())
            QThread::msleep(1);
    }
    QCoreApplication::processEvents();
    return !busy();
}
} // namespace sketchy
