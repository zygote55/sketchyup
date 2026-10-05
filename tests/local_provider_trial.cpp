// Explicit opt-in corpus. Only synthetic models are sent to the selected provider.
#include "automation/commands.hpp"
#include "core/entity_measure.hpp"
#include "integrations/chatgpt_auth.hpp"
#include "integrations/credential_store.hpp"
#include "integrations/ollama_provider.hpp"
#include "integrations/openai_provider.hpp"
#include "io/document_io.hpp"
#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QJsonDocument>
#include <QNetworkReply>
#include <QRegularExpression>
#include <QSaveFile>
#include <QSettings>
#include <QThread>
#include <iostream>
using namespace sketchy;
void check(bool ok, const char *message) {
    if (!ok)
        throw std::runtime_error(message);
}
QJsonObject batch(Document &doc, QJsonArray commands) {
    return executeBatch(doc, {{"apiVersion", 1},
                              {"documentId", QString::fromStdString(doc.identity())},
                              {"expectedRevision", QString::number(doc.revision())},
                              {"commands", commands}});
}
bool near(double a, double b) { return std::abs(a - b) < 1e-6; }
class TrialNetwork : public QNetworkAccessManager {
  public:
    QJsonArray exchanges;
    QNetworkReply *createRequest(Operation operation, const QNetworkRequest &request,
                                 QIODevice *outgoing) override {
        auto *reply = QNetworkAccessManager::createRequest(operation, request, outgoing);
        auto record = std::make_shared<QJsonObject>(QJsonObject{
            {"path", request.url().path()},
            {"request", outgoing ? QJsonDocument::fromJson(outgoing->peek(4 * 1024 * 1024)).object()
                                 : QJsonObject{}}});
        auto bytes = std::make_shared<QByteArray>();
        auto elapsed = std::make_shared<QElapsedTimer>();
        elapsed->start();
        connect(reply, &QNetworkReply::readyRead, this, [reply, bytes] {
            if (bytes->size() < 1024 * 1024)
                bytes->append(reply->peek(1024 * 1024 - bytes->size()));
        });
        connect(reply, &QNetworkReply::finished, this, [this, reply, record, bytes, elapsed] {
            (*record)["status"] =
                reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
            (*record)["elapsedMs"] = elapsed->elapsed();
            // Authentication errors can echo credential fragments. Retain status/timing,
            // not error bodies; successful responses contain only synthetic task data.
            if ((*record).value("status").toInt() >= 200 &&
                (*record).value("status").toInt() < 300) {
                if (record->value("request").toObject().value("stream") == true) {
                    try {
                        (*record)["response"] =
                            QJsonDocument::fromJson(completedOpenAiStream(*bytes)).object();
                    } catch (const std::exception &) {
                        (*record)["streamIncomplete"] = true;
                    }
                } else
                    (*record)["response"] = QJsonDocument::fromJson(*bytes).object();
            } else
                (*record)["responseOmitted"] = true;
            exchanges.append(*record);
        });
        return reply;
    }
};
int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    try {
        const auto args = app.arguments();
        check(args.size() == 5,
              "Usage: provider_trial ENDPOINT|--openai|--chatgpt MODEL|configured "
              "measure|room|resize|unsupported|unavailable REPORT.json");
        const auto trial = args[3];
        check(
            QStringList{"measure", "room", "resize", "unsupported", "unavailable"}.contains(trial),
            "Unknown corpus task");
        const bool plan = args[1] == "--chatgpt";
        const bool remote = plan || args[1] == "--openai";
        OllamaConfiguration config;
        config.endpoint = remote ? QUrl("https://api.openai.com/v1/responses") : QUrl(args[1]);
        config.model = remote && args[2] == "configured"
                           ? QSettings("SketchyUp", "SketchyUp")
                                 .value(plan ? "assistant/chatgptModel" : "assistant/openaiModel")
                                 .toString()
                           : args[2];
        if (remote)
            check(QRegularExpression("^[A-Za-z0-9][A-Za-z0-9_.:-]{0,127}$")
                      .match(config.model)
                      .hasMatch(),
                  "Configure an explicit OpenAI model ID");
        else
            validateOllamaConfiguration(config);
        const QDir files(QFileInfo(args[4]).absoluteFilePath() + ".files");
        check(!QFileInfo::exists(args[4]) && !files.exists(), "Use a fresh evidence path");
        check(QDir().mkdir(files.path()) &&
                  QFile::setPermissions(files.path(),
                                        QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner),
              "Create private retained synthetic corpus directory");
        Document fixture;
        Id first{}, second{}, room{}, wall{}, frame{}, glass{};
        if (trial == "resize") {
            const auto recipe = batch(fixture, {QJsonObject{{"command", "assembly.room"}}})
                                    .value("recipeOperations")
                                    .toArray()[0]
                                    .toObject();
            room = recipe.value("room").toString().toULongLong();
            wall = recipe.value("wall").toString().toULongLong();
            const auto windows = recipe.value("windows").toArray();
            first = windows[0].toObject().value("body").toString().toULongLong();
            second = windows[1].toObject().value("body").toString().toULongLong();
            for (const auto &[id, body] : fixture.bodies())
                if (body->parent == first) {
                    const auto role = std::get<std::string>(body->properties.at("recipe.role"));
                    if (role == "frame")
                        frame = id;
                    if (role == "glass")
                        glass = id;
                }
        } else if (trial != "room") {
            fixture.addFace({{{0, 0, 0}, {2, 0, 0}, {2, 3, 0}, {0, 3, 0}}});
            first = fixture.bodies().begin()->first;
        }
        const auto input = files.path() + "/input.sketchyup";
        const auto output = files.path() + "/output.sketchyup";
        saveDocument(fixture, input);
        AutomationSession session({input, output, files.path() + "/outcomes", false});
        auto state = [&] {
            return session.execute({{"apiVersion", 1}, {"operation", "session.describe"}});
        };
        QElapsedTimer timer;
        timer.start();
        QJsonArray receipts, activity;
        AssistantBackend backend{
            [&](const QJsonObject &request) {
                try {
                    auto result = session.execute(request);
                    receipts.append(QJsonObject{
                        {"request", request}, {"result", result}, {"elapsedMs", timer.elapsed()}});
                    return result;
                } catch (const std::exception &error) {
                    receipts.append(QJsonObject{{"request", request},
                                                {"error", automationFailure(error)},
                                                {"elapsedMs", timer.elapsed()}});
                    throw;
                }
            },
            state};
        AssistantTask::Options options;
        options.provider = remote ? "OpenAI" : "Ollama";
        options.remote = remote;
        // --openai explicitly authorizes this bounded synthetic corpus, not user models.
        options.remoteContextApproved = remote;
        options.model = config.model;
        options.limits.seconds = 300;
        options.limits.outputTokens = plan ? 8192 : remote ? 2048 : 768;
        options.limits.turns = 12;
        if (trial == "room") {
            options.prompt = "Create a 6 m by 4 m room, 2.7 m high, with two 1.2 m wide windows "
                             "using outer-frame window dimensions and the default room assembly. "
                             "Inspect and measure the private "
                             "result, then present a preview. Do not claim it is applied.";
            options.allowedCommands = {"assembly.room"};
        } else if (trial == "resize") {
            options.prompt =
                "Widen only window body " + QString::number(first) +
                " from 1.2 m to 1.4 m outer-frame width, keeping its 80 mm frame members, height, "
                "sill and center. "
                "Preserve the other window and resize the real wall opening. Use explicit instance "
                "scope. Inspect and measure the private result, then present a preview.";
            options.allowedCommands = {"assembly.window.resize"};
        } else if (trial == "unsupported") {
            options.prompt =
                "Subtract a sphere from this model with an exact solid Boolean difference. If this "
                "operation is unsupported, explain that and leave the model unchanged.";
            options.allowedCommands = {"assembly.room"};
        } else {
            options.prompt = "Use the inspection tools to measure the world-space area of body " +
                             QString::number(first) +
                             ". Report the area in square metres. Do not edit the model.";
        }
        const auto initial = state();
        const auto prompt = options.prompt;
        TrialNetwork network;
        std::unique_ptr<AssistantNetworkProvider> ownedProvider;
        if (plan) {
            // Use a separate manager: OAuth exchanges and model catalogs are never evidence.
            ChatGptAuth auth;
            QByteArray access;
            QObject::connect(&auth, &ChatGptAuth::credentialReady,
                             [&](const QByteArray &token) { access = token; });
            auth.prepare(
                QSettings("SketchyUp", "SketchyUp").value("assistant/chatgptAccount").toString(),
                config.model);
            QElapsedTimer lookup;
            lookup.start();
            while (auth.busy() && lookup.elapsed() < 123000) {
                QCoreApplication::processEvents();
                QThread::msleep(5);
            }
            check(!access.isEmpty(),
                  "ChatGPT session unavailable; connect in Assistant Preferences first");
            ownedProvider = std::make_unique<OpenAiProvider>(
                std::make_unique<AssistantTask>(backend, options), access, &network, nullptr, true);
            access.fill('\0');
        } else if (remote) {
            OpenAiCredentialStore credentials;
            credentials.lookup();
            QElapsedTimer lookup;
            lookup.start();
            while (credentials.phase() == OpenAiCredentialStore::Phase::Working &&
                   lookup.elapsed() < 63000) {
                QCoreApplication::processEvents();
                QThread::msleep(5);
            }
            check(credentials.phase() == OpenAiCredentialStore::Phase::Available,
                  "OpenAI credential unavailable; configure it in native Assistant Preferences");
            ownedProvider =
                std::make_unique<OpenAiProvider>(std::make_unique<AssistantTask>(backend, options),
                                                 credentials.takeCredential(), &network);
        } else
            ownedProvider = std::make_unique<OllamaProvider>(
                std::make_unique<AssistantTask>(backend, options), config, &network);
        auto &provider = *ownedProvider;
        QString lastStatus;
        QObject::connect(&provider, &AssistantNetworkProvider::changed, [&] {
            const auto status = provider.status();
            if (status != lastStatus) {
                activity.append(QJsonObject{{"elapsedMs", timer.elapsed()}, {"status", status}});
                std::cerr << trial.toStdString() << " " << timer.elapsed() << " ms "
                          << status.toStdString() << '\n';
                lastStatus = status;
            }
        });
        provider.start();
        using Phase = AssistantTask::Phase;
        while (provider.task().phase() == Phase::Ready ||
               provider.task().phase() == Phase::AwaitingProvider ||
               provider.task().phase() == Phase::Backoff) {
            QCoreApplication::processEvents();
            QThread::msleep(5);
            if (timer.elapsed() > 310000)
                provider.cancel();
        }
        const auto proposal = provider.task().result();
        // The trial harness explicitly acts as the host reviewer for its disposable fixture.
        if (provider.task().phase() == Phase::PreviewReady &&
            (trial == "room" || trial == "resize"))
            provider.apply();
        const auto result = provider.task().result();
        const auto endState = state();
        bool geometryVerified = false, inspectionVerified = false, unrelatedPreserved = false;
        QJsonObject measurements;
        for (const auto &value : receipts) {
            const auto receipt = value.toObject();
            const auto request = receipt.value("request").toObject();
            if (request.value("query") == "measure.entity" && request.value("space") == "world" &&
                request.value("target").toObject().value("body") == QString::number(first) &&
                near(receipt.value("result")
                         .toObject()
                         .value("data")
                         .toObject()
                         .value("area")
                         .toDouble(-1),
                     6))
                inspectionVerified = true;
        }
        if (result.value("applied") == true) {
            session.execute({{"apiVersion", 1},
                             {"operation", "document.save"},
                             {"documentId", endState.value("documentId")},
                             {"expectedRevision", endState.value("revision")}});
            auto actual = loadDocument(output);
            if (trial == "room") {
                for (const auto &[id, body] : actual.bodies()) {
                    const auto kind = body->properties.find("recipe.kind");
                    if (kind != body->properties.end() &&
                        std::holds_alternative<std::string>(kind->second) &&
                        std::get<std::string>(kind->second) == "room")
                        room = id;
                    const auto it = body->properties.find("recipe.role");
                    if (it != body->properties.end() &&
                        std::holds_alternative<std::string>(it->second)) {
                        if (std::get<std::string>(it->second) == "host-wall")
                            wall = id;
                    }
                }
                if (room && wall) {
                    const auto bounds =
                        measureEntity(actual, {room, SelectionKind::Body, 0}).local.bounds;
                    const auto volume =
                        measureEntity(actual, {wall, SelectionKind::Body, 0}).local.volume;
                    geometryVerified = bounds && volume &&
                                       length(bounds->dimensions() - Vec3{6, 4, 2.7}) < 1e-6 &&
                                       near(*volume, 9.888) && actual.instances().size() == 2;
                    measurements = {{"wallVolume", volume ? *volume : -1},
                                    {"windowInstances", qint64(actual.instances().size())}};
                }
            } else if (trial == "resize" && actual.bodies().contains(first) &&
                       actual.bodies().contains(second)) {
                const auto bounds =
                    measureEntity(actual, {first, SelectionKind::Body, 0}).local.bounds;
                const auto other =
                    measureEntity(actual, {second, SelectionKind::Body, 0}).local.bounds;
                const auto volume =
                    measureEntity(actual, {wall, SelectionKind::Body, 0}).local.volume;
                unrelatedPreserved = true;
                for (const auto &[id, body] : fixture.bodies())
                    if (id != first && id != wall && id != frame && id != glass)
                        unrelatedPreserved &=
                            actual.bodies().contains(id) && *actual.bodies().at(id) == *body;
                bool members = actual.bodies().contains(frame);
                if (members)
                    for (const auto &[id, p] : actual.bodies().at(frame)->surface.vertices)
                        members &=
                            (near(std::abs(p.x), .7) || near(std::abs(p.x), .62)) &&
                            (near(p.z, 0) || near(p.z, .08) || near(p.z, .92) || near(p.z, 1));
                geometryVerified =
                    bounds && other && volume && near(bounds->dimensions().x, 1.4) &&
                    near(bounds->dimensions().y, .1) && near(bounds->dimensions().z, 1) &&
                    near(other->dimensions().x, 1.2) && near(*volume, 9.848) && members &&
                    unrelatedPreserved &&
                    fixture.bodies().at(first)->transform == actual.bodies().at(first)->transform;
                measurements = {{"width", bounds ? bounds->dimensions().x : -1},
                                {"siblingWidth", other ? other->dimensions().x : -1},
                                {"wallVolume", volume ? *volume : -1},
                                {"frameMembersPreserved", members}};
            }
        }
        const bool unchanged = endState.value("revision") == initial.value("revision");
        provider.cancel();
        session.close();
        auto manual = loadDocument(input);
        const auto oldRevision = manual.revision();
        manual.addFace({{{4, 0, 0}, {5, 0, 0}, {5, 1, 0}, {4, 1, 0}}});
        const auto manualPath = files.path() + "/manual.sketchyup";
        saveDocument(manual, manualPath);
        const bool manualVerified = loadDocument(manualPath).revision() == oldRevision + 1;
        QJsonObject report{
            {"trial", trial},
            {"provider", options.provider},
            {"recordedAt", QDateTime::currentDateTimeUtc().toString(Qt::ISODate)},
            {"qtVersion", qVersion()},
            {"fixtureDirectory", files.path()},
            {"transcript", provider.task().transcript()},
            {"endpoint", config.endpoint.toString()},
            {"model", config.model},
            {"contextTokens", remote ? QJsonValue{} : QJsonValue(config.contextTokens)},
            {"threads", remote ? QJsonValue{} : QJsonValue(config.threads)},
            {"outputTokensPerTurn", options.limits.outputTokens},
            {"taskSeconds", options.limits.seconds},
            {"prompt", prompt},
            {"elapsedMs", timer.elapsed()},
            {"proposal", proposal},
            {"result", result},
            {"activity", activity},
            {"exchanges", network.exchanges},
            {"executedReceipts", receipts},
            {"geometryVerified", geometryVerified},
            {"inspectionVerified", inspectionVerified},
            {"unrelatedPreserved", unrelatedPreserved},
            {"revisionUnchanged", unchanged},
            {"manualEditSaveReopenVerified", manualVerified},
            {"measurements", measurements}};
        QSaveFile file(args[4]);
        check(file.open(QIODevice::WriteOnly), "Open evidence path");
        check(file.setPermissions(QFile::ReadOwner | QFile::WriteOwner), "Private evidence file");
        const auto bytes = QJsonDocument(report).toJson();
        check(file.write(bytes) == bytes.size() && file.commit(), "Write evidence");
        std::cout << QJsonDocument(QJsonObject{{"trial", trial},
                                               {"phase", result.value("phase")},
                                               {"applied", result.value("applied")},
                                               {"geometryVerified", geometryVerified},
                                               {"inspectionVerified", inspectionVerified},
                                               {"elapsedMs", timer.elapsed()}})
                         .toJson(QJsonDocument::Compact)
                         .toStdString()
                  << '\n';
        return 0; // A completed measurement is not a model-quality pass; inspect the report.
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
