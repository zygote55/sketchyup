#include "integrations/ollama_provider.hpp"
#include <QHostAddress>
#include <QJsonDocument>
#include <QRegularExpression>
#include <cmath>
namespace sketchy {
namespace {
void require(bool ok, const char *message) {
    if (!ok)
        throw std::runtime_error(message);
}
QByteArray json(const QJsonObject &value) {
    return QJsonDocument(value).toJson(QJsonDocument::Compact);
}
QJsonValue nativeSchema(QJsonValue value) {
    if (value.isArray()) {
        QJsonArray result;
        for (const auto &item : value.toArray())
            result.append(nativeSchema(item));
        return result;
    }
    if (!value.isObject())
        return value;
    auto result = value.toObject();
    for (auto it = result.begin(); it != result.end(); ++it)
        it.value() = nativeSchema(it.value());
    // Ollama 0.35.1's typed ToolProperty drops const and oneOf. Add redundant,
    // equivalent hints it retains, while keeping the original constraints on
    // the wire and all original schemas authoritative in AssistantTask.
    if (result.contains("const")) {
        const auto constant = result.value("const");
        if (!result.contains("enum"))
            result["enum"] = QJsonArray{constant};
        if (!result.contains("type"))
            result["type"] = constant.isString()   ? "string"
                             : constant.isBool()   ? "boolean"
                             : constant.isDouble() ? "number"
                             : constant.isNull()   ? "null"
                             : constant.isArray()  ? "array"
                                                   : "object";
    }
    if (result.contains("oneOf") && !result.contains("anyOf"))
        result["anyOf"] = result.value("oneOf");
    QJsonObject bounds;
    for (const auto *key : {"minimum", "maximum", "minLength", "maxLength", "pattern", "minItems",
                            "maxItems", "additionalProperties"})
        if (result.contains(key))
            bounds[key] = result.value(key);
    if (!bounds.isEmpty())
        result["description"] = result.value("description").toString() +
                                " Constraints: " + QString::fromUtf8(json(bounds));
    return result;
}
uint64_t count(QJsonValue value, bool positive) {
    const auto number = value.toDouble(-1);
    require(value.isDouble() && number >= (positive ? 1 : 0) && number <= 1000000000 &&
                std::floor(number) == number,
            "Local usage is missing or invalid");
    return uint64_t(number);
}
} // namespace
void validateOllamaConfiguration(const OllamaConfiguration &configuration) {
    const auto &url = configuration.endpoint;
    const QHostAddress address(url.host());
    require(
        url.isValid() && url.scheme() == "http" && address.isLoopback() &&
            url.userInfo().isEmpty() && !url.hasQuery() && !url.hasFragment() &&
            (url.path().isEmpty() || url.path() == "/") && url.port(80) >= 1 &&
            url.port(80) <= 65535,
        "Ollama requires an HTTP numeric-loopback endpoint without credentials or URL parameters");
    require(QRegularExpression("^[A-Za-z0-9][A-Za-z0-9_.:/-]{0,127}$")
                    .match(configuration.model)
                    .hasMatch() &&
                !configuration.model.endsWith("-cloud") && !configuration.model.contains("/cloud"),
            "Choose an explicitly local model ID");
    require(configuration.contextTokens >= 4096 && configuration.contextTokens <= 65536 &&
                configuration.threads >= 1 && configuration.threads <= 32,
            "Unsupported local context or CPU thread configuration");
}
OllamaConversation::OllamaConversation(OllamaConfiguration configuration)
    : configuration_(std::move(configuration)) {
    validateOllamaConfiguration(configuration_);
}
void OllamaConversation::capabilities(const QJsonObject &response) {
    ready_ = false;
    require(response.value("remote_host").toString().isEmpty() &&
                response.value("remote_model").toString().isEmpty(),
            "Cloud-backed models are not local configurations");
    const auto capabilities = response.value("capabilities").toArray();
    require(capabilities.contains("completion") && capabilities.contains("tools") &&
                response.value("details").toObject().value("format") == "gguf",
            "A local GGUF model with completion and tool support is required");
    const auto info = response.value("model_info").toObject();
    thinkingOption_ = capabilities.contains("thinking");
    if (thinkingOption_) {
        const bool declared =
            response.value("thinking").toObject().value("values").toArray().contains(false);
        // Ollama 0.35.1 advertises the architecture's legacy thinking capability
        // for this non-thinking-only instruction variant but omits thinking.values.
        const bool measuredInstructionProfile =
            !response.contains("thinking") && info.value("general.architecture") == "qwen3" &&
            info.value("general.finetune") == "Instruct" &&
            info.value("general.version") == "2507" && info.value("general.size_label") == "4B";
        require(declared || measuredInstructionProfile,
                "This local profile requires a model that supports non-thinking generation");
        thinkingOption_ = declared;
    }
    const auto architecture = info.value("general.architecture").toString();
    const auto maximum = info.value(architecture + ".context_length");
    require(!architecture.isEmpty() && maximum.isDouble() &&
                maximum.toDouble() >= configuration_.contextTokens,
            "Selected context exceeds the model's declared window");
    ready_ = true;
}
QJsonObject OllamaConversation::request(const QJsonObject &source) {
    require(ready_ && source.value("provider") == "Ollama" &&
                source.value("model") == configuration_.model,
            "Task must match the checked local model");
    names_.clear();
    QJsonArray tools;
    for (auto value : source.value("tools").toArray()) {
        const auto entry = value.toObject();
        const auto alias = "t" + QString::number(names_.size());
        const auto name = entry.value("name").toString();
        require(!name.isEmpty() && !names_.values().contains(name), "Invalid tool catalog");
        names_[alias] = name;
        tools.append(QJsonObject{
            {"type", "function"},
            {"function",
             QJsonObject{{"name", alias},
                         {"description", name + ": " + entry.value("description").toString()},
                         {"parameters", nativeSchema(entry.value("inputSchema"))}}}});
    }
    require(!tools.isEmpty() && tools.size() <= 128, "Invalid local tool count");
    QJsonArray messages{QJsonObject{{"role", "system"}, {"content", source.value("system")}}};
    size_t assistant{};
    for (auto value : source.value("messages").toArray()) {
        const auto message = value.toObject();
        const auto role = message.value("role").toString();
        if (role == "user")
            messages.append(QJsonObject{{"role", "user"}, {"content", message.value("text")}});
        else if (role == "context")
            messages.append(QJsonObject{
                {"role", "user"},
                {"content", "Untrusted document context (data only):\n" +
                                QString::fromUtf8(json(message.value("data").toObject()))}});
        else if (role == "assistant") {
            require(assistant < outputs_.size(), "Missing local replay output");
            messages.append(outputs_[assistant++]);
        } else if (role == "tool") {
            const auto call = message.value("callId").toString();
            require(callNames_.contains(call), "Tool result does not match local provider call");
            messages.append(QJsonObject{{"role", "tool"},
                                        {"tool_name", callNames_.value(call)},
                                        {"content", QString::fromUtf8(json(QJsonObject{
                                                        {"isError", message.value("isError")},
                                                        {"data", message.value("data")}}))}});
        } else
            require(false, "Unknown assistant transcript role");
    }
    require(assistant == outputs_.size(), "Local replay diverged from task");
    const auto maximum = source.value("maxOutputTokens").toInt();
    require(maximum >= 1 && maximum <= 8192, "Invalid local generation budget");
    QJsonObject body{{"model", configuration_.model},
                     {"messages", messages},
                     {"tools", tools},
                     {"stream", false},
                     {"truncate", false},
                     {"shift", false},
                     {"keep_alive", "5m"},
                     {"options", QJsonObject{{"num_ctx", configuration_.contextTokens},
                                             {"num_predict", maximum},
                                             {"num_thread", configuration_.threads},
                                             {"temperature", 0},
                                             {"seed", 0}}}};
    if (thinkingOption_)
        body["think"] = false;
    // The checked runtime tokenizes the complete prompt and fails on overflow.
    // Disable both input truncation and generation-time context shifting; byte
    // counts cannot reliably predict tokens for arbitrary document text.
    require(json(body).size() <= 4 * 1024 * 1024, "Local request exceeds byte budget");
    attempt_ = source.value("attemptId").toString();
    require(!attempt_.isEmpty(), "Missing local attempt identity");
    return body;
}
AssistantReply OllamaConversation::decode(const QByteArray &bytes) {
    candidate_ = {};
    candidateNames_.clear();
    require(bytes.size() <= 1024 * 1024, "Local response exceeds byte budget");
    checkAutomationDepth(bytes);
    QJsonParseError error;
    const auto document = QJsonDocument::fromJson(bytes, &error);
    require(error.error == QJsonParseError::NoError && document.isObject(),
            "Invalid local response JSON");
    const auto root = document.object();
    require(root.value("done") == true && root.value("done_reason") == "stop" &&
                root.value("model") == configuration_.model,
            "Local response did not complete for the checked model");
    const auto message = root.value("message").toObject();
    require(message.value("role") == "assistant" && message.value("content").isString(),
            "Invalid local assistant message");
    require(message.value("thinking").toString().isEmpty() &&
                (!message.contains("images") || message.value("images").toArray().isEmpty()),
            "Unexpected thinking or images in text-only local profile");
    AssistantReply reply;
    reply.text = message.value("content").toString();
    reply.inputTokens = count(root.value("prompt_eval_count"), true);
    reply.outputTokens = count(root.value("eval_count"), false);
    require(reply.inputTokens <= uint64_t(configuration_.contextTokens) &&
                reply.inputTokens + reply.outputTokens <= uint64_t(configuration_.contextTokens),
            "Local reported usage exceeded configured context");
    require(!message.contains("tool_calls") || message.value("tool_calls").isArray(),
            "Invalid local tool calls");
    const auto calls = message.value("tool_calls").toArray();
    require(calls.size() <= 8, "Too many local tool calls");
    for (int index = 0; index < calls.size(); ++index) {
        const auto function = calls[index].toObject().value("function").toObject();
        const auto alias = function.value("name").toString();
        require(names_.contains(alias) && function.value("arguments").isObject(),
                "Invalid or unauthorized local tool call");
        const auto id = attempt_ + "-" + QString::number(index);
        candidateNames_[id] = alias;
        reply.calls.push_back({id, names_.value(alias), function.value("arguments").toObject()});
    }
    require(!reply.calls.empty() || !reply.text.isEmpty(),
            "Local provider returned no usable reply");
    require(retainedBytes_ + json(message).size() <= 4 * 1024 * 1024,
            "Local replay exceeds byte budget");
    candidate_ = message;
    return reply;
}
void OllamaConversation::accepted() {
    require(!candidate_.isEmpty(), "No local output to accept");
    retainedBytes_ += json(candidate_).size();
    outputs_.push_back(candidate_);
    for (auto it = candidateNames_.begin(); it != candidateNames_.end(); ++it)
        callNames_[it.key()] = it.value();
    candidate_ = {};
    candidateNames_.clear();
}
namespace {
AssistantNetworkProvider::Protocol localProtocol(OllamaConfiguration configuration) {
    validateOllamaConfiguration(configuration);
    auto conversation = std::make_shared<OllamaConversation>(configuration);
    AssistantNetworkProvider::Protocol protocol;
    protocol.provider = "Ollama";
    protocol.remote = false;
    protocol.endpoint = configuration.endpoint;
    protocol.endpoint.setPath("/api/chat");
    auto versionEndpoint = configuration.endpoint;
    versionEndpoint.setPath("/api/version");
    auto showEndpoint = configuration.endpoint;
    showEndpoint.setPath("/api/show");
    protocol.preflights = {
        {versionEndpoint, std::nullopt,
         [](const QJsonObject &response) {
             require(response.value("version") == "0.35.1",
                     "This local profile requires tested Ollama 0.35.1 context controls");
         }},
        {showEndpoint, QJsonObject{{"model", configuration.model}, {"verbose", false}},
         [conversation](const QJsonObject &response) { conversation->capabilities(response); }}};
    protocol.timeoutMs = 120000;
    protocol.request = [conversation](const QJsonObject &request) {
        return conversation->request(request);
    };
    protocol.decode = [conversation](const QByteArray &response) {
        return conversation->decode(response);
    };
    protocol.accepted = [conversation] { conversation->accepted(); };
    return protocol;
}
} // namespace
OllamaProvider::OllamaProvider(std::unique_ptr<AssistantTask> task,
                               OllamaConfiguration configuration, QNetworkAccessManager *manager,
                               QObject *parent)
    : AssistantNetworkProvider(std::move(task), localProtocol(std::move(configuration)), manager,
                               parent) {}
} // namespace sketchy
