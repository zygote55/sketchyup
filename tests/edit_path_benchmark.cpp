// R117 (benchmark only; EXCLUDE_FROM_ALL): timings of the document's edit path, headless.
// It times the document API alone (no viewport, no GPU), so it runs on any machine with the core
// build. Frames need the GPU PC and are measured by limits_hardware_benchmark instead.
//
//   edit_path_benchmark MODE SIZE [PLACEMENTS] [SAMPLES]
//
//   prisms SIZE          SIZE 26-sided prism definitions, one placement each: creation curve,
//                        placement moves, definition edits, undo and redo
//   placements SIZE      one 26-sided prism definition placed SIZE times: the same edits
//   sphere-def SIZE P    one closed sphere definition of about SIZE faces, placed P times: the same
//                        edits, plus the history charge and memory of one definition edit
//   sphere-body SIZE     one closed sphere body of about SIZE faces: moves and push-pulls
//
// Prints one JSON object on stdout. Build with -DSKETCHYUP_BENCHMARK_LIMIT_SCALE=16 for counts
// above the product limit; the product build reports the product limits.
#include "core/components.hpp"
#include "core/document_limits.hpp"
#include "core/model.hpp"
#include "geometry/topology.hpp"
#ifdef R117_EDIT_IO
#include "io/document_io.hpp"
#include <QFile>
#include <QFileInfo>
#include <QString>
#endif

#include <malloc.h>
#include <unistd.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <numbers>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

using namespace sketchy;

namespace {

using Clock = std::chrono::steady_clock;

void check(bool value, const std::string &message) {
    if (!value)
        throw std::runtime_error(message);
}

double msSince(Clock::time_point start) {
    return std::chrono::duration<double, std::milli>(Clock::now() - start).count();
}

struct Summary {
    size_t n{};
    double p50{}, p95{}, max{};
};

Summary summarize(std::vector<double> samples) {
    Summary s;
    s.n = samples.size();
    if (samples.empty())
        return s;
    std::sort(samples.begin(), samples.end());
    auto rank = [&](double q) {
        const auto index = size_t(std::ceil(q * double(samples.size())));
        return samples[std::min(samples.size(), std::max<size_t>(1, index)) - 1];
    };
    s.p50 = rank(.5);
    s.p95 = rank(.95);
    s.max = samples.back();
    return s;
}

std::string json(const Summary &s) {
    std::ostringstream out;
    out << "{\"n\": " << s.n << ", \"p50\": " << s.p50 << ", \"p95\": " << s.p95
        << ", \"max\": " << s.max << "}";
    return out.str();
}

double residentMiB() {
    std::ifstream statm("/proc/self/statm");
    long long pages = 0, resident = 0;
    statm >> pages >> resident;
    return double(resident) * double(sysconf(_SC_PAGESIZE)) / (1024.0 * 1024.0);
}

// Bytes malloc has handed out and not yet freed: what the process holds, not what it has mapped.
double heapInUseMiB() {
    const auto info = mallinfo2();
    return double(info.uordblks + info.hblkhd) / (1024.0 * 1024.0);
}

// A closed triangulated sphere: 2 * sides * rings faces (the R114 shape, copied for this driver).
std::shared_ptr<Body> sphere(Id id, Vec3 centre, double radius, int sides, int rings) {
    auto body = std::make_shared<Body>();
    body->id = id;
    body->name = "Sphere " + std::to_string(id);
    auto &surface = body->surface;
    auto add = [&](Vec3 point) {
        const Id vertex = surface.nextId++;
        surface.vertices.emplace(vertex, centre + point);
        return vertex;
    };
    const Id north = add({0, 0, radius});
    std::vector<std::vector<Id>> ring(static_cast<std::size_t>(rings));
    for (auto &row : ring)
        row.resize(size_t(sides));
    for (int r = 0; r < rings; ++r) {
        const double theta = std::numbers::pi * (r + 1) / (rings + 1);
        for (int j = 0; j < sides; ++j) {
            const double phi = 2 * std::numbers::pi * j / sides;
            ring[size_t(r)][size_t(j)] =
                add({radius * std::sin(theta) * std::cos(phi),
                     radius * std::sin(theta) * std::sin(phi), radius * std::cos(theta)});
        }
    }
    const Id south = add({0, 0, -radius});
    for (int j = 0; j < sides; ++j) {
        const int next = (j + 1) % sides;
        surface.addFaceIds({{north, ring.front()[size_t(j)], ring.front()[size_t(next)]}});
    }
    for (int r = 0; r + 1 < rings; ++r)
        for (int j = 0; j < sides; ++j) {
            const int next = (j + 1) % sides;
            const auto &a = ring[size_t(r)], &b = ring[size_t(r + 1)];
            surface.addFaceIds({{a[size_t(j)], b[size_t(j)], b[size_t(next)]}});
            surface.addFaceIds({{a[size_t(j)], b[size_t(next)], a[size_t(next)]}});
        }
    for (int j = 0; j < sides; ++j) {
        const int next = (j + 1) % sides;
        surface.addFaceIds({{south, ring.back()[size_t(next)], ring.back()[size_t(j)]}});
    }
    body->topology = Topology::rebuild(body->surface, body->topology);
    return body;
}

// The 26-sided prism of the placement families: 28 faces.
std::shared_ptr<Body> prism(Id id, int sides, double radius, Vec3 offset) {
    std::vector<Vec3> loop;
    for (int i = 0; i < sides; ++i) {
        const auto angle = 2 * std::numbers::pi * i / sides;
        loop.push_back({radius * std::cos(angle), radius * std::sin(angle), 0});
    }
    auto body = std::make_shared<Body>();
    body->id = id;
    body->name = "Prism " + std::to_string(id);
    const Id face = body->surface.addFace({loop});
    body->surface.extrude(face, 1);
    body->transform = Transform::translation(offset);
    body->topology = Topology::rebuild(body->surface, body->topology);
    return body;
}

size_t facesOf(const Document &doc) {
    size_t faces = 0;
    for (const auto &[id, body] : doc.bodies())
        faces += body->surface.faces.size();
    return faces;
}

// Timings of one edit kind: each sample edits the same base state, then undoes, redoes and undoes
// it, so one edit is in history at a time and its charge is read alone.
struct EditKind {
    std::vector<double> feedback, undo, redo;
    std::vector<double> charge;
    std::string refusal;
};

// A refusal message as a JSON string body (quotes and backslashes escaped).
std::string escaped(const std::string &text) {
    std::string out;
    for (char c : text) {
        if (c == '"' || c == '\\')
            out += '\\';
        out += c;
    }
    return out;
}

std::string editKindJson(const EditKind &kind) {
    std::ostringstream out;
    out << "{\"samples\": " << kind.feedback.size()
        << ", \"feedbackMs\": " << json(summarize(kind.feedback))
        << ", \"undoMs\": " << json(summarize(kind.undo))
        << ", \"redoMs\": " << json(summarize(kind.redo)) << ", \"historyBytesAfterEdit\": [";
    for (size_t i = 0; i < kind.charge.size(); ++i)
        out << (i ? ", " : "") << kind.charge[i];
    out << "], \"refusal\": \"" << escaped(kind.refusal) << "\"}";
    return out.str();
}

Id memberWithFaces(const ComponentDefinition &definition) {
    for (const auto &[id, body] : definition.members)
        if (!body->surface.faces.empty())
            return id;
    throw std::runtime_error("The definition has a member with faces");
}

} // namespace

int main(int argc, char **argv) {
    try {
        if (argc < 3 || argc > 5)
            throw std::runtime_error("Usage: edit_path_benchmark MODE SIZE [PLACEMENTS] [SAMPLES]");
        const std::string mode = argv[1];
        const long long size = std::atoll(argv[2]);
        // placements SIZE places one definition SIZE times; the others take PLACEMENTS.
        const long long placements =
            mode == "placements" ? size : (argc >= 4 ? std::atoll(argv[3]) : 1);
        const int samples = argc >= 5 ? std::atoi(argv[4]) : 20;
        check(size >= 1 && placements >= 1 && samples >= 1, "Sizes and samples are positive");

        Document doc;
        std::ostringstream creation;
        std::vector<Id> instances;
        Id definition = 0;
        Id target = 0;
        double fixtureMs = 0;
        const auto fixtureStart = Clock::now();
        if (mode == "prisms") {
            std::map<Id, BodyPtr> bodies;
            const int columns = int(std::ceil(std::sqrt(double(size))));
            for (long long i = 0; i < size; ++i)
                bodies.emplace(Id(i + 1), prism(Id(i + 1), 26, 1,
                                                {4.0 * (i % columns), 4.0 * (i / columns), 0}));
            doc.restore("00000000000000000000000000000f01", Id(size + 1), std::move(bodies));
            // Creation curve: cumulative time at each power of two, and at the end.
            auto start = Clock::now();
            creation << "[";
            bool first = true;
            for (long long i = 1; i <= size; ++i) {
                const auto component = createComponent(doc, Id(i), "Part " + std::to_string(i));
                instances.push_back(component.instance);
                if (i == 1)
                    definition = component.definition;
                if ((i & (i - 1)) == 0 || i == size) {
                    creation << (first ? "" : ", ") << "{\"definitions\": " << i
                             << ", \"cumulativeMs\": " << msSince(start) << "}";
                    first = false;
                }
            }
            creation << "]";
        } else if (mode == "placements" || mode == "sphere-def") {
            std::map<Id, BodyPtr> bodies;
            if (mode == "placements")
                bodies.emplace(1, prism(1, 26, 1, {0, 0, 0}));
            else {
                // Closed sphere: 2 * sides * rings faces, with 50 rings up to 40,000 faces and 250
                // above.
                const int rings = size <= 40000 ? 50 : 250;
                const int sides = int(size / (2 * rings));
                check(sides >= 3, "Sphere size is at least 300 faces");
                bodies.emplace(1, sphere(1, {0, 0, 0}, 20, sides, rings));
            }
            doc.restore("00000000000000000000000000000f02", 2, std::move(bodies));
            const auto component = createComponent(doc, 1, "Benchmark definition");
            instances.push_back(component.instance);
            definition = component.definition;
            const int columns = int(std::ceil(std::sqrt(double(placements))));
            for (long long i = 1; i < placements; ++i)
                instances.push_back(
                    placeComponent(
                        doc, definition,
                        Transform::translation(
                            {(mode == "placements" ? 4.0 : 50.0) * double(i % columns),
                             (mode == "placements" ? 4.0 : 50.0) * double(i / columns), 0}))
                        .instance);
        } else if (mode == "sphere-body") {
            const int rings = size <= 40000 ? 50 : 250;
            const int sides = int(size / (2 * rings));
            check(sides >= 3, "Sphere size is at least 300 faces");
            std::map<Id, BodyPtr> bodies{{1, sphere(1, {0, 0, 0}, 20, sides, rings)}};
            doc.restore("00000000000000000000000000000f03", 2, std::move(bodies));
            target = 1;
        } else {
            throw std::runtime_error("Unknown mode: " + mode);
        }
        fixtureMs = msSince(fixtureStart);
        // Start from an empty history, as a user's opened file does: the fixture's own edits do
        // not count against the edits measured below.
        doc = doc.readSnapshot();
        std::ostringstream native;
#ifdef R117_EDIT_IO
        // Native save and open, through the production path (product builds only).
        {
            const std::string file =
                std::string(std::getenv("R117_SCRATCH") ? std::getenv("R117_SCRATCH") : "/tmp") +
                "/r117-edit-path-" + mode + "-" + std::to_string(size) + ".sketchyup";
            const auto saveStart = Clock::now();
            saveDocument(doc, QString::fromStdString(file));
            const double saveSeconds = msSince(saveStart) / 1000;
            const auto openStart = Clock::now();
            Document reopened = loadDocument(QString::fromStdString(file));
            const double openSeconds = msSince(openStart) / 1000;
            native << ",\n  \"native\": {\"saveSeconds\": " << saveSeconds
                   << ", \"openSeconds\": " << openSeconds
                   << ", \"fileBytes\": " << QFileInfo(QString::fromStdString(file)).size()
                   << ", \"reopenedDefinitions\": " << reopened.definitions().size()
                   << ", \"reopenedHasNoHistory\": " << (reopened.canUndo() ? "false" : "true")
                   << "}";
            QFile::remove(QString::fromStdString(file));
            check(reopened.definitions().size() == doc.definitions().size(),
                  "The reopened document keeps every definition");
        }
#endif
        check(!doc.canUndo() && doc.historyBytes() == 0, "The measured document has no history");

        std::ostringstream report;
        report << "{\n  \"mode\": \"" << mode << "\",\n  \"size\": " << size
               << ",\n  \"placements\": " << placements << ",\n  \"samples\": " << samples
               << ",\n  \"limitScale\": " << DocumentLimits::scale << ",\n  \"buildType\": \""
               << SKETCHYUP_BENCHMARK_BUILD_TYPE << "\""
               << ",\n  \"fixtureMs\": " << fixtureMs << native.str()
               << ",\n  \"creationCurveMs\": "
               << (mode == "prisms" ? creation.str() : std::string("[]"))
               << ",\n  \"counts\": {\"definitions\": " << doc.definitions().size()
               << ", \"instances\": " << doc.instances().size()
               << ", \"bodies\": " << doc.bodies().size() << ", \"faces\": " << facesOf(doc) << "}";

        if (mode == "sphere-body") {
            // Push-pull and moves of one body. A push-pull grows the solid by 1 cm and is undone.
            const auto faceId = doc.bodies().at(target)->surface.faces.begin()->first;
            const auto base = doc.bodies().at(target)->transform;
            const auto parent = doc.bodies().at(target)->parent;
            std::vector<double> pushMs, pushUndoMs, moveMs;
            std::string refusal;
            // Memory of one body move, first sample: what the history holds after it.
            double bodyHeapBefore = heapInUseMiB(), bodyHeapEdited = 0, bodyHeapUndone = 0;
            double bodyCharge = 0;
            for (int i = 0; i < samples; ++i) {
                auto start = Clock::now();
                try {
                    doc.pushPull(target, faceId, .01);
                } catch (const std::exception &error) {
                    refusal = error.what();
                    break;
                }
                pushMs.push_back(msSince(start));
                start = Clock::now();
                doc.undo();
                pushUndoMs.push_back(msSince(start));
            }
            for (int i = 0; i < samples; ++i) {
                const auto start = Clock::now();
                doc.transform(
                    target, Transform::translation({0, 0, .001 * (i % 2 ? -1 : 1)}) * base, parent);
                moveMs.push_back(msSince(start));
                if (i == 0) {
                    bodyHeapEdited = heapInUseMiB();
                    bodyCharge = double(doc.historyBytes());
                }
                doc.undo();
                if (i == 0)
                    bodyHeapUndone = heapInUseMiB();
            }
            report << ",\n  \"bodyMoveMemoryMiB\": {\"heapInUseBefore\": " << bodyHeapBefore
                   << ", \"heapInUseAfterMove\": " << bodyHeapEdited
                   << ", \"heapInUseAfterUndo\": " << bodyHeapUndone
                   << ", \"historyBytesAfterMove\": " << bodyCharge << "}";
            report << ",\n  \"pushPullMs\": " << json(summarize(pushMs))
                   << ",\n  \"pushPullUndoMs\": " << json(summarize(pushUndoMs))
                   << ",\n  \"bodyMoveMs\": " << json(summarize(moveMs))
                   << ",\n  \"pushPullRefusal\": \"" << escaped(refusal) << "\"";
        } else {
            // Placement moves: one placement, moved and undone, as the harness does.
            const Id moved = instances.front();
            const auto base = doc.bodies().at(moved)->transform;
            const auto parent = doc.bodies().at(moved)->parent;
            std::vector<double> moveMs;
            for (int i = 0; i < samples; ++i) {
                const auto start = Clock::now();
                doc.transform(moved, Transform::translation({0, 0, .001 * (i % 2 ? -1 : 1)}) * base,
                              parent);
                moveMs.push_back(msSince(start));
                doc.undo();
            }
            report << ",\n  \"placementMoveMs\": " << json(summarize(moveMs));

            // Definition edits: one member moved in the definition, every placement regenerated.
            EditKind kind;
            const auto definitionRecord = doc.definitions().at(definition);
            const Id member = memberWithFaces(*definitionRecord);
            const auto memberBody = definitionRecord->members.at(member);
            const Transform memberBase = memberBody->transform;
            const Id memberParent = memberBody->parent;
            const double rssBefore = residentMiB(), heapBefore = heapInUseMiB();
            double rssEdited = 0, heapEdited = 0, rssUndone = 0, heapUndone = 0;
            for (int i = 0; i < samples; ++i) {
                const auto start = Clock::now();
                try {
                    editComponentDefinition(doc, definition, [&](Document &draft) {
                        draft.transform(member,
                                        Transform::translation({0, 0, .001 * (i % 2 ? -1 : 1)}) *
                                            memberBase,
                                        memberParent);
                        return ChangeReport{};
                    });
                } catch (const std::exception &error) {
                    kind.refusal = error.what();
                    break;
                }
                kind.feedback.push_back(msSince(start));
                kind.charge.push_back(double(doc.historyBytes()));
                if (i == 0) {
                    rssEdited = residentMiB();
                    heapEdited = heapInUseMiB();
                }
                auto undoStart = Clock::now();
                doc.undo();
                kind.undo.push_back(msSince(undoStart));
                if (i == 0) {
                    rssUndone = residentMiB();
                    heapUndone = heapInUseMiB();
                }
                auto redoStart = Clock::now();
                doc.redo();
                kind.redo.push_back(msSince(redoStart));
                doc.undo(); // Back to the base state; the next edit clears the redo entry.
            }
            report << ",\n  \"definitionEdit\": " << editKindJson(kind)
                   << ",\n  \"memoryMiB\": {\"residentBefore\": " << rssBefore
                   << ", \"residentAfterEdit\": " << rssEdited
                   << ", \"residentAfterUndo\": " << rssUndone
                   << ", \"heapInUseBefore\": " << heapBefore
                   << ", \"heapInUseAfterEdit\": " << heapEdited
                   << ", \"heapInUseAfterUndo\": " << heapUndone << "}";
        }
        report << "\n}\n";
        std::cout << report.str();
        return 0;
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
