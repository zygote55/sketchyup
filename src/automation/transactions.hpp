#pragma once
#include "automation/transaction_coordinator.hpp"
namespace sketchy {
inline constexpr int transactionRequestBytes = 64 * 1024;
inline constexpr int transactionResponseBytes = 272 * 1024;
QJsonArray transactionCatalog();
QJsonObject transactionCapabilities();
class TransactionDispatcher {
  public:
    struct Limits {
        size_t drafts{4};
        size_t retainedBytes{128 * 1024 * 1024};
    };
    explicit TransactionDispatcher(TransactionCoordinator &actor);
    TransactionDispatcher(TransactionCoordinator &actor, Limits limits,
                          StagingSession::Now now = StagingSession::Clock::now);
    QJsonObject execute(const QJsonObject &request);
    void clear();

  private:
    struct Applied {
        QString hash;
        QJsonObject reply;
    };
    struct Draft {
        Document::SaveStamp base;
        uint64_t revision{};
        int version{};
        StagingSession::Clock::time_point expires;
        QJsonArray commands;
        QJsonObject history, sealed;
        std::map<QString, Applied> applied;
        std::unique_ptr<StagingSession> stage;
        QString stageId;
    };
    TransactionCoordinator &actor_;
    Limits limits_;
    StagingSession::Now now_;
    std::map<QString, Draft> drafts_;
    bool active_{};
    size_t charge(const Draft &draft) const;
    size_t retainedBytes() const;
    void retire(const QString &id);
    void prune(const QString &skip = {});
    Draft &get(const QString &id);
    int remainingSeconds(const Draft &draft) const;
    QJsonObject describe(const QString &id, const Draft &draft) const;
    QJsonObject run(const QJsonObject &request);
};
} // namespace sketchy
