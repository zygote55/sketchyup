#include "automation/mcp.hpp"
#include <QBuffer>
#include <QCoreApplication>
#include <QFile>
#include <QJsonDocument>
#include <QProcess>
#include <QProcessEnvironment>
#include <QTemporaryDir>
#include <future>
#include <iostream>
using namespace sketchy;
void check(bool ok, const char *message) {
    if (!ok)
        throw std::runtime_error(message);
}
void record(const QJsonObject &message) {
    const auto path = qEnvironmentVariable("SKETCHYUP_MCP_TRANSCRIPT");
    if (path.isEmpty())
        return;
    QFile file(path);
    check(file.open(QIODevice::WriteOnly | QIODevice::Append), "Open synthetic MCP transcript");
    file.write(QJsonDocument(message).toJson(QJsonDocument::Compact) + '\n');
}
QJsonObject metadata() {
    return {{"io.modelcontextprotocol/protocolVersion", mcpProtocolVersion},
            {"io.modelcontextprotocol/clientCapabilities", QJsonObject{}}};
}
QJsonObject rpc(QJsonValue id, QString method, QJsonObject params = {}) {
    params["_meta"] = metadata();
    return {{"jsonrpc", "2.0"}, {"id", id}, {"method", method}, {"params", params}};
}
QJsonObject op(QString name, QJsonObject fields = {}) {
    fields["apiVersion"] = 1;
    fields["operation"] = name;
    return fields;
}
QJsonObject callRequest(QJsonValue id, QString name, QJsonObject args) {
    return rpc(id, "tools/call", {{"name", name}, {"arguments", args}});
}
QJsonObject invoke(McpServer &server, const QJsonObject &request) {
    const auto messages = server.handle(request);
    check(!messages.empty(), "MCP request returns response");
    for (const auto &m : messages)
        record(m.toObject());
    return messages[0].toObject();
}
class Client {
  public:
    QProcess process;
    explicit Client(QStringList args) {
        auto env = QProcessEnvironment::systemEnvironment();
        for (const auto *name : {"DISPLAY", "WAYLAND_DISPLAY", "QT_QPA_PLATFORM"})
            env.remove(name);
        process.setProcessEnvironment(env);
        process.start(QStringLiteral(CLI_PATH), args);
        check(process.waitForStarted(10000), "Start MCP stdio server");
    }
    ~Client() {
        if (process.state() != QProcess::NotRunning) {
            process.kill();
            process.waitForFinished();
        }
    }
    void send(QJsonObject request) {
        const auto bytes = QJsonDocument(request).toJson(QJsonDocument::Compact) + '\n';
        check(process.write(bytes) == bytes.size() && process.waitForBytesWritten(10000),
              "Send MCP message");
    }
    QJsonObject read() {
        while (!process.canReadLine())
            check(process.waitForReadyRead(10000),
                  qPrintable(QString("Missing MCP message: %1")
                                 .arg(QString::fromUtf8(process.readAllStandardError()))));
        const auto parsed = QJsonDocument::fromJson(process.readLine());
        check(parsed.isObject(), "Only JSON-RPC on stdout");
        record(parsed.object());
        return parsed.object();
    }
    QJsonObject exchange(QJsonObject request) {
        send(request);
        const auto response = read();
        check(response["id"] == request["id"], "MCP response preserves ID");
        return response;
    }
    void finish() {
        process.closeWriteChannel();
        check((process.state() == QProcess::NotRunning || process.waitForFinished(10000)) &&
                  process.exitStatus() == QProcess::NormalExit && process.exitCode() == 0,
              "Graceful MCP EOF");
    }
};
int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    try {
        QFile schema(QStringLiteral(SOURCE_DIR "/docs/api/mcp-2026-07-28.json"));
        check(schema.open(QIODevice::ReadOnly) &&
                  QJsonDocument::fromJson(schema.readAll()).object() == mcpCapabilities(),
              "MCP discovery artifact matches registry");
        QTemporaryDir files;
        check(files.isValid(), "MCP test storage");
        {
            AutomationSession session({{},
                                       files.path() + "/protocol.sketchyup",
                                       files.path() + "/protocol-outcomes",
                                       true});
            McpServer server(session);
            check(invoke(server, callRequest(900, "session.describe",
                                             {{"apiVersion", 1},
                                              {"operation", "session.describe"},
                                              {"query", "view.capture"}}))
                      .contains("error"),
                  "Mixed dispatch discriminators rejected before backend execution");

            const auto tools = invoke(server, rpc(1, "tools/list"))["result"].toObject();
            check(tools["resultType"] == "complete" &&
                      tools["tools"].toArray() == mcpToolCatalog() &&
                      tools["tools"].toArray().size() == 29 && tools["cacheScope"] == "private" &&
                      tools["ttlMs"] == 0,
                  "Modern tools list works without a handshake and matches shared schemas");
            auto missing = rpc(2, "server/discover");
            missing["params"] = QJsonObject{};
            check(invoke(server, missing)["error"].toObject()["code"] == -32602,
                  "Metadata is required on every request");
            auto unsupported = rpc(3, "server/discover");
            auto params = unsupported["params"].toObject(), meta = metadata();
            meta["io.modelcontextprotocol/protocolVersion"] = "2025-11-25";
            params["_meta"] = meta;
            unsupported["params"] = params;
            const auto rejected = invoke(server, unsupported)["error"].toObject();
            check(rejected["code"] == -32022 &&
                      rejected["data"].toObject()["supported"].toArray() ==
                          QJsonArray{mcpProtocolVersion},
                  "Unsupported version reports modern negotiation error");
            check(invoke(server, rpc(4, "initialize"))["error"].toObject()["code"] == -32601,
                  "Legacy handshake is not silently accepted");
            check(invoke(server, rpc(5, "resources/read", {{"uri", "file:///etc/passwd"}}))["error"]
                          .toObject()["code"] == -32602,
                  "Arbitrary resource files are outside scope");
            check(
                invoke(server, callRequest(6, "shell", op("shell")))["error"].toObject()["code"] ==
                    -32602,
                "No shell tool is exposed");
            check(
                invoke(server, callRequest(7, "session.describe", op("transaction.begin")))["error"]
                        .toObject()["code"] == -32602,
                "Tool name cannot smuggle another registered operation");
            auto invalid = rpc(QJsonValue::Null, "tools/list");
            const auto badId = invoke(server, invalid);
            check(badId["error"].toObject()["code"] == -32600 && !badId.contains("id"),
                  "Malformed modern request ID is omitted in error");
            const auto info = session.execute(op("session.describe"));
            QJsonObject selection{{"apiVersion", 1},
                                  {"documentId", info["documentId"]},
                                  {"expectedRevision", info["revision"]},
                                  {"query", "selection.get"}};
            const auto unavailable =
                invoke(server, callRequest(8, "selection.get", selection))["result"].toObject();
            check(unavailable["isError"] == true &&
                      unavailable["structuredContent"].toObject()["code"] == "UNAVAILABLE_CONTEXT",
                  "Headless MCP cannot invent desktop selection");
            auto wrong =
                op("transaction.begin", {{"documentId", "foreign"}, {"expectedRevision", "0"}});
            const auto scope =
                invoke(server, callRequest(9, "transaction.begin", wrong))["result"].toObject();
            check(scope["isError"] == true &&
                      scope["structuredContent"].toObject()["code"] == "WRONG_DOCUMENT",
                  "Shared model errors remain actionable tool results");
            bool limited = false;
            for (int i = 0; i < 150; ++i) {
                const auto r = invoke(server, callRequest(100 + i, "session.describe",
                                                          op("session.describe")))["result"]
                                   .toObject();
                limited |= r["structuredContent"].toObject()["code"] == "RATE_LIMIT";
            }
            check(limited, "Tool invocation rate is bounded");
            auto thread = std::async(std::launch::async, [&] {
                try {
                    server.handle(rpc(1000, "tools/list"));
                } catch (const InspectionError &e) {
                    return e.code() == "WRONG_THREAD";
                }
                return false;
            });
            check(thread.get(), "Protocol dispatch is owner-thread confined");
        }
        const auto model = files.path() + "/live.sketchyup",
                   outcomes = files.path() + "/live-outcomes";
        QJsonObject info, sealed, receipt;
        QString uri;
        {
            Client cli({"--mcp", "--new", "--output", model, "--outcomes", outcomes});
            const auto discovered = cli.exchange(rpc(1, "server/discover"))["result"].toObject();
            check(discovered["supportedVersions"].toArray() == QJsonArray{mcpProtocolVersion} &&
                      discovered["_meta"]
                              .toObject()["io.modelcontextprotocol/serverInfo"]
                              .toObject()["name"] == "sketchyup",
                  "MCP server discovery identifies current protocol");
            info =
                cli.exchange(callRequest(2, "session.describe", op("session.describe")))["result"]
                    .toObject()["structuredContent"]
                    .toObject();
            const auto resources =
                cli.exchange(rpc(3, "resources/list"))["result"].toObject()["resources"].toArray();
            check(resources.size() == 1, "Exactly one bound document-state resource");
            uri = resources[0].toObject()["uri"].toString();
            cli.send(rpc("events", "subscriptions/listen",
                         {{"notifications", QJsonObject{{"resourceSubscriptions", QJsonArray{uri}},
                                                        {"toolsListChanged", true}}}}));
            const auto ack = cli.read();
            check(ack["method"] == "notifications/subscriptions/acknowledged" &&
                      ack["params"]
                              .toObject()["_meta"]
                              .toObject()["io.modelcontextprotocol/subscriptionId"] == "events" &&
                      !ack["params"].toObject()["notifications"].toObject().contains(
                          "toolsListChanged"),
                  "Subscription first acknowledges only supported filters");
            auto scoped = [&](QString name, QJsonObject fields = {}) {
                fields["documentId"] = info["documentId"];
                return op(name, fields);
            };
            const auto begun = cli.exchange(callRequest(
                4, "transaction.begin",
                scoped("transaction.begin", {{"expectedRevision", "0"}})))["result"]
                                   .toObject()["structuredContent"]
                                   .toObject();
            const auto id = begun["transactionId"].toString();
            const auto apply =
                scoped("transaction.apply",
                       {{"transactionId", id},
                        {"expectedVersion", 0},
                        {"operationId", "units"},
                        {"commands",
                         QJsonArray{QJsonObject{{"command", "document.units"}, {"units", "mm"}}}}});
            check(cli.exchange(callRequest(5, "transaction.apply", apply))["result"]
                          .toObject()["isError"] == false,
                  "Shared mutation applies privately over MCP");
            sealed = cli.exchange(callRequest(
                6, "transaction.preview",
                scoped("transaction.preview",
                       {{"transactionId", id}, {"expectedVersion", 1}})))["result"]
                         .toObject()["structuredContent"]
                         .toObject();
            auto durable = [&](QString name) {
                return scoped(name, {{"requestId", sealed["requestId"]},
                                     {"payloadHash", sealed["payloadHash"]}});
            };
            receipt = cli.exchange(callRequest(7, "transaction.commit",
                                               durable("transaction.commit")))["result"]
                          .toObject();
            check(receipt["structuredContent"].toObject()["status"] == "committed",
                  "MCP commit succeeds once");
            const auto event = cli.read();
            check(event["method"] == "notifications/resources/updated" &&
                      event["params"].toObject()["uri"] == uri,
                  "Commit emits bounded resource update after response");
            check(cli.exchange(callRequest(8, "transaction.commit",
                                           durable("transaction.commit")))["result"]
                          .toObject() == receipt,
                  "MCP lost-reply retry retains original outcome");
            cli.send({{"jsonrpc", "2.0"},
                      {"method", "notifications/cancelled"},
                      {"params", QJsonObject{{"requestId", 7}}}});
            check(cli.exchange(callRequest(9, "transaction.status",
                                           durable("transaction.status")))["result"]
                          .toObject()["structuredContent"]
                          .toObject()["status"] == "committed",
                  "Late JSON-RPC cancellation does not undo completed commit or reply to "
                  "notification");
            cli.send({{"jsonrpc", "2.0"},
                      {"method", "notifications/cancelled"},
                      {"params", QJsonObject{{"requestId", "events"}}}});
            check(cli.exchange(callRequest(
                      10, "document.save",
                      scoped("document.save", {{"expectedRevision", "1"}})))["result"]
                          .toObject()["isError"] == false,
                  "Scoped save uses shared API");
            const auto state =
                cli.exchange(rpc(11, "resources/read", {{"uri", uri}}))["result"].toObject();
            check(state["contents"].toArray().size() == 1,
                  "Cancelled subscription leaves no queued save notification");
            for (int i = 0; i < 8; ++i) {
                cli.send(rpc(
                    20 + i, "subscriptions/listen",
                    {{"notifications", QJsonObject{{"resourceSubscriptions", QJsonArray{uri}}}}}));
                check(cli.read()["method"] == "notifications/subscriptions/acknowledged",
                      "Bounded subscription accepted");
            }
            check(cli.exchange(rpc(30, "subscriptions/listen",
                                   {{"notifications", QJsonObject{}}}))["error"]
                          .toObject()["code"] == -32602,
                  "Ninth subscription rejected");
            check(cli.exchange(rpc(20, "tools/list"))["error"].toObject()["code"] == -32602,
                  "In-flight subscription ID cannot be reused");
            cli.finish();
            int ended = 0;
            while (cli.process.canReadLine()) {
                const auto end = cli.read();
                check(end["result"]
                              .toObject()["_meta"]
                              .toObject()["io.modelcontextprotocol/subscriptionId"] == end["id"],
                      "EOF closes subscription with original identity");
                ++ended;
            }
            check(ended == 8, "All uncancelled subscriptions end gracefully");
        }
        {
            Client cli({"--mcp", "--input", model, "--outcomes", outcomes});
            const auto retried = cli.exchange(callRequest(
                1, "transaction.commit",
                op("transaction.commit", {{"documentId", info["documentId"]},
                                          {"requestId", sealed["requestId"]},
                                          {"payloadHash", sealed["payloadHash"]}})))["result"]
                                     .toObject();
            check(retried == receipt, "Fresh MCP process resolves original durable receipt");
            const auto begun = cli.exchange(callRequest(
                2, "transaction.begin",
                op("transaction.begin",
                   {{"documentId", info["documentId"]}, {"expectedRevision", "1"}})))["result"]
                                   .toObject()["structuredContent"]
                                   .toObject();
            const auto id = begun["transactionId"].toString();
            cli.exchange(callRequest(
                3, "transaction.apply",
                op("transaction.apply",
                   {{"documentId", info["documentId"]},
                    {"transactionId", id},
                    {"expectedVersion", 0},
                    {"operationId", "unsaved"},
                    {"commands",
                     QJsonArray{QJsonObject{{"command", "document.units"}, {"units", "m"}}}}})));
            sealed = cli.exchange(callRequest(
                4, "transaction.preview",
                op("transaction.preview", {{"documentId", info["documentId"]},
                                           {"transactionId", id},
                                           {"expectedVersion", 1}})))["result"]
                         .toObject()["structuredContent"]
                         .toObject();
            cli.finish();
        }
        {
            AutomationSession reopened({model, {}, outcomes});
            const auto status = reopened.execute(
                op("transaction.status", {{"documentId", info["documentId"]},
                                          {"requestId", sealed["requestId"]},
                                          {"payloadHash", sealed["payloadHash"]}}));
            check(status["status"] == "aborted" &&
                      loadDocument(model).displayUnits() == DisplayUnit::Millimeters,
                  "Disconnect retires uncommitted staging without reverting committed geometry");
        }
        for (const auto &bytes :
             {QByteArray("not-json\n[]\n"), QByteArray(sessionWireBytes + 1, 'x') + '\n',
              QByteArray(65, '[') + QByteArray(65, ']') + '\n'}) {
            AutomationSession session({model, {}, outcomes});
            McpServer server(session);
            QBuffer input, output;
            input.setData(bytes);
            input.open(QIODevice::ReadOnly);
            output.open(QIODevice::WriteOnly);
            const auto code = runMcpStream(server, input, output);
            check(code == (bytes.startsWith("not-json") ? 0 : 1),
                  "Fatal framing limits are distinct from recoverable protocol errors");
            for (const auto &line : output.data().split('\n'))
                if (!line.isEmpty()) {
                    const auto object = QJsonDocument::fromJson(line).object();
                    record(object);
                    check(object["jsonrpc"] == "2.0" && object.contains("error"),
                          "Malformed wire input produces only protocol errors");
                }
        }
        std::cout << "Modern MCP discovery, shared transactions, bounded subscriptions, "
                     "cancellation and reconnect passed\n";
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
