#pragma once
#include "automation/session.hpp"
#include <optional>
#include <set>
#include <thread>
namespace sketchy {
// Adapters translate these values; only the owner thread delivers completions.
struct AssistantToolCall {
    QString id, name;
    QJsonObject arguments;
};
struct AssistantReply {
    QString text;
    std::vector<AssistantToolCall> calls;
    uint64_t inputTokens{}, outputTokens{};
};
struct AssistantBackend {
    std::function<QJsonObject(const QJsonObject &)> call;
    std::function<QJsonObject()> state;
};
class AssistantTask {
  public:
    enum class Phase {
        Ready,
        AwaitingProvider,
        Backoff,
        PreviewReady,
        AwaitingClarification,
        Completed,
        Canceled,
        Failed,
        Stale,
        OutcomeUnknown
    };
    enum class ProviderFailure { RateLimited, Timeout, Unavailable, Fatal };
    struct Limits {
        int turns{16}, toolCalls{64}, retries{2}, seconds{180}, outputTokens{2048};
        uint64_t totalReportedTokens{131072};
        qsizetype responseBytes{64 * 1024}, contextBytes{256 * 1024},
            conversationBytes{4 * 1024 * 1024};
    };
    struct Options {
        QString prompt, provider, model;
        bool remote{}, remoteContextApproved{};
        // Only hosts with a structured user-answer UI advertise clarification.
        bool clarificationAvailable{};
        // Trusted host-selected inspection requests; never executable instructions.
        QJsonArray context;
        // Empty permits inspection only. Every staged command must be authorized here.
        QStringList allowedCommands;
        Limits limits;
    };
    using Clock = StagingSession::Clock;
    using Now = StagingSession::Now;
    AssistantTask(AutomationSession &session, Options options, Now now = Clock::now);
    AssistantTask(AssistantBackend backend, Options options, Now now = Clock::now);
    ~AssistantTask();
    AssistantTask(const AssistantTask &) = delete;
    AssistantTask &operator=(const AssistantTask &) = delete;
    QJsonObject disclosure() const;
    QJsonObject result() const;
    // Bounded in-memory host diagnostics; never contains provider credentials.
    QJsonArray transcript() const;
    // Empty when awaiting a completion, backing off, or terminal. No blocking waits.
    std::optional<QJsonObject> nextRequest();
    bool accept(const QString &attempt, const AssistantReply &reply);
    bool providerFailed(const QString &attempt, ProviderFailure failure, int retryAfterMs = 0);
    void cancel();
    void apply();
    void reconcile();
    void answer(const QString &clarificationId, const QString &choiceId, const QString &text = {});
    Phase phase() const;

  private:
    AssistantBackend backend_;
    Options options_;
    Now now_;
    std::thread::id owner_{std::this_thread::get_id()};
    bool active_{};
    Phase phase_{Phase::Ready};
    QString taskId_, documentId_, revision_, draft_, attempt_, modelText_;
    QJsonArray tools_, messages_;
    QJsonObject sealed_, receipt_, error_, context_, clarification_;
    QString clarificationCall_;
    std::set<QString> callIds_;
    int turns_{}, calls_{}, retries_{};
    uint64_t tokens_{};
    Clock::time_point deadline_, retryAt_;
    struct Guard;
    void owner() const;
    bool current();
    bool deadline();
    void stop(Phase phase, QJsonObject error = {});
    void retire();
    QJsonObject execute(const AssistantToolCall &call);
    QJsonObject operation(QString name) const;
    void resolve(const QJsonObject &outcome);
};
} // namespace sketchy
