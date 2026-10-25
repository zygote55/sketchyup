#pragma once
// The shared exchange runner (the screens refactor of R097.e1, R098.a3 and R097.e2): one worker
// thread runs one job, with the atomic cancel flag, the queued phase signals, runFinished, the
// result, the error code, the message and the report, wasCancelled and the elapsed clock. The
// IFC import, the STEP import and the IFC export were three copies of this; the next format
// (R102.c, the .skp import) needs only a job and the strings of its screens.
//
// The job runs on the thread and returns its result; a refusal or a failure is an
// IfcImportError or an IfcExportError (their code, words and the report so far), a cancel is
// the code "cancelled", and anything else is an error with the job's fallback message. The
// progress sheet and the window read the result back after the thread has finished.
#include "io/document_io.hpp"
#include <QElapsedTimer>
#include <QThread>
#include <QJsonObject>
#include <functional>
#include <optional>
#include <QString>
#include <string>
namespace sketchy {
// One exchange's result: done (the import's document is set, or the export's file is written)
// or an error (the code, the message, the report so far); a cancel is a code "cancelled" run.
struct ExchangeResult {
    bool done = false;
    bool cancelled = false; // the user's Cancel ended the run (the code is "cancelled")
    std::string code; // "" on success
    QString message;
    QJsonObject report;
    std::optional<Document> document; // the import's new model; unset on an export
    qint64 bytes = 0; // the export's written file size; 0 on an import
};
// The worker's hooks, as the workers' options carry them: the cancel flag (checked between
// tasks) and the phase channel (the phases and counts, live while the work runs).
struct ExchangeWorkerHooks {
    std::function<bool()> cancelled;
    std::function<void(const QString &phase, qint64 value)> progress;
};
class ExchangeRunner : public QThread {
    Q_OBJECT
  public:
    // The job runs on the thread and returns its result. `fallbackMessage` is the last-resort
    // message when the job throws something that is not an IfcImportError or an
    // IfcExportError ("The IFC import failed." ...).
    using Job = std::function<ExchangeResult(const ExchangeWorkerHooks &)>;
    ExchangeRunner();
    // Cancels a running job and waits for it (as the copies' destructors did).
    ~ExchangeRunner() override;
    // Resets the result and the cancel flag, starts the clock and starts the thread: the job
    // runs there, and the UI reads it back through the queued signals.
    void begin(const Job &job, const QString &fallbackMessage);
    // Set by the progress sheet's Cancel; the job's worker checks it without the mutex.
    void requestCancel();
    bool wasCancelled() const;
    bool hasResult() const;
    std::string code() const; // "" on success
    QString message() const;
    const QJsonObject &report() const;
    std::optional<Document> &document(); // non-const, so the window can move the document out
    qint64 bytes() const; // the written file's size, when the export succeeded
    QElapsedTimer clock; // started in begin(); for the progress sheet and the banner
  signals:
    void phaseChanged(const QString &phase, qint64 value);
    void runFinished();
  private:
    void run() override;
    struct Private;
    Private *p;
};
} // namespace sketchy
