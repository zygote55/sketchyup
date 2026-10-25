#pragma once
// R098.a3: the STEP import's progress sheet (ADR 0172 section 8) — the IFC sheet's own seven steps
// (check, read, units and structure, convert, build, check that nothing was dropped, create) with
// the STEP helper's own phases: "read" (the header, the units, the shapes), "tessellate" (the
// BRepMesh of the shapes, the bar the helper's task counts drive) and "write" (the package). The
// import itself runs on a worker thread (StepImportRunner); this sheet only shows it and ends it.
#include "app/settled_dialog.hpp"
#include "app/step_import_runner.hpp"
#include <QLabel>
#include <QProgressBar>
#include <QTimer>
namespace sketchy {
class StepProgressSheet : public Settled<QDialog> {
    Q_OBJECT
  public:
    StepProgressSheet(StepImportRunner *runner, const QString &fileName, QWidget *parent = nullptr);
    // Escape or the title bar's close while the import runs is Cancel: it ends the helper, and
    // the sheet closes when the runner has finished (so the window never reads a running import).
    void reject() override;
  public slots:
    void phase(const QString &name, qint64 value);
  private:
    void build(const QString &fileName);
    void markStep(int index, bool working);
    StepImportRunner *runner;
    QLabel *fileLabel;
    QLabel *stepState[7];
    QProgressBar *bar;
    QLabel *restartLine;
    QLabel *elapsed;
    QTimer *clock;
    qint64 tasksTotal = 0;
    qint64 tasksDone = 0;
    int currentStep = 1; // the "check" step, done before the helper
};
} // namespace sketchy
