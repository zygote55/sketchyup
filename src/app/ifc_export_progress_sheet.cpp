#include "app/ifc_export_progress_sheet.hpp"
#include <QHBoxLayout>
#include <QPushButton>
#include <QVBoxLayout>
namespace sketchy {
namespace {
const char *const stepNames[4] = {"Capture the model", "Build the file", "Check its size", "Publish the file"};
int stepForPhase(const QString &phase) {
    if (phase == "capture")
        return 0;
    if (phase == "build")
        return 1;
    if (phase == "check")
        return 2;
    if (phase == "publish")
        return 3;
    return -1;
}
} // namespace
IfcExportProgressSheet::IfcExportProgressSheet(IfcExportRunner *runner, const QString &fileName, QWidget *parent)
    : Settled<QDialog>(parent), runner(runner) {
    setObjectName("ifcExportProgressSheet");
    setWindowTitle("Exporting IFC");
    build(fileName);
    QObject::connect(runner, &IfcExportRunner::phaseChanged, this, &IfcExportProgressSheet::phase);
    QObject::connect(runner, &IfcExportRunner::runFinished, this, [this, runner] {
        // A cancelled export just closes; it does not flash the finished state, and the window
        // shows its status line instead of a sheet (ADR 0171 section 16.8).
        if (runner && runner->wasCancelled()) {
            done(QDialog::Rejected);
            return;
        }
        for (int i = 0; i < 4; ++i)
            markStep(i, false);
        for (auto *state : stepState)
            state->setText("✓");
        // Show the finished state briefly, then close so the window can take the result.
        QTimer::singleShot(150, this, [this] { done(QDialog::Accepted); });
    });
}
void IfcExportProgressSheet::reject() {
    if (runner && runner->isRunning()) {
        runner->requestCancel();
        return;
    }
    Settled<QDialog>::reject();
}
void IfcExportProgressSheet::build(const QString &fileName) {
    auto *root = new QVBoxLayout(this);
    auto *title = new QLabel("Exporting IFC");
    title->setStyleSheet("font-size: 20px; font-weight: 600;");
    fileLabel = new QLabel(fileName);
    fileLabel->setObjectName("ifcExportProgressFile");
    root->addWidget(title);
    root->addWidget(fileLabel);
    for (int i = 0; i < 4; ++i) {
        auto *row = new QHBoxLayout;
        auto *name = new QLabel(stepNames[i]);
        name->setObjectName(QString("ifcExportProgressStep%1").arg(i + 1));
        stepState[i] = new QLabel("○");
        stepState[i]->setObjectName(QString("ifcExportProgressStep%1State").arg(i + 1));
        row->addWidget(name, 1);
        row->addWidget(stepState[i]);
        root->addLayout(row);
    }
    bar = new QProgressBar;
    bar->setObjectName("ifcExportProgressBar");
    bar->setRange(0, 0); // the export has no running count; the bar is busy
    bar->setAccessibleName("Exporting IFC");
    root->addWidget(bar);
    auto *footer = new QHBoxLayout;
    elapsed = new QLabel("0 s");
    elapsed->setObjectName("ifcExportProgressElapsed");
    auto *cancel = new QPushButton("Cancel");
    cancel->setObjectName("ifcExportProgressCancel");
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
    markStep(0, true); // the capture starts at once
    setMinimumWidth(460);
}
void IfcExportProgressSheet::markStep(int index, bool working) {
    if (index < 0 || index > 3)
        return;
    for (int i = 0; i < 4; ++i) {
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
void IfcExportProgressSheet::phase(const QString &name, qint64) {
    const int step = stepForPhase(name);
    if (step < 0)
        return;
    markStep(step, true);
    if (step > currentStep)
        currentStep = step;
}
} // namespace sketchy
#include "app/ifc_export_progress_sheet.moc"
