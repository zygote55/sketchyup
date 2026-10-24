#include "app/ifc_import_runner.hpp"
#include <QMutex>
#include <QString>
#include <QStringList>
namespace sketchy {
struct IfcImportRunner::Private {
    QMutex mutex;
    QString path;
    IfcImportOptions import;
    IfcWorkerOptions worker;
    std::optional<IfcImport> result;
    bool done = false;
    bool cancelled = false;
    std::string code;
    QString message;
    QJsonObject report;
    // Set by the progress sheet's Cancel; the helper checks it between tasks.
    bool cancelRequested = false;
};
IfcImportRunner::IfcImportRunner() : p(new Private) {}
IfcImportRunner::~IfcImportRunner() {
    if (isRunning())
        requestCancel();
    if (isRunning())
        wait(5000);
    delete p;
}
void IfcImportRunner::begin(const QString &path, IfcImportOptions import, IfcWorkerOptions worker) {
    {
        QMutexLocker lock(&p->mutex);
        p->path = path;
        p->import = import;
        p->worker = worker;
        p->done = false;
        p->cancelled = false;
        p->cancelRequested = false;
        p->code.clear();
        p->message.clear();
        p->report = {};
    }
    clock.start();
    start(); // the import runs on this thread; the UI reads it back through the queued signals
}
void IfcImportRunner::requestCancel() {
    QMutexLocker lock(&p->mutex);
    p->cancelRequested = true;
}
bool IfcImportRunner::wasCancelled() const {
    QMutexLocker lock(&p->mutex);
    return p->cancelled;
}
bool IfcImportRunner::hasResult() const {
    QMutexLocker lock(&p->mutex);
    return p->done;
}
std::string IfcImportRunner::code() const {
    QMutexLocker lock(&p->mutex);
    return p->code;
}
QString IfcImportRunner::message() const {
    QMutexLocker lock(&p->mutex);
    return p->message;
}
const IfcImport &IfcImportRunner::result() const {
    return *p->result;
}
IfcImport &IfcImportRunner::result() { return *p->result; }
const QJsonObject &IfcImportRunner::report() const {
    return p->report;
}
void IfcImportRunner::run() {
    QString path;
    IfcImportOptions import;
    IfcWorkerOptions worker;
    {
        QMutexLocker lock(&p->mutex);
        path = p->path;
        import = p->import;
        worker = p->worker;
    }
    // Cancel: the helper is checked between tasks; the application's conversion checks it between nodes.
    worker.cancelled = [this] { return p->cancelRequested; };
    worker.progress = [this](const QString &phase, qint64 value) {
        QMetaObject::invokeMethod(this, [this, phase, value] { emit phaseChanged(phase, value); }, Qt::QueuedConnection);
    };
    try {
        const auto imported = importIfcFile(path, import, worker);
        QMutexLocker lock(&p->mutex);
        p->result = std::move(imported);
        p->done = true;
        p->report = p->result->report;
    } catch (const IfcImportError &error) {
        QMutexLocker lock(&p->mutex);
        p->code = error.code();
        p->message = QString::fromStdString(error.what());
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
        p->message = "The IFC import failed.";
    }
    QMetaObject::invokeMethod(this, [this] { emit runFinished(); }, Qt::QueuedConnection);
}
} // namespace sketchy
#include "app/ifc_import_runner.moc"
