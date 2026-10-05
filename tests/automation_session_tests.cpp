#include "automation/session.hpp"
#include <QBuffer>
#include <QCoreApplication>
#include <QFile>
#include <QJsonDocument>
#include <QProcess>
#include <QProcessEnvironment>
#include <QTemporaryDir>
#include <iostream>
using namespace sketchy;
void check(bool ok, const char *message) {
    if (!ok)
        throw std::runtime_error(message);
}
QJsonObject op(QString name, QJsonObject fields = {}) {
    fields["apiVersion"] = 1;
    fields["operation"] = name;
    return fields;
}
QJsonObject scoped(QString name, const QJsonObject &info, QJsonObject fields = {}) {
    fields["documentId"] = info["documentId"];
    return op(name, fields);
}
QJsonObject call(AutomationSession &session, QJsonObject request) {
    return session.respond({{"id", "test"}, {"request", request}});
}
void rejected(const QJsonObject &response, QString code) {
    check(response["ok"] == false && response["error"].toObject()["code"] == code,
          qPrintable(QString("Expected %1: %2")
                         .arg(code, QString::fromUtf8(
                                        QJsonDocument(response).toJson(QJsonDocument::Compact)))));
}
template <class F> void rejects(QString code, F action) {
    try {
        action();
    } catch (const std::exception &e) {
        check(automationFailure(e)["code"] == code, e.what());
        return;
    }
    throw std::runtime_error("Expected failure: " + code.toStdString());
}
QJsonObject units() { return {{"command", "document.units"}, {"units", "mm"}}; }
QJsonObject prepare(AutomationSession &session, const QJsonObject &info) {
    const auto begun = session.execute(
        scoped("transaction.begin", info, {{"expectedRevision", info["revision"]}}));
    const auto id = begun["transactionId"].toString();
    session.execute(scoped("transaction.apply", info,
                           {{"transactionId", id},
                            {"expectedVersion", 0},
                            {"operationId", "units"},
                            {"commands", QJsonArray{units()}}}));
    return session.execute(
        scoped("transaction.preview", info, {{"transactionId", id}, {"expectedVersion", 1}}));
}
QJsonObject durable(QString name, const QJsonObject &info, const QJsonObject &sealed) {
    return scoped(name, info,
                  {{"requestId", sealed["requestId"]}, {"payloadHash", sealed["payloadHash"]}});
}
class Client {
  public:
    QProcess process;
    explicit Client(QStringList args) {
        auto env = QProcessEnvironment::systemEnvironment();
        env.remove("DISPLAY");
        env.remove("WAYLAND_DISPLAY");
        env.remove("QT_QPA_PLATFORM");
        process.setProcessEnvironment(env);
        process.start(QStringLiteral(CLI_PATH), args);
        check(process.waitForStarted(10000), "Start headless CLI");
    }
    ~Client() {
        if (process.state() != QProcess::NotRunning) {
            process.kill();
            process.waitForFinished();
        }
    }
    QJsonObject raw(QByteArray bytes) {
        check(process.write(bytes) == bytes.size() && process.waitForBytesWritten(10000),
              "Write CLI request");
        while (!process.canReadLine())
            check(process.waitForReadyRead(10000),
                  qPrintable(QString("CLI did not reply: %1")
                                 .arg(QString::fromUtf8(process.readAllStandardError()))));
        auto result = QJsonDocument::fromJson(process.readLine());
        check(result.isObject(), "CLI reply is JSON object");
        return result.object();
    }
    QJsonObject call(QJsonObject request) {
        return raw(QJsonDocument(QJsonObject{{"id", "client"}, {"request", request}})
                       .toJson(QJsonDocument::Compact) +
                   '\n');
    }
    void finish(int expected = 0) {
        process.closeWriteChannel();
        check((process.state() == QProcess::NotRunning || process.waitForFinished(10000)) &&
                  process.exitStatus() == QProcess::NormalExit && process.exitCode() == expected,
              qPrintable(QString("Unexpected CLI exit: %1 %2")
                             .arg(process.exitCode())
                             .arg(QString::fromUtf8(process.readAllStandardError()))));
    }
};
int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    try {
        QTemporaryDir files;
        check(files.isValid(), "Temporary session directory");
        QFile schema(QStringLiteral(SOURCE_DIR "/docs/api/headless-session-v1.json"));
        check(schema.open(QIODevice::ReadOnly) &&
                  QJsonDocument::fromJson(schema.readAll()).object() == sessionCapabilities(),
              "Installed session schema tracks live capabilities");
        const auto model = files.path() + "/model.sketchyup", outcomes = files.path() + "/outcomes";
        QJsonObject info, sealed, receipt;
        {
            AutomationSession session({{}, model, outcomes, true});
            info = session.execute(op("session.describe"));
            check(info["revision"] == "0" && info["selectionAvailable"] == false &&
                      QFile::exists(model),
                  "Explicit create persists identity with no display state");
            rejects("FILE_BUSY", [&] {
                AutomationSession competing({model, model, files.path() + "/other-outcomes"});
            });
            rejected(call(session, op("document.save",
                                      {{"documentId", "foreign"}, {"expectedRevision", "0"}})),
                     "WRONG_DOCUMENT");
            rejected(call(session, scoped("document.save", info,
                                          {{"expectedRevision", "0"}, {"path", "/tmp/unscoped"}})),
                     "INVALID_REQUEST");
            rejected(call(session, scoped("document.save", info, {{"expectedRevision", "1"}})),
                     "STALE_REVISION");
            sealed = prepare(session, info);
            receipt = session.execute(durable("transaction.commit", info, sealed));
            check(receipt["status"] == "committed", "Headless session commits shared transaction");
            const auto saved =
                session.execute(scoped("document.save", info, {{"expectedRevision", "1"}}));
            check(saved["status"] == "saved" && saved["dirty"] == false &&
                      !saved["sha256"].toString().isEmpty(),
                  "Explicit destination save confirms digest and clean state");
            check(loadDocument(model).displayUnits() == DisplayUnit::Millimeters,
                  "Saved model reopens with committed change");
            const auto latest = session.execute(op("session.describe"));
            const auto draft = session.execute(
                scoped("transaction.begin", latest, {{"expectedRevision", latest["revision"]}}));
            auto apply =
                scoped("transaction.apply", latest,
                       {{"transactionId", draft["transactionId"]},
                        {"expectedVersion", 0},
                        {"operationId", "back"},
                        {"commands",
                         QJsonArray{QJsonObject{{"command", "document.units"}, {"units", "m"}}}}});
            session.execute(apply);
            sealed = session.execute(
                scoped("transaction.preview", latest,
                       {{"transactionId", draft["transactionId"]}, {"expectedVersion", 1}}));
            session.close();
            session.close();
            rejected(call(session, op("session.describe")), "SESSION_CLOSED");
        }
        {
            AutomationSession session({model, {}, outcomes});
            check(session.execute(durable("transaction.status", info, sealed))["status"] ==
                      "aborted",
                  "Graceful close durably aborts pending work");
            rejected(call(session, scoped("document.save", info, {{"expectedRevision", "1"}})),
                     "UNSUPPORTED_CAPABILITY");
            rejected(
                session.respond({{"id", "x"}, {"request", op("session.describe")}, {"extra", 1}}),
                "INVALID_REQUEST");
            rejected(
                session.respond({{"id", QString(129, 'x')}, {"request", op("session.describe")}}),
                "INVALID_REQUEST");
            const auto invalid =
                QJsonDocument(QJsonObject{{"id", "bad"}, {"request", op("no.such.operation")}})
                    .toJson(QJsonDocument::Compact) +
                '\n';
            QBuffer in, out;
            in.setData(invalid);
            in.open(QIODevice::ReadOnly);
            out.open(QIODevice::WriteOnly);
            check(runAutomationStream(session, in, out) == 1 &&
                      QJsonDocument::fromJson(out.data()).object()["ok"] == false,
                  "Structured request errors cause nonzero stream exit");
        }
        {
            AutomationSession session({model, model, outcomes});
            QFile external(model);
            check(external.open(QIODevice::Append), "Simulate external file edit");
            external.write("changed");
            external.close();
            rejected(call(session, scoped("document.save", info, {{"expectedRevision", "1"}})),
                     "FILE_CHANGED");
            check(external.open(QIODevice::ReadOnly) && external.readAll().endsWith("changed"),
                  "Rejected save preserves external bytes");
        }
        {
            const auto target = files.path() + "/uncertain-save.sketchyup";
            AutomationSession session({{}, target, files.path() + "/save-outcomes", true});
            const auto before = session.execute(op("session.describe"));
            check(QFile::link(target, target + ".bak"), "Create conflicting backup link");
            rejected(call(session, scoped("document.save", before, {{"expectedRevision", "0"}})),
                     "SAVE_OUTCOME_UNKNOWN");
            check(session.execute(op("session.describe"))["saveState"] == "unknown",
                  "Unconfirmed save remains explicit");
            rejected(
                call(session, scoped("transaction.begin", before, {{"expectedRevision", "0"}})),
                "SAVE_OUTCOME_UNKNOWN");
            check(loadDocument(target).revision() == 0, "Failed save preserves source file");
        }
        rejects("INVALID_REQUEST", [&] { AutomationSession noTarget({{}, {}, outcomes}); });
        rejects("INVALID_PATH", [&] {
            AutomationSession privateTarget({{}, outcomes + "/bad.sketchyup", outcomes, true});
        });
        rejects("FILE_EXISTS", [&] { AutomationSession exists({{}, model, outcomes, true}); });
        const auto liveModel = files.path() + "/live.sketchyup",
                   liveOutcomes = files.path() + "/live-outcomes";
        QJsonObject liveInfo, liveSeal, liveReceipt;
        {
            Client cli({"--session", "--new", "--output", liveModel, "--outcomes", liveOutcomes});
            liveInfo = cli.call(op("session.describe"))["result"].toObject();
            check(!liveInfo["documentId"].toString().isEmpty(),
                  "Persistent CLI returns explicitly created document");
            const auto begun = cli.call(scoped("transaction.begin", liveInfo,
                                               {{"expectedRevision", "0"}}))["result"]
                                   .toObject();
            const auto id = begun["transactionId"].toString();
            QJsonObject face{
                {"command", "geometry.face"},
                {"loops", QJsonArray{QJsonArray{QJsonArray{0, 0, 0}, QJsonArray{2, 0, 0},
                                                QJsonArray{2, 3, 0}, QJsonArray{0, 3, 0}}}}};
            const auto applied = cli.call(scoped("transaction.apply", liveInfo,
                                                 {{"transactionId", id},
                                                  {"expectedVersion", 0},
                                                  {"operationId", "face"},
                                                  {"commands", QJsonArray{face}}}));
            check(applied["ok"] == true, "Headless CLI creates geometry privately");
            liveSeal = cli.call(scoped("transaction.preview", liveInfo,
                                       {{"transactionId", id}, {"expectedVersion", 1}}))["result"]
                           .toObject();
            liveReceipt = cli.call(durable("transaction.commit", liveInfo, liveSeal));
            check(liveReceipt["ok"] == true &&
                      liveReceipt["result"].toObject()["status"] == "committed",
                  "CLI commits geometry without display or provider");
            cli.finish(); // Deliberately leave the explicit file at its original baseline.
        }
        {
            Client denied({"--session", "--input", liveModel, "--outcomes", liveOutcomes});
            check(denied.process.waitForFinished(10000) && denied.process.exitCode() == 1,
                  "Older input requires explicit transaction recovery");
            check(QJsonDocument::fromJson(denied.process.readAllStandardError())
                      .object()["error"]
                      .isString(),
                  "Startup errors are structured");
        }
        const auto recovered = files.path() + "/recovered.sketchyup";
        {
            Client cli({"--session", "--input", liveModel, "--outcomes", liveOutcomes,
                        "--recover-latest", "--output", recovered});
            check(cli.call(durable("transaction.commit", liveInfo, liveSeal)) == liveReceipt,
                  "Reconnect retry returns exact original committed receipt");
            check(cli.call(scoped("document.save", liveInfo, {{"expectedRevision", "1"}}))["ok"] ==
                      true,
                  "Recovered transaction saves to selected copy");
            QJsonObject query{{"apiVersion", 1},
                              {"documentId", liveInfo["documentId"]},
                              {"expectedRevision", "1"},
                              {"query", "entities.query"}};
            const auto entities = cli.call(query);
            check(entities["ok"] == true && !entities["result"].toObject().contains("document"),
                  "Headless bounded inspection avoids full document dump");
            const auto savedModel = loadDocument(recovered);
            query["query"] = "measure.entity";
            query["target"] = inspectionReference(savedModel, savedModel.bodies().begin()->first);
            query["space"] = "world";
            const auto measurement = cli.call(query);
            check(measurement["ok"] == true &&
                      std::abs(
                          measurement["result"].toObject()["data"].toObject()["area"].toDouble() -
                          6.0) < tolerance,
                  "Created, recovered and saved geometry measures six square meters without a "
                  "display");
            rejected(cli.raw("not-json\n"), "INVALID_REQUEST");
            rejected(cli.call(scoped("transaction.begin", liveInfo, {{"expectedRevision", "0"}})),
                     "STALE_REVISION");
            check(cli.call(op("session.describe"))["ok"] == true,
                  "A request failure does not prevent later status inspection");
            cli.finish(1);
        }
        check(loadDocument(liveModel).revision() == 0 && loadDocument(recovered).revision() == 1,
              "Recovery preserves original file");
        {
            Client cli({"--session", "--input", recovered, "--outcomes", liveOutcomes});
            rejected(cli.raw(QByteArray(sessionWireBytes + 1, 'x') + '\n'), "LIMIT_EXCEEDED");
            cli.finish(1);
        }
        {
            Client cli({"--session", "--input", recovered, "--outcomes", liveOutcomes});
            rejected(cli.raw(QByteArray(65, '[') + QByteArray(65, ']') + '\n'), "LIMIT_EXCEEDED");
            cli.finish(1);
        }
        {
            Client cli({"--session", "--new", "--output", files.path() + "/unused.sketchyup",
                        "--outcomes", files.path() + "/unused-outcomes", "--script",
                        "forbidden.json"});
            check(cli.process.waitForFinished(10000) && cli.process.exitCode() == 1 &&
                      !QFile::exists(files.path() + "/unused.sketchyup"),
                  "Invalid mode fails before creating model");
        }
        std::cout << "Explicit headless sessions, bounded IPC, durable reconnect, scoped saves and "
                     "cleanup passed\n";
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
