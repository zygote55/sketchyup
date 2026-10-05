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
