#pragma once
#include "core/model.hpp"
#include "integrations/blender_job.hpp"
#include "integrations/presentation_store.hpp"
#include "integrations/render_snapshot.hpp"
#include <QObject>
#include <functional>
#include <memory>
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

  private:
    PresentationCapture(Document document, QByteArray encoded, PresentationPlan plan);
    Document document_;
    QByteArray encoded_;
    QString sha256_;
    std::string documentId_;
    std::uint64_t revision_{};
    PresentationPlan plan_;
    std::vector<CameraFrame> frames_;
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
  signals:
    void changed();

  private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace sketchy
