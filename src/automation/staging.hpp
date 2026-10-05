#pragma once
#include "automation/inspection.hpp"
#include <chrono>
#include <functional>
#include <memory>
namespace sketchy {
// In-process preparation only. Durable commit/outcome dispatch owns publication.
// Caller serializes access with document operations.
class StagingSession {
  public:
    using Clock = std::chrono::steady_clock;
    using Now = std::function<Clock::time_point()>;
    struct Limits {
        size_t proposals{4};
        size_t retainedBytes{128 * 1024 * 1024};
    };
    StagingSession();
    explicit StagingSession(Limits limits, Now now = Clock::now);
    QJsonObject prepare(const Document &live, const QJsonObject &batch, int ttlSeconds = 60);
    QJsonObject describe(const Document &live, const QString &stageId);
    QJsonObject changes(const Document &live, const QString &stageId, size_t offset = 0,
                        size_t limit = 50);
    QJsonObject inspect(const Document &live, const QString &stageId, const QJsonObject &request);
    // Pin the immutable proposal for the trusted durable coordinator. This is not
    // a commit, authorization decision, idempotency record or terminal outcome.
    std::shared_ptr<const Document::PreparedEdit> proposal(const Document &live,
                                                           const QString &stageId);
    void release(const QString &stageId);
    void clear();
    size_t retainedCount() const { return stages_.size(); }
    size_t retainedBytes() const { return bytes_; }

  private:
    struct ChangeRow {
        QString kind, action;
        Id id{};
    };
    struct Stage {
        std::shared_ptr<const Document::PreparedEdit> prepared;
        QJsonObject result;
        std::vector<ChangeRow> changes;
        Clock::time_point expires;
        size_t bytes{};
    };
    Limits limits_;
    Now now_;
    size_t bytes_{};
    std::map<QString, Stage> stages_;
    void prune();
    Stage &get(const Document &live, const QString &stageId);
    QJsonObject envelope(const Document &live, const QString &stageId, const Stage &stage) const;
};
} // namespace sketchy
