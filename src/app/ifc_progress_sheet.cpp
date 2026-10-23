#include "app/ifc_progress_sheet.hpp"
#include <QFile>
#include <QHBoxLayout>
#include <QPushButton>
#include <QVBoxLayout>
namespace sketchy {
namespace {
const char *const stepNames[7] = {"Check the file", "Read the file", "Read units and structure",
                                   "Convert geometry", "Build the model", "Check that nothing was dropped",
                                   "Create the new model"};
int stepForPhase(const QString &phase) {
    if (phase == "parse")
        return 2;
    if (phase == "units" || phase == "model")
        return 3;
    if (phase == "geometry")
        return 4;
    if (phase == "assemble" || phase == "account" || phase == "write")
        return 5;
    return -1; // tasks and done are counts, not steps
}
} // namespace
IfcProgressSheet::IfcProgressSheet(IfcImportRunner *runner, const QString &fileName, QWidget *parent)
    : Settled<QDialog>(parent), runner(runner) {
    setObjectName("ifcProgressSheet");
    setWindowTitle("Importing IFC");
    build();
    QObject::connect(runner, &IfcImportRunner::phaseChanged, this, &IfcProgressSheet::phase);
    QObject::connect(runner, &IfcImportRunner::runFinished, this, [this] {
        for (int i = 0; i < 7; ++i)
            markStep(i, false);
        for (auto *state : stepState)
            state->setText("✓");
        // Show the finished state briefly, then close so the window can take the result.
        QTimer::singleShot(150, this, [this] { done(QDialog::Accepted); });
    });
}
void IfcProgressSheet::build() {
    auto *root = new QVBoxLayout(this);
    auto *title = new QLabel("Importing IFC");
    title->setStyleSheet("font-size: 20px; font-weight: 600;");
    fileLabel = new QLabel(fileName);
    fileLabel->setObjectName("ifcProgressFile");
    root->addWidget(title);
    root->addWidget(fileLabel);
    for (int i = 0; i < 7; ++i) {
        auto *row = new QHBoxLayout;
        auto *name = new QLabel(stepNames[i]);
        name->setObjectName(QString("ifcProgressStep%1").arg(i + 1));
        stepState[i] = new QLabel("○");
        stepState[i]->setObjectName(QString("ifcProgressStep%1State").arg(i + 1));
        row->addWidget(name, 1);
        row->addWidget(stepState[i]);
        root->addLayout(row);
    }
    bar = new QProgressBar;
    bar->setObjectName("ifcProgressBar");
    bar->setRange(0, 100);
    bar->setValue(0);
    root->addWidget(bar);
    restartLine = new QLabel;
    restartLine->setObjectName("ifcProgressRestart");
    restartLine->hide();
    root->addWidget(restartLine);
    auto *footer = new QHBoxLayout;
    elapsed = new QLabel("0 s");
    elapsed->setObjectName("ifcProgressElapsed");
    auto *cancel = new QPushButton("Cancel");
    cancel->setObjectName("ifcProgressCancel");
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
void IfcProgressSheet::markStep(int index, bool working) {
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
void IfcProgressSheet::phase(const QString &name, qint64 value) {
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
    const int step = stepForPhase(name);
    if (step < 0)
        return;
    markStep(step, true);
    if (step > currentStep)
        currentStep = step;
}
} // namespace sketchy
#include "app/ifc_progress_sheet.moc"
