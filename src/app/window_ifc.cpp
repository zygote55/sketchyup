// R097.e1: File › Import IFC… / Show IFC import report… / Export IFC… (ADR 0171 §16). The helper
// (sketchyup-ifc-worker) does the heavy work; the import runs on a worker thread so the UI is
// never blocked (16.4). A missing helper is "not installed"; a missing kernel is "limited"
// (16.7). Export is R097.e2, so Export IFC… opens the availability sheet only.
#include "app/window.hpp"
#include "app/ifc_export_dialog.hpp"
#include "app/ifc_import_dialog.hpp"
#include "app/ifc_import_runner.hpp"
#include "app/ifc_progress_sheet.hpp"
#include "app/ifc_sheets.hpp"
#include "io/ifc_export.hpp"
#include "io/ifc_worker.hpp"
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
// The report chip (the design's mockup `after`): while the imported model is open it says what
// the import produced and reopens the report; the accessible name follows the design's 16.9
// wording. A cancel or a failed import (the model was not replaced) clears it.
void setIfcReportChip(QPushButton *chip, const QJsonObject &report, bool visible) {
    if (!chip)
        return;
    if (!visible) {
        chip->hide();
        return;
    }
    // The import report has no `attention` member (only the export report does); what needs
    // attention after an import is what the helper did not convert.
    const int attention = report["accounting"].toObject()["omitted"].toInt(0);
    if (attention > 0) {
        chip->setText(QString("\u26a0 IFC import \u00b7 %1 need attention \u00b7 Show report").arg(attention));
        chip->setAccessibleName(
            QString("IFC import, %1 items need attention. Show report").arg(attention));
    } else {
        chip->setText("IFC import report");
        chip->setAccessibleName("IFC import report");
    }
    chip->show();
}
} // namespace
void Window::setIfcWorkerPath(const QString &path) { ifcWorkerPath_ = path; }
QString Window::ifcWorkerPath() { return ifcWorkerPath_; }
IfcProbe Window::ifcProbe() { return ifcProbe_; }
void Window::probeIfcNow() {
    const QString worker = resolveIfcWorker(ifcWorkerPath_);
    // No helper: the availability's state, without a probe to run.
    if (worker.isEmpty()) {
        ifcProbe_ = IfcProbe(); // Missing.
        return;
    }
    // The probe starts the helper; it has its own deadline, and it is the user's action, so it
    // runs on the UI thread (a hung helper is "cannot start" when the deadline passes, 16.7).
    ifcProbe_ = runIfcProbe(worker);
}
void Window::clearIfcReportState() {
    ifcImportReport_ = QJsonObject();
    ifcImportFileName_.clear();
    ifcImportElapsedMs_ = 0;
    ifcImportRestarts_ = 0;
    setIfcReportChip(ifcReportChip_, QJsonObject(), false);
    if (ifcReportAction_)
        ifcReportAction_->setEnabled(false);
}
void Window::importIfc() {
    // A committed import replaces the model and (re)sets the report chip below; a refused or
    // failed import leaves the model untouched, so the current import's chip, if any, stays
    // (16.1's lifetime: the chip is with the imported model).
    probeIfcNow();
    const IfcProbe &probe = ifcProbe_;
    if (probe.state == IfcState::Missing) {
        showIfcUnavailable(this, probe, false);
        return;
    }
    if (probe.state == IfcState::Broken) {
        showIfcUnavailable(this, probe, false); // "IFC cannot start"
        return;
    }
    const QString path = QFileDialog::getOpenFileName(this, "Choose an IFC file", {},
                                                    "IFC models (*.ifc *.IFC);;All files (*.*)");
    if (path.isEmpty())
        return;
    IfcImportDialog dialog(this);
    dialog.setProbe(probe);
    dialog.setFile(path);
    if (dialog.exec() != QDialog::Accepted || dialog.refused())
        return;
    const IfcImportOptions import = dialog.options();
    IfcWorkerOptions workerOptions = dialog.workerOptions();
    workerOptions.workerPath = ifcProbe_.workerPath;
    // The confirm phase: before the helper starts, ask about unsaved changes (16.3).
    if (!canReplace())
        return;
    auto *runner = new IfcImportRunner;
    runner->begin(path, import, workerOptions);
    auto *sheet = new IfcProgressSheet(runner, QFileInfo(path).fileName(), this);
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
            runner->result().report["engine"].toObject()["restarts"].toInt(0);
        const qint64 elapsed = runner->clock.elapsed();
        const QJsonObject report = runner->result().report;
        doc_ = std::move(runner->result().document);
        resetRecoveryContext();
        path_.clear(); // Save must choose a new native file, never overwrite the source.
        viewport_->cancel();
        viewport_->setSelection(0);
        viewport_->fit();
        sync();
        status_->setText("Imported an unsaved IFC model · Source file unchanged");
        ifcImportReport_ = report;
        ifcImportFileName_ = path;
        ifcImportElapsedMs_ = elapsed;
        ifcImportRestarts_ = restarts;
        if (ifcReportAction_)
            ifcReportAction_->setEnabled(true);
        setIfcReportChip(ifcReportChip_, report, true); // the design's mockup `after`
        runner->wait();
        delete runner;
        ::sketchy::showIfcImportReport(this, path, report, elapsed, restarts);
        return;
    }
    // A refusal or a failure after the restarts: the model was not replaced, so the current
    // import's report chip, if any, stays with its model.
    showIfcRefusal(this, path, runner->code(), runner->message(), runner->report());
    runner->wait();
    delete runner;
}
void Window::exportIfc() {
    probeIfcNow();
    const IfcProbe &probe = ifcProbe_;
    if (probe.state == IfcState::Missing || probe.state == IfcState::Broken) {
        // 16.7: export needs the helper (not the kernel); a missing helper is an information
        // state, with the install and "still works" lines.
        showIfcUnavailable(this, probe, true);
        return;
    }
    // 16.8: the plan, computed from the open model before anything is written, with no
    // options; a cheap refusal is a red banner in the dialog with Export disabled.
    IfcExportDialog dialog(this);
    dialog.setProbe(probe);
    dialog.setDocument(doc_);
    if (dialog.exec() != QDialog::Accepted)
        return;
    // The system Save dialog, as File > Export 2D DXF... (no overwrite prompt).
    QFileDialog save(this, "New IFC file");
    save.setObjectName("ifcExportFileDialog");
    save.setAcceptMode(QFileDialog::AcceptSave);
    save.setFileMode(QFileDialog::AnyFile);
    save.setOption(QFileDialog::DontConfirmOverwrite);
    save.setNameFilter("IFC models (*.ifc *.IFC);;All files (*.*)");
    save.setDefaultSuffix("ifc");
    if (save.exec() != QDialog::Accepted || save.selectedFiles().isEmpty())
        return;
    const QString path = save.selectedFiles().first();
    // A name that exists is a refusal (16.8): nothing is built and nothing is written.
    const QFileInfo info(path);
    if (info.exists() || info.isSymLink()) {
        showIfcExportRefusal(this, path, "destinationExists",
                             "That file already exists. An existing file is never replaced; choose a new name.", QJsonObject());
        return;
    }
    // (R097.e2 step 2 moves this to a worker thread.)
    IfcExportOptions options;
    IfcWorkerOptions worker;
    worker.workerPath = probe.workerPath;
    QJsonObject report;
    QString message;
    std::string code;
    try {
        const auto exported = exportIfcFile(doc_, path, options, worker);
        report = exported.report;
    } catch (const IfcExportError &error) {
        code = error.code();
        message = QString::fromUtf8(error.what()).left(240);
        report = error.report();
    }
    if (code.empty()) {
        status_->setText("Exported a new IFC file · Model unchanged");
        showIfcExportReport(this, path, report);
    } else {
        showIfcExportRefusal(this, path, code, message, report);
    }
}
void Window::showIfcImportReport() {
    if (ifcImportReport_.isEmpty())
        return;
    ::sketchy::showIfcImportReport(this, ifcImportFileName_, ifcImportReport_,
                                     ifcImportElapsedMs_, ifcImportRestarts_);
}
} // namespace sketchy
