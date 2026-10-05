#pragma once
#include "integrations/provider_transport.hpp"
#include <QNetworkAccessManager>
#include <QObject>
#include <memory>
namespace sketchy {
// One codec belongs to one task. It retains opaque provider output only in memory.
class OpenAiConversation {
  public:
    explicit OpenAiConversation(bool chatGptPlan = false) : chatGptPlan_(chatGptPlan) {}
    QJsonObject request(const QJsonObject &taskRequest);
    AssistantReply decode(const QByteArray &response);
    void accepted();

  private:
    bool chatGptPlan_{};
    QMap<QString, QString> names_;
    std::vector<QJsonArray> outputs_;
    QJsonArray candidate_;
    qsizetype retainedBytes_{};
};
// Decode only a terminal completed SSE response; partial tools never execute.
QByteArray completedOpenAiStream(const QByteArray &stream);
class OpenAiProvider : public AssistantNetworkProvider {
  public:
    OpenAiProvider(std::unique_ptr<AssistantTask> task, QByteArray apiKey,
                   QNetworkAccessManager *manager = nullptr, QObject *parent = nullptr,
                   bool chatGptPlan = false);
};
} // namespace sketchy
