#pragma once
#include "automation/assistant.hpp"
#include <QNetworkAccessManager>
#include <QObject>
#include <memory>
namespace sketchy {
// One codec belongs to one task. It retains opaque provider output only in memory.
class OpenAiConversation {
  public:
    QJsonObject request(const QJsonObject &taskRequest);
    AssistantReply decode(const QByteArray &response);
    void accepted();

  private:
    QMap<QString, QString> names_;
    std::vector<QJsonArray> outputs_;
    QJsonArray candidate_;
    qsizetype retainedBytes_{};
};
class OpenAiProvider : public QObject {
    Q_OBJECT
  public:
    // The injected manager is a trusted host dependency and must outlive this object.
    OpenAiProvider(std::unique_ptr<AssistantTask> task, QByteArray apiKey,
                   QNetworkAccessManager *manager = nullptr, QObject *parent = nullptr);
    ~OpenAiProvider() override;
    void start();
    void cancel();
    void apply();
    void reconcile();
    AssistantTask &task();
    QString status() const;
  signals:
    void changed();

  private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace sketchy
