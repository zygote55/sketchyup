#pragma once
// R101.e2 (UX_DESIGN §19, approved 2026-10-27): the Quantities section of the Model Tray. It shows
// the report engine's totals for the whole model, grouped and filtered as the person chooses; the
// invalid list; saved schedules (save, rename, delete, run, stale rows); and the CSV and JSON
// exports. The section computes only while it is shown. Every pass and every export runs on a
// ReportJob over a snapshot of the model, so the model stays usable and Cancel returns at once.
#include "app/viewport.hpp"
#include <QElapsedTimer>
#include <QJsonObject>
#include <QString>
#include <QWidget>
#include <atomic>
#include <functional>
#include <map>
#include <set>
#include <vector>

class QCheckBox;
class QComboBox;
class QFrame;
class QLabel;
class QListWidget;
class QProgressBar;
class QPushButton;
class QRadioButton;
class QTimer;
class QTreeWidget;
class QVBoxLayout;

namespace sketchy {

class ReportJob;

class QuantitiesPanel : public QWidget {
    Q_OBJECT
  public:
    QuantitiesPanel(Document &document, Viewport &viewport, QWidget *parent = nullptr);
    ~QuantitiesPanel() override;

    // The model's file, or "" for an unsaved model. A schedule needs a saved model (§19.6), and an
    // export is named after the model. The window sets it.
    void setModelPath(std::function<QString()> path) { modelPath_ = std::move(path); }

    // Called on every window sync. Runs the report again when the revision or the selection the
    // totals use has changed, and only while the section is shown (§19.1, §19.3).
    void refresh();

    // View → Quantities: focus lands on Group by.
    void focusGroupBy();

    // The Tags, Materials or Definitions popover, filled and not shown; a click on its value shows
    // it. A Wayland test fills it through here: Qt's Wayland popups need an input serial that
    // synthetic input cannot supply, so showing one there prints a warning (D-068).
    QFrame *buildPicker(const QString &kind);

    // Tests only: called on the worker thread before each pass or export, with the run's cancel
    // flag, so a test can hold the work until it is cancelled. Empty in the application.
    static inline std::function<void(const std::atomic<bool> &)> runGateForTesting;

  protected:
    void showEvent(QShowEvent *event) override;
    void hideEvent(QHideEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;

  private:
    // A complete result, with the revision and the request it was computed at (§19.3).
    struct Result {
        std::uint64_t revision{};
        QString signature;
        QJsonObject data;
        bool valid{};
    };
    // A saved schedule as listed for this model (§19.6). `staleText` is set for a stale one
    // (§19.7).
    struct Row {
        QString name, summary, saved, staleText;
        QJsonObject request;
        bool refused{};
    };

    Document &doc_;
    Viewport &view_;
    std::function<QString()> modelPath_;

    // The live controls (§19.2).
    QLabel *header_{}, *headerRevision_{}, *backLive_{};
    QPushButton *backButton_{};
    QComboBox *groupBy_{};
    QRadioButton *includeHidden_{}, *visibleOnly_{};
    QLabel *visibleCaption_{};
    QPushButton *tagsValue_{}, *materialsValue_{}, *definitionsValue_{};
    QCheckBox *selectionOnly_{};
    QLabel *selectionHint_{};
    QWidget *missing_{};
    QVBoxLayout *missingLayout_{};
    QString missingSignature_;
    QLabel *error_{}, *status_{};

    // The computing line (§19.3).
    QWidget *computing_{};
    QLabel *computingText_{};
    QProgressBar *bar_{};
    QPushButton *cancel_{};
    QElapsedTimer clock_;
    QTimer *ticker_{};

    // The totals (§19.1, §19.4, §19.5).
    QWidget *totals_{};
    QLabel *totalsTitle_{}, *precision_{}, *empty_{};
    QLabel *area_{}, *volume_{}, *contexts_{}, *instances_{}, *definitionCount_{}, *unvalidated_{};
    QLabel *definitionsCaption_{}, *unvalidatedCaption_{};
    QTreeWidget *groups_{};
    QLabel *revisionLine_{};
    QWidget *notSolids_{};
    QVBoxLayout *notSolidsLayout_{};
    QWidget *terrain_{};
    QVBoxLayout *terrainLayout_{};

    // The exports and the schedules (§19.6 to §19.8).
    QPushButton *exportCsv_{}, *exportJson_{}, *saveSchedule_{};
    QLabel *scheduleNote_{};
    QWidget *schedules_{};
    QLabel *scheduleCount_{}, *storeError_{}, *otherModels_{};
    QListWidget *scheduleList_{};

    // The filters the live view uses. They are IDs, and the document decides whether they resolve.
    std::set<Id> tags_, materials_, definitions_;

    // The active schedule (§19.6), when Run made it the view.
    QString scheduleName_;
    QJsonObject scheduleRequest_;
    std::vector<Row> rows_;
    std::map<QString, QString>
        refusals_; // a stale row's refusal sentence, by schedule name (§19.7)
    QString storePath_, storeFault_;
    bool listed_{};
    std::uint64_t listedRevision_{};

    // The shown result and the job that computes the next one.
    Result shown_;
    ReportJob *job_{};
    bool jobExport_{};
    std::uint64_t jobRevision_{};
    QString jobSignature_;
    bool jobAnnounce_{};
    bool queued_{}; // a newer request waits for the running pass to end
    QString
        cancelledSignature_; // Cancel stands for this request until something it depends on changes
    QString exportPath_, exportSchedule_;
    bool exportingCsv_{};

    void buildTotals();
    void buildSchedules();
    void syncControls();
    void refreshMissing();
    void pickerMenu(const QString &kind, QWidget *anchor);

    QJsonObject liveRequest(bool withSelection) const;
    std::set<Id> selectedBodies() const;
    bool filtersResolved() const;
    void requestRun(bool announce);
    void startJob(ReportJob *job, bool exporting, bool announce);
    void jobFinished();
    void finishExport(ReportJob *job);
    void showResult(const Result &result);
    void showComputing(bool visible);
    void setError(const QString &text);

    void reloadSchedules();
    void runSchedule(const QString &name);
    void backToLive();
    void saveSchedule(const QJsonObject &request, const QString &rename);
    void renameSchedule(const QString &name);
    void deleteSchedule(const QString &name);
    void scheduleActions(const QString &name, const QPoint &at);
    void exportFile(bool csv, const QString &schedule);
    void exportReport(const QString &path, bool csv, const QString &schedule,
                      const QJsonObject &exportData, std::uint64_t revision);
    QString modelName() const;
};

} // namespace sketchy
