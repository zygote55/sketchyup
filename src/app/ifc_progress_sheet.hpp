#pragma once
// R097.e1: the IFC import progress sheet (ADR 0171 section 16.4). One modal sheet for the whole
// import: the file, the seven steps (check, read, units and structure, convert, build, check that
// nothing was dropped, create), one determinate bar (the converted/total elements while the
// geometry converts), the elapsed time, and a Cancel that ends the helper. The import itself runs
// on a worker thread (IfcImportRunner); this sheet only shows it and ends it.
#include "app/settled_dialog.hpp"
#include "app/ifc_import_runner.hpp"
#include <QLabel>
#include <QProgressBar>
#include <QTimer>
namespace sketchy {
class IfcProgressSheet : public Settled<QDialog> {
    Q_OBJECT
  public:
    IfcProgressSheet(IfcImportRunner *runner, const QString &fileName, QWidget *parent = nullptr);
    // Escape or the title bar's close while the import runs is Cancel: it ends the helper, and
    // the sheet closes when the runner has finished (so the window never reads a running import).
    void reject() override;
  public slots:
    void phase(const QString &name, qint64 value);
  private:
    void build(const QString &fileName);
    void markStep(int index, bool working);
    IfcImportRunner *runner;
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
