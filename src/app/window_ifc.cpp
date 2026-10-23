// R097.e1: File › Import IFC… / Show IFC import report… / Export IFC… (ADR 0171 §16). The helper
// (sketchyup-ifc-worker) does the heavy work; the import runs on a worker thread so the UI is
// never blocked (16.4). A missing helper is "not installed"; a missing kernel is "limited"
// (16.7). Export is R097.e2, so Export IFC… opens the availability sheet only.
#include "app/window.hpp"
#include "app/ifc_import_dialog.hpp"
#include "app/ifc_import_runner.hpp"
#include "app/ifc_progress_sheet.hpp"
#include "app/ifc_sheets.hpp"
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
void setChip(QLabel *chip, const IfcProbe &probe) {
    if (!chip)
        return;
    QString text;
    if (probe.state == IfcState::Available)
        text = "IFC ready";
    else if (probe.state == IfcState::Limited)
        text = "IFC limited";
    chip->setText(text);
    chip->setVisible(!text.isEmpty());
}
} // namespace
void Window::setIfcWorkerPath(const QString &path) { ifcWorkerPath_ = path; }
QString Window::ifcWorkerPath() { return ifcWorkerPath_; }
IfcProbe Window::ifcProbe() { return ifcProbe_; }
void Window::syncIfcAvailability() {
    if (!ifcStatusChip_) {
        ifcStatusChip_ = new QLabel(this);
        ifcStatusChip_->setObjectName("ifcStatusChip");
        statusBar()->addPermanentWidget(ifcStatusChip_);
    }
    const QString worker = resolveIfcWorker(ifcWorkerPath_);
    if (worker.isEmpty()) {
        ifcProbe_ = IfcProbe(); // Missing.
        setChip(ifcStatusChip_, ifcProbe_);
        return;
    }
    // The probe starts the helper; run it off the UI thread so startup never blocks.
    QPointer<Window> guard(this);
    std::thread([guard, worker] {
        const IfcProbe probe = runIfcProbe(worker);
        QMetaObject::invokeMethod(guard.data(), [guard, probe] {
            if (!guard)
                return;
            guard->ifcProbe_ = probe;
            setChip(guard->ifcStatusChip_, probe);
        }, Qt::QueuedConnection);
    }).detach();
}
void Window::importIfc() {
    const QString worker = resolveIfcWorker(ifcWorkerPath_);
    if (worker.isEmpty()) {
        IfcProbe probe; // Missing.
        showIfcUnavailable(this, probe, false);
        return;
    }
    // User-initiated: run the probe now (it has its own deadline).
    const IfcProbe probe = runIfcProbe(worker);
    ifcProbe_ = probe;
    setChip(ifcStatusChip_, probe);
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
    workerOptions.workerPath = worker;
    // The confirm phase: before the helper starts, ask about unsaved changes (16.3).
    if (!canReplace())
        return;
    auto *runner = new IfcImportRunner;
    runner->begin(path, import, workerOptions);
    auto *sheet = new IfcProgressSheet(runner, QFileInfo(path).fileName(), this);
    sheet->setAttribute(Qt::WA_DeleteOnClose);
    sheet->exec();
    // The import is done (or cancelled). Take the result.
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
        const int entities = report["accounting"].toObject()["entities"].toInt(0);
        if (ifcStatusChip_)
            ifcStatusChip_->setText(QString("IFC ready · %1 entities").arg(entities));
        runner->wait();
        delete runner;
        showIfcImportReport(this, path, report, elapsed, restarts);
        return;
    }
    // A refusal or a failure after the restarts.
    showIfcRefusal(this, path, runner->code(), runner->message(), runner->report());
    runner->wait();
    delete runner;
}
void Window::exportIfc() {
    const QString worker = resolveIfcWorker(ifcWorkerPath_);
    if (worker.isEmpty()) {
        IfcProbe probe; // Missing.
        showIfcUnavailable(this, probe, true);
        return;
    }
    // Export is R097.e2; this release only offers the availability sheet.
    ReportSheet sheet("ifcExportPending", "IFC export", this);
    sheet.resize(600, 320);
    sheet.summary->setObjectName("ifcExportPendingSummary");
    sheet.summary->setWordWrap(true);
    sheet.summary->setTextFormat(Qt::PlainText);
    sheet.summary->setText(
        "IFC export is not available in this build yet.\n\n"
        "The helper is present, but the export screen arrives in a later release "
        "(roadmap R097.e2). Import IFC… is available now.");
    sheet.body->addWidget(sheet.summary);
    auto *ok = new QPushButton("OK");
    ok->setObjectName("ifcExportPendingOK");
    QObject::connect(ok, &QPushButton::clicked, &sheet, &QDialog::accept);
    auto *footerLayout = qobject_cast<QHBoxLayout *>(sheet.footer->layout());
    footerLayout->addStretch();
    footerLayout->addWidget(ok);
    sheet.buttons->hide();
    sheet.exec();
}
void Window::showIfcImportReport() {
    if (ifcImportReport_.isEmpty())
        return;
    showIfcImportReport(this, ifcImportFileName_, ifcImportReport_, ifcImportElapsedMs_,
                          ifcImportRestarts_);
}
} // namespace sketchy
