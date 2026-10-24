#include "app/step_progress_sheet.hpp"
#include <QFile>
#include <QHBoxLayout>
#include <QPushButton>
#include <QVBoxLayout>
namespace sketchy {
namespace {
// The same seven steps as the IFC sheet's: the check runs in the application, the helper's own
// phases ("read", "tessellate", "write") cover the rest, and the application's conversion and
// document creation finish the last three.
const char *const stepNames[7] = {"Check the file", "Read the file", "Read units and structure",
                                   "Convert geometry", "Build the model", "Check that nothing was dropped",
                                   "Create the new model"};
int stepForPhase(const QString &phase) {
    // The STEP helper's phases (ADR 0172 section 7): "read" is the header, the units and the
    // shapes (steps 2 and 3, one phase of the helper); "tessellate" is the BRepMesh of the
    // shapes (step 4); "write" is the package (step 5).
    if (phase == "read")
        return 2;
    if (phase == "tessellate")
        return 4;
    if (phase == "write")
        return 5;
    return -1; // tasks, at and done are counts, not steps
}
} // namespace
StepProgressSheet::StepProgressSheet(StepImportRunner *runner, const QString &fileName, QWidget *parent)
    : Settled<QDialog>(parent), runner(runner) {
    setObjectName("stepProgressSheet");
    setWindowTitle("Importing STEP");
    build(fileName);
    QObject::connect(runner, &StepImportRunner::phaseChanged, this, &StepProgressSheet::phase);
    QObject::connect(runner, &StepImportRunner::runFinished, this, [this] {
        for (int i = 0; i < 7; ++i)
            markStep(i, false);
        for (auto *state : stepState)
            state->setText("✓");
        // Show the finished state briefly, then close so the window can take the result.
        QTimer::singleShot(150, this, [this] { done(QDialog::Accepted); });
    });
}
void StepProgressSheet::reject() {
    if (runner && runner->isRunning()) {
        runner->requestCancel();
        return;
    }
    Settled<QDialog>::reject();
}
void StepProgressSheet::build(const QString &fileName) {
    auto *root = new QVBoxLayout(this);
    auto *title = new QLabel("Importing STEP");
    title->setStyleSheet("font-size: 20px; font-weight: 600;");
    fileLabel = new QLabel(fileName);
    fileLabel->setObjectName("stepProgressFile");
    root->addWidget(title);
    root->addWidget(fileLabel);
    for (int i = 0; i < 7; ++i) {
        auto *row = new QHBoxLayout;
        auto *name = new QLabel(stepNames[i]);
        name->setObjectName(QString("stepProgressStep%1").arg(i + 1));
        stepState[i] = new QLabel("○");
        stepState[i]->setObjectName(QString("stepProgressStep%1State").arg(i + 1));
        row->addWidget(name, 1);
        row->addWidget(stepState[i]);
        root->addLayout(row);
    }
    bar = new QProgressBar;
    bar->setObjectName("stepProgressBar");
    bar->setRange(0, 100);
    bar->setValue(0);
    root->addWidget(bar);
    restartLine = new QLabel;
    restartLine->setObjectName("stepProgressRestart");
    restartLine->hide(); // the STEP import has no restart protocol; the line stays off
    root->addWidget(restartLine);
    auto *footer = new QHBoxLayout;
    elapsed = new QLabel("0 s");
    elapsed->setObjectName("stepProgressElapsed");
    auto *cancel = new QPushButton("Cancel");
    cancel->setObjectName("stepProgressCancel");
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
    markStep(0, true); // the check ran before the helper
    setMinimumWidth(460);
}
void StepProgressSheet::markStep(int index, bool working) {
    if (index < 0 || index > 6)
        return;
    for (int i = 0; i < 7; ++i) {
        if (i < index)
            stepState[i]->setText("✓");
        else if (i == index)
            stepState[i]->setText(working ? "●" : "✓");
        else
            stepState[i]->setText("○");
    }
    if (working)
        currentStep = index;
}
void StepProgressSheet::phase(const QString &name, qint64 value) {
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
    if (name == "at") {
        // The shape the helper is about to start: the bar follows it while the shape tessellates.
        if (tasksTotal > 0)
            bar->setValue(int(100.0 * value / tasksTotal));
        return;
    }
    const int step = stepForPhase(name);
    if (step < 0)
        return;
    markStep(step, true);
    if (step > currentStep)
        currentStep = step;
}
} // namespace sketchy
#include "app/step_progress_sheet.moc"
