#pragma once
// R101.e2 (UX_DESIGN §19.3, ADR 0175 §5 and §12): one report pass or one export, run off the UI
// thread. The job holds a snapshot of the document (Document::readSnapshot shares the immutable
// records), so the model stays usable while it runs and nothing the UI does later changes what is
// computed. The engine checks the cancel flag between body records (ReportHooks), so Cancel returns
// within the engine's bound. Progress and the result reach the UI through queued signals, and the
// result is read after `finished`.
#include "core/report.hpp"
#include <QJsonObject>
#include <QString>
#include <QThread>
#include <atomic>
#include <functional>

namespace sketchy {

class ReportJob : public QThread {
    Q_OBJECT
  public:
    // The work runs on the thread. It throws ReportCancelled when the cancel flag was set, and an
    // InspectionError for a refusal; anything else is an internal error.
    using Work = std::function<QJsonObject(const ReportHooks &)>;

    explicit ReportJob(Work work, QObject *parent = nullptr);
    // Cancels a running job and waits for it, so no snapshot outlives its owner.
    ~ReportJob() override;

    // Asks the engine to stop at its next record boundary. Safe from the UI thread.
    void requestCancel() { cancel_.store(true); }

    // Valid after finished() is delivered: the result, or the refusal's code and words. A cancel
    // has `cancelled()` set and the code "cancelled".
    const QJsonObject &data() const { return data_; }
    bool cancelled() const { return cancelled_; }
    bool failed() const { return !code_.isEmpty(); }
    const QString &code() const { return code_; }
    const QString &message() const { return message_; }

  signals:
    // Records visited so far and the document's record count. Throttled, so a run queues a few
    // hundred events at most.
    void progressed(qulonglong done, qulonglong total);

  protected:
    void run() override;

  private:
    Work work_;
    std::atomic<bool> cancel_{};
    QJsonObject data_;
    bool cancelled_{};
    QString code_, message_;
};

} // namespace sketchy
