#pragma once
// R097.e1: running the IFC import without blocking the UI (ADR 0171 section 16.4). The whole import
// (the helper, then the application's own conversion) runs on one worker thread; the helper's
// phases and counts come back as queued signals, and Cancel is checked between tasks. The result is
// read back after the thread finishes.
#include "io/ifc_worker.hpp"
#include <QElapsedTimer>
#include <QThread>
#include <optional>
namespace sketchy {
class IfcImportRunner : public QThread {
    Q_OBJECT
  public:
    IfcImportRunner();
    ~IfcImportRunner() override;
    void begin(const QString &path, IfcImportOptions import, IfcWorkerOptions worker);
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
