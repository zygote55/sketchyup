#include "app/ifc_export_runner.hpp"
#include <QMutex>
#include <QString>
#include <atomic>
#include <optional>
namespace sketchy {
struct IfcExportRunner::Private {
    QMutex mutex;
    // A copy, not the window's document: the export reads it on the worker thread. A document is
    // a value over shared immutable bodies, so the copy is cheap and nothing the UI does later
    // can change what is being written.
    std::optional<Document> document;
    QString path;
    IfcExportOptions options;
    IfcWorkerOptions worker;
    bool done = false;
    bool cancelled = false;
    std::string code;
    QString message;
    QJsonObject report;
    qint64 bytes = 0;
    // Set by the progress sheet's Cancel; the export checks it between phases.
    std::atomic<bool> cancelRequested = false; // read by the worker thread without the mutex
};
IfcExportRunner::IfcExportRunner() : p(new Private) {}
IfcExportRunner::~IfcExportRunner() {
    if (isRunning())
        requestCancel();
    if (isRunning())
        wait(5000);
    delete p;
}
void IfcExportRunner::begin(const Document &document, const QString &path, IfcExportOptions options,
                             IfcWorkerOptions worker) {
    {
        QMutexLocker lock(&p->mutex);
        p->document = document;
        p->path = path;
        p->options = options;
        p->worker = worker;
        p->done = false;
        p->cancelled = false;
        p->cancelRequested = false;
        p->code.clear();
        p->message.clear();
        p->report = {};
        p->bytes = 0;
    }
    clock.start();
    start(); // the export runs on this thread; the UI reads it back through the queued signals
}
void IfcExportRunner::requestCancel() {
    QMutexLocker lock(&p->mutex);
    p->cancelRequested = true;
}
bool IfcExportRunner::wasCancelled() const {
    QMutexLocker lock(&p->mutex);
    return p->cancelled;
}
bool IfcExportRunner::hasResult() const {
    QMutexLocker lock(&p->mutex);
    return p->done;
}
std::string IfcExportRunner::code() const {
    QMutexLocker lock(&p->mutex);
    return p->code;
}
QString IfcExportRunner::message() const {
    QMutexLocker lock(&p->mutex);
    return p->message;
}
qint64 IfcExportRunner::bytes() const {
    QMutexLocker lock(&p->mutex);
    return p->bytes;
}
const QJsonObject &IfcExportRunner::report() const {
    return p->report;
}
void IfcExportRunner::run() {
    std::optional<Document> document;
    QString path;
    IfcExportOptions options;
    IfcWorkerOptions worker;
    {
        QMutexLocker lock(&p->mutex);
        document = p->document;
        path = p->path;
        options = p->options;
        worker = p->worker;
    }
    // Cancel: the export checks it between phases; the helper is ended with it.
    worker.cancelled = [this] { return p->cancelRequested.load(); };
    worker.progress = [this](const QString &phase, qint64 value) {
        QMetaObject::invokeMethod(this, [this, phase, value] { emit phaseChanged(phase, value); }, Qt::QueuedConnection);
    };
    try {
        const auto exported = exportIfcFile(*document, path, options, worker);
        QMutexLocker lock(&p->mutex);
        p->done = true;
        p->bytes = exported.bytes;
        p->report = exported.report;
    } catch (const IfcExportError &error) {
        QMutexLocker lock(&p->mutex);
        p->code = error.code();
        p->message = QString::fromUtf8(error.what());
        p->report = error.report();
        if (p->code == "cancelled")
            p->cancelled = true;
    } catch (const std::exception &error) {
        QMutexLocker lock(&p->mutex);
        p->code = "error";
        p->message = QString::fromUtf8(error.what());
    } catch (...) {
        QMutexLocker lock(&p->mutex);
        p->code = "error";
        p->message = "The IFC export failed.";
    }
    QMetaObject::invokeMethod(this, [this] { emit runFinished(); }, Qt::QueuedConnection);
}
} // namespace sketchy
#include "app/ifc_export_runner.moc"
