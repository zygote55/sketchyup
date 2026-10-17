// R094.g: instanced rendering of component parameter variants (ADR 0166 §7).
//
// A placement that shows a variant shares viewport meshes and inference indexes by
// the variant's member records. Placements with equal values share one variant, so
// they share its driven members; members no driver changes are the template's own
// records and share with default placements. This fixture checks, in software GL:
//  - framebuffer and picking hashes are equal with placement sharing on and off;
//  - equal values add no mesh, upload or tessellation, and a new variant adds
//    meshes only for its driven members;
//  - the inference index builds nothing for another placement of a known variant
//    and returns exactly what an expanded brute-force search returns;
//  - 1,000 placements of one non-default variant stay within 10 % of 1,000 default
//    placements' body-cache and GPU vertex payloads.
// Every coordinate, driver delta and translation is a small dyadic rational, so the
// shared path (placement-space vertex plus a draw offset) and the per-body path
// (world-space vertex) round identically and the framebuffers are bit-identical.
#include "app/surface_format.hpp"
#include "app/viewport.hpp"
#include "core/component_parameters.hpp"
#include "core/component_regeneration.hpp"
#include "core/components.hpp"
#include "core/copy_array.hpp"
#include "core/groups.hpp"
#include "core/transform_selection.hpp"
#include "geometry/inference_detail.hpp"
#include <QApplication>
#include <QCryptographicHash>
#include <QDataStream>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTest>
#include <QVBoxLayout>
#include <functional>
#include <iostream>
#include <set>
#include <tuple>
using namespace sketchy;
namespace {
void check(bool value, const std::string &message) {
    if (!value)
        throw std::runtime_error(message);
}
QImage frame(Viewport &view) {
    QCoreApplication::processEvents();
    auto image = view.grabFramebuffer();
    check(!image.isNull() && view.rendererReady(), "Variant framebuffer available");
    return image.convertToFormat(QImage::Format_RGBA8888);
}
// Painter overlays can change between frames independently of geometry.
QImage stableFrame(Viewport &view) {
    auto image = frame(view);
    for (int attempt = 0; attempt < 10; ++attempt) {
        QTest::qWait(20);
        auto next = frame(view);
        if (next == image)
            return image;
        image = std::move(next);
    }
    throw std::runtime_error("Variant framebuffer did not become stable");
}
QString sha256(QByteArrayView bytes) {
    return QString::fromLatin1(
        QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex());
}
QString imageHash(const QImage &image) {
    return sha256(QByteArrayView(image.constBits(), image.sizeInBytes()));
}
Id box(Document &doc, Vec3 low, Vec3 high, const std::string &name) {
    const auto id = doc.addFace({{{low.x, low.y, low.z},
                                  {high.x, low.y, low.z},
                                  {high.x, high.y, low.z},
                                  {low.x, high.y, low.z}}},
                                name);
    doc.extrude(id, doc.bodies().at(id)->surface.faces.begin()->first, high.z - low.z);
    return id;
}
ComponentParameterSpec parameter(std::string name, ParameterType type, std::string expression,
                                 std::optional<double> min = {},
                                 std::optional<double> max = {}) {
    ComponentParameterSpec result;
    result.name = name;
    result.label = name;
    result.type = type;
    result.expression = std::move(expression);
    result.min = min;
    result.max = max;
    return result;
}
ComponentDriverSpec driver(ComponentDriverKind kind, Id member, std::string expression,
                           ComponentAxis axis = ComponentAxis::X) {
    ComponentDriverSpec result;
    result.kind = kind;
    result.member = member;
    result.expression = std::move(expression);
    result.axis = axis;
    result.maxCount = 0;
    return result;
}
ParameterValue metres(double value) {
    return ParameterQuantity{quantizeParameterLength(value), {1, 0}};
}
ParameterValue count(double value) { return ParameterQuantity{value, {0, 0}}; }
// A window: a frame stretched by `width`, a handle positioned by it, a glazing bar
// repeated `panes` times, a sill shown by `sill` and a sash no driver changes.
struct Window {
    Id definition{}, first{}, frame{}, sash{}, handle{}, bar{}, sill{};
};
Window makeWindow(Document &doc) {
    Window w;
    w.frame = box(doc, {0, 0, 0}, {1, .125, 1}, "Frame");
    w.sash = box(doc, {.0625, .125, .0625}, {.4375, .1875, .9375}, "Sash");
    w.handle = box(doc, {.75, .125, .5}, {.8125, .25, .5625}, "Handle");
    w.bar = box(doc, {.5, .125, .125}, {.5625, .1875, .1875}, "Bar");
    w.sill = box(doc, {0, -.125, -.0625}, {1, 0, 0}, "Sill");
    const auto group = createGroup(doc, {w.frame, w.sash, w.handle, w.bar, w.sill}, "Window");
    const auto created = createComponent(doc, group, "Window");
    w.definition = created.definition;
    w.first = created.instance;
    defineComponentParameter(doc, w.definition,
                             parameter("width", ParameterType::Length, "1 m", .5, 3));
    defineComponentParameter(doc, w.definition,
                             parameter("panes", ParameterType::Count, "1", 1, 4));
    defineComponentParameter(doc, w.definition, parameter("sill", ParameterType::Boolean, "true"));
    auto stretch = driver(ComponentDriverKind::Stretch, w.frame, "width");
    stretch.at = .5;
    driveComponent(doc, w.definition, 0, stretch);
    driveComponent(doc, w.definition, 0,
                   driver(ComponentDriverKind::Position, w.handle, "width - 250 mm"));
    auto repeat = driver(ComponentDriverKind::Repeat, w.bar, "panes", ComponentAxis::Z);
    repeat.maxCount = 4;
    repeat.spacing = "250 mm";
    driveComponent(doc, w.definition, 0, repeat);
    driveComponent(doc, w.definition, 0, driver(ComponentDriverKind::Visibility, w.sill, "sill"));
    return w;
}
// `count` placements of `first` (with its overrides) in rows of `columns`, made the
// way a user makes them: a linear array, then copies of that row.
std::vector<Id> grid(Document &doc, Id first, int total, int columns, double dx, double dy) {
    std::vector<Id> row{first};
    if (const int width = std::min(total, columns); width > 1) {
        CopyArray array;
        array.delta = {dx, 0, 0};
        array.copies = unsigned(width - 1);
        for (const auto &copy : copyArraySelected(doc, {{first}}, array).instances)
            row.push_back(copy.copies.at(first));
    }
    auto all = row;
    for (int r = 1; r * columns < total; ++r) {
        const int n = std::min(columns, total - r * columns);
        TransformTargets targets;
        for (int j = 0; j < n; ++j)
            targets.insert({row[size_t(j)]});
        const auto copied = transformSelected(doc, targets, Transform::translation({0, dy * r, 0}),
                                              {}, TransformSpace::World, true);
        for (int j = 0; j < n; ++j)
            all.push_back(copied.copies.at(row[size_t(j)]));
    }
    check(all.size() == size_t(total), "Grid has every placement");
    return all;
}
// Distinct (member record, linear part, hidden) of every placement body with faces:
// the placement meshes a fully shared viewport holds, and how many bodies draw them.
std::pair<size_t, size_t> expectedSharing(const Document &doc) {
    std::set<std::tuple<const Body *, std::array<double, 9>, bool>> keys;
    size_t bodies = 0;
    for (const auto &[root, instance] : doc.instances()) {
        const auto &definition = *doc.definitions().at(instance->definition);
        const auto variant = doc.componentVariant(root);
        const auto &records = variant ? variant->members : definition.members;
        const auto &references = variant ? variant->references : definition.references;
        for (const auto &[member, target] : instance->members) {
            const auto &body = *doc.bodies().at(target);
            if (member == definition.root || references.contains(member) ||
                body.surface.faces.empty())
                continue;
            const auto world = doc.worldTransform(target);
            std::array<double, 9> linear{};
            for (size_t column = 0; column < 3; ++column)
                for (size_t row = 0; row < 3; ++row)
                    linear[column * 3 + row] = world.m[column * 4 + row];
            keys.insert({records.at(member).get(), linear, body.hidden});
            ++bodies;
        }
    }
    return {keys.size(), bodies};
}
// Template members a variant replaces or adds, among those that have faces.
size_t drivenRecords(const Document &doc, Id placement) {
    const auto variant = doc.componentVariant(placement);
    check(variant != nullptr, "Placement shows a variant");
    const auto &definition = *doc.definitions().at(variant->definition);
    size_t result = 0;
    for (const auto &[member, record] : variant->members) {
        const auto found = definition.members.find(member);
        if (member != definition.root && !variant->references.contains(member) &&
            !record->surface.faces.empty() &&
            (found == definition.members.end() || found->second != record))
            ++result;
    }
    return result;
}
// Brute-force inference oracle (as in inference_equivalence_tests): every scene body
// expanded to world primitives with Transform::point and visited linearly.
struct Expanded {
    std::map<Id, BodyPtr> records;
    std::map<Id, Transform> worlds;
    std::map<Id, std::map<Id, std::vector<Id>>> vertexEdges;
    std::vector<std::pair<Id, inference_detail::Primitive>> primitives;
};
Expanded expand(const Document &doc) {
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
        for (const auto &t : body->surface.triangles())
            add(InferenceKind::OnFace, t.face, world.point(t.a), world.point(t.b),
                world.point(t.c));
    }
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
bool same(const InferenceResult &a, const InferenceResult &b) {
    if (a.candidates.size() != b.candidates.size() || a.visitedPrimitives != b.visitedPrimitives ||
        a.intersectionPairs != b.intersectionPairs || a.truncated != b.truncated)
        return false;
    for (size_t i = 0; i < a.candidates.size(); ++i) {
        const auto &x = a.candidates[i], &y = b.candidates[i];
        if (x.kind != y.kind || !(x.point == y.point) || x.body != y.body ||
            x.entity != y.entity || x.otherBody != y.otherBody || x.otherEntity != y.otherEntity ||
            x.pixels != y.pixels || x.depth != y.depth || x.entityType != y.entityType ||
            x.otherEntityType != y.otherEntityType)
            return false;
    }
    return true;
}
struct Family {
    Viewport::GeometryCacheMemory memory;
    Viewport::RenderStats stats;
    size_t localBuilds{}, indexedPrimitives{}, indexBytes{};
};
// 1,000 placements in a fresh document, all at the defaults or all of one variant
// whose drivers keep the topology (stretch and position), as ADR 0166 §7 compares.
Family family(bool overridden, int placements) {
    Document doc;
    const auto w = makeWindow(doc);
    if (overridden)
        overrideComponentParameters(doc, w.first, {{"width", metres(1.5)}});
    const auto all = grid(doc, w.first, placements, 32, 2, 1);
    for (const auto id : all)
        check((doc.componentVariant(id) != nullptr) == overridden,
              "Every family placement shows the expected variant");
    QWidget host;
    host.resize(640, 480);
    QVBoxLayout layout(&host);
    auto *view = new Viewport(doc);
    layout.addWidget(view);
    host.show();
    check(QTest::qWaitForWindowExposed(&host), "Family viewport exposed");
    view->fit();
    frame(*view);
    Family result{view->geometryCacheMemory(), view->renderStats()};
    InferenceIndex index;
    index.sync(doc);
    result.localBuilds = index.localBuilds();
    result.indexedPrimitives = index.indexedPrimitiveCount();
    result.indexBytes = index.indexBytes();
    check(view->renderStats().glError == 0, "Family frame leaves no GL error");
    return result;
}
double ratio(size_t a, size_t b) { return b ? double(a) / double(b) : 0; }
} // namespace
int main(int argc, char **argv) {
    QSurfaceFormat format;
    format.setVersion(3, 3);
    format.setProfile(QSurfaceFormat::CoreProfile);
    format.setDepthBufferSize(24);
    format.setSamples(0);
    sketchy::setDefaultViewportFormat(format);
    QApplication app(argc, argv);
    QJsonArray states;
    try {
        QWidget host;
        host.resize(720, 540);
        QVBoxLayout layout(&host);
        Document doc;
        const auto w = makeWindow(doc);
        std::vector<Id> placements{w.first};
        for (int i = 1; i < 8; ++i)
            placements.push_back(
                placeComponent(doc, w.definition,
                               Transform::translation({3. * (i % 4), 2. * (i / 4), 0}) *
                                   (i == 7 ? Transform::scaling({-1, 1, 1}) : Transform{}))
                    .instance);
        auto *view = new Viewport(doc);
        layout.addWidget(view);
        host.show();
        check(QTest::qWaitForWindowExposed(&host), "Variant viewport exposed");
        view->fit();
        frame(*view);

        // Sharing on and off must give equal framebuffers and equal picks. The
        // shared state is also checked against the expected sharing of the records.
        auto state = [&](const QString &name, const std::function<void()> &prepare = {}) {
            if (prepare)
                prepare();
            view->refresh();
            view->setPlacementMeshSharing(true);
            const auto shared = stableFrame(*view);
            const auto stats = view->renderStats();
            auto picks = [&] {
                QByteArray bytes;
                QDataStream out(&bytes, QIODevice::WriteOnly);
                for (int y = 1; y < 8; ++y)
                    for (int x = 1; x < 10; ++x) {
                        const QPointF point(view->width() * x / 10., view->height() * y / 8.);
                        const auto [body, face] = view->pick(point);
                        const auto [edgeBody, edge] = view->pickEdge(point, 8);
                        out << quint64(body) << quint64(face) << quint64(edgeBody)
                            << quint64(edge);
                        if (x % 4 == 0 && y % 3 == 0) {
                            const auto selected = view->selectionAt(point);
                            out << bool(selected);
                            if (selected)
                                out << quint64(selected->body) << int(selected->kind)
                                    << quint64(selected->entity);
                        }
                    }
                return sha256(bytes);
            };
            const auto sharedPicks = picks();
            view->setPlacementMeshSharing(false);
            const auto own = stableFrame(*view);
            const auto ownStats = view->renderStats();
            const auto ownPicks = picks();
            view->setPlacementMeshSharing(true);
            frame(*view);
            const auto [meshes, bodies] = expectedSharing(doc);
            const QJsonObject result{{"state", name},
                                     {"framebufferShared", imageHash(shared)},
                                     {"framebufferOwn", imageHash(own)},
                                     {"picksShared", sharedPicks},
                                     {"picksOwn", ownPicks},
                                     {"placementMeshes", qint64(stats.placementMeshes)},
                                     {"sharedPlacements", qint64(stats.sharedPlacements)},
                                     {"expectedMeshes", qint64(meshes)},
                                     {"expectedSharedBodies", qint64(bodies)},
                                     {"variants", qint64(doc.componentVariants().variants.size())}};
            states.append(result);
            check(ownStats.placementMeshes == 0 && ownStats.sharedPlacements == 0,
                  "Disabled sharing draws every body from its own mesh");
            check(sharedPicks == ownPicks, "Shared variants pick identically: " + name.toStdString());
            check(shared == own,
                  "Shared variants draw the same framebuffer: " + name.toStdString());
            return stats;
        };
        auto expectFullySharing = [&](const Viewport::RenderStats &stats, const std::string &what) {
            const auto [meshes, bodies] = expectedSharing(doc);
            check(stats.placementMeshes == meshes && stats.sharedPlacements == bodies,
                  what + ": every placement body draws the mesh of its member record (" +
                      std::to_string(stats.placementMeshes) + " meshes, " +
                      std::to_string(stats.sharedPlacements) + " bodies; expected " +
                      std::to_string(meshes) + ", " + std::to_string(bodies) + ")");
        };

        // Defaults only: five members under two linear parts (one placement mirrored).
        auto stats = state("defaults");
        check(stats.placementMeshes == 10 && stats.sharedPlacements == 40,
              "Eight default placements draw ten shared meshes");
        expectFullySharing(stats, "defaults");

        // A new variant adds meshes only for its driven records: the stretched frame,
        // the moved handle and two repeat copies. Sash, sill and bar stay the template's.
        overrideComponentParameters(doc, placements[1], {{"width", metres(1.5)}, {"panes", count(3)}});
        check(drivenRecords(doc, placements[1]) == 4, "Variant A replaces or adds four records");
        stats = state("variant-a");
        check(stats.placementMeshes == 14, "Variant A adds exactly its four driven meshes");
        expectFullySharing(stats, "variant A");

        // Equal values share one variant, so they add no mesh, upload or tessellation.
        InferenceIndex inference;
        inference.sync(doc);
        const auto beforeEqual = view->renderStats();
        const auto localsBeforeEqual = inference.localBuilds();
        for (const auto i : {2, 3})
            overrideComponentParameters(doc, placements[size_t(i)],
                                        {{"panes", count(3)}, {"width", metres(1.5)}});
        view->refresh();
        check(doc.componentVariant(placements[2]) == doc.componentVariant(placements[1]) &&
                  doc.componentVariant(placements[3]) == doc.componentVariant(placements[1]),
              "Equal values share one variant object");
        frame(*view);
        const auto afterEqual = view->renderStats();
        check(afterEqual.placementMeshes == 14 &&
                  afterEqual.tessellations == beforeEqual.tessellations &&
                  afterEqual.meshUploads == beforeEqual.meshUploads,
              "Placements with equal values reuse the variant's meshes (" +
                  std::to_string(afterEqual.meshUploads - beforeEqual.meshUploads) + " uploads, " +
                  std::to_string(afterEqual.tessellations - beforeEqual.tessellations) +
                  " tessellations)");
        inference.sync(doc);
        check(inference.localBuilds() == localsBeforeEqual,
              "Inference builds no local index for another placement of a known variant (" +
                  std::to_string(inference.localBuilds() - localsBeforeEqual) + ")");
        state("variant-a-shared");

        // A second variant hides the sill; the mirrored placement takes variant A.
        overrideComponentParameters(doc, placements[4], {{"width", metres(2)}, {"sill", false}});
        overrideComponentParameters(doc, placements[5], {{"sill", false}, {"width", metres(2)}});
        overrideComponentParameters(doc, placements[7], {{"width", metres(1.5)}, {"panes", count(3)}});
        stats = state("variants-a-b-mirrored");
        expectFullySharing(stats, "variants A, B and mirrored A");
        check(doc.componentVariants().variants.size() == 2, "Two distinct variants");

        // A copy keeps its overrides and therefore its variant's meshes: copying a
        // variant placement costs what copying a default placement costs (its root).
        auto copyCost = [&](Id source) {
            const auto before = view->renderStats();
            const auto locals = inference.localBuilds();
            const auto copied = transformSelected(doc, {{source}},
                                                  Transform::translation({0, 4, 0}), {},
                                                  TransformSpace::World, true)
                                    .copies.at(source);
            placements.push_back(copied);
            view->refresh();
            frame(*view);
            inference.sync(doc);
            const auto after = view->renderStats();
            check(doc.componentVariant(copied) == doc.componentVariant(source),
                  "A copied placement shares its source's variant");
            check(after.placementMeshes == before.placementMeshes,
                  "A copied placement adds no placement mesh");
            return std::array<std::uint64_t, 4>{after.meshUploads - before.meshUploads,
                                                after.tessellations - before.tessellations,
                                                after.sharedPlacements - before.sharedPlacements,
                                                inference.localBuilds() - locals};
        };
        const auto defaultCopy = copyCost(placements[0]);
        const auto variantCopy = copyCost(placements[1]);
        check(variantCopy[0] == defaultCopy[0] && variantCopy[1] == defaultCopy[1] &&
                  variantCopy[3] <= defaultCopy[3] && defaultCopy[2] == 5 && variantCopy[2] == 7,
              "Copying a variant placement uploads, tessellates and indexes no more than "
              "copying a default one (" +
                  std::to_string(variantCopy[0]) + "/" + std::to_string(defaultCopy[0]) +
                  " uploads, " + std::to_string(variantCopy[1]) + "/" +
                  std::to_string(defaultCopy[1]) + " tessellations, " +
                  std::to_string(variantCopy[3]) + "/" + std::to_string(defaultCopy[3]) +
                  " local indexes)");

        state("xray", [&] {
            auto style = doc.style();
            style.mode = ModelStyleMode::XRay;
            doc.setStyle(style);
        });
        state("monochrome-profiles", [&] {
            auto style = doc.style();
            style.mode = ModelStyleMode::Monochrome;
            style.profiles = true;
            doc.setStyle(style);
        });
        state("context", [&] {
            doc.setStyle(ModelStyle{});
            view->enterContext(placements[4]);
        });
        state("show-hidden", [&] {
            view->leaveContext();
            view->showHiddenGeometry(true);
        });
        // Moving a variant placement keeps its meshes; resetting it returns it to
        // the template's records.
        stats = state("moved-and-reset", [&] {
            view->showHiddenGeometry(false);
            doc.transform(placements[2], Transform::translation({0, 0, 1}) *
                                             doc.bodies().at(placements[2])->transform);
            resetComponentParameters(doc, placements[5]);
        });
        expectFullySharing(stats, "moved and reset");
        // Undo and redo restore the same variant records.
        stats = state("undo-redo", [&] {
            doc.undo();
            doc.undo();
            doc.redo();
        });
        expectFullySharing(stats, "undo and redo");

        // Inference over the mixed document equals expanded brute force near every
        // placement, at vertices, edge midpoints and face centres.
        inference.sync(doc);
        const auto expanded = expand(doc);
        const auto camera = view->inferenceCamera();
        size_t queries = 0, candidates = 0;
        for (const auto placement : placements) {
            for (const auto &[member, target] : doc.instances().at(placement)->members) {
                const auto &body = *doc.bodies().at(target);
                if (body.surface.faces.empty())
                    continue;
                const auto world = doc.worldTransform(target);
                std::vector<Vec3> probes;
                for (const auto &[vertex, p] : body.surface.vertices)
                    probes.push_back(world.point(p));
                for (const auto &[edge, e] : body.topology.edges)
                    probes.push_back(world.point((body.surface.vertices.at(e.a) +
                                                  body.surface.vertices.at(e.b)) *
                                                 .5));
                for (size_t i = 0; i < probes.size(); i += 11) {
                    const auto screen = camera.project(probes[i]);
                    if (!screen)
                        continue;
                    {
                        InferenceQuery q{camera, screen->x + .25, screen->y - .25, 8};
                        q.context = i % 2 ? target : Id{};
                        const auto actual = inference.query(q);
                        check(same(actual, bruteForce(expanded, q)),
                              "Variant inference equals expanded brute force at placement #" +
                                  std::to_string(placement));
                        ++queries;
                        candidates += actual.candidates.size();
                    }
                }
            }
        }
        check(queries > 60 && candidates > queries, "Inference probes find real candidates");
        check(view->renderStats().glError == 0, "Variant comparisons leave no GL error");

        // Payloads: 1,000 placements of one non-default variant against 1,000 defaults.
        const auto defaults = family(false, 1000);
        const auto variants = family(true, 1000);
        const auto vectorRatio = ratio(variants.memory.bodyVectorCapacityBytes,
                                       defaults.memory.bodyVectorCapacityBytes);
        const auto gpuRatio =
            ratio(variants.memory.bodyGpuPayloadBytes, defaults.memory.bodyGpuPayloadBytes);
        auto report = [](const Family &f) {
            return QJsonObject{{"bodyCacheVectorCapacityBytes",
                                qint64(f.memory.bodyVectorCapacityBytes)},
                               {"bodyCacheGpuPayloadBytes", qint64(f.memory.bodyGpuPayloadBytes)},
                               {"placementMeshes", qint64(f.stats.placementMeshes)},
                               {"sharedPlacements", qint64(f.stats.sharedPlacements)},
                               {"tessellations", qint64(f.stats.tessellations)},
                               {"inferenceLocalBuilds", qint64(f.localBuilds)},
                               {"inferenceIndexedPrimitives", qint64(f.indexedPrimitives)},
                               {"inferenceIndexBytes", qint64(f.indexBytes)}};
        };
        std::cout << QJsonDocument(QJsonObject{{"platform", QGuiApplication::platformName()},
                                               {"scale", view->devicePixelRatioF()},
                                               {"graphics", view->graphicsDescription()},
                                               {"states", states},
                                               {"inferenceQueries", qint64(queries)},
                                               {"defaultFamily1000", report(defaults)},
                                               {"variantFamily1000", report(variants)},
                                               {"bodyCacheVectorRatio", vectorRatio},
                                               {"bodyCacheGpuPayloadRatio", gpuRatio}})
                         .toJson(QJsonDocument::Compact)
                         .toStdString()
                  << '\n';
        check(variants.stats.sharedPlacements == defaults.stats.sharedPlacements &&
                  variants.stats.placementMeshes == defaults.stats.placementMeshes,
              "One variant's placements share as many meshes as default placements");
        check(vectorRatio >= .9 && vectorRatio <= 1.1 && gpuRatio >= .9 && gpuRatio <= 1.1,
              "1,000 variant placements stay within 10 % of the default family's payloads");
        check(variants.indexedPrimitives <= defaults.indexedPrimitives * 11 / 10,
              "1,000 variant placements index their members once");
        std::cout << "Variant placements share meshes and inference indexes by their member "
                     "records; framebuffers and picks equal with sharing on and off; DPR="
                  << view->devicePixelRatioF() << '\n';
    } catch (const std::exception &error) {
        std::cerr << QJsonDocument(states).toJson(QJsonDocument::Compact).toStdString() << '\n'
                  << error.what() << '\n';
        return 1;
    }
}
