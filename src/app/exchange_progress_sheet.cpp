#include "app/exchange_progress_sheet.hpp"
#include <QHBoxLayout>
#include <QPushButton>
#include <QVBoxLayout>
namespace sketchy {
ExchangeProgressConfig ifcImportProgressConfig() {
    // The IFC import's own seven steps (ADR 0171 section 16.4): the check runs in the
    // application, the helper's phases cover the rest, and the application's conversion
    // and document creation finish the last three.
    ExchangeProgressConfig config;
    config.sheetName = "ifcProgressSheet";
    config.windowTitle = "Importing IFC";
    config.prefix = "ifcProgress";
    config.steps = {"Check the file", "Read the file", "Read units and structure",
                    "Convert geometry", "Build the model", "Check that nothing was dropped",
                    "Create the new model"};
    config.stepForPhase = [](const QString &phase) {
        if (phase == "parse")
            return 2;
        if (phase == "units" || phase == "model")
            return 3;
        if (phase == "geometry")
            return 4;
        if (phase == "assemble" || phase == "account" || phase == "write")
            return 5;
        return -1; // tasks and done are counts, not steps
    };
    config.restartLine = true;
    return config;
}
ExchangeProgressConfig stepImportProgressConfig() {
    // The same seven steps as the IFC sheet's, with the STEP helper's own phases (ADR 0172
    // section 7): "read" is the header, the units and the shapes (one phase of the helper),
    // "tessellate" is the BRepMesh of the shapes (the bar the helper's task counts drive)
    // and "write" is the package.
    ExchangeProgressConfig config;
    config.sheetName = "stepProgressSheet";
    config.windowTitle = "Importing STEP";
    config.prefix = "stepProgress";
    config.steps = {"Check the file", "Read the file", "Read units and structure",
                    "Convert geometry", "Build the model", "Check that nothing was dropped",
                    "Create the new model"};
    config.stepForPhase = [](const QString &phase) {
        if (phase == "read")
            return 1;
        if (phase == "tessellate")
            return 3;
        if (phase == "write")
            return 4;
        return -1; // tasks, at and done are counts, not steps
    };
    config.shapePhase = true; // "at" is the shape the helper is about to start
    config.fullBarAtEnd = true; // the import is done; the bar finishes
    config.restartLine = true; // the STEP import has no restart protocol; the line stays off
    return config;
}
ExchangeProgressConfig skpImportProgressConfig() {
    // The SketchUp import's own five steps (ADR 0176 section 9). The application checks the file
    // first (the initial step). The helper reads it ("read", "parse") and converts its geometry into
    // the package ("package"); the application then decodes the package ("build") and creates the
    // new model from it ("create"). The helper reports phases, not a running count, so the bar is
    // busy until the run ends.
    ExchangeProgressConfig config;
    config.sheetName = "skpProgressSheet";
    config.windowTitle = "Importing .skp file";
    config.prefix = "skpProgress";
    config.steps = {"Check the file", "Read the file", "Convert geometry", "Build the model",
                    "Create the new model"};
    config.stepForPhase = [](const QString &phase) {
        if (phase == "read" || phase == "parse")
            return 1;
        if (phase == "package")
            return 2;
        if (phase == "build")
            return 3;
        if (phase == "create")
            return 4;
        return -1;
    };
    config.busyBar = true;
    return config;
}
ExchangeProgressConfig ifcExportProgressConfig() {
    // The import sheet's own screen for the export (ADR 0171 section 16.8): the four steps,
    // a busy bar (the export has no running count) and a focused Cancel (16.9); a cancelled
    // export just closes — it does not flash the finished state, and the window shows its
    // status line instead of a sheet.
    ExchangeProgressConfig config;
    config.sheetName = "ifcExportProgressSheet";
    config.windowTitle = "Exporting IFC";
    config.prefix = "ifcExportProgress";
    config.steps = {"Capture the model", "Build the file", "Check its size", "Publish the file"};
    config.stepForPhase = [](const QString &phase) {
        if (phase == "capture")
            return 0;
        if (phase == "build")
            return 1;
        if (phase == "check")
            return 2;
        if (phase == "publish")
            return 3;
        return -1;
    };
    config.busyBar = true;
    config.closeOnCancel = true;
    config.focusCancel = true;
    return config;
}
ExchangeProgressConfig stepExportProgressConfig() {
    // The STEP export runs the same four steps, on the same phases (capture, build, check,
    // publish), as the IFC export (R098.c2, ADR 0172 section 8); only the title and the names differ.
    ExchangeProgressConfig config = ifcExportProgressConfig();
    config.sheetName = "stepExportProgressSheet";
    config.windowTitle = "Exporting STEP";
    config.prefix = "stepExportProgress";
    return config;
}
ExchangeProgressSheet::ExchangeProgressSheet(ExchangeRunner *runner, const ExchangeProgressConfig &cfg,
                                            const QString &fileName, QWidget *parent)
    : Settled<QDialog>(parent), runner(runner), config(cfg),
      stepState(new QLabel *[cfg.steps.size()]) {
    setObjectName(config.sheetName);
    setWindowTitle(config.windowTitle);
    build(fileName);
    QObject::connect(runner, &ExchangeRunner::phaseChanged, this, &ExchangeProgressSheet::phase);
    QObject::connect(runner, &ExchangeRunner::runFinished, this, [this] {
        if (config.closeOnCancel && this->runner && this->runner->wasCancelled()) {
            done(QDialog::Rejected);
            return;
        }
        markStep(stepCount, false); // every step is done
        for (int i = 0; i < stepCount; ++i)
            stepState[i]->setText("✓");
        if (config.fullBarAtEnd)
            bar->setValue(100);
        // Show the finished state briefly, then close so the window can take the result.
        QTimer::singleShot(150, this, [this] { done(QDialog::Accepted); });
    });
}
ExchangeProgressSheet::~ExchangeProgressSheet() { delete[] stepState; }
void ExchangeProgressSheet::reject() {
    if (runner && runner->isRunning()) {
        runner->requestCancel();
        return;
    }
    Settled<QDialog>::reject();
}
void ExchangeProgressSheet::build(const QString &fileName) {
    auto *root = new QVBoxLayout(this);
    auto *title = new QLabel(config.windowTitle);
    title->setStyleSheet("font-size: 20px; font-weight: 600;");
    fileLabel = new QLabel(fileName);
    fileLabel->setObjectName(config.prefix + "File");
    root->addWidget(title);
    root->addWidget(fileLabel);
    stepCount = config.steps.size();
    for (int i = 0; i < stepCount; ++i) {
        auto *row = new QHBoxLayout;
        auto *name = new QLabel(config.steps[i]);
        name->setObjectName(QString("%1Step%2").arg(config.prefix).arg(i + 1));
        stepState[i] = new QLabel("○");
        stepState[i]->setObjectName(QString("%1Step%2State").arg(config.prefix).arg(i + 1));
        row->addWidget(name, 1);
        row->addWidget(stepState[i]);
        root->addLayout(row);
    }
    bar = new QProgressBar;
    bar->setObjectName(config.prefix + "Bar");
    if (config.busyBar)
        bar->setRange(0, 0); // the run has no running count; the bar is busy
    else
        bar->setRange(0, 100);
    if (config.busyBar)
        bar->setAccessibleName(config.windowTitle);
    root->addWidget(bar);
    if (config.restartLine) {
        restartLine = new QLabel;
        restartLine->setObjectName(config.prefix + "Restart");
        restartLine->setAccessibleName("Restart line");
        restartLine->setStyleSheet("color: gray; font-size: 12px;");
        restartLine->setToolTip(
            "The engine restarted; the counts start over and the total may grow.");
        restartLine->hide();
        root->addWidget(restartLine);
    }
    auto *footer = new QHBoxLayout;
    elapsed = new QLabel("0 s");
    elapsed->setObjectName(config.prefix + "Elapsed");
    auto *cancel = new QPushButton("Cancel");
    cancel->setObjectName(config.prefix + "Cancel");
    if (config.focusCancel)
        cancel->setFocus(); // focused, but not the default (16.9): Enter does nothing, Esc cancels
    QObject::connect(cancel, &QPushButton::clicked, this, [this] {
        if (runner)
            runner->requestCancel();
    });
    footer->addWidget(elapsed);
    footer->addStretch();
    footer->addWidget(cancel);
    root->addLayout(footer);
    clock = new QTimer(this);
    clock->setInterval(500);
    QObject::connect(clock, &QTimer::timeout, this, [this] {
        if (runner)
            elapsed->setText(QString("%1 s").arg(runner->clock.elapsed() / 1000));
    });
    clock->start();
    markStep(config.initialStep, true); // the first step is working; the rest follow as it reports
    setMinimumWidth(460);
}
void ExchangeProgressSheet::markStep(int index, bool working) {
    // index is the 0-based step number; the vector is 0-based.
    if (index < 0 || index >= stepCount)
        return;
    for (int i = 0; i < stepCount; ++i) {
        if (i < index)
            stepState[i]->setText("✓");
        else if (i == index)
            stepState[i]->setText(working ? "●" : "✓");
        else
            stepState[i]->setText("○");
    }
}
void ExchangeProgressSheet::phase(const QString &name, qint64 value) {
    if (name == "tasks") {
        tasksTotal = value;
        return;
    }
    if (name == "done") {
        tasksDone = value;
        if (tasksTotal > 0)
            bar->setValue(int(100.0 * tasksDone / tasksTotal));
        return;
    }
    if (config.shapePhase && name == "at") {
        // The shape the helper is about to start: the bar follows it while the shape tessellates.
        if (tasksTotal > 0)
            bar->setValue(int(100.0 * value / tasksTotal));
        return;
    }
    const int step = config.stepForPhase(name);
    if (step < 0)
        return;
    markStep(step, true);
}
} // namespace sketchy
#include "app/exchange_progress_sheet.moc"
