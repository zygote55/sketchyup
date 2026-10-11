// R082.ee: the shared-local inference index must return exactly what a fully
// expanded brute-force world-space search returns, and match the pre-R082.ee
// index wherever that index's result did not depend on its traversal order.
#include "core/components.hpp"
#include "core/groups.hpp"
#include "core/selection.hpp"
#include "core/tags.hpp"
#include "geometry/inference_detail.hpp"
#include "legacy_inference_index.hpp"
#include <algorithm>
#include <iostream>
#include <numbers>
#include <random>
#include <sstream>
using namespace sketchy;
namespace {
void check(bool ok, const std::string &message) {
    if (!ok)
        throw std::runtime_error(message);
}
using Matrix = std::array<double, 16>; // Column-major, like InferenceCamera.
Matrix multiply(const Matrix &a, const Matrix &b) {
    Matrix r{};
    for (int c = 0; c < 4; ++c)
        for (int row = 0; row < 4; ++row)
            for (int k = 0; k < 4; ++k)
                r[c * 4 + row] += a[k * 4 + row] * b[c * 4 + k];
    return r;
}
Matrix inverse(Matrix m) {
    Matrix inv{1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
    auto at = [](Matrix &x, int row, int col) -> double & { return x[col * 4 + row]; };
    for (int col = 0; col < 4; ++col) {
        int pivot = col;
        for (int row = col + 1; row < 4; ++row)
            if (std::abs(at(m, row, col)) > std::abs(at(m, pivot, col)))
                pivot = row;
        for (int k = 0; k < 4; ++k) {
            std::swap(at(m, col, k), at(m, pivot, k));
            std::swap(at(inv, col, k), at(inv, pivot, k));
        }
        const auto scale = 1 / at(m, col, col);
        for (int k = 0; k < 4; ++k) {
            at(m, col, k) *= scale;
            at(inv, col, k) *= scale;
        }
        for (int row = 0; row < 4; ++row)
            if (row != col) {
                const auto f = at(m, row, col);
                for (int k = 0; k < 4; ++k) {
                    at(m, row, k) -= f * at(m, col, k);
                    at(inv, row, k) -= f * at(inv, col, k);
                }
            }
    }
    return inv;
}
InferenceCamera makeCamera(Vec3 eye, Vec3 target, double radius, bool perspective,
                           bool floatMatrices) {
    const auto f = normalized(target - eye);
    auto up = std::abs(f.z) > .95 ? Vec3{0, 1, 0} : Vec3{0, 0, 1};
    const auto s = normalized(cross(f, up)), u = cross(s, f);
    const Matrix view{s.x, u.x, -f.x, 0, s.y, u.y, -f.y, 0, s.z, u.z, -f.z, 0,
                      -dot(s, eye), -dot(u, eye), dot(f, eye), 1};
    const double width = 1280, height = 720, aspect = width / height;
    const auto distance = length(target - eye), near = distance * .05, far = distance * 4;
    Matrix projection{};
    if (perspective) {
        const auto t = 1 / std::tan(.45);
        projection = {t / aspect, 0, 0, 0, 0, t, 0, 0, 0, 0, -(far + near) / (far - near), -1,
                      0, 0, -2 * far * near / (far - near), 0};
    } else {
        const auto h = radius * 1.2, w = h * aspect;
        projection = {1 / w, 0, 0, 0, 0, 1 / h, 0, 0, 0, 0, -2 / (far - near), 0,
                      0, 0, -(far + near) / (far - near), 1};
    }
    InferenceCamera camera;
    camera.clipFromWorld = multiply(projection, view);
    camera.worldFromClip = inverse(camera.clipFromWorld);
    if (floatMatrices) // The viewport supplies float matrices with an approximate inverse.
        for (auto *matrix : {&camera.clipFromWorld, &camera.worldFromClip})
            for (auto &value : *matrix)
                value = double(float(value));
    camera.width = width;
    camera.height = height;
    return camera;
}
// Brute-force oracle: every scene body expanded to world primitives exactly as
// the pre-R082.ee index stored them, visited linearly in a shuffled order.
struct Expanded {
    std::map<Id, BodyPtr> records;
    std::map<Id, Transform> worlds;
    std::map<Id, std::map<Id, std::vector<Id>>> vertexEdges;
    std::vector<std::pair<Id, inference_detail::Primitive>> primitives;
};
Expanded expand(const Document &doc, std::mt19937_64 &rng) {
    using inference_detail::bounds;
    Expanded e;
    for (const auto &[id, body] : doc.bodies()) {
        const auto world = doc.worldTransform(id);
        e.records[id] = body;
        e.worlds[id] = world;
        auto add = [&](InferenceKind kind, Id entity, Vec3 a, Vec3 b, Vec3 c) {
            e.primitives.push_back({id, {kind, entity, a, b, c, bounds(a, b, c)}});
        };
        const auto adjacency = body->topology.adjacency(body->surface);
        e.vertexEdges[id] = adjacency.vertexEdges;
        for (const auto &[vertex, local] : body->surface.vertices) {
            const auto p = world.point(local);
            add(InferenceKind::Endpoint, vertex, p, p, p);
            if (adjacency.vertexEdges.contains(vertex) &&
                adjacency.vertexEdges.at(vertex).size() >= 3)
                add(InferenceKind::Intersection, vertex, p, p, p);
        }
        for (const auto &[edge, record] : body->topology.edges) {
            const auto a = world.point(body->surface.vertices.at(record.a)),
                       b = world.point(body->surface.vertices.at(record.b));
            const auto midpoint = (a + b) * .5;
            add(InferenceKind::Midpoint, edge, midpoint, midpoint, midpoint);
            add(InferenceKind::OnEdge, edge, a, b, b);
        }
        for (const auto &[curve, c] : body->curves) {
            const auto center = world.point(c.center);
            add(InferenceKind::Center, curve, center, center, center);
        }
        for (const auto &[guideId, guide] : body->guides) {
            const auto origin = world.point(guide.origin);
            if (guide.kind == GuideKind::Point)
                add(InferenceKind::GuidePoint, guideId, origin, origin, origin);
            else {
                const auto ends =
                    boundedGuideLine(guideLine(origin, world.vector(guide.direction)));
                add(InferenceKind::OnGuide, guideId, ends[0], ends[1], ends[1]);
            }
        }
        for (const auto &t : body->surface.triangles())
            add(InferenceKind::OnFace, t.face, world.point(t.a), world.point(t.b),
                world.point(t.c));
    }
    std::shuffle(e.primitives.begin(), e.primitives.end(), rng);
    return e;
}
InferenceResult bruteForce(const Expanded &e, const InferenceQuery &q) {
    return inference_detail::runQuery(
        q,
        [&](const inference_detail::Planes &planes, Id context, const auto &fn) {
            for (const auto &[id, p] : e.primitives)
                if ((!context || context == id) && inference_detail::intersects(p.box, planes))
                    fn(id, p);
        },
        [&](Id id) {
            return inference_detail::BodyView{&e.vertexEdges.at(id), e.records.at(id).get(),
                                              e.records.at(id).get(), &e.worlds.at(id)};
        });
}
std::string describe(const InferenceCandidate &c) {
    std::ostringstream s;
    s.precision(17);
    s << inferenceLabel(c.kind) << " (" << c.point.x << ',' << c.point.y << ',' << c.point.z
      << ") body=" << c.body << '/' << inferenceEntityLabel(c.entityType) << c.entity
      << " other=" << c.otherBody << '/' << inferenceEntityLabel(c.otherEntityType)
      << c.otherEntity << " px=" << c.pixels << " depth=" << c.depth;
    return s.str();
}
bool same(const InferenceCandidate &a, const InferenceCandidate &b) {
    return a.kind == b.kind && a.point == b.point && a.body == b.body && a.entity == b.entity &&
           a.otherBody == b.otherBody && a.otherEntity == b.otherEntity &&
           a.pixels == b.pixels && a.depth == b.depth && a.entityType == b.entityType &&
           a.otherEntityType == b.otherEntityType;
}
void requireIdentical(const InferenceResult &actual, const InferenceResult &expected,
                      const std::string &where) {
    bool ok = actual.candidates.size() == expected.candidates.size() &&
              actual.visitedPrimitives == expected.visitedPrimitives &&
              actual.intersectionPairs == expected.intersectionPairs &&
              actual.truncated == expected.truncated;
    for (size_t i = 0; ok && i < actual.candidates.size(); ++i)
        ok = same(actual.candidates[i], expected.candidates[i]);
    if (ok)
        return;
    std::cerr << where << ": index visited=" << actual.visitedPrimitives
              << " pairs=" << actual.intersectionPairs << " truncated=" << actual.truncated
              << " | brute visited=" << expected.visitedPrimitives
              << " pairs=" << expected.intersectionPairs << " truncated=" << expected.truncated
              << '\n';
    for (const auto &c : actual.candidates)
        std::cerr << "  index " << describe(c) << '\n';
    for (const auto &c : expected.candidates)
        std::cerr << "  brute " << describe(c) << '\n';
    throw std::runtime_error(where + ": shared index differs from expanded brute force");
}
// The pre-R082.ee index reported an intersecting pair in spatial traversal order.
std::vector<InferenceCandidate> canonical(std::vector<InferenceCandidate> candidates) {
    for (auto &c : candidates)
        if (c.otherBody && std::tie(c.otherBody, c.otherEntityType, c.otherEntity) <
                               std::tie(c.body, c.entityType, c.entity)) {
            std::swap(c.body, c.otherBody);
            std::swap(c.entity, c.otherEntity);
            std::swap(c.entityType, c.otherEntityType);
        }
    // Identity first: a pair's point is evaluated from its first edge, so its last
    // bits (and pixels/depth) may differ when the legacy pair order differs.
    std::sort(candidates.begin(), candidates.end(), [](const auto &a, const auto &b) {
        return std::tie(a.kind, a.body, a.entityType, a.entity, a.otherBody, a.otherEntityType,
                        a.otherEntity, a.pixels, a.depth, a.point.x, a.point.y, a.point.z) <
               std::tie(b.kind, b.body, b.entityType, b.entity, b.otherBody, b.otherEntityType,
                        b.otherEntity, b.pixels, b.depth, b.point.x, b.point.y, b.point.z);
    });
    return candidates;
}
// Exact, except a pair intersection compares within the engine's point tolerance.
bool equivalent(const InferenceCandidate &a, const InferenceCandidate &b) {
    if (!a.otherBody || !b.otherBody)
        return same(a, b);
    return a.kind == b.kind && a.body == b.body && a.entity == b.entity &&
           a.otherBody == b.otherBody && a.otherEntity == b.otherEntity &&
           a.entityType == b.entityType && a.otherEntityType == b.otherEntityType &&
           length(a.point - b.point) <= tolerance && std::abs(a.pixels - b.pixels) <= 1e-6;
}
Id prism(Document &doc, int sides, double radius, double height) {
    std::vector<Vec3> loop;
    for (int i = 0; i < sides; ++i) {
        const auto angle = 2 * std::numbers::pi * i / sides;
        loop.push_back({radius * std::cos(angle), radius * std::sin(angle), 0});
    }
    const auto body = doc.addFace({loop});
    doc.extrude(body, doc.bodies().at(body)->surface.faces.begin()->first, height);
    return body;
}
Id newest(const Document &doc, const std::map<Id, BodyPtr> &before) {
    for (const auto &[id, body] : doc.bodies())
        if (!before.contains(id))
            return id;
    throw std::runtime_error("No new body");
}
struct Scene {
    Document doc;
    Id leafDefinition{}, leafMember{}, assemblyDefinition{}, outer{}, mirrored{}, unique{};
    std::vector<Id> instances;
};
Transform randomPlacement(std::mt19937_64 &rng, int kind) {
    std::uniform_real_distribution<double> unit(-1, 1), angle(0, 2 * std::numbers::pi);
    const auto t = Transform::translation({unit(rng) * 12, unit(rng) * 12, unit(rng) * 2});
    const auto r = Transform::rotation(normalized({unit(rng), unit(rng), unit(rng) + 2}),
                                       angle(rng));
    switch (kind % 4) {
    case 0:
        return t * r;
    case 1: // Mirrored.
        return t * r * Transform::scaling({-1, 1, 1});
    case 2: // Non-uniformly scaled.
        return t * r * Transform::scaling({1.7, .6, 1.25});
    default: // Mirrored and scaled.
        return t * Transform::scaling({.8, -1.3, 1}) * r;
    }
}
// Nested, transformed, mirrored and scaled instances beside unique geometry,
// curves, wires and guides; `offset` moves everything to distant coordinates.
Scene buildScene(std::mt19937_64 &rng, Vec3 offset) {
    Scene s;
    auto &doc = s.doc;
    const auto leafBody = prism(doc, 7, 1, 1.5);
    // Crossing wires inside the definition produce edge/edge intersections.
    doc.addWire(leafBody, {-1.2, 0, .75}, {1.2, 0, .75});
    doc.addWire(leafBody, {0, -1.2, .75}, {0, 1.2, .75});
    const auto leaf = createComponent(doc, leafBody, "Leaf");
    s.leafDefinition = leaf.definition;
    s.leafMember = leaf.movedGeometry.at(leaf.instance);
    s.instances.push_back(leaf.instance);
    for (int i = 0; i < 10; ++i)
        s.instances.push_back(
            placeComponent(doc, leaf.definition, randomPlacement(rng, i)).instance);
    auto before = doc.bodies();
    doc.addCurve(0, centerCurve(CurveKind::Circle,
                                DrawingPlane::make({3, 0, .2}, {0, 0, 1}, {1, 0, 0}), .7, 0,
                                2 * std::numbers::pi, 16));
    const auto curveBody = newest(doc, before);
    const auto assemblyRoot = createGroup(doc, {s.instances.front(), curveBody}, "Assembly");
    const auto assembly = createComponent(doc, assemblyRoot, "Assembly");
    s.assemblyDefinition = assembly.definition;
    std::vector<Id> assemblies{assembly.instance};
    for (int i = 0; i < 6; ++i)
        assemblies.push_back(
            placeComponent(doc, assembly.definition, randomPlacement(rng, i + 1)).instance);
    s.mirrored = assemblies[1];
    s.unique = prism(doc, 5, 1.3, 2);
    doc.transform(s.unique, randomPlacement(rng, 2));
    const auto second = prism(doc, 4, .9, .7);
    doc.transform(second, randomPlacement(rng, 0));
    // Deep nesting with transformed groups above several instances.
    auto group = createGroup(doc, {assemblies[2], assemblies[3], s.instances[4], second},
                             "Nested 1");
    doc.transform(group, Transform::rotation({0, 0, 1}, .3) * Transform::scaling({1, 1, -1}));
    group = createGroup(doc, {group}, "Nested 2");
    doc.transform(group, Transform::translation({1, -2, .5}));
    s.outer = createGroup(doc, std::set<Id>{group, s.unique, s.instances[5], assemblies[4]},
                          "Outer");
    if (offset.x || offset.y || offset.z) {
        std::set<Id> roots;
        for (const auto &[id, body] : doc.bodies())
            if (!body->parent)
                roots.insert(id);
        doc.transform(createGroup(doc, roots, "Distant"), Transform::translation(offset));
    }
    doc.addGuide(0, guideLine(offset + Vec3{0, 0, .75}, {1, .2, 0}));
    doc.addGuide(0, guideLine(offset + Vec3{.5, 0, 0}, {0, 0, 1}));
    doc.addGuide(0, guidePoint(offset + Vec3{3, 0, .2}));
    return s;
}
struct Policy {
    Selection hidden, nested;
};
// Viewport-equivalent hidden/locked/tag/context filtering through the core
// Selection model, as Viewport::updateInference supplies it.
Policy makePolicy(Scene &s, std::mt19937_64 &rng) {
    auto &doc = s.doc;
    const auto tag = createTag(doc, "Hidden tag");
    assignTag(doc, s.instances[2], tag);
    editTag(doc, tag, {}, {}, false);
    setEntityState(doc, s.instances[3], true, {});
    setEntityState(doc, s.unique, {}, true);
    Policy policy;
    for (auto *selection : {&policy.hidden, &policy.nested})
        selection->sync(doc);
    SelectionSet hide;
    std::vector<Id> ids;
    for (const auto &[id, body] : doc.bodies())
        if (!body->surface.vertices.empty())
            ids.push_back(id);
    for (int i = 0; i < 8; ++i) {
        const auto id = ids[rng() % ids.size()];
        const auto &body = *doc.bodies().at(id);
        if (i % 2 && !body.topology.edges.empty()) {
            auto edge = body.topology.edges.begin();
            std::advance(edge, rng() % body.topology.edges.size());
            hide.insert({id, SelectionKind::Edge, edge->first});
        } else if (!body.surface.faces.empty()) {
            auto face = body.surface.faces.begin();
            std::advance(face, rng() % body.surface.faces.size());
            hide.insert({id, SelectionKind::Face, face->first});
        }
    }
    policy.hidden.hide(doc, hide);
    policy.hidden.lock(doc, s.instances[7], true);
    policy.nested.enter(doc, s.mirrored);
    return policy;
}
void applyPolicy(InferenceQuery &q, const Document &doc, const Selection &selection) {
    q.visible = [&doc, &selection](Id body, InferenceEntity type, Id entity) {
        const SelectedEntity e = type == InferenceEntity::Face ? SelectedEntity{body, SelectionKind::Face, entity}
                                 : type == InferenceEntity::Edge
                                     ? SelectedEntity{body, SelectionKind::Edge, entity}
                                 : type == InferenceEntity::Guide
                                     ? SelectedEntity{body, SelectionKind::Guide, entity}
                                     : SelectedEntity{body, SelectionKind::Body, 0};
        return selection.exists(doc, e) && !selection.hidden(doc, e);
    };
    q.eligible = [&doc, &selection](Id body, InferenceEntity, Id) {
        return selection.inContext(doc, body) && !selection.locked(doc, body);
    };
}
struct Totals {
    size_t queries{}, candidates{}, intersections{}, truncated{}, legacyCompared{};
};
void compareQueries(Scene &s, const InferenceIndex &index, const legacy::InferenceIndex &old,
                    const Policy &policy, std::mt19937_64 &rng, int count, Totals &totals,
                    const std::string &label) {
    const auto &doc = s.doc;
    const auto expanded = expand(doc, rng);
    Vec3 low{1e300, 1e300, 1e300}, high{-1e300, -1e300, -1e300};
    std::vector<std::pair<Id, Vec3>> features;
    for (const auto &[id, body] : doc.bodies()) {
        const auto world = doc.worldTransform(id);
        for (const auto &[vertex, p] : body->surface.vertices) {
            const auto w = world.point(p);
            features.push_back({id, w});
            low = {std::min(low.x, w.x), std::min(low.y, w.y), std::min(low.z, w.z)};
            high = {std::max(high.x, w.x), std::max(high.y, w.y), std::max(high.z, w.z)};
        }
        for (const auto &[edge, e] : body->topology.edges)
            features.push_back({id, world.point((body->surface.vertices.at(e.a) +
                                                body->surface.vertices.at(e.b)) *
                                               .5)});
        for (const auto &t : body->surface.triangles())
            features.push_back({id, world.point((t.a + t.b + t.c) * (1. / 3))});
    }
    const auto center = (low + high) * .5;
    const auto radius = length(high - low) * .5;
    std::uniform_real_distribution<double> unit(-1, 1), jitter(-6, 6);
    const std::array<double, 4> radii{4, 8, 12, 24};
    for (int i = 0; i < count; ++i) {
        const auto direction = normalized({unit(rng), unit(rng), unit(rng) * .8 + .5});
        const bool perspective = i % 3 != 2, floats = i % 2;
        const auto camera = makeCamera(center + direction * (radius * (perspective ? 2.2 : 3)),
                                       center, radius, perspective, floats);
        const auto &[featureBody, feature] = features[rng() % features.size()];
        const auto screen = camera.project(feature);
        if (!screen) {
            --i;
            continue;
        }
        InferenceQuery q{camera, screen->x + jitter(rng), screen->y + jitter(rng),
                         radii[rng() % radii.size()]};
        const auto mode = rng() % 8;
        if (mode == 1)
            q.context = featureBody;
        if (mode == 2)
            q.plane = DrawingPlane::make(feature, normalized({unit(rng), unit(rng), 1}),
                                         normalized({1, unit(rng) * .1, 0}));
        if (mode == 3)
            q.includeGuides = false;
        if (mode == 4)
            applyPolicy(q, doc, policy.hidden);
        if (mode == 5)
            applyPolicy(q, doc, policy.nested);
        if (mode == 6) {
            const auto clip = feature.x + unit(rng) * .5;
            q.pointVisible = [clip](Id, Vec3 p) { return p.x < clip; };
        }
        if (mode == 7)
            q.x = std::floor(q.x) + .5; // Pixel centers, as mouse events deliver.
        const auto where = label + " query " + std::to_string(i) + " mode " + std::to_string(mode);
        const auto actual = index.query(q);
        requireIdentical(actual, bruteForce(expanded, q), where);
        ++totals.queries;
        totals.candidates += actual.candidates.size();
        totals.truncated += actual.truncated;
        totals.intersections += std::count_if(
            actual.candidates.begin(), actual.candidates.end(),
            [](const auto &c) { return c.kind == InferenceKind::Intersection && c.otherBody; });
        const auto before = old.query(q);
        if (before.truncated || actual.truncated)
            continue;
        const auto a = canonical(actual.candidates), b = canonical(before.candidates);
        bool ok = a.size() == b.size() && actual.visitedPrimitives == before.visitedPrimitives &&
                  actual.intersectionPairs == before.intersectionPairs;
        for (size_t c = 0; ok && c < a.size(); ++c)
            ok = equivalent(a[c], b[c]);
        if (!ok) {
            for (const auto &c : actual.candidates)
                std::cerr << "  index  " << describe(c) << '\n';
            for (const auto &c : before.candidates)
                std::cerr << "  legacy " << describe(c) << '\n';
            throw std::runtime_error(where + ": differs from the pre-R082.ee index");
        }
        ++totals.legacyCompared;
    }
}
void scenario(Vec3 offset, std::uint64_t seed, const std::string &label) {
    std::mt19937_64 rng(seed);
    auto s = buildScene(rng, offset);
    auto policy = makePolicy(s, rng);
    validateComponentInstances(s.doc.definitions(), s.doc.instances(), s.doc.bodies());
    InferenceIndex index;
    legacy::InferenceIndex old;
    index.sync(s.doc);
    old.sync(s.doc);
    check(index.primitiveCount() == old.primitiveCount(),
          label + ": logical primitive count matches the expanded index");
    check(index.indexedPrimitiveCount() < old.primitiveCount() / 2,
          label + ": instances share definition-local primitives");
    check(index.bodyBuilds() == s.doc.bodies().size(), label + ": one placement per scene body");
    Totals totals;
    compareQueries(s, index, old, policy, rng, 120, totals, label);

    // Moving a placement updates only top-level entries for its scene records.
    auto &doc = s.doc;
    auto locals = index.localBuilds(), builds = index.bodyBuilds();
    const auto movedMembers = doc.instances().at(s.mirrored)->members.size();
    doc.transform(s.mirrored,
                  Transform::translation({.25, -.5, .125}) * doc.bodies().at(s.mirrored)->transform);
    index.sync(doc);
    old.sync(doc);
    size_t nestedRecords = 0;
    for (const auto &[member, target] : doc.instances().at(s.mirrored)->members)
        if (doc.instances().contains(target) && target != s.mirrored)
            nestedRecords += doc.instances().at(target)->members.size() - 1;
    check(index.localBuilds() == locals, label + ": moving a placement builds no local index");
    check(index.bodyBuilds() - builds == movedMembers + nestedRecords,
          label + ": moving a placement rebuilds only its own placement entries");
    compareQueries(s, index, old, policy, rng, 30, totals, label + " moved");

    // Adding a placement of an indexed definition builds no geometry index.
    locals = index.localBuilds();
    placeComponent(doc, s.leafDefinition, randomPlacement(rng, 3));
    index.sync(doc);
    old.sync(doc);
    check(index.localBuilds() == locals, label + ": a new placement reuses its definition");
    compareQueries(s, index, old, policy, rng, 30, totals, label + " added");

    // Editing a definition rebuilds its local index once for every instance.
    locals = index.localBuilds();
    editComponentDefinition(doc, s.leafDefinition, [&](Document &draft) {
        return draft.insertEdges(s.leafMember, {0, 0, 1.5}, {0, 0, 1},
                                 {{Vec3{-.5, -.5, 1.5}, Vec3{.5, .5, 1.5}}});
    });
    index.sync(doc);
    old.sync(doc);
    check(index.localBuilds() - locals == 1,
          label + ": a definition edit rebuilds its local index once, not per instance (" +
              std::to_string(index.localBuilds() - locals) + ")");
    check(index.primitiveCount() == old.primitiveCount(),
          label + ": edited logical primitive count matches the expanded index");
    compareQueries(s, index, old, policy, rng, 40, totals, label + " edited");

    // Undo is another definition change: again one local build for all instances.
    locals = index.localBuilds();
    doc.undo();
    index.sync(doc);
    old.sync(doc);
    compareQueries(s, index, old, policy, rng, 20, totals, label + " undone");
    check(index.localBuilds() - locals == 1, label + ": undo rebuilds the definition once");
    std::cout << label << ": queries=" << totals.queries << " candidates=" << totals.candidates
              << " pairIntersections=" << totals.intersections
              << " truncated=" << totals.truncated
              << " legacyCompared=" << totals.legacyCompared
              << " logicalPrimitives=" << index.primitiveCount()
              << " indexedPrimitives=" << index.indexedPrimitiveCount()
              << " localBuilds=" << index.localBuilds() << " undoLocalBuilds="
              << index.localBuilds() - locals << '\n';
    check(totals.candidates > totals.queries, label + ": queries acquire real candidates");
    check(totals.intersections > 0, label + ": queries cover edge/edge intersections");
    check(totals.legacyCompared * 2 > totals.queries,
          label + ": most queries compare against the pre-R082.ee index");
}
} // namespace
int main() {
    try {
        scenario({}, 0x082ee001, "local");
        // R082.e distant-coordinate placement: identical results far from the origin.
        scenario({900000, -900000, 900000}, 0x082ee002, "far");
        // Pair identity no longer depends on traversal order.
        Document crossing;
        crossing.addWire(0, {-1, 0, 0}, {1, 0, 0});
        crossing.addWire(0, {0, -1, 0}, {0, 1, 0});
        InferenceIndex index;
        index.sync(crossing);
        const auto camera = makeCamera({0, 0, 5}, {}, 1, false, false);
        const auto screen = *camera.project({});
        const auto result = index.query({camera, screen.x, screen.y, 8});
        check(std::any_of(result.candidates.begin(), result.candidates.end(),
                          [](const auto &c) {
                              return c.kind == InferenceKind::Intersection && c.otherBody &&
                                     c.body < c.otherBody;
                          }),
              "Edge pairs report the lower body first");
        std::cout << "Instanced inference equivalence passed\n";
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
