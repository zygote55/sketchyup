#pragma once
// R098.a3: running the STEP import without blocking the UI (ADR 0172 section 8, the IFC import's 16.4). The whole
// import (the helper's --import-step, then the application's own conversion) runs on one worker thread; the
// helper's phases and counts come back as queued signals, and Cancel is checked between tasks. The result is
// read back after the thread finishes.
#include "io/step_worker.hpp"
#include <QElapsedTimer>
#include <QThread>
#include <optional>
namespace sketchy {
class StepImportRunner : public QThread {
    Q_OBJECT
  public:
    StepImportRunner();
    ~StepImportRunner() override;
    void begin(const QString &path, StepWorkerOptions worker);
    void requestCancel();
    bool wasCancelled() const;
    bool hasResult() const;
    std::string code() const; // "" on success
    QString message() const;
    const IfcImport &result() const; // valid only when hasResult()
    IfcImport &result(); // non-const, so the window can move the document out
    const QJsonObject &report() const;
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
