// Opt-in reproduction of native assistant requests on a disposable, empty document.
// Usage: assistant_workflow_trial MODEL PROMPT.txt NEW_REPORT.json
#include "automation/native_assistant_session.hpp"
#include "core/entity_measure.hpp"
#include "integrations/chatgpt_auth.hpp"
#include "integrations/openai_provider.hpp"
#include "io/document_io.hpp"
#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QJsonDocument>
#include <QSaveFile>
#include <QSettings>
#include <QThread>
#include <iostream>
using namespace sketchy;
void check(bool ok, const char *message) {
    if (!ok)
        throw std::runtime_error(message);
}
QJsonArray measurements(const Document &document) {
    QJsonArray result;
    const auto point = [](Vec3 p) { return QJsonArray{p.x, p.y, p.z}; };
    for (const auto &[id, body] : document.bodies()) {
        const auto metric = measureEntity(document, {id, SelectionKind::Body, 0});
        if (metric.world.bounds)
            result.append(QJsonObject{{"body", QString::number(id)},
                                      {"name", QString::fromStdString(body->name)},
                                      {"parent", QString::number(body->parent)},
                                      {"low", point(metric.world.bounds->low)},
                                      {"high", point(metric.world.bounds->high)},
                                      {"dimensions", point(metric.world.bounds->dimensions())}});
    }
    return result;
}
int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    try {
        const auto args = app.arguments();
        if (args.size() == 3 && args[1] == "--inspect") {
            std::cout << QJsonDocument(measurements(loadDocument(args[2]))).toJson().toStdString();
            return 0;
        }
        check(args.size() == 4, "Usage: assistant_workflow_trial MODEL PROMPT.txt NEW_REPORT.json");
        check(!QFile::exists(args[3]), "Report must be a fresh path");
        const QString root = args[3] + ".files";
        check(!QFile::exists(root) && QDir().mkdir(root), "Fresh trial directory");
        QFile::setPermissions(root, QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner);
        QFile prompt(args[2]);
        check(prompt.open(QIODevice::ReadOnly) && prompt.size() <= 4096, "Bounded prompt file");
        Document document;
        Selection selection;
        selection.sync(document);
        NativeAssistantSession session(document, root + "/outcomes", &selection);
        AssistantTask::Options options;
        options.prompt = QString::fromUtf8(prompt.readAll()).trimmed();
        options.provider = "OpenAI";
        options.model = args[1];
        options.remote = options.remoteContextApproved = true;
        options.clarificationAvailable = true;
        options.allowedCommands = nativeAssistantCommands();
        options.limits = chatGptAssistantLimits();
        options.context =
            QJsonArray{QJsonObject{{"apiVersion", 1},
                                   {"query", "document.describe"},
                                   {"documentId", QString::fromStdString(document.identity())},
                                   {"expectedRevision", QString::number(document.revision())}}};
        ChatGptAuth auth;
        QByteArray access;
        QObject::connect(&auth, &ChatGptAuth::credentialReady,
                         [&](const QByteArray &value) { access = value; });
        auth.prepare(
            QSettings("SketchyUp", "SketchyUp").value("assistant/chatgptAccount").toString(),
            options.model);
        QElapsedTimer lookup;
        lookup.start();
        while (auth.busy() && lookup.elapsed() < 123000) {
            QCoreApplication::processEvents();
            QThread::msleep(5);
        }
        if (access.isEmpty())
            throw std::runtime_error(auth.status().toStdString());
        OpenAiProvider provider(std::make_unique<AssistantTask>(session.backend(), options), access,
                                nullptr, nullptr, true);
        access.fill('\0');
        QElapsedTimer timer;
        timer.start();
        auto record = [&] {
            QJsonObject report{{"prompt", options.prompt},
                               {"model", options.model},
                               {"taskSeconds", options.limits.seconds},
                               {"elapsedMs", timer.elapsed()},
                               {"result", provider.task().result()},
                               {"activity", provider.task().transcript()}};
            QSaveFile out(args[3]);
            check(out.open(QIODevice::WriteOnly), "Open diagnostic report");
            out.setPermissions(QFile::ReadOwner | QFile::WriteOwner);
            const auto bytes = QJsonDocument(report).toJson();
            check(out.write(bytes) == bytes.size() && out.commit(), "Retain diagnostic report");
        };
        int lastTurn = -1;
        QObject::connect(&provider, &AssistantNetworkProvider::changed, [&] {
            record();
            const auto result = provider.task().result();
            const auto turn = result.value("providerTurns").toInt();
            if (turn != lastTurn) {
                std::cerr << "elapsed " << timer.elapsed() << " ms, turn " << turn
                          << ", reported tokens " << result.value("reportedTokens").toInteger()
                          << '\n';
                lastTurn = turn;
            }
        });
        provider.start();
        using Phase = AssistantTask::Phase;
        while (provider.task().phase() == Phase::Ready ||
               provider.task().phase() == Phase::AwaitingProvider ||
               provider.task().phase() == Phase::Backoff) {
            QCoreApplication::processEvents();
            QThread::msleep(5);
        }
        if (provider.task().phase() == Phase::PreviewReady)
            provider.apply(); // Host approval of this disposable synthetic fixture only.
        record();
        const auto result = provider.task().result();
        std::cout << "phase=" << result.value("phase").toString().toStdString()
                  << " turns=" << result.value("providerTurns").toInt()
                  << " tokens=" << result.value("reportedTokens").toInteger() << '\n';
        if (result.value("applied") == true) {
            saveDocument(document, root + "/result.sketchyup");
            return 0;
        }
        return 1;
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 2;
    }
}
