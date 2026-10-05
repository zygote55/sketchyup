#include "app/native_mcp.hpp"
#include "app/inspection_service.hpp"
#include "app/viewport.hpp"
#include "automation/inspection_validation.hpp"
#include "automation/local_mcp.hpp"
#include "automation/mcp.hpp"
#include <QJsonDocument>
#include <QLocalServer>
#include <QLocalSocket>
#include <QPointer>
#include <QThread>
#include <QTimer>
#include <algorithm>
namespace sketchy {
namespace {
QJsonArray nativeTools() {
    QJsonArray tools;
    for (const auto &value : mcpToolCatalog())
        if (value.toObject()["name"] == "session.describe")
            tools.append(value);
    for (const auto &value : desktopInspectionCapabilities()["queries"].toArray()) {
        const auto entry = value.toObject();
        if (entry["name"] == "view.capture")
            continue;
        tools.append(
            QJsonObject{{"name", entry["name"]},
                        {"description",
                         entry["name"].toString() + ": inspect the explicitly bound native window"},
                        {"inputSchema", entry["parameters"]},
                        {"annotations", QJsonObject{{"readOnlyHint", true},
                                                    {"destructiveHint", false},
                                                    {"idempotentHint", false},
                                                    {"openWorldHint", false}}}});
    }
    return tools;
}
} // namespace
QJsonObject nativeMcpCapabilities() {
    auto result = mcpCapabilities();
    result["transport"] = "stdio bridge to an explicitly launched private Linux filesystem socket";
    result["tools"] = nativeTools();
    result["selection"] = "Actual native window selection, context and camera";
    result["scope"] =
        "One explicitly opened native document session; replacement closes the endpoint";
    result["mutationAvailable"] = false;
    result["viewCaptureAvailable"] = false;
    auto limits = result["limits"].toObject();
    limits["clients"] = 2;
    limits["queuedOutputBytesPerClient"] = 2 * sessionResponseBytes;
    limits["retainedSnapshotBytesPerClient"] = 32 * 1024 * 1024;
    result["limits"] = limits;
    return result;
}
struct NativeMcpService::Impl : QObject {
    Viewport &view;
    Document::SaveStamp scope;
    QLocalServer listener;
    QTimer timer;
    struct Client : QObject {
        Impl &service;
        QLocalSocket *socket;
        std::unique_ptr<DesktopInspection> inspection;
        std::unique_ptr<McpServer> server;
        bool scheduled{}, closing{};
        Client(Impl &service, QLocalSocket *socket)
            : QObject(&service), service(service), socket(socket),
              inspection(std::make_unique<DesktopInspection>(service.view)) {
            socket->setParent(this);
            socket->setReadBufferSize(sessionWireBytes + 2);
            server = std::make_unique<McpServer>(McpBackend{
                [this](const QJsonObject &request) {
                    serviceScope();
                    if (request.value("operation") == "session.describe") {
                        inspection_detail::validateParameters(
                            request, nativeTools()[0].toObject()["inputSchema"].toObject());
                        return state();
                    }
                    bool allowed = false;
                    for (const auto &tool : nativeTools())
                        if (tool.toObject()["name"] == request.value("query"))
                            allowed = true;
                    if (!allowed || request.contains("operation"))
                        throw InspectionError("INVALID_REQUEST",
                                              "Tool is outside native inspection scope");
                    return inspection->execute(request);
                },
                [this] { return state(); }, [this] { inspection.reset(); }, nativeTools()});
            connect(socket, &QLocalSocket::readyRead, this, [this] { schedule(); });
            connect(socket, &QLocalSocket::bytesWritten, this, [this] { schedule(); });
            connect(socket, &QLocalSocket::disconnected, &service, [&service, this] {
                QTimer::singleShot(0, &service, [&service, this] {
                    std::erase_if(service.clients,
                                  [this](const auto &client) { return client.get() == this; });
                });
            });
            schedule();
        }
        void serviceScope() {
            if (!service.view.document().owns(service.scope))
                throw InspectionError("WRONG_DOCUMENT", "Native MCP document session was replaced");
        }
        QJsonObject state() {
            serviceScope();
            const auto &doc = service.view.document();
            return {
                {"apiVersion", 1},
                {"documentId", QString::fromStdString(doc.identity())},
                {"revision", QString::number(doc.revision())},
                {"dirty", doc.dirty()},
                {"selectionAvailable", true},
                {"viewAvailable", true},
                {"mutationAvailable", false},
                {"editorState", QString::fromLatin1(desktopInspectionStamp(service.view).toHex())}};
        }
        void schedule() {
            if (scheduled)
                return;
            scheduled = true;
            QTimer::singleShot(0, this, [this] {
                scheduled = false;
                pump();
            });
        }
        void send(const QJsonArray &messages) {
            QByteArray bytes;
            for (const auto &message : messages) {
                const auto line =
                    QJsonDocument(message.toObject()).toJson(QJsonDocument::Compact) + '\n';
                if (line.size() > sessionResponseBytes ||
                    bytes.size() + line.size() > 2 * sessionResponseBytes)
                    throw InspectionError("LIMIT_EXCEEDED",
                                          "Native MCP response exceeds transport bound");
                bytes += line;
            }
            if (socket->bytesToWrite() + bytes.size() > 2 * sessionResponseBytes ||
                (!bytes.isEmpty() && socket->write(bytes) != bytes.size()))
                throw InspectionError("TRANSPORT_ERROR",
                                      "Cannot queue bounded native MCP response");
        }
        void pump() {
            if (socket->state() != QLocalSocket::ConnectedState || socket->bytesToWrite())
                return;
            if (closing) {
                socket->disconnectFromServer();
                return;
            }
            try {
                serviceScope();
                if (socket->canReadLine()) {
                    const auto line = socket->readLine(sessionWireBytes + 2);
                    if (line.size() > sessionWireBytes)
                        throw InspectionError("LIMIT_EXCEEDED", "MCP message exceeds 66 KiB");
                    checkAutomationDepth(line);
                    QJsonParseError error;
                    const auto json = QJsonDocument::fromJson(line, &error);
                    if (error.error != QJsonParseError::NoError)
                        send({McpServer::error(-32700, "Invalid JSON")});
                    else if (!json.isObject())
                        send({McpServer::error(-32600, "JSON-RPC batching is not supported")});
                    else if (json.object() ==
                             QJsonObject{{"jsonrpc", "2.0"},
                                         {"method", "notifications/sketchyup/close"}}) {
                        closing = true;
                        send(server->close());
                    } else
                        send(server->handle(json.object()));
                    schedule();
                } else if (socket->bytesAvailable() > sessionWireBytes)
                    throw InspectionError("LIMIT_EXCEEDED", "MCP message exceeds 66 KiB");
                else
                    send(server->poll());
            } catch (const std::exception &e) {
                closing = true;
                try {
                    send({McpServer::error(-32603, "Native MCP stream closed",
                                           QJsonValue::Undefined, automationFailure(e))});
                } catch (...) {
                    socket->abort();
                    return;
                }
                schedule();
            }
        }
    };
    std::vector<std::unique_ptr<Client>> clients;
    Impl(Viewport &view, const QString &path) : view(view) {
        if (QThread::currentThread() != view.thread())
            throw InspectionError("WRONG_THREAD", "Native MCP must run on the GUI thread");
        scope = view.document().saveStamp();
        const auto checked = checkedMcpSocketPath(path, false);
        listener.setSocketOptions(QLocalServer::UserAccessOption);
        listener.setMaxPendingConnections(2);
        if (!listener.listen(checked))
            throw InspectionError("TRANSPORT_ERROR", "Cannot listen on native MCP endpoint");
        connect(&listener, &QLocalServer::newConnection, this, [this] {
            while (listener.hasPendingConnections()) {
                QPointer<QLocalSocket> socket = listener.nextPendingConnection();
                try {
                    if (clients.size() >= 2 || !this->view.document().owns(scope))
                        throw InspectionError("LIMIT_EXCEEDED",
                                              "Native MCP connection unavailable");
                    checkMcpPeer(socket->socketDescriptor());
                    clients.push_back(std::make_unique<Client>(*this, socket.data()));
                } catch (...) {
                    if (socket) {
                        socket->abort();
                        socket->deleteLater();
                    }
                }
            }
        });
        connect(&timer, &QTimer::timeout, this, [this] {
            if (!this->view.document().owns(scope)) {
                listener.close();
                for (const auto &client : clients)
                    client->socket->abort();
                timer.stop();
                return;
            }
            for (const auto &client : clients)
                client->schedule();
        });
        timer.start(50);
    }
    ~Impl() override {
        timer.stop();
        for (const auto &client : clients)
            disconnect(client->socket, nullptr, this, nullptr);
        clients.clear();
        listener.close();
    }
};
NativeMcpService::NativeMcpService(Viewport &view, const QString &path)
    : impl_(std::make_unique<Impl>(view, path)) {}
NativeMcpService::~NativeMcpService() = default;
bool NativeMcpService::listening() const { return impl_->listener.isListening(); }
int NativeMcpService::clientCount() const { return int(impl_->clients.size()); }
} // namespace sketchy
