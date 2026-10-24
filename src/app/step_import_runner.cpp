#include "app/step_import_runner.hpp"
#include <QMutex>
#include <QString>
#include <QStringList>
#include <atomic>
namespace sketchy {
struct StepImportRunner::Private {
    QMutex mutex;
    QString path;
    StepWorkerOptions worker;
    std::optional<IfcImport> result;
    bool done = false;
    bool cancelled = false;
    std::string code;
    QString message;
    QJsonObject report;
    // Set by the progress sheet's Cancel; the helper checks it between tasks.
    std::atomic<bool> cancelRequested = false; // read by the worker thread without the mutex
};
StepImportRunner::StepImportRunner() : p(new Private) {}
StepImportRunner::~StepImportRunner() {
    if (isRunning())
        requestCancel();
    if (isRunning())
        wait(5000);
    delete p;
}
void StepImportRunner::begin(const QString &path, StepWorkerOptions worker) {
    {
        QMutexLocker lock(&p->mutex);
        p->path = path;
        p->worker = std::move(worker);
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
void StepImportRunner::requestCancel() {
    QMutexLocker lock(&p->mutex);
    p->cancelRequested = true;
}
bool StepImportRunner::wasCancelled() const {
    QMutexLocker lock(&p->mutex);
    return p->cancelled;
}
bool StepImportRunner::hasResult() const {
    QMutexLocker lock(&p->mutex);
    return p->done;
}
std::string StepImportRunner::code() const {
    QMutexLocker lock(&p->mutex);
    return p->code;
}
QString StepImportRunner::message() const {
    QMutexLocker lock(&p->mutex);
    return p->message;
}
const IfcImport &StepImportRunner::result() const {
    return *p->result;
}
IfcImport &StepImportRunner::result() { return *p->result; }
const QJsonObject &StepImportRunner::report() const {
    return p->report;
}
void StepImportRunner::run() {
    QString path;
    StepWorkerOptions worker;
    {
        QMutexLocker lock(&p->mutex);
        path = p->path;
        worker = p->worker;
    }
    // Cancel: the helper is checked between tasks; the application's conversion checks it between nodes.
    worker.cancelled = [this] { return p->cancelRequested.load(); };
    worker.progress = [this](const QString &phase, qint64 value) {
        QMetaObject::invokeMethod(this, [this, phase, value] { emit phaseChanged(phase, value); }, Qt::QueuedConnection);
    };
    try {
        auto imported = importStepFile(path, worker);
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
        p->message = "The STEP import failed.";
    }
    QMetaObject::invokeMethod(this, [this] { emit runFinished(); }, Qt::QueuedConnection);
}
} // namespace sketchy
#include "app/step_import_runner.moc"
