#pragma once
// R096.f2 (UX_DESIGN §15.6; ADR 0169 §2, R096.a part 2 notes): a terrain replay as a job with
// progress and cancel. Remove (terrain.remove_edit), Edit pad… and Restamp (terrain.update_edit)
// and Reset edits… (terrain.reset_edits) replay the edit list from the base sources:
// - the request is checked and built on the document (parseTerrainReplayRequest, throwing the
//   command's refusals);
// - the replay runs on a worker (runTerrainReplay, which reads no document) with a
//   TerrainProgress, while a chip in the status bar shows "Replaying 3 changes · 64 %" with Cancel;
// - its result is applied on the UI thread as one labelled, undoable edit (applyTerrainReplay),
//   which refuses a terrain changed since the request was made.
// Cancelling stops the worker at its next checkpoint and leaves the model as it was. One job runs
// at a time.
#include "app/terrain_run.hpp"
#include "automation/terrain_commands.hpp"
#include <QFrame>
#include <QJsonObject>
#include <QPointer>
#include <QThreadPool>
#include <QTimer>
#include <memory>
class QLabel;
class QPushButton;
namespace sketchy {
class Viewport;
// The job chip in the window's status bar (hidden while no job runs).
class TerrainReplayChip final : public QFrame {
    Q_OBJECT
  public:
    explicit TerrainReplayChip(QWidget *parent = nullptr);
    QLabel *text() const { return text_; }
    QPushButton *cancelButton() const { return cancel_; }
    void show(const QString &text);

  signals:
    void cancelRequested();

  private:
    QLabel *text_{};
    QPushButton *cancel_{};
};

class TerrainReplayJob final : public QObject {
    Q_OBJECT
  public:
    // Starts `command` (a replaying terrain command) for `title` ("Remove terrain change", …).
    // Refusals before the replay are thrown (the caller shows them). `window` holds the chip
    // (found by its object name; without one the job still runs, with no chip).
    static TerrainReplayJob *start(QWidget *window, Document &doc, Viewport &view,
                                   const QJsonObject &command, const QString &title);
    // The running job, if any (one at a time).
    static TerrainReplayJob *running();
    // Tests: process events until no job runs (or the time is out).
    static bool waitForIdle(int timeoutMs = 20000);
    void cancel();
    const TerrainRunControl &control() const { return *control_; }
    ~TerrainReplayJob() override;

  signals:
    // The replay ended: applied (the document changed), refused, or cancelled.
    void finished(bool applied);

  private:
    TerrainReplayJob(QWidget *window, Document &doc, Viewport &view, QJsonObject command,
                     QString title, TerrainReplayRequest request);
    void poll();
    void done(std::shared_ptr<TerrainImportResult> result);
    QPointer<QWidget> window_;
    Document &doc_;
    Viewport &view_;
    QJsonObject command_;
    QString title_;
    TerrainReplayRequest request_;
    std::shared_ptr<TerrainRunControl> control_;
    QThreadPool pool_;
    QTimer poll_;
    QPointer<TerrainReplayChip> chip_;
    std::size_t changes_{};
};
} // namespace sketchy
