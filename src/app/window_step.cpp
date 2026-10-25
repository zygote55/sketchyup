// R098.a3: File › Import STEP… / Show STEP import report… (ADR 0172 section 8). The helper
// (sketchyup-ifc-worker) does the heavy work in its --import-step; the import runs on a worker
// thread so the UI is never blocked (the IFC import's 16.4, in the STEP's own runner). A missing
// helper is "not installed"; the STEP import needs no kernel, so the helper's Limited state is
// not a limitation for STEP. R098.c2: File › Export STEP… (exportStep) is the same helper's
// --export-step, on the IFC export screens' shape.
#include "app/window.hpp"
#include "app/import_report.hpp"
#include "app/ifc_import_dialog.hpp"
#include "app/step_export_dialog.hpp"
#include "app/step_export_progress_sheet.hpp"
#include "app/step_export_runner.hpp"
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
        stepImportReport_ = ImportReportState{true, "", path, report, elapsed, restarts};
        if (stepReportAction_)
            stepReportAction_->setEnabled(true);
        setImportReportChip(stepReportChip_, stepImportReportFace(), stepImportReport_);
        runner->wait();
        delete runner;
        ::sketchy::showImportReport(this, stepImportReportFace(), stepImportReport_);
        return;
    }
    // A refusal or a failure: the model was not replaced, so the current import's report chip,
    // if any, stays with its model.
    showStepRefusal(this, path, runner->code(), runner->message(), runner->report());
    runner->wait();
    delete runner;
}
void Window::showStepImportReport() {
    if (stepImportReport_.done)
        ::sketchy::showImportReport(this, stepImportReportFace(), stepImportReport_);
}

void Window::exportStep() {
    probeIfcNow();
    const IfcProbe &probe = ifcProbe_;
    if (probe.state == IfcState::Missing || probe.state == IfcState::Broken) {
        // Export needs the helper (not the kernel): a missing helper is the availability sheet,
        // and a Limited one is not a limitation for STEP.
        showStepUnavailable(this, probe, true);
        return;
    }
    // The plan, computed from the open model before anything is written; a cheap refusal is a red
    // banner in the dialog with Export disabled.
    StepExportDialog dialog(this);
    dialog.setProbe(probe);
    dialog.setDocument(doc_);
    if (dialog.exec() != QDialog::Accepted || dialog.refused())
        return;
    // The system Save dialog, as File > Export IFC... does (no overwrite prompt): a name that exists
    // is refused below, with nothing built and nothing written.
    QFileDialog save(this, "New STEP file");
    save.setObjectName("stepExportFileDialog");
    save.setAcceptMode(QFileDialog::AcceptSave);
    save.setFileMode(QFileDialog::AnyFile);
    save.setOption(QFileDialog::DontConfirmOverwrite);
    save.setNameFilter("STEP models (*.step *.stp);;All files (*.*)");
    save.setDefaultSuffix("step");
    if (save.exec() != QDialog::Accepted || save.selectedFiles().isEmpty())
        return;
    const QString path = save.selectedFiles().first();
    const QFileInfo info(path);
    if (info.exists() || info.isSymLink()) {
        showStepExportRefusal(this, path, "destinationExists",
                              "That file already exists. An existing file is never replaced; choose a new name.", QJsonObject());
        return;
    }
    // The export runs on a worker thread over a copy of the document (the IFC export's 16.4): the UI is
    // never blocked, and the document is read, never changed, so no unsaved-changes question is asked.
    IfcWorkerOptions workerOptions;
    workerOptions.workerPath = probe.workerPath;
    auto *runner = new StepExportRunner;
    runner->begin(doc_, path, dialog.options(), workerOptions);
    auto *sheet = new StepExportProgressSheet(runner, QFileInfo(path).fileName(), this);
    sheet->setAttribute(Qt::WA_DeleteOnClose);
    sheet->exec();
    // The sheet closes only after the runner has finished; wait anyway, so the result is never read from
    // a running export.
    runner->wait();
    if (runner->wasCancelled()) {
        status_->setText("The export was cancelled; nothing was written.");
        delete runner;
        return;
    }
    if (runner->hasResult()) {
        const qint64 elapsed = runner->clock.elapsed();
        const QJsonObject report = runner->report();
        status_->setText("Exported a new STEP file · Model unchanged");
        delete runner;
        ::sketchy::showStepExportReport(this, path, report, elapsed);
        return;
    }
    // A refusal or a failure: the model is unchanged and nothing was written.
    showStepExportRefusal(this, path, runner->code(), runner->message(), runner->report());
    delete runner;
}
} // namespace sketchy
