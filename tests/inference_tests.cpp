#include "geometry/inference.hpp"
#include <algorithm>
#include <chrono>
#include <iostream>
#include <numbers>
using namespace sketchy;
void check(bool ok, const char *message) {
    if (!ok)
        throw std::runtime_error(message);
}
InferenceCamera camera(double scale = 1) {
    InferenceCamera c;
    c.clipFromWorld = {scale, 0, 0, 0, 0, scale, 0, 0, 0, 0, -.1, 0, 0, 0, 0, 1};
    c.worldFromClip = {1 / scale, 0, 0, 0, 0, 1 / scale, 0, 0, 0, 0, -10, 0, 0, 0, 0, 1};
    c.width = 1000;
    c.height = 800;
    return c;
}
InferenceResult at(const InferenceIndex &index, Vec3 p, InferenceCamera cam = camera(),
                   double dx = 0, double dy = 0) {
    const auto screen = *cam.project(p);
    return index.query({cam, screen.x + dx, screen.y + dy, 8, {}, {}});
}
bool has(const InferenceResult &r, InferenceKind kind, Vec3 point) {
    return std::any_of(r.candidates.begin(), r.candidates.end(), [&](const auto &c) {
        return c.kind == kind && length(c.point - point) < tolerance;
    });
}
int main(int argc, char **argv) {
    try {
        Document doc;
        doc.addWire(0, {-.8, 0, 0}, {.8, 0, 0});
        InferenceIndex index;
        index.sync(doc);
        for (auto scale : {.5, 1., 2.}) {
            const auto c = camera(scale);
            check(has(at(index, {-.8, 0, 0}, c, 7, 0), InferenceKind::Endpoint, {-.8, 0, 0}),
                  "Seven logical pixels acquires endpoint at every zoom");
            check(!has(at(index, {-.8, 0, 0}, c, 9, 0), InferenceKind::Endpoint, {-.8, 0, 0}),
                  "Nine logical pixels misses endpoint at every zoom");
        }
        check(has(at(index, {0, 0, 0}), InferenceKind::Midpoint, {0, 0, 0}), "Edge midpoint");
        check(has(at(index, {.25, 0, 0}, camera(), 0, 5), InferenceKind::OnEdge, {.25, 0, 0}),
              "Exact edge projection");
        const auto builds = index.bodyBuilds();
        index.sync(doc);
        check(index.bodyBuilds() == builds, "Unchanged snapshot reuses index");
        doc.addWire(0, {0, -.8, 0}, {0, .8, 0});
        index.sync(doc);
        check(index.bodyBuilds() == builds + 1, "Adding context rebuilds only new body");
        check(has(at(index, {0, 0, 0}), InferenceKind::Intersection, {0, 0, 0}),
              "Intersecting loose edges across contexts");
        doc.move(2, {0, 0, .01});
        index.sync(doc);
        auto crossing = at(index, {0, 0, 0});
        check(!has(crossing, InferenceKind::Intersection, {0, 0, 0}),
              "Screen crossing never changes geometric tolerance");
        const auto p = camera().project({0, 0, 0});
        auto filtered = index.query({camera(), p->x, p->y, 8, DrawingPlane{}, 0});
        check(std::all_of(filtered.candidates.begin(), filtered.candidates.end(),
                          [](const auto &c) { return std::abs(c.point.z) < tolerance; }),
              "Locked plane excludes off-plane candidates");
        auto branchA = doc, branchB = doc;
        branchA.move(1, {0, .2, 0});
        branchB.move(1, {0, .4, 0});
        index.sync(branchA);
        index.sync(branchB);
        check(has(at(index, {-.8, .4, 0}), InferenceKind::Endpoint, {-.8, .4, 0}),
              "Different snapshots at the same identity/revision cannot reuse a stale index");
        Document curved;
        curved.addCurve(0, centerCurve(CurveKind::Circle, {}, .5, 0, 2 * std::numbers::pi, 24));
        index.sync(curved);
        check(has(at(index, {}), InferenceKind::Center, {}), "Analytic center");
        check(has(at(index, {.1, .1, 0}), InferenceKind::OnFace, {.1, .1, 0}),
              "On-face ray intersection");
        curved.addFace({{{-.7, -.7, 1}, {.7, -.7, 1}, {.7, .7, 1}, {-.7, .7, 1}}});
        index.sync(curved);
        check(!has(at(index, {}), InferenceKind::Center, {}), "Occluded center is not acquired");
        curved.undo();
        index.sync(curved);
        check(has(at(index, {}), InferenceKind::Center, {}), "Undo invalidates occlusion index");
        Document piercing;
        piercing.addWire(0, {-.5, 0, -1}, {.5, 0, 1});
        index.sync(piercing);
        auto result = index.query({camera(), 500, 400, 8, DrawingPlane{}, 0});
        check(has(result, InferenceKind::Intersection, {}),
              "Edge intersects locked construction plane");
        // Perspective interpolation: projected midpoint is not world midpoint.
        InferenceCamera perspective;
        perspective.width = 1000;
        perspective.height = 800;
        perspective.clipFromWorld = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, -1.02, -1, 0, 0, -.202, 0};
        // Inverse of the preceding OpenGL perspective matrix.
        perspective.worldFromClip = {1, 0, 0, 0,         0, 1, 0,  0,
                                     0, 0, 0, -1 / .202, 0, 0, -1, 1.02 / .202};
        Document depth;
        depth.addWire(0, {-1, 0, -2}, {1, 0, -4});
        index.sync(depth);
        const auto screenA = *perspective.project({-1, 0, -2}),
                   screenB = *perspective.project({1, 0, -4});
        result = index.query({perspective, (screenA.x + screenB.x) / 2, 400, 8, {}, {}});
        check(has(result, InferenceKind::OnEdge, {-1.0 / 3, 0, -8.0 / 3}),
              "Perspective-correct edge acquisition");
        // Dense benchmark uses 1,000 independently transformed bodies / 100k
        // triangles; component instancing remains a later milestone.
        const bool benchmark = argc > 1 && std::string(argv[1]) == "--benchmark";
        const unsigned count = benchmark ? 1000 : 40;
        Body prototype;
        for (unsigned y = 0; y <= 5; ++y)
            for (unsigned x = 0; x <= 10; ++x)
                prototype.surface.vertices[prototype.surface.nextId++] = {x * .01, y * .01, 0};
        for (unsigned y = 0; y < 5; ++y)
            for (unsigned x = 0; x < 10; ++x) {
                const Id a = 1 + y * 11 + x;
                const auto id = prototype.surface.nextId++;
                prototype.surface.faces.emplace(id, Face{id, {{a, a + 1, a + 12, a + 11}}});
            }
        prototype.topology = Topology::rebuild(prototype.surface, {});
        std::map<Id, BodyPtr> bodies;
        for (unsigned i = 0; i < count; ++i) {
            auto body = std::make_shared<Body>(prototype);
            body->id = i + 1;
            body->transform =
                Transform::translation({(i % 20) * .12 - 1.2, (i / 20) * .07 - 1.75, 0});
            bodies.emplace(body->id, body);
        }
        Document dense;
        dense.restore(dense.identity(), count + 1, std::move(bodies));
        const auto start = std::chrono::steady_clock::now();
        index.sync(dense);
        const auto buildMs =
            std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start)
                .count();
        std::vector<double> times;
        size_t visited = 0;
        for (unsigned i = 0; i < 200; ++i) {
            const auto t = std::chrono::steady_clock::now();
            const auto r =
                at(index,
                   {(i % 20) * .12 - 1.15, ((i * 7) % std::max(1u, count / 20)) * .07 - 1.725, 0},
                   camera(.4));
            times.push_back(
                std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t)
                    .count());
            visited += r.visitedPrimitives;
        }
        std::sort(times.begin(), times.end());
        check(visited / 200 < index.primitiveCount() / 3, "Pointer queries visit a spatial subset");
        const auto oldBuilds = index.bodyBuilds();
        dense.move(1, {.01, 0, 0});
        index.sync(dense);
        check(index.bodyBuilds() == oldBuilds + 1, "Dense edit rebuilds one body's geometry");
        std::cout << "Inference planes, occlusion, transforms, perspective and logical-pixel "
                     "acquisition passed\n"
                  << "bodies=" << count << " triangles=" << count * 100
                  << " primitives=" << index.primitiveCount() << " build_ms=" << buildMs
                  << " query_p95_ms=" << times[190] << " mean_visited=" << visited / 200 << '\n';
        if (benchmark)
            check(times[190] < 50, "Dense inference exceeds the 50 ms p95 budget");
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
