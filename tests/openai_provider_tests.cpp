#include "automation/native_assistant_session.hpp"
#include "integrations/credential_store.hpp"
#include "integrations/openai_provider.hpp"
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QFile>
#include <QJsonDocument>
#include <QNetworkReply>
#include <QTemporaryDir>
#include <QThread>
#include <QTimer>
#include <deque>
#include <iostream>
using namespace sketchy;
using Phase = AssistantTask::Phase;
void check(bool ok, const char *message) {
    if (!ok)
        throw std::runtime_error(message);
}
QByteArray json(QJsonObject value) { return QJsonDocument(value).toJson(QJsonDocument::Compact); }
void write(QString path, QByteArray bytes) {
    QFile file(path);
    check(file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size(), "Write fixture");
}
template <class F> void rejects(F operation) {
    try {
        operation();
    } catch (const std::exception &) {
        return;
    }
    throw std::runtime_error("Expected rejection");
}
template <class F> void wait(F done, int limit = 10000) {
    QElapsedTimer timer;
    timer.start();
    while (!done() && timer.elapsed() < limit) {
        QCoreApplication::processEvents();
        QThread::msleep(2);
    }
    check(done(), "Async operation met test deadline");
}
QJsonObject completed(QJsonArray output) {
    return {{"status", "completed"},
            {"output", output},
            {"usage", QJsonObject{{"input_tokens", 100}, {"output_tokens", 40}}}};
}
QJsonObject message(QString text = "The model is unchanged.") {
    return {
        {"type", "message"},
        {"id", "msg_fixture"},
        {"status", "completed"},
        {"role", "assistant"},
        {"phase", "final_answer"},
        {"content", QJsonArray{QJsonObject{
                        {"type", "output_text"}, {"text", text}, {"annotations", QJsonArray{}}}}}};
}
struct Response {
    int code{200}, delay{1};
    QByteArray bytes{json(completed(QJsonArray{message()}))};
    QNetworkReply::NetworkError error{QNetworkReply::NoError};
    QByteArray retryAfter;
};
class Reply : public QNetworkReply {
    QByteArray bytes_;
    qsizetype offset_{};
    bool ready_{};
    int &aborts_;

  public:
    Reply(QNetworkRequest request, Response response, int &aborts, QObject *parent)
        : QNetworkReply(parent), bytes_(std::move(response.bytes)), aborts_(aborts) {
        setRequest(request);
        setUrl(request.url());
        setOperation(QNetworkAccessManager::PostOperation);
        setAttribute(QNetworkRequest::HttpStatusCodeAttribute, response.code);
        if (!response.retryAfter.isEmpty())
            setRawHeader("Retry-After", response.retryAfter);
        open(QIODevice::ReadOnly | QIODevice::Unbuffered);
        if (response.delay >= 0)
            QTimer::singleShot(response.delay, this, [this, error = response.error] {
                if (isFinished())
                    return;
                if (error != NoError)
                    setError(error, "DO NOT EXPOSE RAW ERROR OR CREDENTIAL");
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
        setError(OperationCanceledError, "aborted");
        setFinished(true);
        emit finished();
    }
    qint64 bytesAvailable() const override {
        return (ready_ ? bytes_.size() - offset_ : 0) + QNetworkReply::bytesAvailable();
    }
    qint64 readData(char *data, qint64 maximum) override {
        if (!ready_)
            return 0;
        const auto count = std::min<qint64>(maximum, bytes_.size() - offset_);
        if (!count)
            return -1;
        memcpy(data, bytes_.constData() + offset_, size_t(count));
        offset_ += count;
        return count;
    }
};
class Network : public QNetworkAccessManager {
  public:
    int calls{}, aborts{};
    bool plan{}, rawStream{};
    std::function<Response(QJsonObject, int)> respond;
    std::vector<QJsonObject> requests;
    QNetworkReply *createRequest(Operation op, const QNetworkRequest &request,
                                 QIODevice *out) override {
        check(op == PostOperation && request.url() == QUrl("https://api.openai.com/v1/responses"),
              "Fixed HTTPS Responses endpoint");
        check(request.rawHeader("Authorization") == "Bearer sk-fixture-only",
              "Credential is in authorization header");
        check(request.attribute(QNetworkRequest::RedirectPolicyAttribute).toInt() ==
                  QNetworkRequest::ManualRedirectPolicy,
              "Redirects are not followed");
        const auto bytes = out->readAll();
        check(!bytes.contains("sk-fixture-only"), "No credential in JSON");
        const auto body = QJsonDocument::fromJson(bytes).object();
        check(body.value("store") == false && body.value("stream") == plan &&
                  body.value("parallel_tool_calls") == true,
              "Stateless bounded inspection batches with serial host dispatch");
        requests.push_back(body);
        auto response = respond ? respond(body, calls++) : (++calls, Response{});
        if (plan && !rawStream) {
            check(!body.contains("max_output_tokens") &&
                      request.rawHeader("Accept") == "text/event-stream",
                  "Plan request omits unsupported cap and accepts SSE");
            auto final = QJsonDocument::fromJson(response.bytes).object();
            QByteArray stream;
            int index{};
            for (const auto &item : final.value("output").toArray())
                stream += "event: response.output_item.done\r\ndata: " +
                          json({{"type", "response.output_item.done"},
                                {"output_index", index++},
                                {"item", item}}) +
                          "\r\n\r\n";
            final["output"] = QJsonArray{};
            stream += "event: response.completed\r\ndata: " +
                      json({{"type", "response.completed"}, {"response", final}}) + "\r\n\r\n";
            response.bytes = stream;
        }
        return new Reply(request, response, aborts, this);
    }
};
QString alias(const QJsonObject &body, const QString &name) {
    auto tools = body.value("tools").toArray();
    if (body.value("stream") == true) {
        check(tools.size() == 1 && tools[0].toObject().value("name") == "sketchyup",
              "Namespace groups only advertised tools");
        tools = tools[0].toObject().value("tools").toArray();
    }
    for (auto value : tools) {
        const auto tool = value.toObject();
        check(tool.value("strict") == false, "Original optional schema semantics retained");
        if (tool.value("description").toString().startsWith(name + ":"))
            return tool.value("name").toString();
    }
    throw std::runtime_error("Advertised tool missing");
}
QJsonObject call(const QJsonObject &body, QString name, QJsonObject args, int index) {
    return {{"namespace", body.value("stream") == true ? QJsonValue("sketchyup") : QJsonValue()},
            {"type", "function_call"},
            {"id", "fc_" + QString::number(index)},
            {"call_id", "call_" + QString::number(index)},
            {"status", "completed"},
            {"name", alias(body, name)},
            {"arguments", QString::fromUtf8(json(args))}};
}
QString quote(QString text) { return "'" + text.replace("'", "'\\''") + "'"; }
int fakeSecret(QCoreApplication &app, QStringList args) {
    const auto mode = args[2];
    const auto operation = args[3];
    check(args.contains("org.sketchyup.SketchyUp") && args.contains("OpenAI"),
          "Fixed credential attributes");
    check(!args.contains("sk-fixture-only"), "No secret in argv");
    if (mode == "hang")
        return app.exec();
    if (mode == "missing")
        return 1;
    if (mode == "error") {
        std::cerr << "sk-fixture-only";
        return 1;
    }
    if (mode == "flood") {
        std::cout << std::string(9000, 'x') << std::flush;
        return 0;
    }
    if (operation == "lookup")
        std::cout << "sk-fixture-only" << std::flush;
    if (operation == "store") {
        std::string key((std::istreambuf_iterator<char>(std::cin)), {});
        check(key == "sk-fixture-only", "Credential bytes passed by stdin without newline");
    }
    return 0;
}
int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    try {
        if (app.arguments().contains("--fake-secret"))
            return fakeSecret(app, app.arguments());
        QTemporaryDir files;
        check(files.isValid(), "Private test directory");
        int serial{};
        auto session = [&] {
            const auto path = files.path() + "/model" + QString::number(++serial);
            return std::make_unique<AutomationSession>(
                AutomationSession::Options{{}, path + ".sketchyup", path + "-outcomes", true});
        };
        auto options = [] {
            AssistantTask::Options result;
            result.prompt = "Create and measure a two by three metre face, then preview it.";
            result.provider = "OpenAI";
            result.model = "fixture-responses-model";
            result.remote = true;
            result.remoteContextApproved = true;
            result.allowedCommands = {"geometry.face"};
            return result;
        };
        for (bool plan : {false, true}) {
            auto model = session();
            AssistantTask task(*model, options());
            OpenAiConversation conversation(plan);
            const auto next = task.nextRequest();
            check(next.has_value(), "Initial remote codec request");
            const auto wire = conversation.request(*next);
            check(wire.value("parallel_tool_calls") == true,
                  "Remote request advertises bounded parallel inspections");
            const auto state =
                model->execute({{"apiVersion", 1}, {"operation", "session.describe"}});
            QJsonObject args{{"apiVersion", 1},
                             {"query", "document.describe"},
                             {"documentId", state.value("documentId")},
                             {"expectedRevision", state.value("revision")}};
            QJsonArray reads;
            for (int i = 0; i < 8; ++i)
                reads.append(call(wire, "document.describe", args, i));
            auto accepted = conversation.decode(json(
                QJsonObject{{"status", "completed"},
                            {"output", reads},
                            {"usage", QJsonObject{{"input_tokens", 100}, {"output_tokens", 40}}}}));
            check(accepted.calls.size() == 8,
                  "Eight advertised inspections fit the existing shared reply bound");
            auto tooMany = reads;
            tooMany.append(call(wire, "document.describe", args, 8));
            rejects([&] {
                conversation.decode(json(QJsonObject{
                    {"status", "completed"},
                    {"output", tooMany},
                    {"usage", QJsonObject{{"input_tokens", 100}, {"output_tokens", 40}}}}));
            });
            auto stateful = reads;
            stateful[0] = call(wire, "transaction.begin",
                               QJsonObject{{"apiVersion", 1},
                                           {"operation", "transaction.begin"},
                                           {"documentId", state.value("documentId")},
                                           {"expectedRevision", state.value("revision")}},
                               20);
            // ADR 0167: a well-formed mixed batch is refused as a whole, not a fatal decode.
            const auto mixed = conversation.decode(json(QJsonObject{
                {"status", "completed"},
                {"output", stateful},
                {"usage", QJsonObject{{"input_tokens", 100}, {"output_tokens", 40}}}}));
            check(mixed.calls.size() == 8, "Mixed batch decodes every call");
            for (const auto &refused : mixed.calls)
                check(refused.refusal.value("code") == "BATCH_NOT_READ_ONLY" &&
                          refused.refusal.value("message") ==
                              "Only read-only inspections may be batched; send transaction.begin "
                              "alone.",
                      "Every call in a mixed batch carries the named read-only batch rule");
            check(model->execute({{"apiVersion", 1}, {"operation", "session.describe"}})
                          .value("revision") == state.value("revision"),
                  "Refused mixed provider batch cannot execute or publish a mutation");
            // An unadvertised alias (or, on the plan, a foreign namespace) is refused per call.
            auto unknown = call(wire, "document.describe", args, 40);
            unknown["name"] = "t999";
            auto foreign = call(wire, "document.describe", args, 41);
            foreign["namespace"] = plan ? QJsonValue("other") : QJsonValue();
            const auto aliases = conversation.decode(json(completed(QJsonArray{unknown, foreign})));
            check(aliases.calls.size() == 2 &&
                      aliases.calls[0].refusal.value("code") == "UNKNOWN_TOOL" &&
                      aliases.calls[0].refusal.value("message") ==
                          "t999 is not an advertised tool; call only the advertised tools." &&
                      aliases.calls[0].name == "t999" &&
                      (plan ? aliases.calls[1].refusal.value("code") == "UNKNOWN_TOOL"
                            : aliases.calls[1].refusal.isEmpty() &&
                                  aliases.calls[1].name == "document.describe"),
                      "Unadvertised aliases and namespaces are refused, not fatal");
            // Unusable replies stay fatal: malformed arguments, incomplete calls or responses.
            auto malformed = call(wire, "document.describe", args, 42);
            malformed["arguments"] = "{\"apiVersion\":";
            rejects([&] { conversation.decode(json(completed(QJsonArray{malformed}))); });
            auto unfinished = call(wire, "document.describe", args, 43);
            unfinished["status"] = "in_progress";
            rejects([&] { conversation.decode(json(completed(QJsonArray{unfinished}))); });
            auto incomplete = completed(QJsonArray{call(wire, "document.describe", args, 44)});
            incomplete["status"] = "incomplete";
            rejects([&] { conversation.decode(json(incomplete)); });
            rejects([&] { conversation.decode("{\"status\":\"completed\",\"output\":["); });
            // R086.e corpus v4: a completed reply batching private draft reads (several
            // transaction.inspect calls plus transaction.diff with the advertised maximum page)
            // is a read-only batch (ADR 0025 transaction reads, sideEffects "none").
            const QString draft = "00000000-0000-4000-8000-000000000000";
            const QJsonObject draftArgs{{"apiVersion", 1},
                                        {"documentId", state.value("documentId")},
                                        {"transactionId", draft}};
            QJsonArray draftReads;
            for (int i = 0; i < 4; ++i) {
                auto inspect = draftArgs;
                inspect["operation"] = "transaction.inspect";
                auto request = args;
                request["expectedRevision"] = "1";
                inspect["request"] = request;
                draftReads.append(call(wire, "transaction.inspect", inspect, 30 + i));
            }
            auto diff = draftArgs;
            diff["operation"] = "transaction.diff";
            diff["limit"] = 100;
            draftReads.append(call(wire, "transaction.diff", diff, 34));
            auto describe = draftArgs;
            describe["operation"] = "transaction.describe";
            draftReads.append(call(wire, "transaction.describe", describe, 35));
            const auto draftBatch = conversation.decode(json(completed(draftReads)));
            check(draftBatch.calls.size() == 6 && draftBatch.calls[4].name == "transaction.diff" &&
                      draftBatch.calls[4].arguments.value("limit") == 100 &&
                      draftBatch.calls[5].name == "transaction.describe",
                  "Private draft inspect, diff and describe reads form one bounded batch");
            check(task.accept(next->value("attemptId").toString(), draftBatch) &&
                      task.phase() != Phase::Failed,
                  "Host accepts the read-only draft batch instead of failing the task");
            auto statefulDraft = draftReads;
            statefulDraft[5] = call(wire, "transaction.preview",
                                    QJsonObject{{"apiVersion", 1},
                                                {"operation", "transaction.preview"},
                                                {"documentId", state.value("documentId")},
                                                {"transactionId", draft},
                                                {"expectedVersion", 1}},
                                    36);
            const auto illegal = conversation.decode(json(completed(statefulDraft)));
            check(illegal.calls.size() == 6 && illegal.calls[5].name == "transaction.preview",
                  "Draft reads with a preview decode as a refused batch");
            for (const auto &refused : illegal.calls)
                check(refused.refusal.value("message") ==
                          "Only read-only inspections may be batched; send transaction.preview "
                          "alone.",
                      "A preview batched with draft reads is refused with the named rule");
        }
        {
            auto model = session();
            auto opts = options();
            opts.clarificationAvailable = true;
            Network network;
            network.respond = [&](QJsonObject request, int turn) {
                if (turn == 0) {
                    const QJsonObject question{
                        {"question", "Which instance?"},
                        {"allowFreeText", false},
                        {"choices", QJsonArray{QJsonObject{{"id", "one"}, {"label", "Selected"}},
                                               QJsonObject{{"id", "all"}, {"label", "All"}}}}};
                    return Response{200, 1,
                                    json(completed(QJsonArray{
                                        call(request, "assistant.ask_user", question, 0)}))};
                }
                const auto last = request.value("input").toArray().last().toObject();
                const auto output =
                    QJsonDocument::fromJson(last.value("output").toString().toUtf8()).object();
                check(turn == 1 && last.value("type") == "function_call_output" &&
                          last.value("call_id") == "call_0" &&
                          output.value("data").toObject().value("choiceId") == "one",
                      "Host answer resumes the exact OpenAI tool call");
                return Response{};
            };
            OpenAiProvider provider(std::make_unique<AssistantTask>(*model, opts),
                                    "sk-fixture-only", &network);
            provider.start();
            wait([&] { return provider.task().phase() == Phase::AwaitingClarification; });
            QElapsedTimer pause;
            pause.start();
            wait([&] { return pause.elapsed() >= 100; });
            check(network.calls == 1, "No polling request while waiting for a user answer");
            provider.answer(
                provider.task().result().value("clarification").toObject().value("id").toString(),
                "one");
            wait([&] { return provider.task().phase() == Phase::Completed; });
            check(network.calls == 2 && provider.task().result().value("applied") == false,
                  "Provider resumes once without an edit");
        }
        {
            auto model = session();
            auto opts = options();
            opts.remoteContextApproved = false;
            Network network;
            OpenAiProvider provider(std::make_unique<AssistantTask>(*model, opts),
                                    "sk-fixture-only", &network);
            provider.start();
            check(provider.task().phase() == Phase::Failed && network.calls == 0,
                  "No remote bytes without consent");
            opts.remote = false;
            rejects([&] {
                OpenAiProvider invalid(std::make_unique<AssistantTask>(*model, opts),
                                       "sk-fixture-only", &network);
            });
        }
        for (int profile : {0, 1, 2}) {
            const bool plan = profile != 0;
            const bool legacyBudget = profile == 1;
            Document document;
            NativeAssistantSession model(document, files.path() + "/native-profile-" +
                                                       QString::number(profile));
            Network network;
            network.plan = plan;
            const auto state = model.state();
            QString draft;
            network.respond = [&](QJsonObject body, int wireTurn) {
                QJsonObject args{{"apiVersion", 1}, {"documentId", state.value("documentId")}};
                // Exercise repeated context costs while a real edit remains privately staged.
                if (plan && wireTurn >= 3 && wireTurn < 14) {
                    args["query"] = "document.describe";
                    args["expectedRevision"] = state.value("revision");
                    auto result = completed(
                        QJsonArray{call(body, "document.describe", args, 100 + wireTurn)});
                    result["usage"] =
                        QJsonObject{{"input_tokens", 23034},
                                    {"input_tokens_details", QJsonObject{{"cached_tokens", 20000}}},
                                    {"output_tokens", 163}};
                    return Response{200, 1, json(result)};
                }
                const int turn = plan && wireTurn >= 14 ? wireTurn - 11 : wireTurn;
                QString name;
                if (turn == 0) {
                    name = "document.describe";
                    args["query"] = name;
                    args["expectedRevision"] = state.value("revision");
                }
                if (turn == 1) {
                    name = "transaction.begin";
                    args["operation"] = name;
                    args["expectedRevision"] = state.value("revision");
                }
                if (turn >= 2) {
                    const auto last = body.value("input").toArray().last().toObject();
                    const auto receipt =
                        QJsonDocument::fromJson(last.value("output").toString().toUtf8()).object();
                    check(receipt.value("isError") == false,
                          "Real tool succeeded through provider mapping");
                    if (turn == 2)
                        draft = receipt.value("data").toObject().value("transactionId").toString();
                    check(!draft.isEmpty(), "Draft created by real engine");
                    args["transactionId"] = draft;
                    if (turn != 3)
                        args["expectedVersion"] = turn == 2 ? 0 : 1;
                    name = turn == 2   ? "transaction.apply"
                           : turn == 3 ? "transaction.inspect"
                                       : "transaction.preview";
                    args["operation"] = name;
                    if (turn == 2) {
                        args["operationId"] = "face";
                        args["commands"] = QJsonArray{QJsonObject{
                            {"command", "geometry.face"},
                            {"loops",
                             QJsonArray{QJsonArray{QJsonArray{0, 0, 0}, QJsonArray{2, 0, 0},
                                                   QJsonArray{2, 3, 0}, QJsonArray{0, 3, 0}}}}}};
                    }
                }
                if (turn == 3) {
                    const auto input = body.value("input").toArray();
                    QJsonObject receipt;
                    for (const auto &value : input) {
                        const auto item = value.toObject();
                        if (item.value("type") == "function_call_output" &&
                            item.value("call_id") == "call_2")
                            receipt =
                                QJsonDocument::fromJson(item.value("output").toString().toUtf8())
                                    .object();
                    }
                    check(!receipt.isEmpty(),
                          "Staged revision remains available after inspections");
                    args["request"] =
                        QJsonObject{{"apiVersion", 1},
                                    {"query", "document.describe"},
                                    {"documentId", state.value("documentId")},
                                    {"expectedRevision",
                                     receipt.value("data").toObject().value("proposedRevision")}};
                }
                if (turn == 4) {
                    int receipts{};
                    for (const auto &v : body.value("input").toArray()) {
                        const auto item = v.toObject();
                        if (item.value("type") == "function_call_output" &&
                            QStringList{"call_30", "call_31", "call_32", "call_33"}.contains(
                                item.value("call_id").toString())) {
                            check(QJsonDocument::fromJson(item.value("output").toString().toUtf8())
                                          .object()
                                          .value("isError") == false,
                                  "Each private inspection completed against the actual staged "
                                  "model");
                            ++receipts;
                        }
                    }
                    check(receipts == 4, "All matching batched receipts replay before preview");
                }
                check(turn < 5, "Provider stops at preview");
                const QJsonObject reasoning{{"type", "reasoning"},
                                            {"id", "rs_" + QString::number(turn)},
                                            {"summary", QJsonArray{}},
                                            {"encrypted_content", "opaque-fixture"}};
                Response response;
                QJsonArray output{reasoning};
                if (turn == 3)
                    for (int i = 0; i < 4; ++i)
                        output.append(call(body, name, args, 30 + i));
                else
                    output.append(call(body, name, args, turn));
                auto result = completed(output);
                if (plan)
                    result["usage"] = QJsonObject{
                        {"input_tokens", 23034},
                        {"input_tokens_details", QJsonObject{{"cached_tokens", 20000}}},
                        {"output_tokens", 12000},
                        {"output_tokens_details", QJsonObject{{"reasoning_tokens", 11000}}}};
                response.bytes = json(result);
                return response;
            };
            auto settings = options();
            if (plan)
                settings.limits = chatGptAssistantLimits();
            if (legacyBudget)
                settings.limits.totalReportedTokens = 262144;
            OpenAiProvider provider(std::make_unique<AssistantTask>(model.backend(), settings),
                                    "sk-fixture-only", &network, nullptr, plan);
            provider.start();
            wait([&] {
                return provider.task().phase() == Phase::PreviewReady ||
                       provider.task().phase() == Phase::Failed;
            });
            if (legacyBudget) {
                const auto error = provider.task().result().value("error").toObject();
                check(provider.task().phase() == Phase::Failed &&
                          error.value("budget") == "remaining task tokens" &&
                          error.value("used") == "23034 input + 163 output" &&
                          provider.task().result().value("applied") == false,
                      "Previous plan budget reproduces the reported failure during staged work");
                check(model.state().value("revision") == state.value("revision"),
                      "Exhausted plan budget leaves the public document unchanged");
                continue;
            }
            check(provider.task().phase() == Phase::PreviewReady &&
                      network.calls == (plan ? 16 : 5),
                  QJsonDocument(provider.task().result()).toJson().constData());
            check(
                provider.task().result().value("reportedTokens") == (plan ? 430337 : 700),
                "All sixteen plan turns fit with repeated input, cached input and reasoning usage");
            check(provider.task().result().value("applied") == false,
                  "Provider never applies edits");
            const auto input = network.requests.back().value("input").toArray();
            int reasoning{};
            for (auto item : input)
                if (item.toObject().value("type") == "reasoning")
                    ++reasoning;
            check(reasoning == 4, "All prior opaque reasoning items replayed");
            provider.apply();
            check(provider.task().result().value("applied") == true,
                  "Host Apply commits actual staged result");
            provider.cancel();
            check(provider.task().result().value("applied") == true,
                  "Late cancellation preserves applied receipt");
        }
        // ADR 0167 scripted replay through the corpus v4 transport (native session, plan
        // stream and limits): a batched transaction.inspect + transaction.diff reply is
        // accepted; an inspect + transaction.preview batch is answered with tool errors naming
        // the rule and the task continues to a sealed preview and host Apply. The capped run
        // shows that repeated refusals end at the turn budget, never in a loop.
        for (int run : {0, 1, 2}) {
            const bool plan = run != 1;
            const bool capped = run == 2;
            Document document;
            NativeAssistantSession model(document,
                                         files.path() + "/refusal-" + QString::number(run));
            Network network;
            network.plan = plan;
            const auto state = model.state();
            const auto documentId = state.value("documentId");
            const QString rule =
                "Only read-only inspections may be batched; send transaction.preview alone.";
            QString draft, proposed;
            auto receipts = [](const QJsonObject &body) {
                QMap<QString, QJsonObject> result;
                for (const auto &value : body.value("input").toArray()) {
                    const auto item = value.toObject();
                    if (item.value("type") == "function_call_output")
                        result[item.value("call_id").toString()] =
                            QJsonDocument::fromJson(item.value("output").toString().toUtf8())
                                .object();
                }
                return result;
            };
            network.respond = [&](QJsonObject body, int turn) {
                const auto seen = receipts(body);
                const QJsonObject base{
                    {"apiVersion", 1}, {"documentId", documentId}, {"transactionId", draft}};
                auto inspect = base;
                inspect["operation"] = "transaction.inspect";
                inspect["request"] = QJsonObject{{"apiVersion", 1},
                                                 {"query", "document.describe"},
                                                 {"documentId", documentId},
                                                 {"expectedRevision", proposed}};
                auto preview = base;
                preview["operation"] = "transaction.preview";
                preview["expectedVersion"] = 1;
                QJsonArray output;
                if (turn == 0)
                    output.append(call(body, "transaction.begin",
                                       {{"apiVersion", 1},
                                        {"documentId", documentId},
                                        {"operation", "transaction.begin"},
                                        {"expectedRevision", state.value("revision")}},
                                       0));
                else if (turn == 1) {
                    draft = seen.value("call_0").value("data").toObject().value("transactionId")
                                .toString();
                    check(!draft.isEmpty(), "Replay draft begins");
                    output.append(call(
                        body, "transaction.apply",
                        {{"apiVersion", 1},
                         {"documentId", documentId},
                         {"operation", "transaction.apply"},
                         {"transactionId", draft},
                         {"expectedVersion", 0},
                         {"operationId", "face"},
                         {"commands",
                          QJsonArray{QJsonObject{
                              {"command", "geometry.face"},
                              {"loops", QJsonArray{QJsonArray{
                                            QJsonArray{0, 0, 0}, QJsonArray{2, 0, 0},
                                            QJsonArray{2, 3, 0}, QJsonArray{0, 3, 0}}}}}}}},
                        1));
                } else if (turn == 2) {
                    const auto applied = seen.value("call_1");
                    check(applied.value("isError") == false, "Replay draft applies the face");
                    proposed =
                        applied.value("data").toObject().value("proposedRevision").toString();
                    inspect["request"] = QJsonObject{{"apiVersion", 1},
                                                     {"query", "document.describe"},
                                                     {"documentId", documentId},
                                                     {"expectedRevision", proposed}};
                    auto diff = base;
                    diff["operation"] = "transaction.diff";
                    diff["limit"] = 100;
                    output.append(call(body, "transaction.inspect", inspect, 20));
                    output.append(call(body, "transaction.diff", diff, 21));
                } else if (turn == 3 || capped) {
                    if (turn == 3)
                        for (const auto *id : {"call_20", "call_21"})
                            check(seen.value(id).value("isError") == false,
                                  "Batched transaction.inspect and transaction.diff are accepted");
                    output.append(call(body, "transaction.inspect", inspect, 10 * turn));
                    output.append(call(body, "transaction.preview", preview, 10 * turn + 1));
                } else {
                    check(turn == 4, "Replay ends at preview");
                    for (const auto *id : {"call_30", "call_31"}) {
                        const auto receipt = seen.value(id);
                        const auto data = receipt.value("data").toObject();
                        check(receipt.value("isError") == true &&
                                  data.value("code") == "BATCH_NOT_READ_ONLY" &&
                                  data.value("message") == rule &&
                                  data.value("executed") == false && data.value("refused") == true,
                              "The illegal batch is answered with a tool error naming the rule");
                    }
                    output.append(call(body, "transaction.preview", preview, 40));
                }
                auto result = completed(output);
                if (plan)
                    result["usage"] = QJsonObject{{"input_tokens", 23034},
                                                  {"output_tokens", 163}};
                return Response{200, 1, json(result)};
            };
            auto settings = options();
            if (plan)
                settings.limits = chatGptAssistantLimits();
            if (capped)
                settings.limits.turns = 6;
            OpenAiProvider provider(std::make_unique<AssistantTask>(model.backend(), settings),
                                    "sk-fixture-only", &network, nullptr, plan);
            QStringList statuses;
            QObject::connect(&provider, &AssistantNetworkProvider::changed, [&] {
                if (statuses.isEmpty() || statuses.last() != provider.status())
                    statuses.append(provider.status());
            });
            provider.start();
            wait([&] {
                return provider.task().phase() == Phase::PreviewReady ||
                       provider.task().phase() == Phase::Failed;
            });
            const auto result = provider.task().result();
            check(statuses.contains("Provider reply refused: " + rule) &&
                      statuses.contains("Waiting for OpenAI to correct a refused reply") &&
                      !statuses.join('\n').contains("incomplete or invalid"),
                  "Host status names the refused rule while the task continues");
            if (capped) {
                check(provider.task().phase() == Phase::Failed && network.calls == 6 &&
                          result.value("error").toObject().value("code") == "BUDGET_EXCEEDED" &&
                          result.value("error").toObject().value("message").toString().contains(
                              "Provider turn budget exhausted (6 of 6 turns") &&
                          result.value("toolCalls") == 10 && result.value("applied") == false &&
                          model.state().value("revision") == state.value("revision"),
                      "Repeated refused replies consume turns and end at the turn budget");
                continue;
            }
            check(provider.task().phase() == Phase::PreviewReady && network.calls == 5 &&
                      result.value("providerTurns") == 5 && result.value("toolCalls") == 7,
                  "A refused reply uses its turn and tool calls, then the task reaches preview");
            check(model.state().value("revision") == state.value("revision"),
                  "A refused reply publishes nothing");
            provider.apply();
            check(provider.task().result().value("applied") == true && document.bodies().size() == 1,
                  "Host Apply commits the corrected task");
        }
        for (const auto &stream : std::vector<QByteArray>{
                 "data: {\"type\":\"response.output_text.delta\",\"delta\":\"partial\"}\n\n",
                 "data: {\"type\":\"response.incomplete\"}\n\n",
                 "data: "
                 "{\"type\":\"response.failed\",\"response\":{\"error\":{\"code\":\"subscription_"
                 "sharing_usage_limit_exceeded\"}}}\n\n",
                 "data: [DONE]\n\n", QByteArray(1024 * 1024 + 1, 'x')}) {
            auto model = session();
            Network network;
            network.plan = true;
            network.rawStream = true;
            network.respond = [&](QJsonObject, int) { return Response{200, 1, stream}; };
            OpenAiProvider provider(std::make_unique<AssistantTask>(*model, options()),
                                    "sk-fixture-only", &network, nullptr, true);
            provider.start();
            wait([&] { return provider.task().phase() == Phase::Failed; });
            check(provider.task().result().value("applied") == false && network.calls == 1,
                  "Failed/partial stream executes no edits and does not retry");
            if (stream.contains("usage_limit"))
                check(provider.status().contains("Manage ChatGPT usage"),
                      "Plan limit directs to usage settings");
        }
        {
            auto model = session();
            AssistantTask task(*model, options());
            const auto request = task.nextRequest();
            check(request.has_value(), "Task request for codec");
            OpenAiConversation codec(true);
            const auto body = codec.request(*request);
            auto fn = call(body, "document.describe", {}, 0);
            fn["namespace"] = "other";
            // ADR 0167: a foreign namespace is refused as an unadvertised tool, not fatal.
            check(codec.decode(json(completed(QJsonArray{fn}))).calls.at(0).refusal.value("code") ==
                      "UNKNOWN_TOOL",
                  "A foreign tool namespace is refused");
            const auto good = "data: " +
                              json({{"type", "response.completed"},
                                    {"response", completed(QJsonArray{message()})}}) +
                              "\n\n";
            auto event = [](const QJsonObject &e) { return "data: " + json(e) + "\n\n"; };
            const auto item = message();
            const auto done =
                event({{"type", "response.output_item.done"}, {"output_index", 0}, {"item", item}});
            const auto emptyEnd =
                event({{"type", "response.completed"}, {"response", completed({})}});
            check(QJsonDocument::fromJson(completedOpenAiStream(done + emptyEnd))
                          .object()
                          .value("output")
                          .toArray() == QJsonArray{item},
                  "Reconstruct terminal envelope from completed output items");
            rejects([&] { completedOpenAiStream(done); });
            rejects([&] { completedOpenAiStream(done + done + emptyEnd); });
            rejects([&] {
                completedOpenAiStream(event({{"type", "response.output_item.done"},
                                             {"output_index", 1},
                                             {"item", item}}) +
                                      emptyEnd);
            });
            rejects([&] {
                completedOpenAiStream(event({{"type", "response.output_item.added"},
                                             {"output_index", 0},
                                             {"item", item}}) +
                                      emptyEnd);
            });
            rejects([&] {
                completedOpenAiStream(
                    done + event({{"type", "response.completed"},
                                  {"response", completed(QJsonArray{message("different")})}}));
            });
            const auto start = event(
                {{"type", "response.output_item.added"}, {"output_index", 0}, {"item", item}});
            check(!completedOpenAiStream(start + done + emptyEnd).isEmpty(),
                  "Matching item start and completion accepted");
            rejects([&] { completedOpenAiStream(start + start + done + emptyEnd); });
            auto differentItem = item;
            differentItem["id"] = "other-item";
            rejects([&] {
                completedOpenAiStream(start +
                                      event({{"type", "response.output_item.done"},
                                             {"output_index", 0},
                                             {"item", differentItem}}) +
                                      emptyEnd);
            });
            rejects([&] {
                completedOpenAiStream(done +
                                      event({{"type", "response.output_item.done"},
                                             {"output_index", 1},
                                             {"item", item}}) +
                                      emptyEnd);
            });
            rejects([&] {
                completedOpenAiStream(event({{"type", "response.created"},
                                             {"response", QJsonObject{{"id", "other-response"}}}}) +
                                      done + emptyEnd);
            });
            check(!completedOpenAiStream(good).isEmpty(), "Completed SSE accepted");
            rejects([&] { completedOpenAiStream(good + good); });
            rejects([&] { completedOpenAiStream(good + "data: {\"type\":\"error\"}\n\n"); });
        }
        for (const auto mode :
             {"text", "429", "503", "408", "auth", "redirect", "invalid", "incomplete", "usage",
              "unknown-tool", "arguments", "oversize", "parallel"}) {
            auto model = session();
            Network network;
            network.respond = [&](QJsonObject body, int turn) {
                Response response;
                const QString kind(mode);
                if ((kind == "429" || kind == "503" || kind == "408") && turn == 0) {
                    response.code = kind.toInt();
                    response.retryAfter = "0";
                    return response;
                }
                if (kind == "auth")
                    response.code = 401;
                if (kind == "redirect")
                    response.code = 302;
                if (kind == "invalid")
                    response.bytes = "invalid json sk-fixture-only";
                if (kind == "oversize")
                    response.bytes = QByteArray(1024 * 1024 + 1, 'x');
                if (kind == "incomplete") {
                    auto root = completed(QJsonArray{message()});
                    root["status"] = "incomplete";
                    response.bytes = json(root);
                }
                if (kind == "usage") {
                    auto root = completed(QJsonArray{message()});
                    root.remove("usage");
                    response.bytes = json(root);
                }
                if (kind == "unknown-tool" || kind == "arguments" || kind == "parallel") {
                    auto item = call(body, "document.describe", {}, turn);
                    if (kind == "unknown-tool")
                        item["name"] = "shell";
                    if (kind == "arguments")
                        item["arguments"] = "not JSON";
                    QJsonArray items{item};
                    if (kind == "parallel")
                        items.append(item);
                    response.bytes = json(completed(items));
                }
                return response;
            };
            OpenAiProvider provider(std::make_unique<AssistantTask>(*model, options()),
                                    "sk-fixture-only", &network);
            provider.start();
            wait([&] {
                return provider.task().phase() == Phase::Completed ||
                       provider.task().phase() == Phase::Failed;
            });
            const QString kind(mode);
            const bool success = kind == "text" || kind == "429" || kind == "503" || kind == "408";
            check((provider.task().phase() == Phase::Completed) == success,
                  "HTTP/protocol failures translated honestly");
            check(provider.task().result().value("applied") == false,
                  "Text/error does not prove edits");
            check(!provider.status().contains("sk-fixture-only") &&
                      !json(provider.task().result()).contains("sk-fixture-only"),
                  "Raw error body and credential absent from status/results");
            if (kind == "429" || kind == "503" || kind == "408")
                check(network.calls == 2, "Single controlled task retry");
        }
        {
            auto model = session();
            Network network;
            network.respond = [](QJsonObject, int) {
                Response r;
                r.delay = -1;
                return r;
            };
            OpenAiProvider provider(std::make_unique<AssistantTask>(*model, options()),
                                    "sk-fixture-only", &network);
            provider.start();
            provider.cancel();
            check(provider.task().phase() == Phase::Canceled && network.aborts == 1,
                  "In-flight cancel aborts network");
        }
        {
            auto model = session();
            Network network;
            network.respond = [](QJsonObject, int) {
                Response r;
                r.code = 429;
                return r;
            };
            OpenAiProvider provider(std::make_unique<AssistantTask>(*model, options()),
                                    "sk-fixture-only", &network);
            provider.start();
            wait([&] { return provider.task().phase() == Phase::Failed; });
            check(network.calls == 3, "Retries bounded by engine budget");
        }
        for (const auto mode : {"stale", "deadline", "destroy", "closed"}) {
            Network network;
            network.respond = [](QJsonObject, int) {
                Response r;
                r.delay = -1;
                return r;
            };
            QJsonObject state{
                {"documentId", "bound-document"}, {"revision", "0"}, {"outcomeUncertain", false}};
            auto now = AssistantTask::Clock::now();
            bool closed{};
            AssistantBackend backend{[](const QJsonObject &) { return QJsonObject{}; },
                                     [&] {
                                         check(!closed, "Closed document backend");
                                         return state;
                                     }};
            auto task = std::make_unique<AssistantTask>(backend, options(), [&] { return now; });
            auto provider =
                std::make_unique<OpenAiProvider>(std::move(task), "sk-fixture-only", &network);
            provider->start();
            if (QString(mode) == "stale")
                state["revision"] = "1";
            else if (QString(mode) == "deadline")
                now += std::chrono::seconds(301);
            else if (QString(mode) == "closed")
                closed = true;
            else
                provider.reset();
            wait([&] { return network.aborts == 1; });
            if (provider)
                check(provider->task().phase() == (QString(mode) == "stale"    ? Phase::Stale
                                                   : QString(mode) == "closed" ? Phase::Canceled
                                                                               : Phase::Failed),
                      "Stale source/task deadline aborts pending HTTP without replay");
        }
        auto helper = [&](QString mode) {
            const auto path = files.path() + "/secret-" + mode;
            write(path, ("#!/bin/sh\nexec " + quote(app.applicationFilePath()) + " --fake-secret " +
                         quote(mode) + " \"$@\"\n")
                            .toUtf8());
            check(
                QFile::setPermissions(path, QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner),
                "Fixture helper executable");
            return path;
        };
        using Credential = OpenAiCredentialStore;
        for (auto mode : {"success", "missing", "error", "flood", "hang"}) {
            Credential credential;
            credential.setExecutable(helper(mode));
            credential.lookup();
            if (QString(mode) == "hang")
                credential.cancel();
            wait([&] { return credential.phase() != Credential::Phase::Working; });
            if (QString(mode) == "success") {
                check(credential.phase() == Credential::Phase::Available &&
                          credential.takeCredential() == "sk-fixture-only",
                      "Credential lookup remains private");
                rejects([&] { credential.takeCredential(); });
                credential.store("sk-fixture-only");
                wait([&] { return credential.phase() != Credential::Phase::Working; });
                check(credential.phase() == Credential::Phase::Stored,
                      "Store passes exact secret through stdin");
                credential.clear();
                wait([&] { return credential.phase() != Credential::Phase::Working; });
                check(credential.phase() == Credential::Phase::Cleared, "Credential clear");
            } else if (QString(mode) == "missing")
                check(credential.phase() == Credential::Phase::Missing, "Missing key");
            else if (QString(mode) == "hang")
                check(credential.phase() == Credential::Phase::Canceled, "Credential cancellation");
            else
                check(credential.phase() == Credential::Phase::Failed,
                      "Helper errors never expose diagnostics");
        }
        Credential absent;
        absent.setExecutable(files.path() + "/absent");
        absent.lookup();
        check(absent.phase() == Credential::Phase::Unavailable,
              "Missing secret service helper does not fall back to plaintext");
        rejects([&] { absent.store("bad\ncredential"); });
        std::cout << "OpenAI wire mapping, actual staged tools, bounded HTTP failures and private "
                     "credential helper passed\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
