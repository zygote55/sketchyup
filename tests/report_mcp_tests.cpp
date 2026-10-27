// R101.d (ADR 0175 §9, decision 12): the report surface on MCP, under the registry names.
// report.run is the bounded inspection query and report.schedule the session operation that writes
// the user-level schedule store. Each MCP result equals the session's, the data equals the
// committed golden byte for byte, and each refusal carries the session's code. Every schedule lives
// in a temporary user data folder (HOME and XDG_DATA_HOME), so no test touches the real user's
// store. That isolation holds on Linux only, so the store and CLI checks run there and are skipped
// elsewhere.
#include "automation/mcp.hpp"
#include "automation/report_commands.hpp"
#include "automation/report_schedules.hpp"
#include "automation/session.hpp"
#include <QCoreApplication>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QProcessEnvironment>
#include <QTemporaryDir>
#include <iostream>
#include <stdexcept>
#include <string>
#include <utility>

using namespace sketchy;

namespace {

void check(bool condition, const std::string &message) {
    if (!condition)
        throw std::runtime_error(message);
}

QString sourcePath(const QString &relative) {
    return QString::fromLocal8Bit(SOURCE_DIR) + QLatin1Char('/') + relative;
}

QByteArray readFile(const QString &path) {
    QFile file(path);
    check(file.open(QIODevice::ReadOnly), "Cannot read " + path.toStdString());
    return file.readAll();
}

// The compact bytes of a JSON object, the comparison V12 uses.
QByteArray compact(const QJsonObject &object) {
    return QJsonDocument(object).toJson(QJsonDocument::Compact);
}

// The quantities-a fixture, copied so that no test writes to the repository copy.
QString copyFixture(const QTemporaryDir &dir, const QString &name) {
    const auto path = dir.filePath(name);
    check(QFile::copy(sourcePath("tests/fixtures/reports/quantities-a.sketchyup"), path),
          "Copy the quantities-a fixture");
    return path;
}

// The two golden requests of report_output_tests: the whole document at full precision, and the
// visible material grouping at three decimals.
QJsonObject defaultRequest() {
    return {{"metrics", "all"}, {"groupBy", "none"}, {"precision", "full"}};
}

QJsonObject materialRequest() {
    return {{"metrics", "all"},
            {"groupBy", "material"},
            {"precision", 3},
            {"filters", QJsonObject{{"visibility", "visible"}}}};
}

QJsonObject metadata() {
    return {{"io.modelcontextprotocol/protocolVersion", mcpProtocolVersion},
            {"io.modelcontextprotocol/clientCapabilities", QJsonObject{}}};
}

QJsonObject rpc(QJsonValue id, QString method, QJsonObject params = {}) {
    params["_meta"] = metadata();
    return {{"jsonrpc", "2.0"}, {"id", id}, {"method", method}, {"params", params}};
}

QJsonObject toolCall(QJsonValue id, QString name, QJsonObject arguments) {
    return rpc(id, "tools/call", {{"name", name}, {"arguments", arguments}});
}

// A shared operation as an MCP call carries it: the discriminator and the version.
QJsonObject operation(QString name, QJsonObject fields = {}) {
    fields["apiVersion"] = 1;
    fields["operation"] = name;
    return fields;
}

// A shared query as an MCP call carries it: the discriminator and the version.
QJsonObject query(QString name, QJsonObject fields = {}) {
    fields["apiVersion"] = 1;
    fields["query"] = name;
    return fields;
}

// The one response to a request, matched by its ID.
QJsonObject respond(McpServer &server, const QJsonObject &request) {
    for (const auto &message : server.handle(request))
        if (message.toObject()["id"] == request["id"])
            return message.toObject();
    throw std::runtime_error("MCP request returns its response");
}

// A tool result: whether the tool reported a failure, and its structured content. A failure's
// structured content is the refusal, with its code and message.
struct Outcome {
    bool failed{};
    QJsonObject content;
    QString code() const { return content["code"].toString(); }
    QString message() const { return content["message"].toString(); }
};

Outcome invoke(McpServer &server, const QJsonObject &request) {
    const auto response = respond(server, request);
    check(!response.contains("error"), "A tool call returns a result, not a protocol error");
    const auto result = response["result"].toObject();
    return {result["isError"].toBool(), result["structuredContent"].toObject()};
}

// ADR 0175 §9 as realised: the registry names are listed, and no reports.* alias is.
void catalogue() {
    QJsonObject run, schedule;
    int reportTools = 0;
    for (const auto &value : mcpToolCatalog()) {
        const auto tool = value.toObject();
        const auto name = tool["name"].toString();
        check(!name.startsWith("reports."), "No reports.* alias is listed");
        if (name.startsWith("report.")) {
            ++reportTools;
            (name == "report.run" ? run : schedule) = tool;
        }
    }
    check(reportTools == 2, "Exactly report.run and report.schedule are listed");
    const auto runHints = run["annotations"].toObject();
    check(runHints["readOnlyHint"].toBool() && !runHints["destructiveHint"].toBool(),
          "report.run is a read-only query");
    const auto scheduleHints = schedule["annotations"].toObject();
    check(!scheduleHints["readOnlyHint"].toBool() && scheduleHints["destructiveHint"].toBool(),
          "report.schedule is a write");
    check(schedule["inputSchema"].toObject()["properties"].toObject().contains("action"),
          "report.schedule advertises its action");
    check(mcpToolCatalog().size() <= mcpToolLimit, "The MCP tool list stays within its bound");
}

// report.run over MCP is the session's query. Its data is the committed golden, byte for byte, for
// both golden requests (V12: the CLI, the session and MCP write the same bytes).
void runParity(const QTemporaryDir &dir) {
    AutomationSession session(
        {copyFixture(dir, "run.sketchyup"), {}, dir.filePath("run-outcomes")});
    McpServer server(session);
    const auto info = session.execute(operation("session.describe"));
    const std::pair<QString, QJsonObject> goldens[] = {
        {"quantities-a-default.json", defaultRequest()},
        {"quantities-a-material.json", materialRequest()}};
    for (const auto &[golden, request] : goldens) {
        const auto args = query("report.run", {{"documentId", info["documentId"]},
                                               {"expectedRevision", info["revision"]},
                                               {"request", request}});
        const auto mcp = invoke(server, toolCall(1, "report.run", args));
        check(!mcp.failed, "MCP report.run succeeds");
        check(compact(mcp.content) == compact(session.execute(args)),
              "MCP report.run is the session's report.run, byte for byte");
        // The session's data less its session-only invalid-row fields is the file's shape, which
        // is the committed golden (R101.e1, reportFileData).
        check(compact(reportFileData(mcp.content["data"].toObject())) ==
                  readFile(sourcePath("tests/fixtures/reports/" + golden)),
              "MCP report.run data is the committed golden, less the session-only fields");
    }
    // A stale revision, a foreign document and an out-of-bounds request are refused with the
    // session's codes.
    const auto args = [&](QJsonObject fields) {
        fields["request"] = defaultRequest();
        return query("report.run", fields);
    };
    const auto stale = invoke(
        server, toolCall(2, "report.run",
                         args({{"documentId", info["documentId"]}, {"expectedRevision", "999"}})));
    check(stale.failed && stale.code() == "STALE_REVISION",
          "MCP report.run refuses a stale revision");
    const auto foreign =
        invoke(server, toolCall(3, "report.run",
                                args({{"documentId", "00000000000000000000000000000000"},
                                      {"expectedRevision", info["revision"]}})));
    check(foreign.failed && foreign.code() == "WRONG_DOCUMENT",
          "MCP report.run refuses another document");
    const auto bounded = invoke(
        server, toolCall(4, "report.run",
                         query("report.run",
                               {{"documentId", info["documentId"]},
                                {"expectedRevision", info["revision"]},
                                {"request", QJsonObject{{"metrics", "all"}, {"precision", 99}}}})));
    check(bounded.failed && bounded.code() == "INVALID_REQUEST",
          "MCP report.run refuses a precision outside its bound");
}

#if defined(Q_OS_LINUX)

// Sandboxes the process: the schedule store lives under `home/data`, never in the real user's
// folder. XDG_DATA_HOME is the store's location on Linux; the application names are the ones the
// CLI and the desktop set.
void isolate(const QString &home) {
    qputenv("HOME", home.toUtf8());
    qputenv("XDG_DATA_HOME", (home + "/data").toUtf8());
    QCoreApplication::setOrganizationName("SketchyUp");
    QCoreApplication::setApplicationName("SketchyUp");
}

// A protocol error, which is not a tool result, carries its JSON-RPC code.
int protocolCode(McpServer &server, const QJsonObject &request) {
    return respond(server, request)["error"].toObject()["code"].toInt();
}

// A request in the §1 vocabulary: quantities by kind.
QJsonObject kindRequest() {
    return {{"metrics", QJsonArray{"counts"}},
            {"groupBy", "kind"},
            {"filters", QJsonObject{{"visibility", "all"}}}};
}

// A request that names one tag, so that it can refer to an ID the document does not have.
QJsonObject tagRequest(const QString &tag) {
    return {{"metrics", QJsonArray{"counts", "volumes"}},
            {"groupBy", "tag"},
            {"filters", QJsonObject{{"visibility", "all"}, {"tags", QJsonArray{tag}}}}};
}

// The schedule operation over MCP and the session share one store and one result shape. A schedule
// saved through either surface is described identically through the other.
void scheduleParity(const QTemporaryDir &dir) {
    isolate(dir.path());
    const auto store = reportScheduleStorePath();
    check(store.startsWith(dir.path()),
          "The schedule store is under the temporary user data folder");
    AutomationSession session(
        {copyFixture(dir, "schedule.sketchyup"), {}, dir.filePath("schedule-outcomes")});
    McpServer server(session);
    // A value, not a reference: operator[] on the temporary result would refer into it.
    const QJsonValue id = session.execute(operation("session.describe"))["documentId"];
    const auto scheduleCall = [&](QJsonObject fields) {
        fields["documentId"] = id;
        return operation("report.schedule", fields);
    };

    // MCP saves; the session describes the same record.
    const auto saved = invoke(
        server, toolCall(1, "report.schedule",
                         scheduleCall(
                             {{"action", "save"}, {"name", "Kinds"}, {"request", kindRequest()}})));
    check(!saved.failed && saved.content["status"] == "saved", "MCP saves a schedule");
    check(QFileInfo::exists(store), "The schedule is written to the temporary store");
    const auto described =
        session.execute(scheduleCall({{"action", "describe"}, {"name", "Kinds"}}));
    check(compact(saved.content["schedule"].toObject()) ==
              compact(described["schedule"].toObject()),
          "The session describes the schedule MCP saved, identically");
    const auto viaMcp =
        invoke(server, toolCall(2, "report.schedule",
                                scheduleCall({{"action", "describe"}, {"name", "Kinds"}})));
    check(!viaMcp.failed && compact(viaMcp.content) == compact(described),
          "MCP describes a schedule exactly as the session does");

    // The session saves; MCP lists both schedules, in name order.
    // Grouped by tag with no tag filter, so it names no ID that the fixture might not have.
    const QJsonObject byTag{{"metrics", QJsonArray{"counts"}},
                            {"groupBy", "tag"},
                            {"filters", QJsonObject{{"visibility", "all"}}}};
    const auto sessionSaved = session.execute(
        scheduleCall({{"action", "save"}, {"name", "Session Tags"}, {"request", byTag}}));
    check(sessionSaved["status"] == "saved", "The session saves a schedule");
    const auto list =
        invoke(server, toolCall(3, "report.schedule", scheduleCall({{"action", "describe"}})));
    const auto schedules = list.content["schedules"].toArray();
    check(!list.failed && schedules.size() == 2 && schedules[0].toObject()["name"] == "Kinds" &&
              schedules[1].toObject()["name"] == "Session Tags",
          "MCP lists the schedules of both surfaces in name order");

    // R101.e1: MCP renames and updates; the session describes the same records, and the rename
    // keeps the request while the update keeps the name.
    const auto renamed = invoke(server, toolCall(5, "report.schedule",
                                                 scheduleCall({{"action", "rename"},
                                                               {"name", "Session Tags"},
                                                               {"newName", "Tags Renamed"}})));
    check(!renamed.failed && renamed.content["status"] == "renamed", "MCP renames a schedule");
    const auto renamedView =
        session.execute(scheduleCall({{"action", "describe"}, {"name", "Tags Renamed"}}));
    check(compact(renamed.content["schedule"].toObject()) ==
              compact(renamedView["schedule"].toObject()),
          "The session describes the schedule MCP renamed, identically");
    check(renamedView["schedule"].toObject()["request"].toObject()["groupBy"] == "tag",
          "A rename keeps the saved request");
    const auto updated = invoke(server, toolCall(6, "report.schedule",
                                                 scheduleCall({{"action", "update"},
                                                               {"name", "Tags Renamed"},
                                                               {"request", kindRequest()}})));
    check(!updated.failed && updated.content["status"] == "updated" &&
              updated.content["schedule"].toObject()["name"] == "Tags Renamed",
          "MCP updates a schedule's request and keeps its name");

    // MCP deletes; the session no longer finds the schedule.
    const auto deleted =
        invoke(server, toolCall(4, "report.schedule",
                                scheduleCall({{"action", "delete"}, {"name", "Kinds"}})));
    check(!deleted.failed && deleted.content["status"] == "deleted", "MCP deletes a schedule");
    const auto remaining = session.execute(scheduleCall({{"action", "describe"}}));
    check(remaining["schedules"].toArray().size() == 1,
          "The session no longer lists the deleted schedule");
}

// The refusals through MCP: the codes and bounds of the session's report.schedule, and the protocol
// errors for a name that is not a registered tool or does not match its arguments.
void refusals(const QTemporaryDir &dir) {
    isolate(dir.path());
    AutomationSession session(
        {copyFixture(dir, "refusals.sketchyup"), {}, dir.filePath("refusal-outcomes")});
    McpServer server(session);
    // A value, not a reference: operator[] on the temporary result would refer into it.
    const QJsonValue id = session.execute(operation("session.describe"))["documentId"];
    int calls = 0;
    // Sends one report.schedule call for the session's document unless the fields name another.
    const auto send = [&](QJsonObject fields) {
        if (!fields.contains("documentId"))
            fields["documentId"] = id;
        return invoke(server,
                      toolCall(++calls, "report.schedule", operation("report.schedule", fields)));
    };
    const auto refused = [&](QJsonObject fields, const char *code, const char *text = nullptr) {
        const auto outcome = send(std::move(fields));
        check(outcome.failed && outcome.code() == QString::fromUtf8(code),
              std::string("MCP refuses with ") + code + ", got " +
                  (outcome.failed ? "" : "success ") + outcome.code().toStdString() + ": " +
                  outcome.message().toStdString());
        if (text)
            check(outcome.message().contains(QString::fromUtf8(text)),
                  std::string("The refusal names ") + text);
    };
    const auto save = [](QString name, QJsonObject request) {
        return QJsonObject{{"action", "save"}, {"name", name}, {"request", request}};
    };

    refused({{"documentId", "00000000000000000000000000000000"}, {"action", "describe"}},
            "WRONG_DOCUMENT");
    refused({{"documentId", "00000000000000000000000000000000"},
             {"action", "save"},
             {"name", "Kinds"},
             {"request", kindRequest()}},
            "WRONG_DOCUMENT");
    refused({{"action", "describe"}, {"name", "Missing"}}, "NOT_FOUND");
    refused({{"action", "delete"}, {"name", "Missing"}}, "NOT_FOUND");
    refused({{"action", "save"}, {"name", "Kinds"}}, "INVALID_REQUEST");
    refused({{"action", "describe"}, {"request", kindRequest()}}, "INVALID_REQUEST");
    refused({{"action", "delete"}}, "INVALID_REQUEST");
    refused({{"action", "rename"}}, "INVALID_REQUEST");
    // The wire schema's maxLength refuses the name before the store does, so MCP answers with the
    // validator's LIMIT_EXCEEDED; report_schedule_tests covers the store's own INVALID_REQUEST.
    refused(save(QString(65, 'a'), kindRequest()), "LIMIT_EXCEEDED");
    refused(save("a,b", kindRequest()), "INVALID_REQUEST", "starts and ends");
    refused(save("Selected", {{"metrics", "all"}, {"filters", QJsonObject{{"selection", true}}}}),
            "INVALID_REQUEST", "session selection");
    refused(save("Stale", tagRequest("987654")), "NOT_FOUND", "tags 987654");

    // A saved schedule stays, and a second save of its name is refused.
    check(!send(save("Kinds", kindRequest())).failed, "A valid schedule saves");
    refused(save("Kinds", kindRequest()), "INVALID_REQUEST", "already exists");

    // The bound of 32 schedules per document: fill the rest, then refuse the thirty-third.
    for (int i = 1; i < 32; ++i)
        check(!send(save("Schedule " + QString::number(i), kindRequest())).failed,
              "A schedule within the bound saves");
    refused(save("Thirty three", kindRequest()), "LIMIT_EXCEEDED", "at most 32");
    const auto listed =
        session.execute(operation("report.schedule", {{"documentId", id}, {"action", "describe"}}));
    check(listed["schedules"].toArray().size() == 32, "A refused save leaves the store unchanged");

    // Protocol errors: a call name that is not a registered tool, and a name that does not match
    // the operation or query in its arguments.
    const auto describe = QJsonObject{{"documentId", id}, {"action", "describe"}};
    check(protocolCode(server, toolCall(++calls, "reports.schedule",
                                        operation("report.schedule", describe))) == -32602,
          "MCP has no reports.schedule alias");
    check(protocolCode(server, toolCall(++calls, "report.schedule",
                                        operation("report.run", describe))) == -32602,
          "The tool name must match the operation it carries");
    check(protocolCode(server, toolCall(++calls, "report.run",
                                        operation("report.schedule", describe))) == -32602,
          "The report.run name must carry a query, not an operation");
}

// The built CLI over stdio: the same tools answer across the process boundary, and the schedule one
// process saves is in the temporary store that the next process reads. The child gets the temporary
// user data folder through its environment.
void stdio(const QTemporaryDir &dir) {
    const auto home = dir.filePath("home");
    auto env = QProcessEnvironment::systemEnvironment();
    for (const auto *name : {"DISPLAY", "WAYLAND_DISPLAY", "QT_QPA_PLATFORM"})
        env.remove(name);
    env.insert("HOME", home);
    env.insert("XDG_DATA_HOME", home + "/data");
    const auto model = copyFixture(dir, "stdio.sketchyup");

    QProcess process;
    process.setProcessEnvironment(env);
    process.start(QStringLiteral(CLI_PATH),
                  {"--mcp", "--input", model, "--outcomes", dir.filePath("stdio-outcomes")});
    check(process.waitForStarted(10000), "Start the MCP stdio server");
    const auto exchange = [&](const QJsonObject &request) {
        const auto bytes = QJsonDocument(request).toJson(QJsonDocument::Compact) + '\n';
        check(process.write(bytes) == bytes.size() && process.waitForBytesWritten(10000),
              "Send an MCP message");
        while (!process.canReadLine())
            check(process.waitForReadyRead(10000), "Read an MCP message");
        const auto response = QJsonDocument::fromJson(process.readLine()).object();
        check(response["id"] == request["id"], "The MCP response keeps its request ID");
        return response["result"].toObject();
    };

    const auto info = exchange(toolCall(1, "session.describe",
                                        operation("session.describe")))["structuredContent"]
                          .toObject();
    const auto id = info["documentId"];
    const auto saved =
        exchange(toolCall(2, "report.schedule",
                          operation("report.schedule", {{"documentId", id},
                                                        {"action", "save"},
                                                        {"name", "Stdio"},
                                                        {"request", kindRequest()}})));
    check(saved["isError"] == false && saved["structuredContent"].toObject()["status"] == "saved",
          "The CLI's MCP server saves a schedule");
    const auto run = exchange(toolCall(3, "report.run",
                                       query("report.run", {{"documentId", id},
                                                            {"expectedRevision", info["revision"]},
                                                            {"request", defaultRequest()}})));
    check(run["isError"] == false &&
              compact(reportFileData(run["structuredContent"].toObject()["data"].toObject())) ==
                  readFile(sourcePath("tests/fixtures/reports/quantities-a-default.json")),
          "The CLI's MCP report.run data is the committed golden, less the session-only fields");
    process.closeWriteChannel();
    check(process.waitForFinished(10000) && process.exitStatus() == QProcess::NormalExit &&
              process.exitCode() == 0,
          "The CLI's MCP server exits cleanly at end of input");

    // A later process (here, the test) reads the schedule the CLI saved, from the same folder.
    isolate(home);
    AutomationSession reader(
        {copyFixture(dir, "reader.sketchyup"), {}, dir.filePath("reader-outcomes")});
    const auto listed =
        reader.execute(operation("report.schedule", {{"documentId", id}, {"action", "describe"}}));
    const auto schedules = listed["schedules"].toArray();
    check(schedules.size() == 1 && schedules[0].toObject()["name"] == "Stdio",
          "The schedule the CLI saved is in the temporary store");
}

#endif

} // namespace

int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    try {
        QTemporaryDir guard, run, schedule, refusal, cli;
        check(guard.isValid() && run.isValid() && schedule.isValid() && refusal.isValid() &&
                  cli.isValid(),
              "Temporary folders");
#if defined(Q_OS_LINUX)
        // Nothing in this process reads the real user's folders, whichever check runs.
        isolate(guard.path());
#endif
        catalogue();
        runParity(run);
#if defined(Q_OS_LINUX)
        scheduleParity(schedule);
        refusals(refusal);
        stdio(cli);
#else
        std::cout << "schedule, refusal and CLI checks skipped: the store location is isolated "
                     "only on Linux\n";
#endif
        std::cout << "report MCP tests passed\n";
    } catch (const std::exception &error) {
        std::cerr << "report MCP tests failed: " << error.what() << '\n';
        return 1;
    }
}
