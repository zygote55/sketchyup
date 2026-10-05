#include "integrations/ollama_provider.hpp"
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QJsonDocument>
#include <QNetworkProxy>
#include <QNetworkReply>
#include <QTemporaryDir>
#include <QThread>
#include <QTimer>
#include <iostream>
using namespace sketchy;
using Phase = AssistantTask::Phase;
void check(bool ok, const char *message) {
    if (!ok)
        throw std::runtime_error(message);
}
QByteArray json(QJsonObject object) { return QJsonDocument(object).toJson(QJsonDocument::Compact); }
template <class F> void rejects(F function) {
    try {
        function();
    } catch (const std::exception &) {
        return;
    }
    throw std::runtime_error("Expected rejection");
}
template <class F> void wait(F done) {
    QElapsedTimer timer;
    timer.start();
    while (!done() && timer.elapsed() < 10000) {
        QCoreApplication::processEvents();
        QThread::msleep(2);
    }
    check(done(), "Async local operation completed");
}
QJsonObject capabilities() {
    return {{"capabilities", QJsonArray{"completion", "tools"}},
            {"details", QJsonObject{{"format", "gguf"}}},
            {"model_info",
             QJsonObject{{"general.architecture", "fixture"}, {"fixture.context_length", 32768}}}};
}
QJsonObject response(QJsonObject message) {
    return {{"model", "fixture-local"}, {"done", true},
            {"done_reason", "stop"},    {"message", message},
            {"prompt_eval_count", 100}, {"eval_count", 40}};
}
QJsonObject message() { return {{"role", "assistant"}, {"content", "No edits were applied."}}; }
struct Spec {
    int status{200};
    QJsonObject body;
    bool hang{};
};
class Reply : public QNetworkReply {
    QByteArray body_;
    qsizetype offset_{};
    bool ready_{};
    int &aborts_;

  public:
    Reply(QNetworkRequest request, Spec spec, int &aborts, QObject *parent)
        : QNetworkReply(parent), body_(json(spec.body)), aborts_(aborts) {
        setRequest(request);
        setUrl(request.url());
        setOperation(QNetworkAccessManager::PostOperation);
        setAttribute(QNetworkRequest::HttpStatusCodeAttribute, spec.status);
        open(QIODevice::ReadOnly | QIODevice::Unbuffered);
        if (!spec.hang)
            QTimer::singleShot(1, this, [this] {
                if (isFinished())
                    return;
                ready_ = true;
                emit readyRead();
                if (isFinished())
                    return;
                setFinished(true);
                emit finished();
            });
    }
    void abort() override {
        if (isFinished())
            return;
        ++aborts_;
        setFinished(true);
        setError(OperationCanceledError, "canceled");
        emit finished();
    }
    qint64 bytesAvailable() const override {
        return (ready_ ? body_.size() - offset_ : 0) + QNetworkReply::bytesAvailable();
    }
    qint64 readData(char *data, qint64 maximum) override {
        if (!ready_)
            return 0;
        auto count = std::min<qint64>(maximum, body_.size() - offset_);
        if (!count)
            return -1;
        memcpy(data, body_.constData() + offset_, size_t(count));
        offset_ += count;
        return count;
    }
};
class Network : public QNetworkAccessManager {
  public:
    int versions{}, probes{}, chats{}, aborts{};
    std::vector<QJsonObject> requests;
    std::function<Spec(QString, QJsonObject, int)> respond;
    QNetworkReply *createRequest(Operation operation, const QNetworkRequest &request,
                                 QIODevice *outgoing) override {
        check(request.url().host() == "127.0.0.1", "Only local endpoint receives requests");
        check(!request.hasRawHeader("Authorization") && proxy().type() == QNetworkProxy::NoProxy,
              "Local requests have no credential or proxy");
        check(request.attribute(QNetworkRequest::RedirectPolicyAttribute).toInt() ==
                  QNetworkRequest::ManualRedirectPolicy,
              "No local redirects");
        const auto body =
            outgoing ? QJsonDocument::fromJson(outgoing->readAll()).object() : QJsonObject{};
        const auto path = request.url().path();
        int turn{};
        if (path == "/api/version") {
            check(operation == GetOperation && body.isEmpty(), "Version probe has no context");
            ++versions;
        } else if (path == "/api/show") {
            check(versions == 1 && operation == PostOperation, "Version checked before model");
            ++probes;
            check(body.size() == 2 && body.value("model") == "fixture-local",
                  "Capability preflight contains no document context");
        } else {
            check(path == "/api/chat" && probes > 0, "Checked model before document transfer");
            check(request.url().path() == "/api/chat" && operation == PostOperation &&
                      body.value("truncate") == false && body.value("shift") == false,
                  "Runtime rejects overflow without dropping history");
            for (const auto value : body.value("tools").toArray()) {
                const auto properties = value.toObject()
                                            .value("function")
                                            .toObject()
                                            .value("parameters")
                                            .toObject()
                                            .value("properties")
                                            .toObject();
                const auto discriminator = properties.contains("query") ? "query" : "operation";
                const auto field = properties.value(discriminator).toObject();
                check(field.value("type") == "string" &&
                          field.value("enum").toArray() == QJsonArray{field.value("const")},
                      "Native runtime receives discriminator despite dropping const");
                check(properties.value("apiVersion").toObject().value("enum").toArray() ==
                          QJsonArray{1},
                      "Native runtime receives fixed API version");
            }
            turn = chats++;
            requests.push_back(body);
        }
        auto spec =
            respond ? respond(path, body, turn)
                    : Spec{200, path == "/api/show" ? capabilities() : response(message()), false};
        return new Reply(request, spec, aborts, this);
    }
};
QString alias(QJsonObject request, QString name) {
    for (auto value : request.value("tools").toArray()) {
        const auto f = value.toObject().value("function").toObject();
        if (f.value("description").toString().startsWith(name + ":"))
            return f.value("name").toString();
    }
    throw std::runtime_error("Tool missing");
}
QJsonObject call(QJsonObject request, QString name, QJsonObject arguments) {
    return {{"role", "assistant"},
            {"content", ""},
            {"tool_calls",
             QJsonArray{QJsonObject{{"function", QJsonObject{{"name", alias(request, name)},
                                                             {"arguments", arguments}}}}}}};
}
int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    try {
        OllamaConfiguration config;
        config.model = "fixture-local";
        for (const auto *url :
             {"https://127.0.0.1:11434", "http://example.com", "http://localhost:11434",
              "http://127.0.0.1/api/chat", "http://user:pass@127.0.0.1",
              "http://127.0.0.1?forward=remote", "http://127.0.0.1#part"}) {
            auto invalid = config;
            invalid.endpoint = QUrl(url);
            rejects([&] { validateOllamaConfiguration(invalid); });
        }
        auto ipv6 = config;
        ipv6.endpoint = QUrl("http://[::1]:11434");
        validateOllamaConfiguration(ipv6);
        QTemporaryDir files;
        check(files.isValid(), "Local provider test directory");
        int serial{};
        auto session = [&] {
            auto path = files.path() + "/" + QString::number(++serial);
            return std::make_unique<AutomationSession>(
                AutomationSession::Options{{}, path + ".sketchyup", path + "-outcomes", true});
        };
        auto options = [&] {
            AssistantTask::Options o;
            o.provider = "Ollama";
            o.model = config.model;
            o.prompt = "Create a two by three metre face and preview it.";
            o.allowedCommands = {"geometry.face"};
            return o;
        };
        {
            auto model = session();
            Network network;
            const auto state =
                model->execute({{"apiVersion", 1}, {"operation", "session.describe"}});
            QString draft;
            network.respond = [&](QString path, QJsonObject request, int turn) {
                if (path == "/api/version")
                    return Spec{200, {{"version", "0.35.1"}}};
                if (path == "/api/show")
                    return Spec{200, capabilities()};
                QJsonObject args{{"apiVersion", 1}, {"documentId", state.value("documentId")}};
                QString operation;
                if (turn == 0) {
                    operation = "transaction.begin";
                    args["expectedRevision"] = state.value("revision");
                } else {
                    const auto last = request.value("messages").toArray().last().toObject();
                    check(last.value("role") == "tool" && last.value("tool_name").isString(),
                          "Local tool receipt has wire alias");
                    const auto receipt =
                        QJsonDocument::fromJson(last.value("content").toString().toUtf8()).object();
                    check(receipt.value("isError") == false, "Actual staged tool succeeded");
                    if (turn == 1)
                        draft = receipt.value("data").toObject().value("transactionId").toString();
                    check(!draft.isEmpty(), "Private draft identity retained");
                    args["transactionId"] = draft;
                    args["expectedVersion"] = turn == 1 ? 0 : 1;
                    operation = turn == 1 ? "transaction.apply" : "transaction.preview";
                    if (turn == 1) {
                        args["operationId"] = "local-face";
                        args["commands"] = QJsonArray{QJsonObject{
                            {"command", "geometry.face"},
                            {"loops",
                             QJsonArray{QJsonArray{QJsonArray{0, 0, 0}, QJsonArray{2, 0, 0},
                                                   QJsonArray{2, 3, 0}, QJsonArray{0, 3, 0}}}}}};
                    }
                }
                args["operation"] = operation;
                check(turn < 3, "Provider stops at preview");
                return Spec{200, response(call(request, operation, args))};
            };
            OllamaProvider provider(std::make_unique<AssistantTask>(*model, options()), config,
                                    &network);
            provider.start();
            wait([&] {
                return provider.task().phase() == Phase::PreviewReady ||
                       provider.task().phase() == Phase::Failed;
            });
            if (provider.task().phase() == Phase::Failed)
                std::cerr << provider.status().toStdString() << '\n';
            check(provider.task().phase() == Phase::PreviewReady && network.probes == 1 &&
                      network.chats == 3,
                  "Local actual staging reaches preview");
            provider.apply();
            check(provider.task().result().value("applied") == true,
                  "Host applies local-provider preview");
        }
        for (auto mode : {"old-runtime", "remote", "no-tools", "too-small", "thinking-only",
                          "instruction-legacy", "incomplete", "unknown", "arguments", "usage",
                          "context", "redirect", "503", "hang-probe", "hang-chat", "text"}) {
            auto model = session();
            Network network;
            network.respond = [&](QString path, QJsonObject request, int turn) {
                const QString kind(mode);
                if (path == "/api/version")
                    return Spec{200, {{"version", kind == "old-runtime" ? "0.20.0" : "0.35.1"}}};
                auto caps = capabilities();
                if (path == "/api/show") {
                    if (kind == "remote")
                        caps["remote_host"] = "https://cloud.invalid";
                    if (kind == "no-tools")
                        caps["capabilities"] = QJsonArray{"completion"};
                    if (kind == "too-small")
                        caps["model_info"] = QJsonObject{{"general.architecture", "fixture"},
                                                         {"fixture.context_length", 2048}};
                    if (kind == "thinking-only") {
                        caps["capabilities"] = QJsonArray{"completion", "tools", "thinking"};
                        caps["thinking"] = QJsonObject{{"values", QJsonArray{true}}};
                    }
                    if (kind == "instruction-legacy") {
                        caps["capabilities"] = QJsonArray{"completion", "tools", "thinking"};
                        caps["model_info"] = QJsonObject{{"general.architecture", "qwen3"},
                                                         {"qwen3.context_length", 262144},
                                                         {"general.finetune", "Instruct"},
                                                         {"general.version", "2507"},
                                                         {"general.size_label", "4B"}};
                    }
                    return Spec{200, caps, kind == "hang-probe"};
                }
                auto result = response(message());
                if (kind == "incomplete")
                    result["done_reason"] = "length";
                if (kind == "usage")
                    result.remove("prompt_eval_count");
                if (kind == "context")
                    result["prompt_eval_count"] = 40000;
                if (kind == "unknown" || kind == "arguments") {
                    auto m = call(request, "document.describe", {});
                    auto calls = m.value("tool_calls").toArray();
                    auto item = calls[0].toObject();
                    auto function = item.value("function").toObject();
                    if (kind == "unknown")
                        function["name"] = "shell";
                    else
                        function["arguments"] = "unparsed";
                    item["function"] = function;
                    calls[0] = item;
                    m["tool_calls"] = calls;
                    result = response(m);
                }
                return Spec{kind == "redirect"           ? 302
                            : kind == "503" && turn == 0 ? 503
                                                         : 200,
                            result, kind == "hang-chat"};
            };
            OllamaProvider provider(std::make_unique<AssistantTask>(*model, options()), config,
                                    &network);
            provider.start();
            const QString kind(mode);
            if (kind == "hang-probe" || kind == "hang-chat") {
                wait(
                    [&] { return kind == "hang-chat" ? network.chats == 1 : network.probes == 1; });
                provider.cancel();
                check(network.aborts == 1 && provider.task().phase() == Phase::Canceled,
                      "Local pending phase cancellation");
                continue;
            }
            wait([&] {
                return provider.task().phase() == Phase::Completed ||
                       provider.task().phase() == Phase::Failed;
            });
            const bool succeeds = kind == "text" || kind == "503" || kind == "instruction-legacy";
            check((provider.task().phase() == Phase::Completed) == succeeds,
                  "Local capability/protocol outcome");
            if (kind == "old-runtime" || kind == "remote" || kind == "no-tools" ||
                kind == "too-small" || kind == "thinking-only")
                check(network.chats == 0, "Invalid model gets no document context");
            if (kind == "503")
                check(network.chats == 2 && network.probes == 1,
                      "Shared retry policy retains successful preflight");
            check(provider.task().result().value("applied") == false,
                  "Local prose/errors never prove edits");
        }
        std::cout << "Local capability gate, loopback policy, actual staging, replay, errors and "
                     "cancellation passed\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
