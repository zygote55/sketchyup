#include "integrations/openai_provider.hpp"
#include "automation/recipe.hpp"
#include <QJsonDocument>
#include <QNetworkReply>
#include <QPointer>
#include <QRegularExpression>
#include <QScopeGuard>
#include <QThread>
#include <QTimer>
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
bool running(AssistantTask::Phase phase) {
    return phase == AssistantTask::Phase::Ready ||
           phase == AssistantTask::Phase::AwaitingProvider ||
           phase == AssistantTask::Phase::Backoff || phase == AssistantTask::Phase::PreviewReady;
}
} // namespace
QJsonObject OpenAiConversation::request(const QJsonObject &source) {
    require(source.value("provider") == "OpenAI", "Expected explicitly selected OpenAI provider");
    const auto model = source.value("model").toString();
    require(QRegularExpression("^[A-Za-z0-9][A-Za-z0-9_.:-]{0,127}$").match(model).hasMatch(),
            "Configure a valid explicit OpenAI model ID");
    names_.clear();
    QJsonArray tools;
    for (auto value : source.value("tools").toArray()) {
        const auto entry = value.toObject();
        const auto alias = "t" + QString::number(names_.size());
        const auto name = entry.value("name").toString();
        require(!name.isEmpty() && !names_.values().contains(name), "Invalid tool catalog");
        names_[alias] = name;
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
                     {"parallel_tool_calls", false},
                     {"max_output_tokens", maximum},
                     {"store", false},
                     {"stream", false},
                     {"include", QJsonArray{"reasoning.encrypted_content"}}};
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
            require(item.value("status").toString("completed") == "completed" &&
                        names_.contains(item.value("name").toString()) &&
                        item.value("arguments").isString() && item.value("call_id").isString(),
                    "Invalid or unauthorized function call");
            reply.calls.push_back({item.value("call_id").toString(),
                                   names_.value(item.value("name").toString()),
                                   parse(item.value("arguments").toString().toUtf8())});
            require(reply.calls.size() <= 1, "Parallel tool replies are disabled");
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
    candidate_ = output;
    return reply;
}
void OpenAiConversation::accepted() {
    require(!candidate_.isEmpty(), "No accepted provider output");
    retainedBytes_ += json(candidate_).size();
    outputs_.push_back(candidate_);
    candidate_ = {};
}
struct OpenAiProvider::Impl {
    OpenAiProvider &owner;
    std::unique_ptr<AssistantTask> task;
    QByteArray key, bytes;
    QNetworkAccessManager *manager;
    QPointer<QNetworkReply> pending;
    QTimer poll, deadline;
    OpenAiConversation conversation;
    QString attempt, status{"Ready"};
    bool started{}, processing{};
    AssistantTask::Phase last;
    Impl(OpenAiProvider &owner, std::unique_ptr<AssistantTask> task, QByteArray key,
         QNetworkAccessManager *manager)
        : owner(owner), task(std::move(task)), key(std::move(key)), manager(manager), poll(&owner),
          deadline(&owner), last(AssistantTask::Phase::Ready) {
        require(bool(this->task), "OpenAI provider requires a task");
        const auto disclosure = this->task->disclosure();
        require(disclosure.value("remote") == true && disclosure.value("provider") == "OpenAI",
                "OpenAI requires a task configured for remote context consent");
        require(this->key.size() >= 8 && this->key.size() <= 4096 && !this->key.contains('\r') &&
                    !this->key.contains('\n') && !this->key.contains('\0'),
                "Configure a valid API credential");
        if (!this->manager)
            this->manager = new QNetworkAccessManager(&owner);
        require(this->manager->thread() == owner.thread(),
                "Provider network manager must share owner thread");
        poll.setInterval(20);
        deadline.setSingleShot(true);
        QObject::connect(&poll, &QTimer::timeout, &owner, [this] { guarded([this] { pump(); }); });
        QObject::connect(&deadline, &QTimer::timeout, &owner, [this] {
            guarded([this] {
                fail(AssistantTask::ProviderFailure::Timeout, "OpenAI request timed out");
            });
        });
    }
    ~Impl() {
        abort();
        key.fill('\0');
    }
    void checkOwner() const {
        require(QThread::currentThread() == owner.thread(),
                "OpenAI provider requires owner thread");
    }
    void abort() {
        deadline.stop();
        if (pending) {
            auto *reply = pending.data();
            pending = nullptr;
            QObject::disconnect(reply, nullptr, &owner, nullptr);
            reply->abort();
            reply->deleteLater();
        }
        bytes.clear();
    }
    void notify() {
        last = task->phase();
        emit owner.changed();
    }
    void fail(AssistantTask::ProviderFailure failure, QString message, int retryAfter = 0) {
        abort();
        task->providerFailed(attempt, failure, retryAfter);
        status = std::move(message);
        notify();
    }
    void guarded(const std::function<void()> &operation) {
        try {
            operation();
        } catch (const std::exception &) {
            abort();
            poll.stop();
            task->cancel();
            status = "Local document unavailable; OpenAI task stopped";
            notify();
        }
    }
    void drain() {
        if (!pending)
            return;
        const auto chunk = pending->readAll();
        if (chunk.size() > 1024 * 1024 - bytes.size()) {
            fail(AssistantTask::ProviderFailure::Fatal, "OpenAI response exceeded its byte limit");
            return;
        }
        bytes += chunk;
    }
    void finished() {
        if (!pending)
            return;
        drain();
        if (!pending)
            return;
        auto *reply = pending.data();
        const auto code = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        bool delayValid{};
        const auto seconds = reply->rawHeader("Retry-After").toInt(&delayValid);
        const int retryAfter = delayValid ? std::clamp(seconds, 0, 10) * 1000 : 0;
        if (code == 429) {
            fail(AssistantTask::ProviderFailure::RateLimited,
                 "OpenAI rate limit; retrying within the task budget", retryAfter);
            return;
        }
        if (code == 408 || code == 504) {
            fail(AssistantTask::ProviderFailure::Timeout, "OpenAI request timed out");
            return;
        }
        if (code >= 500 || (!code && reply->error() != QNetworkReply::NoError)) {
            fail(AssistantTask::ProviderFailure::Unavailable, "OpenAI connection unavailable");
            return;
        }
        if (code != 200 || reply->error() != QNetworkReply::NoError) {
            fail(AssistantTask::ProviderFailure::Fatal,
                 code == 401 || code == 403
                     ? "OpenAI rejected the credential or account access"
                     : "OpenAI rejected the request; check model configuration");
            return;
        }
        const auto response = bytes;
        abort();
        try {
            const auto translated = conversation.decode(response);
            if (task->accept(attempt, translated))
                conversation.accepted();
            status = "OpenAI response received";
            notify();
        } catch (const std::exception &) {
            task->providerFailed(attempt, AssistantTask::ProviderFailure::Fatal);
            status = "OpenAI returned an incomplete or invalid response";
            notify();
        }
    }
    void pump() {
        checkOwner();
        if (processing)
            return;
        processing = true;
        const auto reset = qScopeGuard([this] { processing = false; });
        auto request =
            task->nextRequest(); // Also checks revision/deadline while a request is in flight.
        if (!running(task->phase())) {
            abort();
            poll.stop();
        }
        if (task->phase() != last)
            notify();
        if (!request || task->phase() != AssistantTask::Phase::AwaitingProvider)
            return;
        attempt = request->value("attemptId").toString();
        try {
            const auto body = json(conversation.request(*request));
            QNetworkRequest network(QUrl("https://api.openai.com/v1/responses"));
            network.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
            network.setRawHeader("Authorization", "Bearer " + key);
            network.setRawHeader("Accept", "application/json");
            network.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                                 QNetworkRequest::ManualRedirectPolicy);
            network.setTransferTimeout(30000);
            pending = manager->post(network, body);
            bytes.clear();
            pending->setReadBufferSize(1024 * 1024 + 1);
            QObject::connect(pending, &QNetworkReply::readyRead, &owner,
                             [this] { guarded([this] { drain(); }); });
            QObject::connect(pending, &QNetworkReply::finished, &owner,
                             [this] { guarded([this] { finished(); }); });
            deadline.start(30000);
            status = "Waiting for OpenAI";
            notify();
        } catch (const std::exception &) {
            fail(AssistantTask::ProviderFailure::Fatal,
                 "OpenAI request could not be prepared within configured limits");
        }
    }
};
OpenAiProvider::OpenAiProvider(std::unique_ptr<AssistantTask> task, QByteArray key,
                               QNetworkAccessManager *manager, QObject *parent)
    : QObject(parent),
      impl_(std::make_unique<Impl>(*this, std::move(task), std::move(key), manager)) {}
OpenAiProvider::~OpenAiProvider() = default;
void OpenAiProvider::start() {
    impl_->checkOwner();
    require(!impl_->started, "Provider task can start only once");
    impl_->started = true;
    impl_->poll.start();
    impl_->guarded([this] { impl_->pump(); });
}
void OpenAiProvider::cancel() {
    impl_->checkOwner();
    impl_->task->cancel();
    impl_->abort();
    impl_->poll.stop();
    impl_->status = "OpenAI request stopped";
    impl_->notify();
}
void OpenAiProvider::apply() {
    impl_->checkOwner();
    impl_->task->apply();
    impl_->notify();
}
void OpenAiProvider::reconcile() {
    impl_->checkOwner();
    impl_->task->reconcile();
    impl_->notify();
}
AssistantTask &OpenAiProvider::task() {
    impl_->checkOwner();
    return *impl_->task;
}
QString OpenAiProvider::status() const {
    impl_->checkOwner();
    return impl_->status;
}
} // namespace sketchy
