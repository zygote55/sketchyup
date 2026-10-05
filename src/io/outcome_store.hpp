#pragma once
#include "io/document_io.hpp"
#include <QDateTime>
#include <functional>
#include <memory>
namespace sketchy {
class OutcomeStoreError : public std::runtime_error {
  public:
    OutcomeStoreError(std::string code, std::string message)
        : std::runtime_error(std::move(message)), code_(std::move(code)) {}
    const std::string &code() const { return code_; }

  private:
    std::string code_;
};
// One exclusive writer per document/epoch. Only trusted coordinator code may
// issue identities or record commits; this is not an externally callable tool.
class OutcomeStore {
  public:
    enum class Phase { BeforeWrite, AfterWrite, AfterFileSync, AfterRename, AfterDirectorySync };
    using Fault = std::function<void(Phase)>;
    using Now = std::function<QDateTime()>;
    struct Limits {
        size_t outcomes{10000};
        size_t bytes{64 * 1024 * 1024};
    };
    OutcomeStore(const QString &root, const QString &documentId);
    OutcomeStore(const QString &root, const QString &documentId, Limits limits,
                 Now now = QDateTime::currentDateTimeUtc, Fault fault = {});
    ~OutcomeStore();
    OutcomeStore(const OutcomeStore &) = delete;
    OutcomeStore &operator=(const OutcomeStore &) = delete;
    // Server-issued monotonic identities prevent expired/unknown retries from
    // being interpreted as new work. Begin itself never modifies a document.
    QJsonObject begin(const Document &base, const QString &payloadHash, int ttlSeconds = 60);
    QJsonObject lookup(const QString &requestId, const QString &payloadHash) const;
    QJsonObject abort(const QString &requestId, const QString &payloadHash, const QString &reason);
    QJsonObject commit(const QString &requestId, const QString &payloadHash, const Document &before,
                       const Document &after, const QJsonObject &result);
    // Reread and durably synchronize a complete validated checkpoint after an
    // uncertain replacement. No commands or model operations are replayed here.
    void reconcile();
    bool uncertain() const;
    QByteArray latestBefore() const;
    QByteArray latestAfter() const;
    QJsonObject latestOutcome() const;
    QString directory() const;
    // Bounded internal enumeration for the owning coordinator to abort lost staging.
    std::vector<QJsonObject> pendingRequests() const;

  private:
    struct State;
    std::unique_ptr<State> state_;
};
} // namespace sketchy
