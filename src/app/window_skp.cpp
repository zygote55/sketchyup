// R102.c (ADR 0176 section 9): File › Import .skp file… / Show .skp import report…, the STEP
// import's own screens with the SketchUp helper (sketchyup-skp-worker). The parser runs only in that
// helper (ADR 0176 decision 2); the import runs on a worker thread, so the UI is never blocked. A
// missing helper is the availability sheet. The import has no options (decision 5), so the file picker
// leads straight to the progress sheet, and the commit asks about unsaved changes again (16.3).
#include "app/window.hpp"
#include "app/import_report.hpp"
#include "app/ifc_sheets.hpp"
#include "app/skp_import_runner.hpp"
#include "app/skp_progress_sheet.hpp"
#include "io/skp_worker.hpp"

#include <QFileDialog>
#include <QFileInfo>
#include <QLabel>
#include <QPushButton>
#include <QStatusBar>

namespace sketchy {

void Window::setSkpWorkerPath(const QString &path) { skpWorkerPath_ = path; }
QString Window::skpWorkerPath() { return skpWorkerPath_; }
void Window::setSkpDeadlineSeconds(int seconds) { skpDeadlineSeconds_ = seconds; }

void Window::clearSkpReportState() { clearImportReportState(skpImportReport_, skpReportAction_, skpReportChip_); }

void Window::importSkp() {
    // A committed import replaces the model and (re)sets the report chip below; a refused or failed
    // import leaves the model untouched, so the current import's chip, if any, stays.
    const QString worker = findSkpWorker(skpWorkerPath_);
    if (worker.isEmpty()) {
        showSkpUnavailable(this);
        return;
    }
    const QString path = QFileDialog::getOpenFileName(this, "Choose a SketchUp file", {},
                                                      ".skp files (*.skp *.SKP);;All files (*.*)");
    if (path.isEmpty())
        return;
    // R105.a: a downloaded file of a refused type (a ZIP archive, COLLADA, USDZ or FBX) is named
    // before the helper.
    if (refuseUnsupportedImport(path))
        return;
    // The confirm phase: before the helper starts, ask about unsaved changes (16.3).
    if (!canReplace())
        return;
    SkpWorkerOptions workerOptions;
    workerOptions.workerPath = worker;
    workerOptions.deadlineSeconds = skpDeadlineSeconds_;
    auto *runner = new SkpImportRunner;
    runner->begin(path, workerOptions);
    auto *sheet = new SkpProgressSheet(runner, QFileInfo(path).fileName(), this);
    sheet->setAttribute(Qt::WA_DeleteOnClose);
    sheet->exec();
    // The import is done (or cancelled). The sheet closes only after the runner has finished; wait
    // anyway, so the result is never read from a running import.
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
        const qint64 elapsed = runner->clock.elapsed();
        const QJsonObject report = runner->report();
        doc_ = std::move(*runner->document()); // the import's document, moved out of the runner
        resetRecoveryContext();
        path_.clear(); // Save must choose a new native file, never overwrite the source.
        viewport_->cancel();
        viewport_->setSelection(0);
        viewport_->fit();
        sync();
        status_->setText("Imported an unsaved SketchUp model · Source file unchanged");
        // The helper has no restart protocol (ADR 0176 section 1): the report carries no restarts.
        skpImportReport_ = ImportReportState{true, "", path, report, elapsed, 0};
        if (skpReportAction_)
            skpReportAction_->setEnabled(true);
        setImportReportChip(skpReportChip_, skpImportReportFace(), skpImportReport_);
        runner->wait();
        delete runner;
        ::sketchy::showImportReport(this, skpImportReportFace(), skpImportReport_);
        return;
    }
    // A refusal or a failure: the model was not replaced, so the current import's report chip, if any,
    // stays with its model. A helper that is gone by now is the availability sheet, as at the start.
    if (runner->code() == "workerMissing")
        showSkpUnavailable(this);
    else
        showSkpRefusal(this, path, runner->code(), runner->message(), runner->report());
    runner->wait();
    delete runner;
}

void Window::showSkpImportReport() {
    if (skpImportReport_.done)
        ::sketchy::showImportReport(this, skpImportReportFace(), skpImportReport_);
}

} // namespace sketchy
