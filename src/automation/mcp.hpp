#pragma once
#include "automation/session.hpp"
#include <thread>
namespace sketchy {
inline constexpr const char *mcpProtocolVersion = "2026-07-28";
QJsonArray mcpToolCatalog();
QJsonObject mcpCapabilities();
struct McpBackend {
    std::function<QJsonObject(const QJsonObject &)> call;
    std::function<QJsonObject()> state;
    std::function<void()> close;
    QJsonArray tools;
};
class McpServer {
  public:
    explicit McpServer(AutomationSession &session);
    explicit McpServer(McpBackend backend);
    ~McpServer();
    QJsonArray handle(const QJsonObject &request);
    QJsonArray close();
    static QJsonObject error(int code, QString message, QJsonValue id = QJsonValue::Undefined,
                             QJsonObject data = {});

  private:
    McpBackend backend_;
    QString uri_;
    QJsonObject state_;
    struct Subscription {
        QJsonValue id;
        bool resource{};
    };
    std::map<QString, Subscription> subscriptions_;
    std::thread::id owner_{std::this_thread::get_id()};
    bool closed_{}, active_{};
    double tokens_{120};
    StagingSession::Clock::time_point refilled_{StagingSession::Clock::now()};
    QJsonArray dispatch(const QJsonObject &request);
    QJsonObject call(const QJsonObject &params);
    void updates(QJsonArray &messages);
    void owner() const;
};
int runMcpStream(McpServer &server, QIODevice &input, QIODevice &output);
} // namespace sketchy
