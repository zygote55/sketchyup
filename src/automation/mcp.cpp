#include "automation/mcp.hpp"
#include <QFile>
#include <QJsonDocument>
#include <QUrl>
#include <algorithm>
#include <cmath>
namespace sketchy {
namespace {
struct ProtocolError : std::runtime_error {
    int code;
    QJsonObject data;
    ProtocolError(int code, const char *message, QJsonObject data = {})
        : std::runtime_error(message), code(code), data(std::move(data)) {}
};
void require(bool ok, const char *message) {
    if (!ok)
        throw ProtocolError(-32602, message);
}
bool validId(const QJsonValue &id) {
    if (id.isString())
        return id.toString().toUtf8().size() <= 128;
    const auto n = id.toDouble();
    return id.isDouble() && std::isfinite(n) && std::floor(n) == n &&
           std::abs(n) <= 9007199254740991.0;
}
QString key(const QJsonValue &id) {
    return QString::fromUtf8(QJsonDocument(QJsonArray{id}).toJson(QJsonDocument::Compact));
}
QJsonObject info() { return {{"name", "sketchyup"}, {"version", "0.1.0"}}; }
QJsonObject complete(QJsonObject result = {}) {
    result["resultType"] = "complete";
    auto metadata = result["_meta"].toObject();
    metadata["io.modelcontextprotocol/serverInfo"] = info();
    result["_meta"] = metadata;
    return result;
}
QJsonObject cached(QJsonObject result) {
    result["ttlMs"] = 0;
    result["cacheScope"] = "private";
    return result;
}
QJsonObject reply(const QJsonValue &id, QJsonObject result) {
    return {{"jsonrpc", "2.0"}, {"id", id}, {"result", complete(result)}};
}
QJsonObject notice(QString method, const QJsonValue &id, QJsonObject params) {
    params["_meta"] = QJsonObject{{"io.modelcontextprotocol/subscriptionId", id}};
    return {{"jsonrpc", "2.0"}, {"method", method}, {"params", params}};
}
void fields(const QJsonObject &object, std::initializer_list<const char *> allowed) {
    for (auto i = object.begin(); i != object.end(); ++i)
        require(std::any_of(allowed.begin(), allowed.end(),
                            [&](const auto *name) { return i.key() == name; }),
                "Unsupported protocol parameter");
}
QJsonObject toolResult(QJsonObject result, bool failed = false) {
    return {
        {"content", QJsonArray{QJsonObject{{"type", "text"},
                                           {"text", QString::fromUtf8(QJsonDocument(result).toJson(
                                                        QJsonDocument::Compact))}}}},
        {"structuredContent", result},
        {"isError", failed}};
}
QJsonObject serverCapabilities() {
    return {{"tools", QJsonObject{{"listChanged", false}}},
            {"resources", QJsonObject{{"subscribe", true}, {"listChanged", false}}}};
}
void emitMessages(QIODevice &output, const QJsonArray &messages) {
    for (const auto &message : messages)
        writeAutomationResponse(output, message.toObject());
}
} // namespace
QJsonArray mcpToolCatalog() {
    const auto session = sessionCapabilities();
    QJsonArray result;
    auto add = [&](const QJsonArray &entries, bool queries) {
        for (const auto &value : entries) {
            const auto entry = value.toObject();
            const auto name = entry["name"].toString();
            const bool readOnly = queries || name.startsWith("session.");
            const bool retry = name == "transaction.apply" || name == "transaction.preview" ||
                               name == "transaction.commit" || name == "transaction.cancel" ||
                               name == "transaction.status";
            result.append(QJsonObject{
                {"name", name},
                {"description",
                 entry["description"].toString(name + ": shared versioned SketchyUp operation")},
                {"inputSchema", entry["parameters"]},
                {"annotations", QJsonObject{{"readOnlyHint", readOnly},
                                            {"destructiveHint", !readOnly},
                                            {"idempotentHint", retry},
                                            {"openWorldHint", false}}}});
        }
    };
    add(session["operations"].toArray(), false);
    add(session["inspection"].toObject()["queries"].toArray(), true);
    add(session["transactions"].toObject()["operations"].toArray(), false);
    return result;
}
QJsonObject mcpCapabilities() {
    return {
        {"protocolVersion", mcpProtocolVersion},
        {"transport", "stdio"},
        {"capabilities", serverCapabilities()},
        {"tools", mcpToolCatalog()},
        {"limits", QJsonObject{{"wireBytes", sessionWireBytes},
                               {"responseBytes", sessionResponseBytes},
                               {"requestDepth", 64},
                               {"subscriptions", 8},
                               {"toolBurst", 120},
                               {"toolRequestsPerSecond", 2}}},
        {"scope", "One explicitly launched document; shared tool arguments retain "
                  "document/revision guards; no arbitrary paths or shell"},
        {"selection", "Headless binding reports UNAVAILABLE_CONTEXT for desktop selection"},
        {"versions", "Modern per-request metadata protocol only; no legacy initialize handshake"}};
}
McpServer::McpServer(AutomationSession &session)
    : McpServer(McpBackend{
          [&](const QJsonObject &request) { return session.execute(request); },
          [&] { return session.execute({{"apiVersion", 1}, {"operation", "session.describe"}}); },
          [&] { session.close(); }, mcpToolCatalog()}) {}
McpServer::McpServer(McpBackend backend) : backend_(std::move(backend)) {
    require(bool(backend_.call) && bool(backend_.state) && bool(backend_.close) &&
                !backend_.tools.empty() && backend_.tools.size() <= 64,
            "Invalid MCP backend");
    state_ = backend_.state();
    require(state_["documentId"].isString() && !state_["documentId"].toString().isEmpty(),
            "MCP backend requires explicit document identity");
    uri_ = "sketchyup://document/" +
           QString::fromLatin1(QUrl::toPercentEncoding(state_["documentId"].toString())) + "/state";
}
McpServer::~McpServer() {
    try {
        close();
    } catch (...) {
    }
}
void McpServer::owner() const {
    if (std::this_thread::get_id() != owner_)
        throw InspectionError("WRONG_THREAD",
                              "MCP dispatcher belongs to its document owner thread");
}
QJsonObject McpServer::error(int code, QString message, QJsonValue id, QJsonObject data) {
    QJsonObject value{{"code", code}, {"message", message.left(2048)}};
    if (!data.isEmpty())
        value["data"] = data;
    QJsonObject result{{"jsonrpc", "2.0"}, {"error", value}};
    if (!id.isUndefined())
        result["id"] = id;
    return result;
}
QJsonArray McpServer::handle(const QJsonObject &request) {
    owner();
    if (active_)
        throw InspectionError("REENTRANT_TRANSACTION", "MCP dispatch cannot reenter itself");
    struct Guard {
        bool &active;
        ~Guard() { active = false; }
    } guard{active_};
    active_ = true;
    const auto id = request.value("id");
    const bool notification = !request.contains("id") && request["method"].isString();
    QJsonArray messages;
    try {
        if (closed_)
            throw ProtocolError(-32600, "MCP server has closed");
        if (QJsonDocument(request).toJson(QJsonDocument::Compact).size() > sessionWireBytes)
            throw ProtocolError(-32600, "MCP message exceeds 66 KiB");
        if (request["jsonrpc"] != "2.0" || !request["method"].isString() ||
            (!notification && !validId(id)))
            throw ProtocolError(
                -32600, "Expected a JSON-RPC request with a bounded string or safe integer ID");
        fields(request, {"jsonrpc", "id", "method", "params"});
        if (request.contains("params") && !request["params"].isObject())
            throw ProtocolError(-32602, "Protocol params must be an object");
        if (notification) {
            if (request["method"] == "notifications/cancelled") {
                const auto params = request["params"].toObject();
                fields(params, {"requestId", "reason", "_meta"});
                require(validId(params["requestId"]), "Cancellation requires a valid request ID");
                subscriptions_.erase(key(params["requestId"]));
            }
            return {};
        }
        require(!subscriptions_.contains(key(id)), "Request ID is already in flight");
        messages = dispatch(request);
    } catch (const ProtocolError &e) {
        if (!notification)
            messages.append(error(e.code, e.what(),
                                  validId(id) ? id : QJsonValue(QJsonValue::Undefined), e.data));
    } catch (const std::exception &e) {
        if (!notification)
            messages.append(error(-32603, "MCP operation failed",
                                  validId(id) ? id : QJsonValue(QJsonValue::Undefined),
                                  automationFailure(e)));
    }
    updates(messages);
    return messages;
}
QJsonArray McpServer::dispatch(const QJsonObject &request) {
    const auto id = request["id"];
    const auto params = request["params"].toObject(), metadata = params["_meta"].toObject();
    require(params["_meta"].isObject() &&
                metadata["io.modelcontextprotocol/protocolVersion"].isString() &&
                metadata["io.modelcontextprotocol/clientCapabilities"].isObject(),
            "Every request requires protocol version and client capabilities metadata");
    if (metadata["io.modelcontextprotocol/protocolVersion"] != mcpProtocolVersion)
        throw ProtocolError(-32022, "Unsupported MCP protocol version",
                            {{"requested", metadata["io.modelcontextprotocol/protocolVersion"]},
                             {"supported", QJsonArray{mcpProtocolVersion}}});
    const auto method = request["method"].toString();
    if (method == "server/discover") {
        fields(params, {"_meta"});
        return {reply(
            id, cached({{"supportedVersions", QJsonArray{mcpProtocolVersion}},
                        {"capabilities", serverCapabilities()},
                        {"instructions",
                         "Use session.describe to discover the explicitly bound document. Pass its "
                         "identity and revision to shared tools. Commit retries require the "
                         "original durable requestId/payloadHash; disconnect is not evidence that "
                         "work was absent. No arbitrary file or shell access is available."}}))};
    }
    if (method == "tools/list" || method == "resources/list" ||
        method == "resources/templates/list") {
        fields(params, {"_meta", "cursor"});
        require(!params.contains("cursor") ||
                    (params["cursor"].isString() && params["cursor"].toString().isEmpty()),
                "This bounded catalog fits one page; cursor is unsupported");
        if (method == "tools/list")
            return {reply(id, cached({{"tools", backend_.tools}}))};
        if (method == "resources/templates/list")
            return {reply(id, cached({{"resourceTemplates", QJsonArray{}}}))};
        return {reply(
            id, cached({{"resources",
                         QJsonArray{QJsonObject{
                             {"uri", uri_},
                             {"name", "document-state"},
                             {"description",
                              "Bound document identity, revision, save and publication state"},
                             {"mimeType", "application/json"}}}}}))};
    }
    if (method == "resources/read") {
        fields(params, {"_meta", "uri"});
        require(params["uri"] == uri_, "Resource URI is outside the bound document");
        return {reply(
            id,
            cached({{"contents",
                     QJsonArray{QJsonObject{
                         {"uri", uri_},
                         {"mimeType", "application/json"},
                         {"text", QString::fromUtf8(QJsonDocument(backend_.state())
                                                        .toJson(QJsonDocument::Compact))}}}}}))};
    }
    if (method == "tools/call")
        return {reply(id, call(params))};
    if (method == "subscriptions/listen") {
        fields(params, {"_meta", "notifications"});
        require(params["notifications"].isObject(), "Subscription requires a notification filter");
        const auto filter = params["notifications"].toObject();
        fields(filter, {"resourceSubscriptions", "toolsListChanged", "resourcesListChanged",
                        "promptsListChanged"});
        for (const auto *name : {"toolsListChanged", "resourcesListChanged", "promptsListChanged"})
            require(!filter.contains(name) || filter[name].isBool(),
                    "List-change filters must be boolean");
        QJsonObject honored;
        bool resource = false;
        if (filter.contains("resourceSubscriptions")) {
            require(filter["resourceSubscriptions"].isArray(),
                    "Resource subscriptions must be an array");
            const auto uris = filter["resourceSubscriptions"].toArray();
            require(uris.size() <= 1 && (uris.empty() || uris[0] == uri_),
                    "Subscription resource is outside the bound document or exceeds the "
                    "one-resource limit");
            resource = !uris.empty();
            if (resource)
                honored["resourceSubscriptions"] = uris;
        }
        require(subscriptions_.size() < 8, "At most eight subscriptions may be active");
        subscriptions_.emplace(key(id), Subscription{id, resource});
        return {
            notice("notifications/subscriptions/acknowledged", id, {{"notifications", honored}})};
    }
    throw ProtocolError(-32601, "Unknown MCP method");
}
QJsonObject McpServer::call(const QJsonObject &params) {
    fields(params, {"_meta", "name", "arguments"});
    require(params["name"].isString(), "Tool name must be a string");
    require(!params.contains("arguments") || params["arguments"].isObject(),
            "Tool arguments must be an object");
    const auto name = params["name"].toString();
    bool found = false;
    for (const auto &tool : backend_.tools)
        if (tool.toObject()["name"] == name) {
            found = true;
            break;
        }
    require(found, "Unknown tool");
    const auto args = params["arguments"].toObject();
    require(args["operation"] == name || args["query"] == name,
            "Tool name must match the shared operation or query in its arguments");
    const auto now = StagingSession::Clock::now();
    tokens_ = std::min(120.0, tokens_ + std::chrono::duration<double>(now - refilled_).count() * 2);
    refilled_ = now;
    if (tokens_ < 1)
        return toolResult({{"code", "RATE_LIMIT"},
                           {"message", "Tool rate limit reached; wait before retrying"},
                           {"retryAfterMs", 500}},
                          true);
    tokens_ -= 1;
    try {
        return toolResult(backend_.call(args));
    } catch (const std::exception &e) {
        return toolResult(automationFailure(e), true);
    }
}
void McpServer::updates(QJsonArray &messages) {
    if (closed_)
        return;
    const auto current = backend_.state();
    if (current["documentId"] != state_["documentId"])
        throw InspectionError("WRONG_DOCUMENT", "MCP backend cannot change document identity");
    if (current == state_)
        return;
    state_ = current;
    for (const auto &[key, subscription] : subscriptions_)
        if (subscription.resource)
            messages.append(
                notice("notifications/resources/updated", subscription.id, {{"uri", uri_}}));
}
QJsonArray McpServer::close() {
    owner();
    if (closed_)
        return {};
    if (active_)
        throw InspectionError("REENTRANT_TRANSACTION", "Cannot close during MCP dispatch");
    backend_.close();
    closed_ = true;
    QJsonArray messages;
    for (const auto &[key, subscription] : subscriptions_)
        messages.append(reply(
            subscription.id,
            {{"_meta", QJsonObject{{"io.modelcontextprotocol/subscriptionId", subscription.id}}}}));
    subscriptions_.clear();
    return messages;
}
int runMcpStream(McpServer &server, QIODevice &input, QIODevice &output) {
    try {
        for (;;) {
            const auto line = input.readLine(sessionWireBytes + 2);
            if (line.isEmpty()) {
                if (auto file = dynamic_cast<QFile *>(&input);
                    file && file->error() != QFileDevice::NoError)
                    throw InspectionError("INPUT_ERROR", "Could not read MCP stream");
                break;
            }
            if (line.size() > sessionWireBytes)
                throw InspectionError("LIMIT_EXCEEDED",
                                      "MCP message exceeds 66 KiB; stream closed");
            checkAutomationDepth(line);
            QJsonParseError error;
            const auto json = QJsonDocument::fromJson(line, &error);
            if (error.error != QJsonParseError::NoError)
                writeAutomationResponse(output, McpServer::error(-32700, "Invalid JSON"));
            else if (!json.isObject())
                writeAutomationResponse(
                    output, McpServer::error(-32600, "JSON-RPC batching is not supported"));
            else
                emitMessages(output, server.handle(json.object()));
        }
        emitMessages(output, server.close());
        return 0;
    } catch (const std::exception &e) {
        try {
            writeAutomationResponse(output,
                                    McpServer::error(-32603, "MCP stream closed",
                                                     QJsonValue::Undefined, automationFailure(e)));
        } catch (...) {
        }
        try {
            emitMessages(output, server.close());
        } catch (...) {
        }
        return 1;
    }
}
} // namespace sketchy
