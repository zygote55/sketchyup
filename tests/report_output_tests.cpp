// R101.b (ADR 0175 §6, §8, §11): the CSV dialect and the JSON form of a report, the golden bytes of
// quantities-a (V10), the session parity of the JSON (V12), and the generator of the fixture and
// its golden outputs.
//
// R101.e1 adds the session's invalid rows with their body IDs, the file's JSON without them (the
// goldens do not change), the selection file (UX_DESIGN §19.8, owner choice 2) and a cancelled
// write, which publishes nothing.
//
//   report_output_tests --write-fixture PATH       writes quantities-a as a native container
//   report_output_tests --write-goldens DIRECTORY  writes the golden CSV and JSON of quantities-a
//
// The goldens are committed under tests/fixtures/reports and hash-pinned below. A change to the
// output changes a pin on purpose, in the same commit as the golden file.
#include "automation/inspection.hpp"
#include "automation/report_commands.hpp"
#include "io/document_io.hpp"
#include "io/report_csv.hpp"
#include "report_fixtures.hpp"
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>
#include <atomic>
#include <iostream>
#include <set>
#include <stdexcept>
#include <string>

using namespace sketchy;
using namespace sketchy::report_fixtures;

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
    check(file.open(QIODevice::WriteOnly | QIODevice::Truncate) &&
              file.write(bytes) == bytes.size(),
          "Cannot write " + path.toStdString());
}

QString sha256(const QByteArray &bytes) {
    return QString::fromLatin1(QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex());
}

// The session's report.run data for a request, through the bounded query path.
QJsonObject sessionReport(const Document &doc, const QJsonObject &request) {
    const QJsonObject query{{"apiVersion", 1},
                            {"query", "report.run"},
                            {"documentId", QString::fromStdString(doc.identity())},
                            {"expectedRevision", QString::number(doc.revision())},
                            {"request", request}};
    return inspectDocument(doc, query).value("data").toObject();
}

// One golden: the CLI flags and the matching report.run request, which must describe the same
// report.
struct GoldenCase {
    QString name;
    ReportFileOptions options;
    QJsonObject request;
};

// V10: the default report at full precision, and the material grouping at three decimals over the
// visible content. Their group names carry a comma and quotes (the material "Steel,
// ""galvanised""").
GoldenCase defaultCase() {
    ReportFileOptions options;
    options.precision = "full";
    return {QStringLiteral("quantities-a-default"), options,
            QJsonObject{{"metrics", "all"}, {"groupBy", "none"}, {"precision", "full"}}};
}

GoldenCase materialCase() {
    ReportFileOptions options;
    options.groupBy = "material";
    options.visible = true;
    options.precision = "3";
    return {QStringLiteral("quantities-a-material"), options,
            QJsonObject{{"metrics", "all"},
                        {"groupBy", "material"},
                        {"precision", 3},
                        {"filters", QJsonObject{{"visibility", "visible"}}}}};
}

const QString goldenDirectory = QStringLiteral("tests/fixtures/reports");

// SHA-256 of each committed golden file (V10). A golden changes only together with its pin.
const std::pair<QString, QString> goldenPins[]{
    {"quantities-a-default.csv",
     "29a9f34053f4f287c42979dd387dd5fd841a4a9f342a65a427bf5fd4365e1d97"},
    {"quantities-a-default.json",
     "0b2203a0d15987c167a2226fc92367b4e2df86469eee2af71d9afaf5804e0cac"},
    {"quantities-a-material.csv",
     "55549071d0581710104e8cbe9defe8f7b2838e382bf18bdb12c2eadaafa4c447"},
    {"quantities-a-material.json",
     "28883ea5050d38e6ba667bd3f25160b2cf3bd85e1b00a6b4c49d04c3dca4e143"},
};

QByteArray jsonBytes(const QJsonObject &data) {
    return QJsonDocument(data).toJson(QJsonDocument::Compact);
}

// The document payload with its identity set to a fixed value. The identity (and so the stamp's
// documentId) is random for every new Document, so two builds of the same content differ only
// there.
QJsonObject payloadWithoutIdentity(const Document &doc) {
    auto payload = QJsonDocument::fromJson(encodeDocument(doc)).object();
    payload["documentId"] = "fixture";
    return payload;
}

// Writes the fixture as a native container. Its identity is random, so only the payload is fixed.
void writeFixture(const QString &path) {
    Document doc;
    buildQuantitiesA(doc);
    writeFile(path, encodeContainer(doc));
}

// The CSV dialect (ADR 0175 §6.1), checked on the default report of quantities-a.
void csvDialect() {
    const auto doc = loadDocument(sourcePath("tests/fixtures/reports/quantities-a.sketchyup"));
    const auto csv = reportCsv(sessionReport(doc, defaultCase().request));

    check(!csv.startsWith("\xEF\xBB\xBF"), "The CSV has no byte-order mark");
    check(!csv.contains('\r'), "The CSV uses LF line endings only");
    check(csv.endsWith('\n') && !csv.endsWith("\n\n"), "The CSV ends with exactly one LF");
    const auto lines = csv.split('\n');
    for (qsizetype i = 0; i + 1 < lines.size(); ++i)
        check(!lines[i].endsWith(' ') && !lines[i].endsWith('\t'),
              "A CSV line has trailing whitespace: " + lines[i].toStdString());

    // The metadata block, in the fixed order (§6.1), then the header and the rows.
    const QStringList metadataOrder{
        "#sketchyup-report=", "#format=",       "#documentId=", "#revision=",
        "#ad-hoc=",           "#units.length=", "#units.area=", "#units.volume=",
        "#precision=",        "#metrics=",      "#group_by=",   "#filters.visibility=",
        "#sheets=",           "#views=",        "#items="};
    qsizetype index = 0;
    for (; index < lines.size() && lines[index].startsWith('#'); ++index) {
        check(index < metadataOrder.size() &&
                  QString::fromUtf8(lines[index]).startsWith(metadataOrder[index]),
              "The metadata line " + std::to_string(index) +
                  " is out of order: " + lines[index].toStdString());
    }
    check(index == metadataOrder.size(), "The metadata block has exactly the fixed lines");
    check(lines[index] == "section,group,tag,material,definition,metric,value",
          "The column header is exact");

    // Rows: one tidy row per (section, group, metric[, material]), counts then areas then volumes,
    // invalid, terrain (§6.1). The totals are the ADR's corrected figures (R101.a notes).
    const auto has = [&](const char *row) { return lines.contains(QByteArray(row)); };
    check(has("counts,All,,,,contexts,120") && has("counts,All,,,,instances,15") &&
              has("counts,All,,,,definitions,4"),
          "The All counts: 120 contexts, 15 instances, 4 definitions");
    check(has("volumes,All,,,,volume,82.4") && has("volumes,All,,,,unvalidated,2"),
          "The All volumes: 82.4 m3 known and 2 unvalidated");
    check(has("invalid,open_boundary,,,,contexts,1") && has("invalid,non_manifold,,,,contexts,1"),
          "The invalid rows carry their exact statuses");

    // Sections come in the fixed order (§6.1), and the All area rows start with the no-material
    // row.
    const QList<QByteArray> sectionOrder{"counts", "areas", "volumes", "invalid", "terrain"};
    qsizetype rank = 0, firstAllArea = -1;
    for (qsizetype i = index + 1; i + 1 < lines.size(); ++i) {
        const auto section = sectionOrder.indexOf(lines[i].split(',').first());
        check(section >= 0 && section >= rank,
              "A row is out of section order: " + lines[i].toStdString());
        rank = section;
        if (firstAllArea < 0 && lines[i].startsWith("areas,All,"))
            firstAllArea = i;
    }
    check(firstAllArea >= 0 && lines[firstAllArea].startsWith("areas,All,,—,,area,"),
          "The All area rows start with the no-material row");
}

// Minimal RFC 4180 quoting, on hand-built data: a comma, quotes, CR and LF quote the field, and the
// quotes are doubled; any other name stays bare.
void quoting() {
    auto data = [](const QString &name, const char *groupBy, const QString &key) {
        return QJsonObject{
            {"metadata",
             QJsonObject{{"documentId", "0"},
                         {"revision", "1"},
                         {"metrics", QJsonArray{"counts"}},
                         {"groupBy", groupBy},
                         {"units", QJsonObject{{"length", "m"}, {"area", "m2"}, {"volume", "m3"}}},
                         {"precision", "full"},
                         {"filters", QJsonObject{{"visibility", "all"}}},
                         {"sheets", 0},
                         {"views", 0},
                         {"items", 0}}},
            {"counts", QJsonArray{QJsonObject{{"key", key},
                                              {"name", name},
                                              {"contexts", 1},
                                              {"instances", 0},
                                              {"definitions", 0}}}}};
    };
    // A data row is a whole line, so the search is for the row between two LFs.
    const auto hasRow = [](const QByteArray &csv, const QByteArray &row) {
        return csv.contains(QByteArray("\n") + row + "\n");
    };
    check(hasRow(reportCsv(data("Walls", "tag", "5")), "counts,Walls,5,,,contexts,1"),
          "A plain name is not quoted");
    check(hasRow(reportCsv(data("Walls, \"north\"", "tag", "5")),
                 "counts,\"Walls, \"\"north\"\"\",5,,,contexts,1"),
          "A comma and quotes quote the field and double its quotes");
    check(hasRow(reportCsv(data("Line\r\nBreak", "tag", "5")),
                 "counts,\"Line\r\nBreak\",5,,,contexts,1"),
          "CR and LF quote the field");
    check(hasRow(reportCsv(data("—", "tag", "untagged")), "counts,—,—,,,contexts,1"),
          "The em dash is UTF-8 and bare; an untagged group has the dash in its tag cell");
    check(hasRow(reportCsv(data("All", "none", "All")), "counts,All,,,,contexts,1"),
          "The All row has no axis cells");
    check(!hasRow(reportCsv(data("Walls", "none", "5")), "counts,Walls,5,,,contexts,1"),
          "The tag cell is set only when the report groups by tag");
}

// V8 and §6.1: the metadata states the unit codes and the precision for each unit setting and each
// precision, and the values use the converted units.
void metadataMatrix() {
    struct UnitCase {
        DisplayUnit unit;
        const char *length, *area, *volume;
    };
    const UnitCase units[]{{DisplayUnit::Meters, "m", "m2", "m3"},
                           {DisplayUnit::Millimeters, "mm", "mm2", "mm3"},
                           {DisplayUnit::FeetInches, "ft", "ft2", "ft3"}};
    for (const auto &unit : units) {
        for (const bool full : {true, false}) {
            auto doc = loadDocument(sourcePath("tests/fixtures/reports/quantities-a.sketchyup"));
            doc.setDisplayUnits(unit.unit, std::nullopt);
            QJsonObject request{{"metrics", "all"}};
            if (full)
                request["precision"] = "full";
            else
                request["precision"] = 3;
            const auto csv = QString::fromUtf8(reportCsv(sessionReport(doc, request)));
            const auto header =
                QStringList{QStringLiteral("#units.length=%1").arg(unit.length),
                            QStringLiteral("#units.area=%1").arg(unit.area),
                            QStringLiteral("#units.volume=%1").arg(unit.volume),
                            QStringLiteral("#precision=%1").arg(full ? "full" : "3")};
            for (const auto &line : header)
                check(csv.split('\n').contains(line), "The metadata states " + line.toStdString());
        }
    }
    // The same report at mm, full precision: 82.4 m3 is 82,400,000,000 mm3 (V8).
    auto doc = loadDocument(sourcePath("tests/fixtures/reports/quantities-a.sketchyup"));
    doc.setDisplayUnits(DisplayUnit::Millimeters, std::nullopt);
    const auto csv =
        QString::fromUtf8(reportCsv(sessionReport(doc, QJsonObject{{"precision", "full"}})));
    check(csv.contains(QStringLiteral("volumes,All,,,,volume,82400000000\n")),
          "82.4 m3 is 82400000000 mm3 in the CSV");
}

// The goldens are byte-identical across runs, and the file writer publishes exactly the golden
// bytes.
void goldens() {
    const auto doc = loadDocument(sourcePath("tests/fixtures/reports/quantities-a.sketchyup"));
    QTemporaryDir dir;
    check(dir.isValid(), "A temporary directory");
    for (const auto &golden : {defaultCase(), materialCase()}) {
        for (const auto &extension : {QStringLiteral("csv"), QStringLiteral("json")}) {
            const auto expected =
                readFile(sourcePath(goldenDirectory + "/" + golden.name + "." + extension));
            ReportFileOptions options = golden.options;
            options.path = dir.filePath(golden.name + "-run1." + extension);
            options.format = extension;
            writeReportFile(doc, options);
            const auto first = readFile(options.path);
            options.path = dir.filePath(golden.name + "-run2." + extension);
            writeReportFile(doc, options);
            check(first == readFile(options.path), "A repeated run is byte-identical");
            check(first == expected,
                  "The output is the golden " + (golden.name + "." + extension).toStdString());
        }
    }
}

// V10: each committed golden is the hash-pinned bytes.
void pins() {
    for (const auto &[file, pin] : goldenPins)
        check(sha256(readFile(sourcePath(goldenDirectory + "/" + file))) == pin,
              "The golden matches its SHA-256 pin: " + file.toStdString());
}

// R101.e1: the session's invalid rows carry their body IDs, the truncation flag and the words of
// the Entity info panel (UX_DESIGN §19.4). The file's JSON keeps the R101.b shape, so the goldens
// are unchanged, and the CSV never had them.
void invalidRowsInSession() {
    const auto doc = loadDocument(sourcePath("tests/fixtures/reports/quantities-a.sketchyup"));
    const auto data = sessionReport(doc, defaultCase().request);
    Id plate = 0;
    for (const auto &[id, record] : doc.bodies())
        if (record->name == "Plate")
            plate = id;
    check(plate != 0, "The quantities-a fixture has the open plate");
    const auto invalid = data["invalid"].toArray();
    check(invalid.size() == 2, "Two invalid rows in the session data");
    const auto open = invalid[0].toObject();
    check(open["status"] == "open_boundary" && open["contexts"] == 1 &&
              open["bodies"].toArray() == QJsonArray{QString::number(plate)} &&
              open["truncated"] == false && open["label"] == "Not a solid: open boundary",
          "The open boundary row names the plate, its words and no truncation");
    check(invalid[1].toObject()["label"] == "Not a solid: non-manifold boundary" &&
              invalid[1].toObject()["bodies"].toArray().size() == 1,
          "The non-manifold row names the shared-face cubes");
    const auto file = reportFileData(data)["invalid"].toArray()[0].toObject();
    check(file.size() == 2 && file["status"] == "open_boundary" && file["contexts"] == 1,
          "The file's invalid rows keep only the status and the count");
}

// UX_DESIGN §19.8 and owner choice 2: a selection file holds the selection's figures and says so in
// its metadata (ADR 0175 §6.1). The file writer takes the bodies as an argument; without them, or
// for a schedule, a selection is refused, and nothing is written.
void selectionFile() {
    auto doc = loadDocument(sourcePath("tests/fixtures/reports/quantities-a.sketchyup"));
    Id scaled = 0;
    for (const auto &[id, record] : doc.bodies())
        if (record->name == "Scaled")
            scaled = id;
    check(scaled != 0, "The quantities-a fixture has the scaled copy");
    const std::set<Id> bodies{scaled};
    QTemporaryDir dir;
    check(dir.isValid(), "A temporary directory");

    ReportFileOptions csv;
    csv.path = dir.filePath("selection.csv");
    csv.format = "csv";
    csv.metrics = "all";
    csv.precision = "full";
    csv.selection = true;
    writeReportFile(doc, csv, &bodies);
    const auto text = QString::fromUtf8(readFile(csv.path));
    check(text.split('\n').contains("#filters.selection=1"),
          "A selection file carries #filters.selection=1");
    check(text.contains("counts,All,,,,contexts,1\n") &&
              text.contains("volumes,All,,,,volume,32\n"),
          "The selection file holds the selected body alone: 1 context and 32 m3");
    check(text.contains("#ad-hoc=1\n"), "A selection file is an ad-hoc run");

    ReportFileOptions json = csv;
    json.path = dir.filePath("selection.json");
    json.format = "json";
    writeReportFile(doc, json, &bodies);
    const auto written = QJsonDocument::fromJson(readFile(json.path)).object();
    check(written["metadata"].toObject()["filters"].toObject()["selection"].toBool(),
          "The selection JSON states the selection in its filters");

    // Refusals: no bodies, a schedule, and the CSV without the selection line on a whole-model
    // file.
    const auto refused = [](auto action, const char *code) {
        try {
            action();
        } catch (const InspectionError &error) {
            return std::string(error.code()) == code;
        }
        return false;
    };
    ReportFileOptions none = csv;
    none.path = dir.filePath("no-bodies.csv");
    check(refused([&] { writeReportFile(doc, none, nullptr); }, "INVALID_REQUEST") &&
              !QFileInfo::exists(none.path),
          "A selection without the bodies is refused, and nothing is written");
    ReportFileOptions scheduled = csv;
    scheduled.path = dir.filePath("scheduled.csv");
    scheduled.schedule = "Anything";
    check(refused([&] { writeReportFile(doc, scheduled, &bodies); }, "INVALID_REQUEST") &&
              !QFileInfo::exists(scheduled.path),
          "A schedule cannot use the selection, and nothing is written");
    ReportFileOptions whole = csv;
    whole.path = dir.filePath("whole.csv");
    whole.selection = false;
    writeReportFile(doc, whole, &bodies);
    check(!QString::fromUtf8(readFile(whole.path)).contains("#filters.selection"),
          "A whole-model file has no selection line, even when bodies are passed");
}

// R101.e1 (UX_DESIGN §19.3, §19.8): a cancelled write publishes nothing.
void cancelledWrite() {
    const auto doc = loadDocument(sourcePath("tests/fixtures/reports/quantities-a.sketchyup"));
    QTemporaryDir dir;
    check(dir.isValid(), "A temporary directory");
    std::atomic<bool> stop{true};
    ReportHooks hooks;
    hooks.cancel = &stop;
    ReportFileOptions options;
    options.path = dir.filePath("cancelled.csv");
    options.format = "csv";
    bool cancelled = false;
    try {
        writeReportFile(doc, options, nullptr, hooks);
    } catch (const ReportCancelled &) {
        cancelled = true;
    }
    check(cancelled, "A cancelled write throws ReportCancelled");
    check(!QFileInfo::exists(options.path), "A cancelled write leaves no file behind");
}

// V12 (JSON): the file is byte-identical to the session's report.run data, less the session-only
// fields of its invalid rows (reportFileData), and the CSV is the CSV of the same data. The CLI
// writes the same bytes because it calls the same functions.
void sessionParity() {
    const auto doc = loadDocument(sourcePath("tests/fixtures/reports/quantities-a.sketchyup"));
    QTemporaryDir dir;
    check(dir.isValid(), "A temporary directory");
    for (const auto &golden : {defaultCase(), materialCase()}) {
        const auto data = sessionReport(doc, golden.request);
        ReportFileOptions json = golden.options;
        json.path = dir.filePath(golden.name + "-parity.json");
        json.format = "json";
        writeReportFile(doc, json);
        check(readFile(json.path) == jsonBytes(reportFileData(data)),
              "The JSON file is the session's report.run data, less the session-only fields");

        ReportFileOptions csv = golden.options;
        csv.path = dir.filePath(golden.name + "-parity.csv");
        csv.format = "csv";
        writeReportFile(doc, csv);
        check(readFile(csv.path) == reportCsv(data), "The CSV file is the CSV of the session data");
    }
}

} // namespace

int main(int argc, char **argv) {
    // Generators (see the header comment). They write only the path they are given.
    if (argc == 3 && QString::fromLocal8Bit(argv[1]) == "--write-fixture") {
        writeFixture(QString::fromLocal8Bit(argv[2]));
        return 0;
    }
    if (argc == 3 && QString::fromLocal8Bit(argv[1]) == "--write-goldens") {
        const auto doc = loadDocument(sourcePath("tests/fixtures/reports/quantities-a.sketchyup"));
        const auto directory = QString::fromLocal8Bit(argv[2]);
        for (const auto &golden : {defaultCase(), materialCase()}) {
            const auto data = sessionReport(doc, golden.request);
            writeFile(directory + "/" + golden.name + ".csv", reportCsv(data));
            writeFile(directory + "/" + golden.name + ".json", jsonBytes(data));
        }
        return 0;
    }
    try {
        // The fixture is the builder's document, payload for payload (the identity aside).
        Document fresh;
        buildQuantitiesA(fresh);
        const auto loaded = loadDocument(sourcePath("tests/fixtures/reports/quantities-a.sketchyup"));
        check(payloadWithoutIdentity(loaded) == payloadWithoutIdentity(fresh),
              "The quantities-a fixture is the builder's document");
        csvDialect();
        quoting();
        metadataMatrix();
        pins();
        goldens();
        sessionParity();
        invalidRowsInSession();
        selectionFile();
        cancelledWrite();
        std::cout << "report output tests passed\n";
    } catch (const std::exception &error) {
        std::cerr << "report output tests failed: " << error.what() << '\n';
        return 1;
    }
    return 0;
}
