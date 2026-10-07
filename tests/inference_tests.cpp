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
        // Real viewport matrices from the 25-unique-prism performance fixture.
        // The float inverse has a small round-trip error. Reprojecting a surface
        // candidate must not let its own face hide it.
        Document precision;
        std::vector<Vec3> precisionLoop;
        for (int i = 0; i < 26; ++i) {
            const auto angle = 2 * std::numbers::pi * i / 26;
            precisionLoop.push_back({1.064 * std::cos(angle), 1.064 * std::sin(angle), 0});
        }
        const auto precisionBody = precision.addFace({precisionLoop});
        precision.extrude(precisionBody,
                          precision.bodies().at(precisionBody)->surface.faces.begin()->first, 1);
        precision.transform(precisionBody, Transform::translation({12, 4, 0}));
        InferenceCamera captured;
        captured.clipFromWorld = {0.9602474570274353,  -0.9791561961174011, -0.5792322754859924,
                                  -0.5792279839515686, 0.9602474570274353,  0.9791561961174011,
                                  0.5792322754859924,  0.5792279839515686,  0,
                                  1.9776078462600708,  -0.5735806822776794, -0.5735764503479004,
                                  -15.455582618713379, -1.050899863243103,  37.42549514770508,
                                  37.43265151977539};
        captured.worldFromClip = {0.520699143409729,
                                  0.520699143409729,
                                  0,
                                  0,
                                  -0.16797274214241043,
                                  0.1679857684948729,
                                  0.3393215835094452,
                                  8.094048666862363e-07,
                                  -3975.7705078125,
                                  1810.4658203125,
                                  -2936.37890625,
                                  -134.52928161621094,
                                  3975.220703125,
                                  -1809.900146484375,
                                  2935.8271484375,
                                  134.5302734375};
        captured.width = 1920;
        captured.height = 1080;
        InferenceIndex precisionIndex;
        precisionIndex.sync(precision);
        const InferenceQuery precisionQuery{captured, 957.2705078125, 655.7332153320312, 8};
        const auto precise = precisionIndex.query(precisionQuery);
        check(std::any_of(precise.candidates.begin(), precise.candidates.end(),
                          [&](const auto &candidate) { return candidate.body == precisionBody; }),
              "A visible face cannot occlude itself after camera round-trip error");
        const auto blocker =
            precision.addFace({{{-100, -100, 2}, {100, -100, 2}, {100, 100, 2}, {-100, 100, 2}}});
        precisionIndex.sync(precision);
        const auto blocked = precisionIndex.query(precisionQuery);
        check(
            std::none_of(blocked.candidates.begin(), blocked.candidates.end(),
                         [&](const auto &candidate) { return candidate.body == precisionBody; }) &&
                std::any_of(blocked.candidates.begin(), blocked.candidates.end(),
                            [&](const auto &candidate) { return candidate.body == blocker; }),
            "A separate foreground face still occludes the original surface");
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
        InferenceQuery policy{camera(), 500, 400};
        policy.visible = [](Id body, InferenceEntity, Id) { return body != 2; };
        check(has(index.query(policy), InferenceKind::Center, {}),
              "Hidden face does not occlude visible geometry");
        policy.visible = {};
        policy.pointVisible = [](Id body, Vec3 point) { return body != 2 || point.z < .5; };
        check(has(index.query(policy), InferenceKind::Center, {}),
              "Clipped ray intersections no longer occlude retained inference");
        check(!has(index.query(policy), InferenceKind::OnFace, {0, 0, 1}),
              "Removed surface points are not inference candidates");
        policy.extraOcclusion = [](Vec3, Vec3, double) { return true; };
        check(index.query(policy).candidates.empty(),
              "Derived section surfaces can occlude native candidates");
        policy.pointVisible = {};
        policy.extraOcclusion = {};
        policy.eligible = [](Id body, InferenceEntity, Id) { return body != 2; };
        check(index.query(policy).candidates.empty(),
              "Visible locked geometry still occludes eligible geometry");
        policy.visible = [](Id body, InferenceEntity type, Id) {
            return body == 1 && type != InferenceEntity::Edge;
        };
        check(!has(index.query(policy), InferenceKind::Center, {}),
              "An entirely hidden curve outline does not expose its center");
        const auto endpoint = curved.bodies().at(1)->surface.vertices.begin()->second;
        const auto endpointScreen = *camera().project(endpoint);
        policy.x = endpointScreen.x;
        policy.y = endpointScreen.y;
        check(!has(index.query(policy), InferenceKind::Endpoint, endpoint),
              "Hidden incident edges do not expose vertex inference");
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
        Document guides;
        guides.addGuide(0, guideLine({0, .2, 0}, {1, 0, 0}));
        guides.addGuide(1, guideLine({.3, 0, 0}, {0, 1, 0}));
        guides.addGuide(1, guidePoint({-.4, -.3, 0}));
        index.sync(guides);
        check(index.primitiveCount() == 3, "Infinite guides have no artificial endpoints or midpoints");
        for (auto scale : {.5, 1., 2.}) {
            check(has(at(index, {-.4, -.3, 0}, camera(scale), 7, 0),
                      InferenceKind::GuidePoint, {-.4, -.3, 0}), "Guide point uses logical pixels");
            check(has(at(index, {.1, .2, 0}, camera(scale), 0, 5),
                      InferenceKind::OnGuide, {.1, .2, 0}), "Guide line acquisition across zooms");
        }
        result = at(index, {.3, .2, 0});
        check(has(result, InferenceKind::Intersection, {.3, .2, 0}), "Infinite guide intersection");
        check(std::any_of(result.candidates.begin(), result.candidates.end(), [](const auto &c) {
            return c.kind == InferenceKind::Intersection && c.entityType == InferenceEntity::Guide &&
                   c.otherEntityType == InferenceEntity::Guide;
        }), "Guide intersection retains both typed identities");
        check(index.query({camera(), 650, 320, 8, {}, 0, false}).candidates.empty(),
              "Hidden guides cannot be acquired");
        guides.addWire(0, {.3, -.5, 0}, {.3, .5, 0});
        index.sync(guides);
        result = at(index, {.3, .2, 0});
        check(std::any_of(result.candidates.begin(), result.candidates.end(), [](const auto &c) {
            return c.kind == InferenceKind::Intersection && c.otherBody &&
                   c.entityType != c.otherEntityType;
        }), "Mixed guide and model intersection has distinct identity namespaces");
        guides.addFace({{{-.9, -.9, 1}, {.9, -.9, 1}, {.9, .9, 1}, {-.9, .9, 1}}});
        index.sync(guides);
        result = at(index, {.3, .2, 0});
        check(has(result, InferenceKind::OnGuide, {.3, .2, 0}), "Guide overlay is visible through faces");
        check(std::none_of(result.candidates.begin(), result.candidates.end(), [](const auto &c) {
            return c.kind == InferenceKind::Intersection && c.otherBody &&
                   c.entityType != c.otherEntityType;
        }), "Occluded model/guide intersections remain hidden");
        Document guideDepth;
        guideDepth.addGuide(0, guideLine({0, 0, -3}, {1, 0, -1}));
        index.sync(guideDepth);
        check(has(at(index, {-.3, 0, -2.7}, perspective), InferenceKind::OnGuide, {-.3, 0, -2.7}),
              "Perspective guide acquisition is world accurate");
        guideDepth.addGuide(1, guideLine({0, 0, -2}, {0, 1, 0}));
        index.sync(guideDepth);
        check(!has(at(index, {0, 0, -3}, perspective), InferenceKind::Intersection, {0, 0, -3}),
              "Projected guide crossings do not create false 3D intersections");
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
