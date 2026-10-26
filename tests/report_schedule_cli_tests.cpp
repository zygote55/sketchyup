// R101.c (ADR 0175 §7, §8, §11 V11): the schedule flags of `sketchyup-cli`, run as the built
// binary. Each flag works; the flags that cannot combine are refused by name, and nothing is
// written when they are; a scheduled run is byte-identical to the ad-hoc run except for its marker
// line; the identity guard and the stale-ID refusal hold on the command line; a corrupt store is
// refused, not reset; and the model file is never changed. The user data folder is a temporary
// folder (HOME and XDG_DATA_HOME), so no test touches the real user's store. That isolation holds
// on Linux only, so the test is skipped (exit 77) elsewhere.
#include "automation/report_schedules.hpp"
#include "core/model.hpp"
#include "io/document_io.hpp"
#include <QCryptographicHash>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QProcessEnvironment>
#include <QStringList>
#include <QTemporaryDir>
#include <iostream>
#include <stdexcept>
#include <string>

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

void writeFile(const QString &path, const QByteArray &bytes) {
    QFile file(path);
    check(file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size(),
          "Cannot write " + path.toStdString());
}

// A JSON string field equals the text (QJsonValue has no const char* comparison).
bool is(const QJsonValue &value, const char *text) {
    return value.toString() == QString::fromUtf8(text);
}

QByteArray sha256(const QByteArray &bytes) {
    return QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex();
}

struct CliRun {
    int exitCode{};
    QByteArray out, err;
    QString code() const { return QJsonDocument::fromJson(err).object()["code"].toString(); }
    QJsonObject json() const { return QJsonDocument::fromJson(out).object(); }
};

CliRun cli(const QStringList &args, const QProcessEnvironment &env) {
    QProcess process;
    process.setProcessEnvironment(env);
    process.start(QStringLiteral(CLI_PATH), args);
    check(process.waitForStarted(10000) && process.waitForFinished(120000),
          "The CLI runs to completion");
    check(process.exitStatus() == QProcess::NormalExit, "The CLI exits normally");
    return {process.exitCode(), process.readAllStandardOutput(), process.readAllStandardError()};
}

// The environment of one test folder: the store is under `home/data`, never the real user's folder.
QProcessEnvironment isolated(const QString &home) {
    auto env = QProcessEnvironment::systemEnvironment();
    env.insert("HOME", home);
    env.insert("XDG_DATA_HOME", home + "/data");
    return env;
}

QString storeFile(const QString &home) {
    return home + "/data/SketchyUp/SketchyUp/report-schedules.json";
}

// The model under test is the committed quantities-a fixture: the CLI must only read it.
const QString fixture = QStringLiteral("tests/fixtures/reports/quantities-a.sketchyup");

// Each flag works, the store is created only by a change, and the model is never changed.
void flags(const QString &home, const QProcessEnvironment &env, const QString &dir) {
    const auto input = sourcePath(fixture);
    const auto modelBefore = sha256(readFile(input));

    auto run = cli({"--input", input, "--list-schedules"}, env);
    check(run.exitCode == 0 && run.json()["schedules"].toArray().isEmpty(),
          "--list-schedules lists nothing for a new store");
    check(!QFileInfo::exists(storeFile(home)), "A listing creates no store");
    const auto documentId = run.json()["documentId"].toString();
    check(documentId.size() == 32, "The listing names the document's identity");

    run = cli({"--input", input, "--save-schedule", "Tags", "--metrics", "counts,volumes",
               "--group-by", "tag"},
              env);
    check(run.exitCode == 0 && is(run.json()["status"], "saved"),
          "--save-schedule saves the report flags as a schedule");
    check(QFileInfo::exists(storeFile(home)), "The store is in the isolated user data folder");

    run = cli({"--input", input, "--list-schedules"}, env);
    check(run.json()["schedules"].toArray().size() == 1,
          "--list-schedules lists the saved schedule");

    const auto scheduled = dir + "/tags-scheduled.csv";
    run = cli({"--input", input, "--report", scheduled, "--schedule", "Tags"}, env);
    check(run.exitCode == 0 && is(run.json()["schedule"], "Tags") &&
              is(run.json()["status"], "exported"),
          "--schedule runs the saved schedule into a new report file");
    const auto csv = readFile(scheduled);
    check(csv.contains("\n#schedule=Tags\n") && !csv.contains("#ad-hoc=1"),
          "A scheduled run names its schedule in place of #ad-hoc=1");

    const auto adhoc = dir + "/tags-adhoc.csv";
    run = cli(
        {"--input", input, "--report", adhoc, "--metrics", "counts,volumes", "--group-by", "tag"},
        env);
    check(run.exitCode == 0, "The ad-hoc run with the same flags succeeds");
    QByteArray withAdhocMarker = csv;
    withAdhocMarker.replace("#schedule=Tags\n", "#ad-hoc=1\n");
    check(withAdhocMarker == readFile(adhoc),
          "A scheduled run is byte-identical to the ad-hoc run except for its marker line");

    const auto scheduledJson = dir + "/tags-scheduled.json", adhocJson = dir + "/tags-adhoc.json";
    check(cli({"--input", input, "--report", scheduledJson, "--schedule", "Tags"}, env).exitCode ==
              0,
          "--schedule writes a JSON report");
    check(cli({"--input", input, "--report", adhocJson, "--metrics", "counts,volumes", "--group-by",
               "tag"},
              env)
                  .exitCode == 0,
          "The ad-hoc JSON run succeeds");
    auto scheduledData = QJsonDocument::fromJson(readFile(scheduledJson)).object();
    auto metadata = scheduledData["metadata"].toObject();
    check(is(metadata["schedule"], "Tags"), "The JSON metadata names the schedule");
    metadata.remove("schedule");
    scheduledData["metadata"] = metadata;
    check(scheduledData == QJsonDocument::fromJson(readFile(adhocJson)).object(),
          "The scheduled JSON data equals the ad-hoc JSON data apart from the schedule name");

    run = cli({"--input", input, "--save-schedule", "Tags", "--metrics", "counts"}, env);
    check(run.exitCode == 1 && run.code() == "INVALID_REQUEST",
          "A duplicate name is refused by name");

    run = cli({"--input", input, "--delete-schedule", "Tags"}, env);
    check(run.exitCode == 0 && is(run.json()["status"], "deleted"), "--delete-schedule deletes it");
    run = cli({"--input", input, "--delete-schedule", "Tags"}, env);
    check(run.exitCode == 1 && run.code() == "NOT_FOUND", "A deleted schedule is not found again");
    run = cli({"--input", input, "--list-schedules"}, env);
    check(run.json()["schedules"].toArray().isEmpty(), "The list is empty after the delete");

    check(sha256(readFile(input)) == modelBefore, "The CLI leaves the model file unchanged");
}

// Flags that cannot combine are refused by name, and nothing is written when they are.
void exclusion(const QProcessEnvironment &env, const QString &dir) {
    const auto input = sourcePath(fixture);
    const auto out = dir + "/refused.csv";
    const QStringList refused[] = {
        {"--input", input, "--schedule", "Tags", "--report", out, "--metrics", "counts"},
        {"--input", input, "--schedule", "Tags", "--report", out, "--group-by", "kind"},
        {"--input", input, "--schedule", "Tags", "--report", out, "--visible"},
        {"--input", input, "--schedule", "Tags", "--report", out, "--format", "csv"},
        {"--input", input, "--schedule", "Tags", "--report", out, "--tag", "1"},
        {"--input", input, "--schedule", "Tags", "--report", out, "--precision", "2"},
        {"--input", input, "--schedule", "Tags"},
        {"--input", input, "--schedule", "", "--report", out},
        {"--input", input, "--list-schedules", "--save-schedule", "Other"},
        {"--input", input, "--list-schedules", "--delete-schedule", "Other"},
        {"--input", input, "--delete-schedule", "Tags", "--report", out},
        {"--input", input, "--schedule", "Tags", "--save-schedule", "Other"},
        {"--input", input, "--list-schedules", "--metrics", "counts"},
        {"--input", input, "--save-schedule", "Other", "--format", "csv"},
        {"--list-schedules"},
        {"--input", input, "--save-schedule", "bad,name"},
    };
    for (const auto &args : refused) {
        const auto run = cli(args, env);
        check(run.exitCode == 1 && run.code() == "INVALID_REQUEST",
              "A schedule flag combination is refused as INVALID_REQUEST: " +
                  args.join(' ').toStdString());
    }
    check(!QFileInfo::exists(out), "A refused combination writes no report file");
}

// The identity guard on the command line: a schedule saved for one document never runs on another.
void identityGuard(const QProcessEnvironment &env, const QString &dir) {
    const auto saved = cli(
        {"--input", sourcePath(fixture), "--save-schedule", "Mine", "--metrics", "counts"}, env);
    check(saved.exitCode == 0, "A schedule is saved for the fixture document");
    Document other;
    other.addFace({{{0, 0, 0}, {1, 0, 0}, {1, 1, 0}, {0, 1, 0}}});
    const auto otherPath = dir + "/other.sketchyup";
    saveDocument(other, otherPath);
    const auto out = dir + "/other.csv";
    const auto run = cli({"--input", otherPath, "--schedule", "Mine", "--report", out}, env);
    check(run.exitCode == 1 && run.code() == "WRONG_DOCUMENT",
          "A schedule saved for another document is refused on this one");
    check(!QFileInfo::exists(out), "A refused run writes no report file");
    const auto listed = cli({"--input", otherPath, "--list-schedules"}, env);
    check(listed.exitCode == 0 && listed.json()["schedules"].toArray().isEmpty(),
          "Another document's listing shows none of this document's schedules");
    const auto deleted = cli({"--input", sourcePath(fixture), "--delete-schedule", "Mine"}, env);
    check(deleted.exitCode == 0, "The fixture's schedule is removed again");
}

// A schedule whose tag is no longer in the document refuses at run time, naming the missing ID.
void staleIds(const QProcessEnvironment &env, const QString &home, const QString &dir) {
    const auto input = sourcePath(fixture);
    const auto listed = cli({"--input", input, "--list-schedules"}, env);
    const auto documentId = listed.json()["documentId"].toString().toStdString();
    ReportScheduleStore store(storeFile(home));
    ReportSchedule stale{"Stale", "2026-10-26T00:00:00Z",
                         QJsonObject{{"metrics", QJsonArray{"counts"}},
                                     {"groupBy", "none"},
                                     {"filters", QJsonObject{{"tags", QJsonArray{"987654"}}}}}};
    store.add(documentId, stale);
    const auto out = dir + "/stale.csv";
    const auto run = cli({"--input", input, "--schedule", "Stale", "--report", out}, env);
    check(run.exitCode == 1 && run.code() == "NOT_FOUND",
          "A schedule whose tag is gone is refused at run time");
    check(QString::fromUtf8(run.err).contains("tags 987654"),
          "The refusal names the missing tag ID");
    check(!QFileInfo::exists(out), "A refused run writes no report file");
    store.remove(documentId, "Stale");
}

// A corrupt store is refused by every operation, located, and left as it was.
void corruptStore(const QProcessEnvironment &env, const QString &home, const QString &dir) {
    const auto input = sourcePath(fixture);
    const QByteArray broken = "{\"storeVersion\":1,\"documents\":[";
    writeFile(storeFile(home), broken);
    const auto listed = cli({"--input", input, "--list-schedules"}, env);
    check(listed.exitCode == 1 && listed.code() == "INPUT_ERROR" &&
              QString::fromUtf8(listed.err).contains("report-schedules.json"),
          "A corrupt store is refused and located by its path");
    const auto saved =
        cli({"--input", input, "--save-schedule", "Again", "--metrics", "counts"}, env);
    check(saved.exitCode == 1 && saved.code() == "INPUT_ERROR",
          "A corrupt store is never overwritten by a save");
    const auto run =
        cli({"--input", input, "--schedule", "Again", "--report", dir + "/x.csv"}, env);
    check(run.exitCode == 1 && run.code() == "INPUT_ERROR", "A corrupt store refuses a run");
    check(readFile(storeFile(home)) == broken, "A corrupt store is left as it was");
    QFile::remove(storeFile(home));
}

} // namespace

int main() {
#if !defined(Q_OS_LINUX)
    std::cout << "report schedule CLI tests skipped: the user data folder is isolated only on "
                 "Linux (XDG_DATA_HOME)\n";
    return 77;
#else
    try {
        QTemporaryDir sandbox;
        check(sandbox.isValid(), "A temporary folder");
        const auto home = sandbox.path();
        const auto env = isolated(home);
        flags(home, env, home);
        exclusion(env, home);
        identityGuard(env, home);
        staleIds(env, home, home);
        corruptStore(env, home, home);
        std::cout << "report schedule CLI tests passed\n";
    } catch (const std::exception &error) {
        std::cerr << "report schedule CLI tests failed: " << error.what() << '\n';
        return 1;
    }
    return 0;
#endif
}
