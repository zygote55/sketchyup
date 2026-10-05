#pragma once
#include "automation/assistant.hpp"
#include <QNetworkAccessManager>
#include <QObject>
#include <QUrl>
namespace sketchy {
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
