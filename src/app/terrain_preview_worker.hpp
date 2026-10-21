#pragma once
// R096.f2 (ADR 0169 §5, the R095.d pattern): the one-thread, latest-wins worker of the terrain
// previews that plan off the UI thread: contour edits, pad stamps and drapes.
//
// - The viewport prepares a request on the UI thread (prepareTerrainEditPreview: the command's
//   own parse, with the immutable edit mesh, current sources and record it plans against) and
//   hands it here. The worker runs runTerrainEditPreview, which reads no document.
// - Latest wins: at most one plan runs at a time. A request made while one runs waits, and a newer
//   request replaces it unseen, so the worker always plans the newest request next and never
//   plans one that a newer request replaced.
// - A result is delivered on the UI thread (finished) when it is newer than every result
//   delivered before, so a continuous drag keeps showing the plan of a recent position instead of
//   none; a result older than the last delivered one is dropped. The viewport also drops a result
//   whose document or revision is no longer current.
// - cancel() withdraws everything: the running plan stops at its next checkpoint and nothing
//   requested before is delivered. The destructor cancels and waits for the running plan.
#include "automation/terrain_commands.hpp"
#include <QObject>
#include <QThreadPool>
#include <atomic>
#include <cstdint>
#include <memory>
namespace sketchy {
class TerrainPreviewWorker final : public QObject {
    Q_OBJECT
  public:
    explicit TerrainPreviewWorker(QObject *parent = nullptr);
    ~TerrainPreviewWorker() override;
    // Asks for the preview of `request`; returns its generation (increasing).
    std::uint64_t request(std::shared_ptr<const TerrainEditPreviewRequest> request);
    // Withdraws every request: the running plan is cancelled and no result before this call is
    // delivered.
    void cancel();
    // Whether a plan runs or waits.
    bool busy() const { return running_ || pending_ != nullptr; }
    // The newest generation requested, and the newest delivered.
    std::uint64_t latest() const { return generation_; }
    std::uint64_t delivered() const { return delivered_; }
    // Plans started and results dropped (tests: a replaced request is never planned).
    std::uint64_t started() const { return started_; }
    // Tests and the benchmark: processes events until nothing runs or waits, or the time is out.
    bool waitForIdle(int timeoutMs = 10000);

  signals:
    void finished(qulonglong generation, std::shared_ptr<const TerrainCommandPreview> preview);

  private:
    void startNext();
    void done(std::uint64_t generation, std::shared_ptr<const TerrainCommandPreview> preview);
    QThreadPool pool_;
    std::shared_ptr<const TerrainEditPreviewRequest> pending_;
    std::uint64_t pendingGeneration_{}, generation_{}, delivered_{}, floor_{}, started_{};
    bool running_{};
    std::shared_ptr<std::atomic<bool>> cancel_; // the running plan's
};
} // namespace sketchy
