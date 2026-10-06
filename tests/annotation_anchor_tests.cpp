#include "core/annotation_anchors.hpp"
#include "core/groups.hpp"
#include "geometry/arrangement.hpp"
#include <iostream>
#include <limits>
using namespace sketchy;
namespace {
void check(bool value, const char *message) {
    if (!value)
        throw std::runtime_error(message);
}
void near(Vec3 a, Vec3 b, const char *message) { check(length(a - b) < 1e-6, message); }
template <class F> void rejects(F action) {
    try {
        action();
    } catch (const std::exception &) {
        return;
    }
    throw std::runtime_error("Invalid annotation anchor accepted");
}
void edges() {
    Document doc;
    const auto id = doc.addWire(0, {0, 0, 0}, {4, 0, 0});
    const auto edge = doc.bodies().at(id)->topology.edges.begin()->first;
    auto anchor = edgeAnchor(doc, id, edge, .25), boundary = edgeAnchor(doc, id, edge, .5);
    const auto vertex = doc.bodies().at(id)->topology.edges.at(edge).b;
    auto endpoint = vertexAnchor(doc, id, vertex);
    auto before = doc.readSnapshot();
    const auto changes = doc.splitEdge(id, edge, .5);
    const auto mapped = remapAnnotationAnchor(before, doc, changes, anchor);
    check(mapped.state == AnchorState::Resolved && mapped.entity != edge,
          "Split edge rebinds to unique geometric descendant");
    near(resolveAnnotationAnchor(doc, mapped).point, {1, 0, 0},
         "Split preserves physical attachment");
    auto duplicate = changes;
    duplicate[id].edges.descendants[edge] = {mapped.entity, mapped.entity};
    rejects([&] { remapAnnotationAnchor(before, doc, duplicate, anchor); });
    auto deformed = doc.readSnapshot();
    auto stretched = std::make_shared<Body>(*deformed.bodies().at(id));
    for (auto &[vertex, point] : stretched->surface.vertices)
        point.x *= 2;
    deformed.apply({"Stretch split", {{id, deformed.bodies().at(id), stretched}}},
                   deformed.revision());
    const auto combined = remapAnnotationAnchor(before, deformed, changes, anchor);
    near(resolveAnnotationAnchor(deformed, combined).point, {2, 0, 0},
         "Combined split and deformation follows surviving endpoint support");
    const auto ambiguous = remapAnnotationAnchor(before, doc, changes, boundary);
    check(ambiguous.state == AnchorState::Ambiguous,
          "Attachment exactly on split boundary remains ambiguous");
    near(ambiguous.fallback, {2, 0, 0}, "Ambiguous marker keeps its last location");
    endpoint = remapAnnotationAnchor(before, doc, changes, endpoint);
    near(resolveAnnotationAnchor(doc, endpoint).point, {4, 0, 0},
         "Surviving vertex retains association");
    before = doc.readSnapshot();
    doc.move(id, {0, 3, 0});
    const auto moved = remapAnnotationAnchor(before, doc, {}, mapped);
    near(resolveAnnotationAnchor(doc, moved).point, {1, 3, 0},
         "Rigid movement updates anchor world position");
    const auto group = createGroup(doc, {id});
    doc.transform(group, Transform::scaling({-2, 3, 1}));
    near(resolveAnnotationAnchor(doc, moved).point, {-2, 9, 0},
         "Nested nonuniform mirror transforms attachment");
    before = doc.readSnapshot();
    doc.erase(id);
    const auto missing = remapAnnotationAnchor(before, doc, {}, moved);
    check(missing.state == AnchorState::Missing, "Deleted context creates missing reference");
    near(missing.fallback, {-2, 9, 0}, "Deleted marker keeps last world position");
    doc.undo();
    check(resolveAnnotationAnchor(doc, missing).state == AnchorState::Missing,
          "Broken records never silently reconnect without history restoration");
    near(resolveAnnotationAnchor(doc, moved).point, {-2, 9, 0},
         "Restoring original anchor with Undo resolves geometry");
    check(remapAnnotationAnchor(before, doc, {}, ambiguous) == ambiguous,
          "Ambiguity stays explicit");
}
void faces() {
    Document doc;
    const auto id = doc.addFace({{{0, 0, 0}, {4, 0, 0}, {4, 4, 0}, {0, 4, 0}},
                                 {{1, 1, 0}, {1, 3, 0}, {3, 3, 0}, {3, 1, 0}}});
    const auto face = doc.bodies().at(id)->surface.faces.begin()->first;
    const auto anchor = faceAnchor(doc, id, face, {.5, 2, 0}),
               boundary = faceAnchor(doc, id, face, {2, .5, 0});
    auto colorOnly = doc.readSnapshot();
    doc.paint(id, {.1f, .2f, .3f});
    check(remapAnnotationAnchor(colorOnly, doc, {}, anchor) == anchor,
          "Appearance-only edits preserve support coordinates exactly");
    rejects([&] { faceAnchor(doc, id, face, {2, 2, 0}); });
    rejects([&] { faceAnchor(doc, id, face, {.5, 2, .01}); });
    const auto before = doc.readSnapshot();
    const auto original = doc.bodies().at(id);
    auto split = partitionFace(original->surface, face, {2, 0, 0}, {1, 0, 0});
    auto after = std::make_shared<Body>(*original);
    after->surface = split.surface;
    const auto changes =
        doc.apply({"Partition", {{id, original, after, split.descendants}}}, doc.revision());
    const auto mapped = remapAnnotationAnchor(before, doc, changes, anchor);
    check(mapped.state == AnchorState::Resolved && mapped.entity != face,
          "Face anchor follows unique split region including holes");
    near(resolveAnnotationAnchor(doc, mapped).point, {.5, 2, 0},
         "Face split retains attachment position");
    const auto ambiguous = remapAnnotationAnchor(before, doc, changes, boundary);
    check(ambiguous.state == AnchorState::Ambiguous,
          "Shared face split boundary is explicitly ambiguous");
    auto old = doc.readSnapshot();
    auto stretched = std::make_shared<Body>(*doc.bodies().at(id));
    for (auto &[vertex, p] : stretched->surface.vertices)
        p.x *= 1.5;
    const auto report =
        doc.apply({"Stretch", {{id, doc.bodies().at(id), stretched}}}, doc.revision());
    const auto moved = remapAnnotationAnchor(old, doc, report, mapped);
    near(resolveAnnotationAnchor(doc, moved).point, {.75, 2, 0},
         "Barycentric face support follows geometry deformation");
    old = doc.readSnapshot();
    const auto erased = doc.eraseFace(id, moved.entity);
    const auto missing = remapAnnotationAnchor(old, doc, erased, moved);
    check(missing.state == AnchorState::Missing, "Deleted face attachment remains visibly missing");
    near(missing.fallback, {.75, 2, 0}, "Deleted face remembers last resolved point");
}
void validation() {
    Document doc;
    auto fixed = pointAnchor({1, 2, 3});
    doc.addWire(0, {0, 0, 0}, {1, 0, 0});
    near(resolveAnnotationAnchor(doc, fixed).point, {1, 2, 3},
         "Fixed world anchor is independent of geometry");
    rejects([&] { pointAnchor({std::numeric_limits<double>::infinity(), 0, 0}); });
    fixed.body = 1;
    rejects([&] { validateAnnotationAnchor(fixed); });
    fixed = pointAnchor({});
    fixed.parameter = .5;
    rejects([&] { validateAnnotationAnchor(fixed); });
    rejects([&] { vertexAnchor(doc, 1, 999); });
    rejects([&] { edgeAnchor(doc, 1, 1, 1.1); });
    const auto id = doc.addFace(
        {{{999990, 999990, 10}, {999994, 999990, 10}, {999994, 999994, 10}, {999990, 999994, 10}}});
    const auto face = doc.bodies().at(id)->surface.faces.begin()->first;
    const auto anchor = faceAnchor(doc, id, face, {999991, 999992, 10});
    near(resolveAnnotationAnchor(doc, anchor).point, {999991, 999992, 10},
         "Site-coordinate face anchor remains stable");
    auto invalid = anchor;
    invalid.weights[0] += 1;
    rejects([&] { validateAnnotationAnchor(invalid); });
    invalid = anchor;
    invalid.vertices[1] = invalid.vertices[0];
    rejects([&] { validateAnnotationAnchor(invalid); });
}
} // namespace
int main() {
    try {
        edges();
        faces();
        validation();
        std::cout << "Annotation anchor references, split lineage, holes, transforms and broken "
                     "states passed\n";
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
