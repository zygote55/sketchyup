#pragma once
// R097.e2: running the IFC export without blocking the UI (ADR 0171 section 16.8). The whole export
// (the capture, the helper, the checks, the publication) runs on one worker thread; the phases come
// back as queued signals, and Cancel is checked between phases. The result is read back after the
// thread finishes. The open document is read, never changed: the export does not replace the model,
// so it asks no dirty-document question.
#include "io/ifc_export.hpp"
#include <QElapsedTimer>
#include <QThread>
namespace sketchy {
class IfcExportRunner : public QThread {
    Q_OBJECT
  public:
    IfcExportRunner();
    ~IfcExportRunner() override;
    // The caller must keep the document alive until the thread has finished (the window deletes
    // the runner after wait()).
    void begin(const Document &document, const QString &path, IfcExportOptions options, IfcWorkerOptions worker);
    void requestCancel();
    bool wasCancelled() const;
    bool hasResult() const;
    std::string code() const; // "" on success
    QString message() const;
    qint64 bytes() const; // the written file's size, when it succeeded
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
