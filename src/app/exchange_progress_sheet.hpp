#pragma once
// The shared exchange progress sheet (the screens refactor of R097.e1, R098.a3 and R097.e2):
// one modal sheet for a whole exchange run — the file, the steps, one bar, the elapsed time
// and a Cancel that ends the job. Escape or the title bar's close while the run is going is
// Cancel: it ends the job, and the sheet closes when the runner has finished (so the window
// never reads a running run). The IFC import, the STEP import and the IFC export were three
// copies of this; a new format (R102.c, the .skp import) needs only its step list and its
// strings. The named sheets (IfcProgressSheet, StepProgressSheet, IfcExportProgressSheet) are
// thin subclasses with their own faces.
#include "app/settled_dialog.hpp"
#include "app/exchange_runner.hpp"
#include <QLabel>
#include <QProgressBar>
#include <QStringList>
#include <QTimer>
#include <functional>
namespace sketchy {
// A progress sheet's face (the format-specific parts; the rest is the shared behaviour).
struct ExchangeProgressConfig {
    QString sheetName; // the sheet's own objectName ("ifcProgressSheet" ...)
    QString windowTitle; // "Importing IFC" ...
    QString prefix; // the object-name prefix ("ifcProgress" ...)
    QStringList steps; // the step names, in order
    // The helper's phase -> the step it works (0-based); -1 is a count, not a step.
    std::function<int(const QString &)> stepForPhase;
    bool shapePhase = false; // the "at" phase (the shape about to start) drives the bar
    bool fullBarAtEnd = false; // the bar finishes at 100 when the run ends
    bool closeOnCancel = false; // a cancelled run closes at once, without the finished state
    bool busyBar = false; // no running count: the bar is busy (0..0)
    bool restartLine = false; // the restart line (the imports' own; the export has none)
    bool focusCancel = false; // the Cancel is focused, but not the default (16.9)
    int initialStep = 0; // the step that is working when the sheet opens (0-based)
};
class ExchangeProgressSheet : public Settled<QDialog> {
    Q_OBJECT
  public:
    ExchangeProgressSheet(ExchangeRunner *runner, const ExchangeProgressConfig &config,
                          const QString &fileName, QWidget *parent = nullptr);
    ~ExchangeProgressSheet() override;
    // Escape or the title bar's close while the run is going is Cancel: it ends the job, and
    // the sheet closes when the runner has finished (so the window never reads a running run).
    void reject() override;
  public slots:
    void phase(const QString &name, qint64 value);
  private:
    void build(const QString &fileName);
    void markStep(int index, bool working);
    ExchangeRunner *runner;
    ExchangeProgressConfig config;
    QLabel *fileLabel;
    QLabel *restartLine = nullptr; // the imports' restart line; the export has none
    int stepCount = 0;
    QLabel **stepState = nullptr; // deleted in the destructor
    QProgressBar *bar;
    QLabel *elapsed;
    QTimer *clock;
    qint64 tasksTotal = 0;
    qint64 tasksDone = 0;
};
// The faces (defined in the .cpp): the IFC import's seven steps (16.4), the STEP import's
// (ADR 0172 section 8), the SketchUp import's five steps (ADR 0176 section 9), the IFC export's
// four steps (16.8) and the STEP export's (the same four steps and the same helper phases, titled
// "Exporting STEP").
ExchangeProgressConfig ifcImportProgressConfig();
ExchangeProgressConfig stepImportProgressConfig();
ExchangeProgressConfig skpImportProgressConfig();
ExchangeProgressConfig ifcExportProgressConfig();
ExchangeProgressConfig stepExportProgressConfig();
} // namespace sketchy
