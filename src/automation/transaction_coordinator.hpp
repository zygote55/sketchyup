#pragma once
#include "automation/staging.hpp"
#include "io/outcome_store.hpp"
#include <thread>
namespace sketchy {
// A serialized document owner. No caller receives mutable live model access.
// Persistent transports and authorization wrap this in-process coordinator.
class TransactionCoordinator {
  public:
    enum class OpenMode { RequireCurrent, RecoverLatest };
    struct Options {
        OpenMode mode{OpenMode::RequireCurrent};
        StagingSession::Limits staging;
        OutcomeStore::Limits outcomes;
        StagingSession::Now monotonic{StagingSession::Clock::now};
        OutcomeStore::Now wallClock{QDateTime::currentDateTimeUtc};
        OutcomeStore::Fault fault;
    };
    TransactionCoordinator(Document document, const QString &root);
    TransactionCoordinator(Document document, const QString &root, Options options);
    const Document &document() const;
    bool uncertain() const;
    void edit(const std::function<void(Document &)> &operation);
    QJsonObject prepare(const QJsonObject &batch, int ttlSeconds = 60);
    QJsonObject preview(const QString &requestId, const QString &payloadHash);
    QJsonObject inspect(const QString &requestId, const QString &payloadHash,
                        const QJsonObject &query);
    QJsonObject diff(const QString &requestId, const QString &payloadHash, size_t offset = 0,
                     size_t limit = 50);
    QJsonObject commit(const QString &requestId, const QString &payloadHash);
    QJsonObject cancel(const QString &requestId, const QString &payloadHash);
    QJsonObject status(const QString &requestId, const QString &payloadHash);
    QJsonObject reconcile();

  private:
    struct Binding {
        QString stageId, hash;
    };
    struct Publication {
        QString requestId, hash;
        Document::SaveStamp base;
        uint64_t revision{};
        std::unique_ptr<Document> candidate;
    };
    class Operation;
    Document document_;
    StagingSession staging_;
    std::unique_ptr<OutcomeStore> store_;
    std::map<QString, Binding> bindings_;
    std::optional<Publication> publication_;
    std::thread::id owner_{std::this_thread::get_id()};
    bool active_{};
    void owner() const;
    void ready() const;
    void release(const QString &requestId);
    Binding &binding(const QString &requestId, const QString &payloadHash);
    QJsonObject checkedStatus(const QString &requestId, const QString &payloadHash);
    void publish();
    void abortOrphans();
};
} // namespace sketchy
