#pragma once
#include "integrations/provider_transport.hpp"
namespace sketchy {
struct OllamaConfiguration {
    QUrl endpoint{"http://127.0.0.1:11434"};
    QString model{"qwen3:4b-instruct"};
    int contextTokens{32768}, threads{8};
};
void validateOllamaConfiguration(const OllamaConfiguration &configuration);
class OllamaConversation {
  public:
    explicit OllamaConversation(OllamaConfiguration configuration);
    void capabilities(const QJsonObject &response);
    QJsonObject request(const QJsonObject &taskRequest);
    AssistantReply decode(const QByteArray &response);
    void accepted();

  private:
    OllamaConfiguration configuration_;
    bool ready_{}, thinkingOption_{};
    QString attempt_;
    QMap<QString, QString> names_, callNames_, candidateNames_;
    std::vector<QJsonObject> outputs_;
    QJsonObject candidate_;
    qsizetype retainedBytes_{};
};
class OllamaProvider : public AssistantNetworkProvider {
  public:
    OllamaProvider(std::unique_ptr<AssistantTask> task, OllamaConfiguration configuration,
                   QNetworkAccessManager *manager = nullptr, QObject *parent = nullptr);
};
} // namespace sketchy
