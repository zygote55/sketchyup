// R098.a3: File › Import STEP… / Show STEP import report… (ADR 0172 section 8). The helper
// (sketchyup-ifc-worker) does the heavy work in its --import-step; the import runs on a worker
// thread so the UI is never blocked (the IFC import's 16.4, in the STEP's own runner). A missing
// helper is "not installed"; the STEP import needs no kernel, so the helper's Limited state is
// not a limitation for STEP.
#include "app/window.hpp"
#include "app/ifc_import_dialog.hpp"
#include "app/step_import_runner.hpp"
#include "app/step_progress_sheet.hpp"
#include "app/ifc_sheets.hpp"
#include <QCoreApplication>
#include <QFileDialog>
#include <QFileInfo>
#include <QJsonDocument>
#include <QLabel>
#include <QPointer>
#include <QPushButton>
#include <QStatusBar>
#include <thread>
namespace sketchy {
namespace {
// The report chip: the IFC chip's own rules for STEP — while the imported model is open it says
// what the import produced and reopens the report; a cancel or a failed import clears it.
void setStepReportChip(QPushButton *chip, const QJsonObject &report, bool visible) {
    if (!chip)
        return;
    if (!visible) {
        chip->hide();
        return;
    }
    // The attention is the helper's own: the solids it read but did not convert (the budget
    // and the tessellation failures); the conversion omits nothing.
    const int attention = report["step"].toObject()["omitted"].toInt(0);
    if (attention > 0) {
        chip->setText(QString("\u26a0 STEP import \u00b7 %1 need attention \u00b7 Show report").arg(attention));
        chip->setAccessibleName(
            QString("STEP import, %1 items need attention. Show report").arg(attention));
    } else {
        chip->setText("STEP import report");
        chip->setAccessibleName("STEP import report");
    }
    chip->show();
}
} // namespace
void Window::clearStepReportState() {
    stepImportReport_ = QJsonObject();
    stepImportFileName_.clear();
    stepImportElapsedMs_ = 0;
    stepImportRestarts_ = 0;
    setStepReportChip(stepReportChip_, QJsonObject(), false);
    if (stepReportAction_)
        stepReportAction_->setEnabled(false);
}
void Window::importStep() {
    // A committed import replaces the model and (re)sets the report chip below; a refused or
    // failed import leaves the model untouched, so the current import's chip, if any, stays.
    probeIfcNow(); // the STEP import runs in the IFC helper; the probe is the helper's own
    const IfcProbe &probe = ifcProbe_;
    if (probe.state == IfcState::Missing) {
        showStepUnavailable(this, probe);
        return;
    }
    if (probe.state == IfcState::Broken) {
        showStepUnavailable(this, probe);
        return;
    }
    // Limited (no OpenCASCADE kernel) is not a limitation for the STEP import: the helper's
    // own STEP reader and BRepMesh need none, so the import proceeds and the dialog shows no
    // kernel banner for the STEP kind.
    const QString path =
        QFileDialog::getOpenFileName(this, "Choose a STEP file", {},
                                     "STEP models (*.step *.STEP *.stp);;All files (*.*)");
    if (path.isEmpty())
        return;
    IfcImportDialog dialog(this);
    dialog.setKind(IfcImportDialog::Kind::Step);
    dialog.setProbe(probe);
    dialog.setFile(path);
    if (dialog.exec() != QDialog::Accepted || dialog.refused())
        return;
    StepWorkerOptions workerOptions = dialog.stepWorkerOptions();
    workerOptions.workerPath = ifcProbe_.workerPath;
    // The confirm phase: before the helper starts, ask about unsaved changes (16.3).
    if (!canReplace())
        return;
    auto *runner = new StepImportRunner;
    runner->begin(path, workerOptions);
    auto *sheet = new StepProgressSheet(runner, QFileInfo(path).fileName(), this);
    sheet->setAttribute(Qt::WA_DeleteOnClose);
    sheet->exec();
    // The import is done (or cancelled). The sheet closes only after the runner has finished;
    // wait anyway, so the result is never read from a running import.
    runner->wait();
    if (runner->wasCancelled()) {
        status_->setText("The import was cancelled; nothing was changed.");
        runner->wait();
        delete runner;
        return;
    }
    if (runner->hasResult()) {
        // The commit phase: ask again (16.3). A cancel here discards the finished import.
        if (!canReplace()) {
            status_->setText("The import finished, but the change was cancelled; the model was not replaced.");
            runner->wait();
            delete runner;
            return;
        }
        const int restarts =
            runner->report()["engine"].toObject()["restarts"].toInt(0);
        const qint64 elapsed = runner->clock.elapsed();
        const QJsonObject report = runner->report();
        doc_ = std::move(*runner->document()); // the import's document, moved out of the runner
        resetRecoveryContext();
        path_.clear(); // Save must choose a new native file, never overwrite the source.
        viewport_->cancel();
        viewport_->setSelection(0);
        viewport_->fit();
        sync();
        status_->setText("Imported an unsaved STEP model · Source file unchanged");
        stepImportReport_ = report;
        stepImportFileName_ = path;
        stepImportElapsedMs_ = elapsed;
        stepImportRestarts_ = restarts;
        if (stepReportAction_)
            stepReportAction_->setEnabled(true);
        setStepReportChip(stepReportChip_, report, true);
        runner->wait();
        delete runner;
        ::sketchy::showStepImportReport(this, path, report, elapsed, restarts);
        return;
    }
    // A refusal or a failure: the model was not replaced, so the current import's report chip,
    // if any, stays with its model.
    showStepRefusal(this, path, runner->code(), runner->message(), runner->report());
    runner->wait();
    delete runner;
}
void Window::showStepImportReport() {
    if (stepImportReport_.isEmpty())
        return;
    ::sketchy::showStepImportReport(this, stepImportFileName_, stepImportReport_,
                                       stepImportElapsedMs_, stepImportRestarts_);
}
} // namespace sketchy
