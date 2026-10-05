#pragma once
#include "automation/staging.hpp"
#include "io/outcome_store.hpp"
#include <thread>
namespace sketchy {
// A serialized document coordinator. The default mode owns its document.
// A trusted native host may bind its existing document on the same owner thread.
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
    struct BorrowedDocument {
        Document &document;
    };
    TransactionCoordinator(Document document, const QString &root);
    TransactionCoordinator(Document document, const QString &root, Options options);
    // The host keeps the document alive, serializes edits and prevents mutation
    // while uncertain(). Replacing its document session invalidates this binding.
    TransactionCoordinator(BorrowedDocument document, const QString &root);
    TransactionCoordinator(BorrowedDocument document, const QString &root, Options options);
    TransactionCoordinator(const TransactionCoordinator &) = delete;
    TransactionCoordinator &operator=(const TransactionCoordinator &) = delete;
    TransactionCoordinator(TransactionCoordinator &&) = delete;
    TransactionCoordinator &operator=(TransactionCoordinator &&) = delete;
    const Document &document() const;
    bool uncertain() const;
    size_t retainedStagingBytes() const;
    void edit(const std::function<void(Document &)> &operation);
    QJsonObject prepare(const QJsonObject &batch, int ttlSeconds = 60);
    QJsonObject preview(const QString &requestId, const QString &payloadHash);
    // Trusted native rendering only; a pinned immutable edit cannot publish itself.
    std::shared_ptr<const Document::PreparedEdit> previewEdit(const QString &requestId,
                                                              const QString &payloadHash);
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
    std::optional<Document> ownedDocument_;
    Document &document_;
    Document::SaveStamp scope_;
    StagingSession staging_;
    std::unique_ptr<OutcomeStore> store_;
    std::map<QString, Binding> bindings_;
    std::optional<Publication> publication_;
    std::thread::id owner_{std::this_thread::get_id()};
    bool active_{};
    void initialize(const QString &root, Options options);
    void owner() const;
    void ready() const;
    void release(const QString &requestId);
    Binding &binding(const QString &requestId, const QString &payloadHash);
    QJsonObject checkedStatus(const QString &requestId, const QString &payloadHash);
    void publish();
    void abortOrphans();
};
} // namespace sketchy
