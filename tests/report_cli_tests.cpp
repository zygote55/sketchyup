// R101.b (ADR 0175 §8, §11 V12): `sketchyup-cli --report`. A report is written to a new file only,
// an existing path is refused, bad flags are refused by name, --input is never changed, the CSV and
// the JSON of one run agree, and the file is byte-identical to the session's report.run data.
// Built only with the CLI (it runs the built sketchyup-cli, whose path CMake passes as CLI_PATH).
#include "automation/inspection.hpp"
#include "automation/report_commands.hpp"
#include "io/document_io.hpp"
#include "io/report_csv.hpp"
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
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

void writeTestFile(const QString &path, const QByteArray &bytes) {
    QFile file(path);
    check(file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size(),
          "Cannot write " + path.toStdString());
}

QByteArray sha256(const QByteArray &bytes) {
    return QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex();
}

struct CliRun {
    int exitCode{};
    QByteArray out, err;
    QString code() const { return QJsonDocument::fromJson(err).object()["code"].toString(); }
};

CliRun cli(const QStringList &args) {
    QProcess process;
    process.start(QStringLiteral(CLI_PATH), args);
    check(process.waitForStarted(10000) && process.waitForFinished(120000),
          "The CLI runs to completion");
    check(process.exitStatus() == QProcess::NormalExit, "The CLI exits normally");
    return {process.exitCode(), process.readAllStandardOutput(), process.readAllStandardError()};
}

const QString fixture = QStringLiteral("tests/fixtures/reports/quantities-a.sketchyup");

// The report.run data for the same request the CLI flags make, from the same file.
QJsonObject sessionData(const QJsonObject &request) {
    const auto doc = loadDocument(sourcePath(fixture));
    const QJsonObject query{{"apiVersion", 1},
                            {"query", "report.run"},
                            {"documentId", QString::fromStdString(doc.identity())},
                            {"expectedRevision", QString::number(doc.revision())},
                            {"request", request}};
    return inspectDocument(doc, query).value("data").toObject();
}

// The rows of a CSV, keyed by "section,group,tag,material,definition,metric" (the value is the last
// cell). The keys hold no quoted group names, so a plain split is exact for these fixtures.
QHash<QString, QString> csvValues(const QByteArray &csv) {
    QHash<QString, QString> values;
    for (const auto &line : QString::fromUtf8(csv).split('\n')) {
        if (line.startsWith('#') || line.isEmpty() || line.startsWith("section,"))
            continue;
        const auto cells = line.split(',');
        values.insert(cells.mid(0, 6).join(','), cells.last());
    }
    return values;
}

// V12 (CLI): the new-file write, its refusals and bad flags, the read-only input, and the CSV and
// JSON parity. Each step asserts on the file the CLI wrote.
void writesNewFileOnly() {
    QTemporaryDir dir;
    check(dir.isValid(), "A temporary directory");
    const auto input = sourcePath(fixture);
    const auto inputBefore = sha256(readFile(input));

    const auto csvPath = dir.filePath("all.csv");
    const auto run =
        cli({"--input", input, "--report", csvPath, "--metrics", "all", "--precision", "full"});
    check(run.exitCode == 0, "A new CSV report succeeds: " + run.err.toStdString());
    const auto stdoutJson = QJsonDocument::fromJson(run.out).object();
    check(stdoutJson["status"] == "exported" &&
              stdoutJson["exportReport"].toObject()["format"] == "csv",
          "The CLI reports the export as exported in CSV");

    const auto csv = readFile(csvPath);
    check(csv == readFile(sourcePath("tests/fixtures/reports/quantities-a-default.csv")),
          "The CLI CSV is the golden default report");
    check(csv == reportCsv(sessionData(
                     QJsonObject{{"metrics", "all"}, {"groupBy", "none"}, {"precision", "full"}})),
          "The CLI CSV is the CSV of the session's report.run data (V12)");

    // The JSON of the same request: the session data, byte for byte (V12).
    const auto jsonPath = dir.filePath("all.json");
    const auto json =
        cli({"--input", input, "--report", jsonPath, "--metrics", "all", "--precision", "full"});
    check(json.exitCode == 0, "A new JSON report succeeds");
    check(readFile(jsonPath) ==
              QJsonDocument(sessionData(QJsonObject{
                                {"metrics", "all"}, {"groupBy", "none"}, {"precision", "full"}}))
                  .toJson(QJsonDocument::Compact),
          "The CLI JSON is the session's report.run data, byte for byte (V12)");

    // The CSV and the JSON of one run agree: the All row's counts and totals (§6.2).
    const auto values = csvValues(csv);
    const auto data = QJsonDocument::fromJson(readFile(jsonPath)).object();
    const auto all = data["counts"].toArray().first().toObject();
    check(values["counts,All,,,,contexts"] == QString::number(all["contexts"].toInt()) &&
              values["counts,All,,,,instances"] == QString::number(all["instances"].toInt()) &&
              values["counts,All,,,,definitions"] == QString::number(all["definitions"].toInt()),
          "The CSV counts equal the JSON counts");
    check(values["volumes,All,,,,volume"] ==
              data["volumes"].toArray().first().toObject()["volume"].toString(),
          "The CSV volume equals the JSON volume as the same decimal string");

    // A material grouping at three decimals over the visible content (V12, another request).
    const auto materialPath = dir.filePath("material.csv");
    const auto grouped = cli({"--input", input, "--report", materialPath, "--group-by", "material",
                              "--visible", "--precision", "3"});
    check(grouped.exitCode == 0, "A grouped report succeeds: " + grouped.err.toStdString());
    check(readFile(materialPath) ==
              readFile(sourcePath("tests/fixtures/reports/quantities-a-material.csv")),
          "The grouped CLI CSV is the golden material report");

    // Refusals: an existing path is never replaced, and the refused write leaves it alone.
    const auto existing = dir.filePath("existing.csv");
    writeTestFile(existing, "keep me");
    const auto refused = cli({"--input", input, "--report", existing});
    check(refused.exitCode != 0 && refused.code() == "FILE_EXISTS",
          "An existing report path is refused as FILE_EXISTS");
    check(readFile(existing) == "keep me", "The refused path is unchanged");

    check(sha256(readFile(input)) == inputBefore, "--input is never changed by a report");
}

// The refusals that name their reason (ADR 0175 §8): bad flags, bad values and bad file names. Each
// refusal writes no file.
void refusesBadFlags() {
    QTemporaryDir dir;
    check(dir.isValid(), "A temporary directory");
    const auto input = sourcePath(fixture);
    struct Case {
        QStringList args;
        const char *code;
        const char *why;
    };
    const QString out = dir.filePath("bad.csv");
    const QString txt = dir.filePath("bad.txt");
    const QString missing = dir.filePath("missing-folder/x.csv");
    const Case cases[]{
        {{"--input", input, "--report", out, "--schedule", "nightly"},
         "INVALID_REQUEST",
         "the schedule flags are not in R101.b"},
        {{"--input", input, "--report", out, "--save-schedule", "nightly"},
         "INVALID_REQUEST",
         "--save-schedule is not in R101.b"},
        {{"--input", input, "--report", out, "--metrics", "volumes,bogus"},
         "INVALID_REQUEST",
         "an unknown metric"},
        {{"--input", input, "--report", out, "--metrics", "counts,counts"},
         "INVALID_REQUEST",
         "a repeated metric"},
        {{"--input", input, "--report", out, "--group-by", "colour"},
         "INVALID_REQUEST",
         "an unknown grouping"},
        {{"--input", input, "--report", out, "--precision", "99"},
         "LIMIT_EXCEEDED",
         "a precision past the document's maximum"},
        {{"--input", input, "--report", out, "--precision", "two"},
         "INVALID_REQUEST",
         "a precision that is not full or an integer"},
        {{"--input", input, "--report", out, "--tag", "0"},
         "INVALID_REQUEST",
         "a tag ID that is not canonical"},
        {{"--input", input, "--report", out, "--tag", "999999"},
         "NOT_FOUND",
         "a tag that does not exist"},
        {{"--input", input, "--report", txt},
         "INVALID_REQUEST",
         "a file that is not .csv or .json"},
        {{"--input", input, "--report", out, "--format", "json"},
         "INVALID_REQUEST",
         "--format that does not match the extension"},
        {{"--report", out}, "INVALID_REQUEST", "a report without --input"},
        {{"--input", input, "--report", missing}, "", "a parent folder that does not exist"},
    };
    for (const auto &item : cases) {
        const auto run = cli(item.args);
        check(run.exitCode != 0, std::string("A refused report exits non-zero: ") + item.why);
        if (item.code[0] != '\0')
            check(run.code() == item.code,
                  std::string("The refusal names its reason (") + item.code + "): " + item.why);
    }
    for (const auto &path : {out, txt, missing})
        check(!QFileInfo::exists(path), "A refused report writes no file: " + path.toStdString());
}

// The CLI advertises the report like the other exports, and never advertises the schedule flags.
void advertises() {
    const auto capabilities = QJsonDocument::fromJson(cli({"--capabilities"}).out).object();
    check(capabilities.contains("reportExport"), "--capabilities lists reportExport");
    const auto described =
        QJsonDocument::fromJson(cli({"--describe-command", "report"}).out).object();
    check(described["name"] == "report", "--describe-command report names the command");
    const auto usage = described["usage"].toString();
    check(usage.contains("--report") && usage.contains("--group-by") &&
              usage.contains("--visible") && !usage.contains("schedule"),
          "The usage carries the R101.b flags and no schedule flag");
}

} // namespace

int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    try {
        writesNewFileOnly();
        refusesBadFlags();
        advertises();
        std::cout << "report cli tests passed\n";
    } catch (const std::exception &error) {
        std::cerr << "report cli tests failed: " << error.what() << '\n';
        return 1;
    }
    return 0;
}
