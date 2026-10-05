// Explicit opt-in live corpus. Only synthetic models are sent to the chosen loopback runtime.
#include "automation/commands.hpp"
#include "core/entity_measure.hpp"
#include "integrations/ollama_provider.hpp"
#include "io/document_io.hpp"
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QJsonDocument>
#include <QNetworkReply>
#include <QSaveFile>
#include <QTemporaryDir>
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
            (*record)["response"] = QJsonDocument::fromJson(*bytes).object();
            exchanges.append(*record);
        });
        return reply;
    }
};
int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    try {
        const auto args = app.arguments();
        check(args.size() == 5, "Usage: local_provider_trial ENDPOINT MODEL "
                                "measure|room|resize|unsupported|unavailable REPORT.json");
        const auto trial = args[3];
        check(
            QStringList{"measure", "room", "resize", "unsupported", "unavailable"}.contains(trial),
            "Unknown corpus task");
        OllamaConfiguration config;
        config.endpoint = QUrl(args[1]);
        config.model = args[2];
        validateOllamaConfiguration(config);
        QTemporaryDir files;
        check(files.isValid(), "Synthetic corpus directory");
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
        options.provider = "Ollama";
        options.model = config.model;
        options.limits.seconds = 300;
        options.limits.outputTokens = 768;
        options.limits.turns = 12;
        if (trial == "room") {
            options.prompt = "Create a 6 m by 4 m room, 2.7 m high, with two 1.2 m wide windows "
                             "using the default room assembly. Inspect and measure the private "
                             "result, then present a preview. Do not claim it is applied.";
            options.allowedCommands = {"assembly.room"};
        } else if (trial == "resize") {
            options.prompt =
                "Widen only window body " + QString::number(first) +
                " from 1.2 m to 1.4 m, keeping its 80 mm frame members, height, sill and center. "
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
        OllamaProvider provider(std::make_unique<AssistantTask>(backend, options), config,
                                &network);
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
            if (request.value("query") == "measure.entity" && near(receipt.value("result")
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
        QJsonObject report{{"trial", trial},
                           {"endpoint", config.endpoint.toString()},
                           {"model", config.model},
                           {"contextTokens", config.contextTokens},
                           {"threads", config.threads},
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
