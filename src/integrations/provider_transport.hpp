#pragma once
#include "automation/assistant.hpp"
#include <QNetworkAccessManager>
#include <QObject>
#include <QUrl>
namespace sketchy {
class ProviderUsageLimit : public std::runtime_error {
  public:
    ProviderUsageLimit()
        : std::runtime_error("ChatGPT plan usage is unavailable or its limit was reached. Use "
                             "Manage ChatGPT usage.") {}
};
// A streamed reply that ended with an error, failed or incomplete event (ADR 0034
// addendum, D-046). detail() holds only bounded, sanitized fields: event, errorType, code,
// message, param, status, incompleteDetails and statusDetails; token-like strings are
// redacted and the raw body is never kept. what() stays the fixed decode diagnostic.
class ProviderStreamError : public std::runtime_error {
  public:
    explicit ProviderStreamError(QJsonObject detail)
        : std::runtime_error("OpenAI stream did not complete"), detail_(std::move(detail)) {}
    const QJsonObject &detail() const { return detail_; }
    // "code: message", or the event name when the event carries neither.
    QString summary() const {
        const auto code = detail_.value("code").toString();
        const auto message = detail_.value("message").toString();
        if (!code.isEmpty() && !message.isEmpty())
            return code + ": " + message;
        if (!code.isEmpty() || !message.isEmpty())
            return code + message;
        return detail_.value("event").toString();
    }

  private:
    QJsonObject detail_;
};
class AssistantNetworkProvider : public QObject {
    Q_OBJECT
  public:
    struct Preflight {
        QUrl endpoint;
        std::optional<QJsonObject> body; // Absent uses GET; present uses POST.
        std::function<void(const QJsonObject &)> validate;
    };
    struct Protocol {
        QString provider;
        bool remote{};
        bool eventStream{};
        QUrl endpoint;
        QByteArray key;
        int timeoutMs{30000};
        std::vector<Preflight> preflights;
        std::function<QJsonObject(const QJsonObject &)> request;
        std::function<AssistantReply(const QByteArray &)> decode;
        std::function<void()> accepted;
    };
    ~AssistantNetworkProvider() override;
    void start();
    void cancel();
    void apply();
    void reconcile();
    void answer(const QString &clarificationId, const QString &choiceId, const QString &text = {});
    AssistantTask &task();
    QString status() const;
  signals:
    void changed();

  protected:
    // Configuration and injected manager are trusted host dependencies.
    AssistantNetworkProvider(std::unique_ptr<AssistantTask> task, Protocol protocol,
                             QNetworkAccessManager *manager, QObject *parent);

  private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace sketchy
