#pragma once
#include "integrations/provider_transport.hpp"
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
class OpenAiProvider : public AssistantNetworkProvider {
  public:
    OpenAiProvider(std::unique_ptr<AssistantTask> task, QByteArray apiKey,
                   QNetworkAccessManager *manager = nullptr, QObject *parent = nullptr);
};
} // namespace sketchy
