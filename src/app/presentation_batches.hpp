#pragma once
// R099.d3 (UX_DESIGN §17.4 to §17.7, §17.10; ADR 0173 §4, §7): the desktop's presentation batches.
// It owns the job store, the batch engine and the video probe cache, and it holds no widgets. The
// batch dialog, the Jobs list, the results sheet and the quit prompt all go through it. The store
// is opened on first use, so a window that never shows batches creates no folder.
#include "core/model.hpp"
#include "integrations/blender_job.hpp"
#include "integrations/presentation_batch.hpp"
#include "integrations/presentation_store.hpp"
#include <QJsonObject>
#include <QObject>
#include <memory>
#include <vector>

namespace sketchy {

// The manifest a batch last published in its output folder, or an empty object when there is none.
// It holds the per-preset frame counts, the videos and the device each preset used.
QJsonObject presentationManifest(const QString &outputDirectory);

class PresentationBatches : public QObject {
    Q_OBJECT
  public:
    // `directory` is the private job store. The desktop passes its app data folder; tests pass a
    // temp folder, so no user data is read or written.
    PresentationBatches(Document &document, QString directory, QObject *parent = nullptr);
    ~PresentationBatches() override;
    // The plan of one camera path and its presets, as the document holds them now.
    PresentationPlan planFor(Id path, const std::vector<Id> &presets, bool video,
                             PresentationVideoCodec codec) const;
    // Starts a batch from the document as it is now. A refusal throws before any folder or job
    // exists.
    QString start(const PresentationPlan &plan, const QString &folder, BlenderJob::Options options);
    // Continues a stopped, failed or canceled batch at its first unfinished frame. Its device is
    // the one its finished frames used; a batch with no finished frame on a device cannot resume.
    void resume(const QString &id);
    // Cancels the running batch at once, discarding its in-flight frame. Finished frames stay.
    void cancel();
    // Stops the running batch now, leaving it Interrupted and resumable (the quit path, §17.12).
    void stop();
    // A batch is Running or Canceling.
    bool running() const;
    // The job ID of the batch this process started or resumed last, or empty.
    QString lastJob() const;
    // Removes a finished batch's record and log. Its output folder stays.
    void remove(const QString &id);
    void clearFinished();
    // The stored jobs, oldest first.
    std::vector<StoredPresentationJob> jobs();
    // The job was started or resumed by this process, so its document is this model's session.
    bool started(const QString &id) const;
    // The model is the one the batch captured, unchanged since (the "model has changed since"
    // test).
    bool modelUnchanged(const StoredPresentationJob &job) const;
    // A saved scene's name, or a note that the scene no longer exists.
    QString sceneName(Id scene) const;
    // A one-line summary for the status chip of the running batch, empty when none runs.
    QString summary();
    // Opens the store on first use. Throws when another process has it open.
    PresentationJobStore &store();
    PresentationVideoChecks &videoChecks();
  signals:
    // Every change of the store or the running batch.
    void changed();
    // One sentence per milestone: the start, each finished preset, each written video and each
    // final state. Never one per frame (UX_DESIGN §17.10).
    void announced(const QString &text);

  private:
    // The Blender options a resume uses: the device the batch's finished frames used.
    BlenderJob::Options resumeOptions(const StoredPresentationJob &job, QString executable) const;
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace sketchy
