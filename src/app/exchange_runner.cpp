#include "app/exchange_runner.hpp"
#include "io/ifc_export.hpp"
#include "io/ifc_import.hpp"
#include <QMutex>
#include <QString>
#include <atomic>
namespace sketchy {
struct ExchangeRunner::Private {
    QMutex mutex;
    Job job;
    QString fallbackMessage;
    ExchangeResult result;
    // Set by the progress sheet's Cancel; the job's worker checks it between tasks.
    std::atomic<bool> cancelRequested = false; // read by the worker thread without the mutex
};
ExchangeRunner::ExchangeRunner() : p(new Private) {}
ExchangeRunner::~ExchangeRunner() {
    if (isRunning())
        requestCancel();
    if (isRunning())
        wait(5000);
    delete p;
}
void ExchangeRunner::begin(const Job &job, const QString &fallbackMessage) {
    {
        QMutexLocker lock(&p->mutex);
        p->job = job;
        p->fallbackMessage = fallbackMessage;
        p->result = {};
    }
    clock.start();
    start(); // the job runs on this thread; the UI reads it back through the queued signals
}
void ExchangeRunner::requestCancel() {
    QMutexLocker lock(&p->mutex);
    p->cancelRequested = true;
}
bool ExchangeRunner::wasCancelled() const {
    QMutexLocker lock(&p->mutex);
    return p->result.cancelled;
}
bool ExchangeRunner::hasResult() const {
    QMutexLocker lock(&p->mutex);
    return p->result.done;
}
std::string ExchangeRunner::code() const {
    QMutexLocker lock(&p->mutex);
    return p->result.code;
}
QString ExchangeRunner::message() const {
    QMutexLocker lock(&p->mutex);
    return p->result.message;
}
const QJsonObject &ExchangeRunner::report() const {
    return p->result.report;
}
std::optional<Document> &ExchangeRunner::document() {
    return p->result.document;
}
qint64 ExchangeRunner::bytes() const {
    QMutexLocker lock(&p->mutex);
    return p->result.bytes;
}
void ExchangeRunner::run() {
    Job job;
    QString fallbackMessage;
    {
        QMutexLocker lock(&p->mutex);
        job = p->job;
        fallbackMessage = p->fallbackMessage;
    }
    ExchangeWorkerHooks hooks;
    // Cancel: the job's worker is checked between tasks; the conversion, between nodes.
    hooks.cancelled = [this] { return p->cancelRequested.load(); };
    // The signals go straight to the UI (the queued connection delivers them there): the
    // progress is live while the job works, not a batch at the thread's end.
    hooks.progress = [this](const QString &phase, qint64 value) {
        QMetaObject::invokeMethod(this, [this, phase, value] { emit phaseChanged(phase, value); },
                                  Qt::QueuedConnection);
    };
    try {
        const ExchangeResult result = job(hooks);
        QMutexLocker lock(&p->mutex);
        p->result = result;
    } catch (const IfcImportError &error) {
        QMutexLocker lock(&p->mutex);
        p->result.code = error.code();
        p->result.message = QString::fromStdString(error.what());
        p->result.report = error.report();
        if (p->result.code == "cancelled")
            p->result.cancelled = true;
    } catch (const IfcExportError &error) {
        QMutexLocker lock(&p->mutex);
        p->result.code = error.code();
        p->result.message = QString::fromUtf8(error.what());
        p->result.report = error.report();
        if (p->result.code == "cancelled")
            p->result.cancelled = true;
    } catch (const std::exception &error) {
        QMutexLocker lock(&p->mutex);
        p->result.code = "error";
        p->result.message = QString::fromUtf8(error.what());
    } catch (...) {
        QMutexLocker lock(&p->mutex);
        p->result.code = "error";
        p->result.message = fallbackMessage;
    }
    QMetaObject::invokeMethod(this, [this] { emit runFinished(); }, Qt::QueuedConnection);
}
} // namespace sketchy
#include "app/exchange_runner.moc"
