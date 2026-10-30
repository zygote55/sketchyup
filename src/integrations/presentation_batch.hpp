#pragma once
#include "core/model.hpp"
#include "integrations/blender_job.hpp"
#include "integrations/presentation_store.hpp"
#include "integrations/render_snapshot.hpp"
#include <QObject>
#include <QStringList>
#include <chrono>
#include <functional>
#include <memory>
#include <optional>
#include <vector>
namespace sketchy {
// The frozen input of one batch (ADR 0173 §4): one document snapshot taken on the owner thread,
// its native encoding and SHA-256, the planned frames and the plan. Every frame of every preset
// renders from this value, so editing the live document during a batch changes nothing.
class PresentationCapture {
  public:
    // Refuses a broken path (a keyframe scene that is gone, or has no camera), a plan over the
    // preset or frame limits, invalid settings and a scene that cannot be captured, before any
    // file or job exists.
    static PresentationCapture capture(const Document &document, PresentationPlan plan);
    // Rebuilds the frozen document from bytes the store has already hash-verified.
    static PresentationCapture restore(const QByteArray &encoded, PresentationPlan plan);
    // One frame of one preset, from the frozen document.
    RenderSnapshot frame(size_t preset, size_t index) const;
    const std::vector<CameraFrame> &frames() const { return frames_; }
    const PresentationPlan &plan() const { return plan_; }
    const QByteArray &encoded() const { return encoded_; }
    const QString &sha256() const { return sha256_; }
    const std::string &documentId() const { return documentId_; }
    std::uint64_t revision() const { return revision_; }
    // R107.c1 (ADR 0181 §3.3, owner decision 8): the DEM notices of the terrain the frames show; empty
    // when no keyframe shows DEM-derived terrain.
    const std::vector<std::string> &credits() const { return credits_; }

  private:
    PresentationCapture(Document document, QByteArray encoded, PresentationPlan plan);
    Document document_;
    QByteArray encoded_;
    QString sha256_;
    std::string documentId_;
    std::uint64_t revision_{};
    PresentationPlan plan_;
    std::vector<CameraFrame> frames_;
    std::vector<std::string> credits_;
};
// The answer of a Blender's video probe (UX_DESIGN 17.5): whether the probe reported its FFmpeg
// codec list, the identifiers it reported ("H264", "MPEG4", ...) and, when it did not, the reason.
struct PresentationVideoCheck {
    bool checked{};
    QStringList codecs;
    QString message;
};
// The video probe of each Blender executable, cached (UX_DESIGN 17.5). An answer is reused until
// the executable's resolved path, size or modification time changes. A failed answer is reused for
// 60 s, so a broken Blender is not probed on every call. The desktop never blocks here: cached()
// only reads, and refresh() starts a BlenderJob probe and reports its end through finished().
// check() is the CLI's form; it waits on a nested event loop, so the desktop must not call it.
// Frames are never rendered by a probe. Owner thread only. The object must outlive the probes it
// starts.
class PresentationVideoChecks : public QObject {
    Q_OBJECT
  public:
    explicit PresentationVideoChecks(QObject *parent = nullptr);
    ~PresentationVideoChecks() override;
    // The answer for this Blender's current file, when one is cached and still stands. Nothing when
    // none is cached, the file changed, or a failed answer is past its retry window. Never blocks.
    std::optional<PresentationVideoCheck> cached(const BlenderJob::Options &options) const;
    // Starts a probe and returns at once; its answer fills the cache, then finished() is emitted.
    // Does nothing while a probe of the same file runs, or when an answer stands, unless `force` is
    // set (the "Check again" of the batch dialog).
    void refresh(const BlenderJob::Options &options, bool force = false);
    // The cached answer when one stands; otherwise probes now, waits on a nested event loop, and
    // caches the answer. The CLI's form only.
    PresentationVideoCheck check(const BlenderJob::Options &options);
    // The clock that dates a failed answer. Tests replace it; the default is the steady clock.
    void setClock(std::function<std::chrono::steady_clock::time_point()> clock);
  signals:
    void finished();

  private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

// Runs one batch at a time on the owner thread. Each frame renders in one Blender worker; a
// canceled or failed batch keeps its verified frames, and retry renders only the rest.
class PresentationExport : public QObject {
    Q_OBJECT
  public:
    explicit PresentationExport(PresentationJobStore &store, QObject *parent = nullptr);
    ~PresentationExport() override;
    // Enqueues and starts a batch in a new output folder. Refusals (a broken destination, the
    // disk budget, a missing Blender, another running batch) throw before anything is created.
    // The 2 GiB output budget is checked per frame: a frame that would pass it fails the batch
    // and keeps the finished frames. Returns the job ID.
    QString start(PresentationCapture capture, QString output, BlenderJob::Options options);
    // Resumes a stopped, failed or canceled batch from its stored capture. A frame counts as
    // done only when its files and the batch manifest agree; every other frame re-renders.
    void retry(const QString &id, BlenderJob::Options options);
    // Cancels at the next frame boundary: the in-flight frame is discarded, finished frames stay.
    void cancel();
    // Stops now, for a quit or a closing application: the in-flight worker is killed, its frame is
    // discarded and the batch is recorded Interrupted, resumable, with its verified frames kept. It
    // does not wait for a frame boundary, and it does nothing when no batch runs. Call it from the
    // owner thread, not from a changed() handler.
    void stop();
    QString jobId() const;
    PresentationJobState state() const;
    int completedFrames() const; // Verified frames across all presets.
    int totalFrames() const;     // Presets times the planned frames per preset.
    QString message() const;
    QString directory() const;
    bool done() const;
    // Free bytes under a directory. Defaults to the file system; tests replace it.
    void setFreeSpaceProbe(std::function<qint64(const QString &)> probe);
    // The output budget in bytes. Defaults to 2 GiB; tests lower it.
    void setOutputLimit(qint64 bytes);
    // Where start and retry read a video's codec check. Set (the desktop), they use only the cached
    // answer and never block: a missing or stale answer refuses before capture, and the batch
    // dialog runs refresh() first. Unset (the CLI), they check the Blender synchronously. Not
    // owned; the object must outlive the batch.
    void setVideoChecks(PresentationVideoChecks *checks);
  signals:
    void changed();

  private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace sketchy
