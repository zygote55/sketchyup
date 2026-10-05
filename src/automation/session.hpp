#pragma once
#include "automation/inspection_session.hpp"
#include "automation/transactions.hpp"
#include <QLockFile>
namespace sketchy {
inline constexpr int sessionWireBytes = 66 * 1024;
inline constexpr int sessionResponseBytes = 1024 * 1024;
QJsonObject sessionCapabilities();
// Paths are supplied by the trusted process launcher, never by tool requests.
class AutomationSession {
  public:
    struct Options {
        QString input, output, outcomes;
        bool create{}, recoverLatest{};
    };
    explicit AutomationSession(Options options);
    ~AutomationSession();
    QJsonObject execute(const QJsonObject &request);
    QJsonObject respond(const QJsonObject &envelope);
    void close();

  private:
    std::unique_ptr<TransactionCoordinator> actor_;
    std::unique_ptr<TransactionDispatcher> transactions_;
    InspectionSession inspection_;
    std::unique_ptr<QLockFile> outputLock_;
    QString output_;
    std::optional<QByteArray> outputHash_;
    bool recovered_{}, saveUnknown_{}, closed_{}, active_{};
    QJsonObject describe() const;
    QJsonObject save(const QJsonObject &request);
};
QJsonObject automationFailure(const std::exception &error);
void checkAutomationDepth(const QByteArray &bytes);
void writeAutomationResponse(QIODevice &output, const QJsonObject &response);
// One bounded JSON object per line. EOF closes staging; request errors yield a
// nonzero final exit status while allowing subsequent reconciliation requests.
int runAutomationStream(AutomationSession &session, QIODevice &input, QIODevice &output);
} // namespace sketchy
