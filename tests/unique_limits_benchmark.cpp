// R110 software measurement for the raised unique limits (ADR 0005, R082w v3). Core and I/O
// only: no viewport, so no frame time. Manual and synthetic; never a timing gate. Each mode prints
// key: value lines.
//   family <count>                  26-sided prisms, the real_model_benchmark unique shape
//   dense                           three closed 100,000-face spheres in one component
//   padded <file> <paddedBodies>    the 5,769-body family, whose first <paddedBodies> bodies carry
//                                   128 properties of 2,040 bytes; written as a container
//   load <file>                     a fresh process: read and load the file, save it, index it
//   history <count> <moved>         the family, one edit that moves the first <moved> bodies and
//                                   stays in undo history; then undo and redo
#include "core/components.hpp"
#include "core/groups.hpp"
#include "core/transform_selection.hpp"
#include "geometry/inference.hpp"
#include "geometry/topology.hpp"
#include "io/document_io.hpp"
#include <QCoreApplication>
#include <QFile>
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <map>
#include <memory>
#include <numbers>
#include <string>
#include <sys/resource.h>
#include <unistd.h>
#include <vector>
using namespace sketchy;
namespace {
using Clock = std::chrono::steady_clock;
using Matrix = std::array<double, 16>;
void check(bool ok, const std::string &message) {
    if (!ok)
        throw std::runtime_error(message);
}
double seconds(Clock::time_point from, Clock::time_point to) {
    return std::chrono::duration<double>(to - from).count();
}
double p95(std::vector<double> values) {
    std::sort(values.begin(), values.end());
    return values.at(std::min(values.size() - 1, size_t(std::ceil(values.size() * .95)) - 1));
}
double peakMiB() {
    rusage usage{};
    check(getrusage(RUSAGE_SELF, &usage) == 0, "Read peak RSS");
    return usage.ru_maxrss / 1024.0;
}
double currentMiB() {
    QFile status("/proc/self/statm");
    check(status.open(QIODevice::ReadOnly), "Read current RSS");
    const auto columns = status.readAll().simplified().split(' ');
    check(columns.size() > 1, "Current RSS has resident pages");
    return double(columns[1].toLongLong()) * double(::sysconf(_SC_PAGESIZE)) / 1048576.0;
}
// A prism of `sides` sides about the origin, extruded 1 m: sides + 2 faces. The offset is the
// body's transform, as the benchmark's transform call sets it.
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
// The same family as real_model_benchmark's unique scenario: `count` distinct prisms on a grid.
Document family(int count, int padded = 0) {
    std::map<Id, BodyPtr> bodies;
    const int columns = int(std::ceil(std::sqrt(count)));
    for (int i = 0; i < count; ++i) {
        const Id id = Id(i + 1);
        auto body = prism(id, 26, 1 + .2 * i / count,
                          {4.0 * (i % columns), 4.0 * (i / columns), 0});
        if (i < padded)
            for (int p = 0; p < 128; ++p)
                body->properties["benchmark_" + std::to_string(p)] =
                    std::string(2040, char('a' + p % 26));
        bodies.emplace(id, body);
    }
    Document doc;
    doc.restore("0000000000000000000000000000083c", Id(count + 1), std::move(bodies));
    return doc;
}
// The R110 densest shape: a closed triangulated sphere of `sides` by `rings` vertices, with
// 2 + sides * rings vertices, 2 * sides * rings faces and 3 edges per face pair.
std::shared_ptr<Body> sphere(Id id, Vec3 centre, int sides, int rings) {
    const double radius = 20;
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
    const size_t perRing = size_t(sides), rowCount = size_t(rings);
    std::vector<Id> row(perRing);
    std::vector<std::vector<Id>> ring(rowCount, row);
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
// Three spheres of 100,000 faces held by one component. Returns the instance to query.
Id densest(Document &doc) {
    std::map<Id, BodyPtr> bodies;
    for (Id id = 1; id <= 3; ++id)
        bodies.emplace(id, sphere(id, {60.0 * double(id - 1), 0, 0}, 200, 250));
    doc.restore("00000000000000000000000000000833", 4, std::move(bodies));
    const auto group = createGroup(doc, {1, 2, 3}, "Dense mesh");
    return createComponent(doc, group, "Dense mesh").instance;
}
// The camera of inference_index_benchmark: a fixed oblique perspective framing the box.
InferenceCamera frame(Vec3 low, Vec3 high) {
    auto multiply = [](const Matrix &a, const Matrix &b) {
        Matrix r{};
        for (int c = 0; c < 4; ++c)
            for (int row = 0; row < 4; ++row)
                for (int k = 0; k < 4; ++k)
                    r[c * 4 + row] += a[k * 4 + row] * b[c * 4 + k];
        return r;
    };
    const auto center = (low + high) * .5, extent = high - low;
    const auto radius = length(extent) * .5;
    const auto eye = center + normalized({-.6, -1, .9}) * radius * 1.6;
    const auto f = normalized(center - eye), s = normalized(cross(f, {0, 0, 1})),
               u = cross(s, f);
    const Matrix view{s.x, u.x, -f.x, 0, s.y, u.y, -f.y, 0, s.z, u.z, -f.z, 0,
                      -dot(s, eye), -dot(u, eye), dot(f, eye), 1};
    const double near = radius * .02, far = radius * 4, t = 1 / std::tan(.4),
                 aspect = 1920. / 1080;
    const Matrix projection{t / aspect, 0, 0, 0, 0, t, 0, 0, 0, 0, -(far + near) / (far - near),
                            -1, 0, 0, -2 * far * near / (far - near), 0};
    InferenceCamera camera;
    camera.clipFromWorld = multiply(projection, view);
    const Matrix inverseProjection{aspect / t, 0, 0, 0, 0, 1 / t, 0, 0, 0, 0, 0,
                                   -(far - near) / (2 * far * near), 0, 0, -1,
                                   (far + near) / (2 * far * near)};
    const Matrix inverseView{s.x, s.y, s.z, 0, u.x, u.y, u.z, 0, -f.x, -f.y, -f.z, 0,
                             eye.x, eye.y, eye.z, 1};
    camera.worldFromClip = multiply(inverseView, inverseProjection);
    camera.width = 1920;
    camera.height = 1080;
    return camera;
}
// The world-space box of every body's vertices.
std::pair<Vec3, Vec3> boundsOf(const Document &doc) {
    Vec3 low{1e300, 1e300, 1e300}, high{-1e300, -1e300, -1e300};
    for (const auto &[id, body] : doc.bodies()) {
        const auto transform = doc.worldTransform(id);
        for (const auto &[vertex, point] : body->surface.vertices) {
            (void)vertex;
            const auto p = transform.point(point);
            low = {std::min(low.x, p.x), std::min(low.y, p.y), std::min(low.z, p.z)};
            high = {std::max(high.x, p.x), std::max(high.y, p.y), std::max(high.z, p.z)};
        }
    }
    return {low, high};
}
// Inference index build, then 50 queries at placements spread over `ids`.
void reportInference(const Document &doc, const std::vector<Id> &ids, Vec3 low, Vec3 high) {
    const auto camera = frame(low, high);
    InferenceIndex index;
    const auto built = Clock::now();
    index.sync(doc);
    const auto buildMs = seconds(built, Clock::now()) * 1000;
    std::vector<double> queries;
    size_t candidates = 0, answered = 0;
    for (int i = 0; i < 50; ++i) {
        const Id id = ids[size_t(i) * (ids.size() - 1) / 49];
        const auto screen = camera.project(doc.worldTransform(id).point({0, 0, 1}));
        if (!screen)
            continue;
        InferenceQuery query;
        query.camera = camera;
        query.x = screen->x;
        query.y = screen->y;
        query.radius = 8;
        const auto started = Clock::now();
        const auto result = index.query(query);
        queries.push_back(seconds(started, Clock::now()) * 1000);
        candidates += result.candidates.size();
        ++answered;
    }
    check(!queries.empty(), "At least one inference query is inside the clip volume");
    std::printf("inference index build: %.1f ms (%zu primitives, %.1f MiB index-owned)\n", buildMs,
                index.primitiveCount(), double(index.indexBytes()) / 1048576.0);
    std::printf("inference query p95: %.2f ms over %zu queries (%.1f candidates each on average)\n",
                p95(queries), answered, double(candidates) / double(answered));
}
// Save and load timings and the container's size, measured in this process.
void reportPersistence(const Document &doc) {
    const auto saved = Clock::now();
    const auto container = encodeContainer(doc);
    const auto saveSeconds = seconds(saved, Clock::now());
    const auto loaded = Clock::now();
    const auto reloaded = decodeContainer(container);
    const auto loadSeconds = seconds(loaded, Clock::now());
    check(reloaded.materializedTotals() == doc.materializedTotals(),
          "The reloaded model has the same totals");
    const auto chunk = encodeDocument(doc, AssetStorage::External).size();
    std::printf("document chunk: %.1f MiB (bound 96 MiB); container: %.1f MiB\n",
                double(chunk) / 1048576.0, double(container.size()) / 1048576.0);
    std::printf("save (encode container): %.2f s; load (decode container): %.2f s\n", saveSeconds,
                loadSeconds);
}
void printTotals(const Document &doc) {
    const auto totals = doc.materializedTotals();
    std::printf("unique geometry: %zu bodies, %zu vertices, %zu faces, %zu edges\n",
                size_t(totals.records), size_t(totals.vertices), size_t(totals.faces),
                size_t(totals.edges));
}
std::vector<Id> bodyIds(const Document &doc) {
    std::vector<Id> ids;
    for (const auto &[id, body] : doc.bodies()) {
        (void)body;
        ids.push_back(id);
    }
    return ids;
}
} // namespace
int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    try {
        const std::string mode = argc > 1 ? argv[1] : "";
        if (mode == "family" && argc == 3) {
            const int count = std::atoi(argv[2]);
            check(count >= 1 && count <= 10000, "Family count must be 1..10000");
            const auto built = Clock::now();
            Document doc = family(count);
            std::printf("family %d: built in %.2f s\n", count, seconds(built, Clock::now()));
            printTotals(doc);
            std::printf("peak RSS after build: %.0f MiB; current %.0f MiB\n", peakMiB(),
                        currentMiB());
            reportPersistence(doc);
            const auto [low, high] = boundsOf(doc);
            reportInference(doc, bodyIds(doc), low, high);
            std::printf("peak RSS: %.0f MiB\n", peakMiB());
        } else if (mode == "dense" && argc == 2) {
            Document doc;
            const auto instance = densest(doc);
            printTotals(doc);
            std::printf("peak RSS after build: %.0f MiB\n", peakMiB());
            reportPersistence(doc);
            reportInference(doc, {instance}, {-20, -20, -20}, {140, 20, 20});
            std::printf("peak RSS: %.0f MiB\n", peakMiB());
        } else if (mode == "padded" && argc == 4) {
            const int padded = std::atoi(argv[3]);
            check(padded >= 0 && padded <= 5769, "Padded bodies must be 0..5769");
            Document doc = family(5769, padded);
            printTotals(doc);
            const auto container = encodeContainer(doc);
            QFile out(argv[2]);
            check(out.open(QIODevice::WriteOnly | QIODevice::Truncate) &&
                      out.write(container) == qint64(container.size()),
                  "Write the padded container");
            std::printf("padded %d bodies: chunk %.1f MiB; container %.1f MiB written to file\n",
                        padded, double(encodeDocument(doc, AssetStorage::External).size()) / 1048576.0,
                        double(container.size()) / 1048576.0);
        } else if (mode == "load" && argc == 3) {
            QFile input(argv[2]);
            check(input.open(QIODevice::ReadOnly), "Open the container file");
            const auto started = Clock::now();
            const auto bytes = input.readAll();
            const auto doc = decodeContainer(bytes);
            const auto loadSeconds = seconds(started, Clock::now());
            std::printf("file %.1f MiB loaded in %.2f s; peak RSS %.0f MiB; current %.0f MiB\n",
                        double(bytes.size()) / 1048576.0, loadSeconds, peakMiB(), currentMiB());
            printTotals(doc);
            reportPersistence(doc);
            const auto [low, high] = boundsOf(doc);
            reportInference(doc, bodyIds(doc), low, high);
            std::printf("peak RSS: %.0f MiB\n", peakMiB());
        } else if (mode == "history" && argc == 4) {
            const int count = std::atoi(argv[2]), moved = std::atoi(argv[3]);
            check(count >= 1 && count <= 10000 && moved >= 1 && moved <= count,
                  "History: 1 <= moved <= count <= 10000");
            Document doc = family(count);
            printTotals(doc);
            std::printf("before the edit: peak RSS %.0f MiB; current %.0f MiB\n", peakMiB(),
                        currentMiB());
            TransformTargets targets;
            for (int i = 1; i <= moved; ++i)
                targets.insert({Id(i)});
            const auto historyBefore = doc.historyBytes();
            const auto edited = Clock::now();
            (void)transformSelected(doc, targets, Transform::translation({0, 0, 1}));
            std::printf("edit of %d bodies accepted in %.2f s; charged %.1f MiB of history (budget "
                        "%.0f MiB)\n",
                        moved, seconds(edited, Clock::now()),
                        double(doc.historyBytes() - historyBefore) / 1048576.0,
                        double(Document::historyLimit) / 1048576.0);
            std::printf("kept in history: peak RSS %.0f MiB; current %.0f MiB\n", peakMiB(),
                        currentMiB());
            const auto undone = Clock::now();
            doc.undo();
            const auto undoSeconds = seconds(undone, Clock::now());
            const auto redone = Clock::now();
            doc.redo();
            std::printf("undo %.2f s; redo %.2f s; peak RSS %.0f MiB\n", undoSeconds,
                        seconds(redone, Clock::now()), peakMiB());
            check(doc.canUndo(), "The edit remains in undo history after redo");
        } else {
            std::cerr << "usage: unique_limits_benchmark family <count> | dense | padded <file> "
                         "<paddedBodies> | load <file> | history <count> <moved>\n";
            return 2;
        }
        return 0;
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
