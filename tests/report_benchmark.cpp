// ADR 0175 §12 budgets on quantities-b (R101.b). Not a test: build with
// `cmake --build <dir> --target report_benchmark` in a Release build and run it. The build type is
// part of the result; the ADR notes name it.
//
//   report_benchmark [placements]          the budgets (default 10,000 placements)
//   report_benchmark --write-fixture PATH  writes quantities-b as a native container, for a CLI run
//
// Measured here: counts, areas and solid volumes over the whole document; the CSV write of a real
// report (a tag grouping of about a thousand groups) and of a 100,000-row
// report; the process's resident memory before the runs and at the peak. Not measured here: the
// desktop re-run and cancel (R101.e) and the schedule re-run (R101.c).
#include "automation/report_commands.hpp"
#include "core/components.hpp"
#include "core/report.hpp"
#include "core/tags.hpp"
#include "io/document_io.hpp"
#include "io/report_csv.hpp"
#include "report_fixtures.hpp"
#include <QByteArray>
#include <QFile>
#include <QJsonArray>
#include <QJsonObject>
#include <chrono>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

using namespace sketchy;
using namespace sketchy::report_fixtures;

namespace {

double seconds(std::chrono::steady_clock::duration span) {
    return std::chrono::duration<double>(span).count();
}

// A field of /proc/self/status (Linux), in kB, or -1.
long statusKilobytes(const std::string &field) {
    std::ifstream status("/proc/self/status");
    for (std::string line; std::getline(status, line);)
        if (line.rfind(field, 0) == 0)
            return std::atol(line.c_str() + field.size());
    return -1;
}

void timed(const char *label, const Document &doc, const ReportRequest &request) {
    const auto start = std::chrono::steady_clock::now();
    const auto result = runReport(doc, request);
    std::cout << label << ": " << seconds(std::chrono::steady_clock::now() - start) << " s";
    if (!result.counts.empty())
        std::cout << " (" << result.counts.front().contexts << " contexts)";
    std::cout << '\n';
}

// The data rows of a CSV: every line that is not a metadata line or the header.
std::size_t dataRows(const QByteArray &csv) {
    std::size_t rows = 0;
    for (const auto &line : csv.split('\n'))
        if (!line.isEmpty() && !line.startsWith('#') && !line.startsWith("section,"))
            ++rows;
    return rows;
}

// Times the CSV write of one data object and reports its rows and size.
void timedCsv(const char *label, const QJsonObject &data) {
    const auto start = std::chrono::steady_clock::now();
    const auto csv = reportCsv(data);
    const auto elapsed = seconds(std::chrono::steady_clock::now() - start);
    std::cout << label << ": " << elapsed << " s, " << dataRows(csv) << " rows, " << csv.size()
              << " bytes\n";
}

} // namespace

int main(int argc, char **argv) {
    if (argc == 3 && std::string(argv[1]) == "--write-fixture") {
        const auto doc = buildQuantitiesB(10000);
        QFile file(QString::fromLocal8Bit(argv[2]));
        if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate) ||
            file.write(encodeContainer(doc)) < 0)
            return 1;
        return 0;
    }
    const auto placements = argc > 1 ? std::size_t(std::atoll(argv[1])) : std::size_t(10000);

    const auto build = std::chrono::steady_clock::now();
    auto doc = buildQuantitiesB(placements);
    std::cout << "quantities-b: " << placements << " placements, " << doc.bodies().size()
              << " records, built in " << seconds(std::chrono::steady_clock::now() - build)
              << " s\n";
    const auto residentBefore = statusKilobytes("VmRSS:");
    std::cout << "VmRSS before the runs: " << residentBefore << " kB\n";

    ReportRequest counts;
    counts.areas = counts.volumes = false;
    timed("counts (groupBy none)", doc, counts);
    ReportRequest areas;
    areas.counts = areas.volumes = false;
    timed("areas", doc, areas);
    ReportRequest volumes;
    volumes.counts = volumes.areas = false;
    timed("solid volumes", doc, volumes);
    timed("all tables", doc, ReportRequest{});

    // The CSV write of a real report. A document holds at most 1024 tags (validateTagRecords), so
    // the placements share 1000 tags, which gives a real report of about a thousand groups.
    std::vector<Id> placed;
    for (const auto &[id, instance] : doc.instances())
        placed.push_back(id);
    std::vector<Id> tagIds;
    for (std::size_t t = 0; t < 1000; ++t)
        tagIds.push_back(createTag(doc, "Placement group " + std::to_string(t)));
    for (std::size_t i = 0; i < placed.size(); ++i)
        assignTag(doc, placed[i], tagIds[i % tagIds.size()]);
    ReportRequest byTag;
    byTag.groupBy = ReportGroup::Tag;
    const auto start = std::chrono::steady_clock::now();
    const auto data = reportDescription(doc, byTag, nullptr);
    std::cout << "report by tag, engine and JSON: "
              << seconds(std::chrono::steady_clock::now() - start) << " s\n";
    timedCsv("CSV write, report by tag", data);

    // A 100,000-row report: 33,334 count groups of three rows each, the metadata of the real
    // report.
    QJsonArray synthetic;
    for (int i = 0; synthetic.size() < 33334; ++i)
        synthetic.append(QJsonObject{{"key", QString::number(i + 1)},
                                     {"name", "Group " + QString::number(i)},
                                     {"contexts", i},
                                     {"instances", 1},
                                     {"definitions", 1}});
    auto large = data;
    large["counts"] = synthetic;
    large["areas"] = QJsonArray();
    large["volumes"] = QJsonArray();
    large["invalid"] = QJsonArray();
    large["terrain"] = QJsonArray();
    timedCsv("CSV write, 100,000 rows", large);

    std::cout << "VmRSS after the runs: " << statusKilobytes("VmRSS:") << " kB\n";
    std::cout << "VmHWM (peak, whole process): " << statusKilobytes("VmHWM:") << " kB\n";
    return 0;
}
