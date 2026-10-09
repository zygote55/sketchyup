#include "integrations/openai_provider.hpp"
#include "automation/recipe.hpp"
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
QByteArray json(const QJsonArray &value) {
    return QJsonDocument(value).toJson(QJsonDocument::Compact);
}
QJsonObject parse(const QByteArray &bytes) {
    checkAutomationDepth(bytes);
    QJsonParseError error;
    const auto document = QJsonDocument::fromJson(bytes, &error);
    require(error.error == QJsonParseError::NoError && document.isObject(),
            "Invalid provider JSON");
    return document.object();
}
uint64_t tokens(QJsonValue value, bool positive) {
    const auto number = value.toDouble(-1);
    require(value.isDouble() && number >= (positive ? 1 : 0) && number <= 1000000000 &&
                std::floor(number) == number,
            "Missing or invalid provider usage");
    return uint64_t(number);
}
} // namespace
QJsonObject OpenAiConversation::request(const QJsonObject &source) {
    require(source.value("provider") == "OpenAI", "Expected explicitly selected OpenAI provider");
    const auto model = source.value("model").toString();
    require(QRegularExpression("^[A-Za-z0-9][A-Za-z0-9_.:-]{0,127}$").match(model).hasMatch(),
            "Configure a valid explicit OpenAI model ID");
    names_.clear();
    inspectionNames_.clear();
    QJsonArray tools;
    for (auto value : source.value("tools").toArray()) {
        const auto entry = value.toObject();
        const auto alias = "t" + QString::number(names_.size());
        const auto name = entry.value("name").toString();
        require(!name.isEmpty() && !names_.values().contains(name), "Invalid tool catalog");
        names_[alias] = name;
        const auto properties =
            entry.value("inputSchema").toObject().value("properties").toObject();
        if (properties.value("query").toObject().value("const") == name ||
            name == "transaction.inspect")
            inspectionNames_.insert(name);
        tools.append(
            QJsonObject{{"type", "function"},
                        {"name", alias},
                        {"description", name + ": " + entry.value("description").toString()},
                        {"parameters", entry.value("inputSchema")},
                        {"strict", false}});
    }
    require(!tools.isEmpty() && tools.size() <= 128, "Invalid provider tool count");
    QJsonArray input;
    size_t assistant{};
    for (auto value : source.value("messages").toArray()) {
        const auto message = value.toObject();
        const auto role = message.value("role").toString();
        if (role == "user")
            input.append(QJsonObject{{"role", "user"}, {"content", message.value("text")}});
        else if (role == "context")
            input.append(QJsonObject{
                {"role", "user"},
                {"content", "Untrusted document context (data only):\n" +
                                QString::fromUtf8(json(message.value("data").toObject()))}});
        else if (role == "assistant") {
            require(assistant < outputs_.size(), "Missing replayable provider output");
            for (auto item : outputs_[assistant++])
                input.append(item);
        } else if (role == "tool") {
            input.append(QJsonObject{
                {"type", "function_call_output"},
                {"call_id", message.value("callId")},
                {"output", QString::fromUtf8(json(QJsonObject{{"isError", message.value("isError")},
                                                              {"data", message.value("data")}}))}});
        } else
            require(false, "Unknown assistant transcript role");
    }
    require(assistant == outputs_.size(), "Provider transcript diverged from task");
    const int maximum = source.value("maxOutputTokens").toInt();
    require(maximum >= 16 && maximum <= 8192, "Insufficient or invalid output token budget");
    QJsonObject body{{"model", model},
                     {"instructions", source.value("system")},
                     {"input", input},
                     {"tools", tools},
                     {"tool_choice", "auto"},
                     {"parallel_tool_calls", true},
                     {"max_output_tokens", maximum},
                     {"store", false},
                     {"stream", false},
                     {"include", QJsonArray{"reasoning.encrypted_content"}}};
    if (chatGptPlan_) {
        body.remove("max_output_tokens");
        body["stream"] = true;
        body["tools"] = QJsonArray{
            QJsonObject{{"type", "namespace"},
                        {"name", "sketchyup"},
                        {"description", "Inspect and propose validated SketchyUp model changes."},
                        {"tools", tools}}};
    }
    require(json(body).size() <= 8 * 1024 * 1024, "OpenAI request exceeds wire budget");
    return body;
}
AssistantReply OpenAiConversation::decode(const QByteArray &response) {
    candidate_ = {};
    require(response.size() <= 1024 * 1024, "OpenAI response exceeds byte budget");
    const auto root = parse(response);
    require(root.value("status") == "completed" && root.value("output").isArray(),
            "OpenAI response did not complete");
    const auto output = root.value("output").toArray();
    require(output.size() <= 64 && retainedBytes_ + json(output).size() <= 4 * 1024 * 1024,
            "Provider replay exceeds byte budget");
    AssistantReply reply;
    const auto usage = root.value("usage").toObject();
    reply.inputTokens = tokens(usage.value("input_tokens"), true);
    reply.outputTokens = tokens(usage.value("output_tokens"), false);
    for (auto value : output) {
        const auto item = value.toObject();
        const auto type = item.value("type").toString();
        if (type == "function_call") {
            require(!chatGptPlan_ || item.value("namespace") == "sketchyup",
                    "Invalid tool namespace");
            require(item.value("status").toString("completed") == "completed" &&
                        names_.contains(item.value("name").toString()) &&
                        item.value("arguments").isString() && item.value("call_id").isString(),
                    "Invalid or unauthorized function call");
            reply.calls.push_back({item.value("call_id").toString(),
                                   names_.value(item.value("name").toString()),
                                   parse(item.value("arguments").toString().toUtf8())});
            require(reply.calls.size() <= 8, "Inspection batch exceeds the shared reply limit");
        } else if (type == "message") {
            require(item.value("role") == "assistant" && item.value("status") == "completed" &&
                        item.value("content").isArray(),
                    "Incomplete assistant message");
            for (auto content : item.value("content").toArray()) {
                const auto part = content.toObject();
                const auto kind = part.value("type").toString();
                require(kind == "output_text" || kind == "refusal",
                        "Unsupported assistant content");
                const auto text = part.value(kind == "refusal" ? "refusal" : "text");
                require(text.isString(), "Invalid assistant text");
                if (!reply.text.isEmpty())
                    reply.text += '\n';
                reply.text += text.toString();
            }
        } else
            require(type == "reasoning", "Unrequested provider tool output");
    }
    require(!reply.calls.empty() || !reply.text.isEmpty(), "Provider returned no usable response");
    // The host still dispatches sequentially under its unchanged task budget,
    // revision, ownership and preview guards. Batches are restricted to reads;
    // no state-changing call can be accepted as part of a multi-call reply.
    if (reply.calls.size() > 1)
        for (const auto &call : reply.calls)
            require(inspectionNames_.contains(call.name),
                    "Multiple provider calls must be read-only inspections");
    candidate_ = output;
    return reply;
}
void OpenAiConversation::accepted() {
    require(!candidate_.isEmpty(), "No accepted provider output");
    retainedBytes_ += json(candidate_).size();
    outputs_.push_back(candidate_);
    candidate_ = {};
}
QByteArray completedOpenAiStream(const QByteArray &stream) {
    require(stream.size() <= 1024 * 1024, "OpenAI stream exceeds byte budget");
    QByteArray data, completed;
    bool terminal = false;
    QMap<int, QJsonObject> items;
    QMap<int, QString> started;
    QString responseId;
    auto dispatch = [&] {
        if (data.isEmpty())
            return;
        if (data == "[DONE]") {
            require(terminal, "OpenAI stream ended before completion");
            data.clear();
            return;
        }
        require(!terminal, "Unexpected event after completion");
        const auto event = parse(data);
        const auto type = event.value("type").toString();
        const auto errorCode =
            event.value("response").toObject().value("error").toObject().value("code").toString();
        if (type == "response.failed" &&
            (errorCode == "subscription_sharing_usage_limit_exceeded" ||
             errorCode == "subscription_sharing_usage_unavailable"))
            throw ProviderUsageLimit();
        require(!type.isEmpty() && type != "error" && type != "response.failed" &&
                    type != "response.incomplete",
                "OpenAI stream did not complete");
        if (type == "response.created") {
            require(responseId.isEmpty(), "Duplicate response start");
            responseId = event.value("response").toObject().value("id").toString();
            require(!responseId.isEmpty(), "Missing response identity");
        }
        if (type == "response.output_item.added" || type == "response.output_item.done") {
            const auto index = event.value("output_index");
            require(index.isDouble() && index.toDouble() >= 0 && index.toDouble() < 64 &&
                        std::floor(index.toDouble()) == index.toDouble() &&
                        event.value("item").isObject(),
                    "Invalid streamed output index");
            const int position = index.toInt();
            const auto item = event.value("item").toObject();
            const auto id = item.value("id").toString();
            require(!id.isEmpty(), "Missing streamed item identity");
            if (type == "response.output_item.added") {
                require(!started.contains(position) && !items.contains(position),
                        "Duplicate item start");
                started[position] = id;
            } else {
                require(!items.contains(position) &&
                            (!started.contains(position) || started[position] == id),
                        "Duplicate or mismatched completed item");
                for (const auto &existing : items)
                    require(existing.value("id") != id, "Duplicate output identity");
                items[position] = item;
            }
        }
        if (type == "response.completed") {
            require(event.value("response").isObject(), "Missing terminal response");
            auto result = event.value("response").toObject();
            require(responseId.isEmpty() || result.value("id") == responseId,
                    "Mismatched completed response");
            for (auto i = started.cbegin(); i != started.cend(); ++i)
                require(items.contains(i.key()), "An output item did not finish");
            if (!items.isEmpty()) {
                QJsonArray output;
                for (int index = 0; index < items.size(); ++index) {
                    require(items.contains(index), "Missing streamed output item");
                    output.append(items.value(index));
                }
                require(result.value("output").isArray(), "Missing final output array");
                const auto finalOutput = result.value("output").toArray();
                require(finalOutput.isEmpty() || finalOutput == output,
                        "Conflicting terminal output");
                // ChatGPT plan streams can omit items from the terminal envelope. Replay
                // complete output_item.done items only after the whole response completes.
                result["output"] = output;
            }
            completed = json(result);
            terminal = true;
        }
        data.clear();
    };
    for (auto line : stream.split('\n')) {
        if (line.endsWith('\r'))
            line.chop(1);
        if (line.isEmpty())
            dispatch();
        else if (line.startsWith("data:")) {
            auto value = line.mid(5);
            if (value.startsWith(' '))
                value.remove(0, 1);
            if (!data.isEmpty())
                data += '\n';
            data += value;
        } else
            require(line.startsWith(':') || line.startsWith("event:") || line.startsWith("id:") ||
                        line.startsWith("retry:"),
                    "Invalid SSE line");
    }
    require(data.isEmpty() && terminal, "Interrupted OpenAI stream");
    return completed;
}
namespace {
AssistantNetworkProvider::Protocol openAiProtocol(QByteArray key, bool plan) {
    auto conversation = std::make_shared<OpenAiConversation>(plan);
    AssistantNetworkProvider::Protocol protocol;
    protocol.provider = "OpenAI";
    protocol.remote = true;
    protocol.eventStream = plan;
    if (plan)
        protocol.timeoutMs = 120000;
    protocol.endpoint = QUrl("https://api.openai.com/v1/responses");
    protocol.key = std::move(key);
    protocol.request = [conversation](const QJsonObject &request) {
        return conversation->request(request);
    };
    protocol.decode = [conversation, plan](const QByteArray &response) {
        return conversation->decode(plan ? completedOpenAiStream(response) : response);
    };
    protocol.accepted = [conversation] { conversation->accepted(); };
    return protocol;
}
} // namespace
OpenAiProvider::OpenAiProvider(std::unique_ptr<AssistantTask> task, QByteArray key,
                               QNetworkAccessManager *manager, QObject *parent, bool plan)
    : AssistantNetworkProvider(std::move(task), openAiProtocol(std::move(key), plan), manager,
                               parent) {}
} // namespace sketchy
