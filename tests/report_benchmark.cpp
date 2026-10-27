// ADR 0175 §12 budgets on quantities-b (R101.b). Not a test: build with
// `cmake --build <dir> --target report_benchmark` in a Release build and run it. The build type is
// part of the result; the ADR notes name it.
//
//   report_benchmark [placements]          the budgets (default 10,000 placements)
//   report_benchmark --write-fixture PATH  writes quantities-b as a native container, for a CLI run
//
// Measured here: counts, areas and solid volumes over the whole document; the CSV write of a real
// report (a tag grouping of about a thousand groups) and of a 100,000-row
// report; the process's resident memory before the runs and at the peak.
//
//   report_benchmark --cancel [placements]   R101.e1: the cancel latency on quantities-b (the flag
//                                            set from another thread), and the longest gap between
//                                            two records, which bounds any cancel
//   report_benchmark --rerun [runs] [terrain]
//                                            R101.e1: the panel's re-run after one edit, the 95th
//                                            percentile (ADR 0175 §12). Without `terrain` the model
//                                            is 8,333 placements of the 12-face part, 99,996 faces;
//                                            with it, quantities-b's terrain is added too (its
//                                            triangles are faces, 349,214 in all, outside the
//                                            budget)
//
// Not measured here: the schedule re-run (R101.c).
#include "automation/report_commands.hpp"
#include "core/components.hpp"
#include "core/report.hpp"
#include "core/tags.hpp"
#include "core/transform.hpp"
#include "io/document_io.hpp"
#include "io/report_csv.hpp"
#include "report_fixtures.hpp"
#include <QByteArray>
#include <QFile>
#include <QJsonArray>
#include <QJsonObject>
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <string>
#include <thread>
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

// R101.e1: the cancel latency. The run is on a worker thread; the main thread waits until the run
// has visited `stopAt` records, sets the flag, and the worker's ReportCancelled is timed from
// there. The longest gap between two records is the worst case for any cancel, because the flag is
// read only between records.
void cancelLatency(std::size_t placements) {
    const auto doc = buildQuantitiesB(placements);
    const auto total = doc.bodies().size();
    std::cout << "cancel on quantities-b: " << total << " records, all four tables\n";
    double worstGap = 0;
    for (const auto stopAt : {std::size_t(1), total / 10, total / 2, total - 1}) {
        std::atomic<bool> stop{false};
        std::atomic<std::size_t> done{0};
        std::chrono::steady_clock::time_point requested, returned, last;
        bool cancelled = false;
        ReportHooks hooks;
        hooks.cancel = &stop;
        hooks.progress = [&](std::size_t count, std::size_t) {
            const auto now = std::chrono::steady_clock::now();
            if (count > 1)
                worstGap = std::max(worstGap, seconds(now - last));
            last = now;
            done.store(count, std::memory_order_release);
        };
        std::thread worker([&] {
            try {
                runReport(doc, ReportRequest{}, nullptr, hooks);
            } catch (const ReportCancelled &) {
                returned = std::chrono::steady_clock::now();
                cancelled = true;
            }
        });
        while (done.load(std::memory_order_acquire) < stopAt)
            std::this_thread::yield();
        const auto visitedAtRequest = done.load(std::memory_order_acquire);
        requested = std::chrono::steady_clock::now();
        stop.store(true, std::memory_order_release);
        worker.join();
        if (!cancelled) {
            std::cout << "  stop after record " << stopAt << ": the run finished first\n";
            continue;
        }
        std::cout << "  flag set at record " << visitedAtRequest << " of " << total
                  << ": returned after " << seconds(returned - requested) * 1000 << " ms\n";
    }
    std::cout << "longest gap between two records: " << worstGap * 1000 << " ms\n";
}

// The nearest-rank percentile of a sample, in seconds.
double percentile(std::vector<double> sample, double fraction) {
    std::sort(sample.begin(), sample.end());
    const auto rank = std::size_t(std::ceil(fraction * double(sample.size())));
    return sample[std::max<std::size_t>(rank, 1) - 1];
}

// The re-run's model: `placements` placements of the 12-face part, built as quantities-b builds
// its part, and with `withTerrain` quantities-b's 250,000-triangle terrain too.
Document partsModel(std::size_t placements, bool withTerrain) {
    Document doc;
    if (withTerrain) {
        const auto mesh = flatGrid(353, 1.);
        createTerrain(doc, mesh, terrainRecord(mesh));
    }
    const auto first = box(doc, {0, 0, 0}, {.5, .5, 2});
    const auto second = box(doc, {1, 0, 0}, {.5, .5, 2});
    const auto group = createGroup(doc, {first, second}, "Part");
    const auto definition = createComponent(doc, group, "Part").definition;
    Selection origin;
    origin.apply(doc, {{doc.instances().begin()->first, SelectionKind::Body, 0}},
                 SelectionMode::Replace);
    eraseSelected(doc, origin);
    const std::size_t columns = 100;
    for (std::size_t i = 0; i < placements; ++i)
        placeComponent(
            doc, definition,
            Transform::translation({double(i % columns) * 2, double(i / columns) * 2 + 10, 0}));
    return doc;
}

// R101.e1 (ADR 0175 §12): the panel's re-run. One edit before each run (which moves the revision),
// then the engine's run and the description the panel reads. The first two runs are warm-up.
void rerun(std::size_t runs, bool withTerrain) {
    auto doc = partsModel(8333, withTerrain);
    std::size_t faces = 0;
    for (const auto &[id, record] : doc.bodies())
        faces += record->surface.faces.size();
    std::vector<Id> placements;
    for (const auto &[id, instance] : doc.instances())
        placements.push_back(id);
    std::cout << "re-run on a model of " << faces << " faces (" << doc.bodies().size()
              << " records, " << placements.size() << " placements, "
              << (withTerrain ? "with" : "without") << " the terrain), " << runs
              << " runs after two warm-ups\n";

    std::vector<double> engine, described;
    for (std::size_t i = 0; i < runs + 2; ++i) {
        doc.transform(placements[i % placements.size()],
                      Transform::translation({0.001 * double(i + 1), 0, 0}));
        auto start = std::chrono::steady_clock::now();
        runReport(doc, ReportRequest{});
        const auto afterEngine = seconds(std::chrono::steady_clock::now() - start);
        start = std::chrono::steady_clock::now();
        reportDescription(doc, ReportRequest{}, nullptr);
        const auto afterDescription = seconds(std::chrono::steady_clock::now() - start);
        if (i >= 2) {
            engine.push_back(afterEngine);
            described.push_back(afterDescription);
        }
    }
    std::cout << "engine run:          p95 " << percentile(engine, 0.95) * 1000 << " ms, median "
              << percentile(engine, 0.5) * 1000 << " ms, max "
              << *std::max_element(engine.begin(), engine.end()) * 1000 << " ms\n";
    std::cout << "run and description: p95 " << percentile(described, 0.95) * 1000 << " ms, median "
              << percentile(described, 0.5) * 1000 << " ms, max "
              << *std::max_element(described.begin(), described.end()) * 1000 << " ms\n";
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
    if (argc >= 2 && std::string(argv[1]) == "--cancel") {
        cancelLatency(argc > 2 ? std::size_t(std::atoll(argv[2])) : std::size_t(10000));
        return 0;
    }
    if (argc >= 2 && std::string(argv[1]) == "--rerun") {
        const bool withTerrain = argc > 3 && std::string(argv[3]) == "terrain";
        rerun(argc > 2 ? std::size_t(std::atoll(argv[2])) : std::size_t(40), withTerrain);
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
