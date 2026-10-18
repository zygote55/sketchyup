#include "core/annotation_anchors.hpp"
#include "core/annotations.hpp"
#include "core/components.hpp"
#include "core/groups.hpp"
#include "core/selection.hpp"
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

// Model annotations follow Edit::transfers, as paper anchors do (ADR 0164 addendum):
// geometry that moves to another context with its IDs unchanged keeps its annotations.
Id vertexAt(const Document &doc, Id body, Vec3 point) {
    for (const auto &[id, p] : doc.bodies().at(body)->surface.vertices)
        if (length(p - point) < 1e-9)
            return id;
    throw std::runtime_error("No vertex at the point");
}
Id edgeBetween(const Document &doc, Id body, Vec3 a, Vec3 b) {
    const auto va = vertexAt(doc, body, a), vb = vertexAt(doc, body, b);
    for (const auto &[id, e] : doc.bodies().at(body)->topology.edges)
        if ((e.a == va && e.b == vb) || (e.a == vb && e.b == va))
            return id;
    throw std::runtime_error("No edge between the points");
}
Id annotate(Document &doc, const char *name, AnnotationKind kind,
            std::vector<AnnotationAnchor> anchors) {
    AnnotationRecord record;
    record.name = name;
    record.kind = kind;
    record.anchors = std::move(anchors);
    if (kind == AnnotationKind::Label)
        record.text = name;
    return createAnnotation(doc, std::move(record));
}
// Two unit squares sharing the edge x = 1 in one raw geometry body.
struct Pair {
    Document doc;
    Id body{}, left{}, right{};
    Pair() {
        body = doc.addFace({{{0, 0, 0}, {1, 0, 0}, {1, 1, 0}, {0, 1, 0}}}, "Pair");
        left = doc.bodies().at(body)->surface.faces.begin()->first;
        const auto old = doc.bodies().at(body);
        auto joined = std::make_shared<Body>(*old);
        right = joined->surface.addFace({{{1, 0, 0}, {2, 0, 0}, {2, 1, 0}, {1, 1, 0}}});
        doc.apply({"Adjacent fixture", {{body, old, joined}}}, doc.revision());
    }
};
using Records = std::map<Id, AnnotationRecord>;
Records snapshot(const Document &doc) {
    Records result;
    for (const auto &[id, record] : doc.annotations())
        result.emplace(id, *record);
    return result;
}
std::map<Id, std::vector<Vec3>> worldPoints(const Document &doc) {
    std::map<Id, std::vector<Vec3>> result;
    for (const auto &[id, record] : doc.annotations())
        for (const auto &anchor : record->anchors) {
            const auto resolved = resolveAnnotationAnchor(doc, anchor);
            check(resolved.state == AnchorState::Resolved, "Annotation anchor resolves");
            result[id].push_back(resolved.point);
        }
    return result;
}
void samePoints(const Document &doc, const std::map<Id, std::vector<Vec3>> &expected,
                const char *message) {
    const auto actual = worldPoints(doc);
    check(actual.size() == expected.size(), message);
    for (const auto &[id, points] : expected)
        for (size_t i = 0; i < points.size(); ++i)
            near(actual.at(id)[i], points[i], message);
}
// Make component of a body with its own geometry moves it into the new member: vertex,
// edge and face anchors all follow, with their IDs unchanged.
void makeComponentOfBody() {
    Pair f;
    auto &doc = f.doc;
    const auto bottom = edgeBetween(doc, f.body, {0, 0, 0}, {1, 0, 0});
    const auto corner = vertexAt(doc, f.body, {2, 1, 0});
    const auto distance = annotate(doc, "Run", AnnotationKind::Distance,
                                   {vertexAnchor(doc, f.body, vertexAt(doc, f.body, {0, 0, 0})),
                                    edgeAnchor(doc, f.body, bottom, .25)});
    const auto vertexLabel =
        annotate(doc, "Corner", AnnotationKind::Label, {vertexAnchor(doc, f.body, corner)});
    const auto faceLabel = annotate(doc, "Bay", AnnotationKind::Label,
                                    {faceAnchor(doc, f.body, f.right, {1.5, .5, 0})});
    const auto points = worldPoints(doc);
    const auto original = snapshot(doc);
    const auto entries = doc.history().total;
    const auto component = createComponent(doc, f.body, "Pair");
    const auto member = component.movedGeometry.at(f.body);
    check(doc.history().total == entries + 1, "Make component is one history entry");
    const auto made = snapshot(doc);
    for (const auto &[id, record] : made) {
        const auto &old = original.at(id);
        check(record.anchors.size() == old.anchors.size(), "Anchor count is kept");
        for (size_t i = 0; i < record.anchors.size(); ++i) {
            const auto &anchor = record.anchors[i], &was = old.anchors[i];
            check(anchor.body == member && anchor.kind == was.kind && anchor.entity == was.entity &&
                      anchor.vertices == was.vertices && anchor.weights == was.weights &&
                      anchor.parameter == was.parameter && anchor.state == AnchorState::Resolved,
                  "The anchor follows its geometry into the member with every ID unchanged");
        }
        auto restored = record;
        restored.anchors = old.anchors;
        check(restored == old, "Only the anchors of the annotation change");
    }
    check(made.size() == 3 && made.contains(distance) && made.contains(vertexLabel) &&
              made.contains(faceLabel),
          "Every annotation is kept");
    samePoints(doc, points, "Annotations stay on the same world points");
    doc.undo();
    check(snapshot(doc) == original, "Undo restores the exact annotation records");
    doc.redo();
    check(snapshot(doc) == made, "Redo restores the exact annotation records");
}
// Group a selection: only the entities that left the source follow the transfer; the
// shared boundary stays in the source, where it still resolves by ID.
void groupSelection() {
    Pair f;
    auto &doc = f.doc;
    const auto leftBottom = edgeBetween(doc, f.body, {0, 0, 0}, {1, 0, 0});
    const auto shared = edgeBetween(doc, f.body, {1, 0, 0}, {1, 1, 0});
    const auto rightTop = edgeBetween(doc, f.body, {1, 1, 0}, {2, 1, 0});
    const auto moving = annotate(doc, "Left", AnnotationKind::Distance,
                                 {vertexAnchor(doc, f.body, vertexAt(doc, f.body, {0, 1, 0})),
                                  edgeAnchor(doc, f.body, leftBottom, .5)});
    const auto face = annotate(doc, "Left face", AnnotationKind::Label,
                               {faceAnchor(doc, f.body, f.left, {.25, .5, 0})});
    const auto staying = annotate(
        doc, "Right", AnnotationKind::Distance,
        {edgeAnchor(doc, f.body, rightTop, .5), faceAnchor(doc, f.body, f.right, {1.5, .5, 0})});
    const auto boundary = annotate(doc, "Shared", AnnotationKind::Distance,
                                   {edgeAnchor(doc, f.body, shared, .5),
                                    vertexAnchor(doc, f.body, vertexAt(doc, f.body, {1, 0, 0}))});
    const auto points = worldPoints(doc);
    const auto original = snapshot(doc);
    Selection selection;
    selection.apply(doc, {{f.body, SelectionKind::Face, f.left}}, SelectionMode::Replace);
    const auto entries = doc.history().total;
    const auto grouped = groupSelected(doc, selection);
    const auto member = grouped.movedGeometry.at(f.body);
    check(doc.history().total == entries + 1, "Group selection is one history entry");
    const auto made = snapshot(doc);
    for (auto id : {moving, face})
        for (const auto &anchor : made.at(id).anchors)
            check(anchor.body == member && anchor.state == AnchorState::Resolved,
                  "Annotations on the grouped face follow it into the group's member");
    for (auto id : {staying, boundary})
        check(made.at(id) == original.at(id),
              "Annotations on the ungrouped face and the shared boundary are untouched");
    check(doc.bodies().at(f.body)->topology.edges.contains(shared) &&
              doc.bodies().at(member)->topology.edges.contains(shared),
          "The shared boundary exists in both contexts, so it never needs a transfer");
    samePoints(doc, points, "Annotations stay on the same world points");
    doc.undo();
    check(snapshot(doc) == original, "Undo restores the exact annotation records");
    doc.redo();
    check(snapshot(doc) == made, "Redo restores the exact annotation records");
}
// When a transfer does not name exactly one successor at the same place the anchor
// breaks as before: no re-binding by geometry.
void ambiguousTransfersBreak() {
    Pair f;
    auto &doc = f.doc;
    const auto bottom = edgeBetween(doc, f.body, {0, 0, 0}, {1, 0, 0});
    annotate(doc, "Run", AnnotationKind::Distance,
             {vertexAnchor(doc, f.body, vertexAt(doc, f.body, {0, 0, 0})),
              edgeAnchor(doc, f.body, bottom, .5)});
    annotate(doc, "Bay", AnnotationKind::Label, {faceAnchor(doc, f.body, f.left, {.5, .5, 0})});
    const auto original = snapshot(doc);
    // The same IDs in two targets: ambiguous.
    const auto old = doc.bodies().at(f.body);
    Edit two{"Duplicate", {{f.body, old, nullptr}}};
    for (Id id : {doc.nextId(), doc.nextId() + 1}) {
        auto copy = std::make_shared<Body>(*old);
        copy->id = id;
        two.changes.push_back({id, nullptr, copy});
        two.transfers[f.body].insert(id);
    }
    const auto entries = doc.history().total;
    doc.apply(std::move(two), doc.revision());
    check(doc.history().total == entries + 1, "The ambiguous edit is one entry");
    for (const auto &[id, record] : doc.annotations())
        for (size_t i = 0; i < record->anchors.size(); ++i) {
            const auto &anchor = record->anchors[i], &was = original.at(id).anchors[i];
            check(anchor.state == AnchorState::Missing && anchor.body == f.body &&
                      anchor.entity == was.entity && anchor.fallback == was.fallback,
                  "Two targets holding the entity break the anchor in place");
            check(measureAnnotation(doc, *record).state == AnchorState::Missing,
                  "The broken annotation reports its state");
        }
    doc.undo();
    check(snapshot(doc) == original, "Undo restores the exact annotation records");
    // One target, but the geometry moved: not the same point.
    const auto kept = doc.bodies().at(f.body);
    Edit shifted{"Shift", {{f.body, kept, nullptr}}};
    auto copy = std::make_shared<Body>(*kept);
    copy->id = doc.nextId();
    copy->transform = Transform::translation({0, 3, 0});
    shifted.changes.push_back({copy->id, nullptr, copy});
    shifted.transfers[f.body].insert(copy->id);
    doc.apply(std::move(shifted), doc.revision());
    for (const auto &[id, record] : doc.annotations())
        for (const auto &anchor : record->anchors)
            check(anchor.state == AnchorState::Missing && anchor.body == f.body,
                  "A transfer to another position is not followed");
    doc.undo();
    check(snapshot(doc) == original, "Undo restores the exact annotation records");
    // No transfer is named: nothing is re-bound by matching IDs and positions.
    const auto bare = doc.bodies().at(f.body);
    Edit unnamed{"Copy without lineage", {{f.body, bare, nullptr}}};
    auto twin = std::make_shared<Body>(*bare);
    twin->id = doc.nextId();
    unnamed.changes.push_back({twin->id, nullptr, twin});
    doc.apply(std::move(unnamed), doc.revision());
    for (const auto &[id, record] : doc.annotations())
        for (const auto &anchor : record->anchors)
            check(anchor.state == AnchorState::Missing && anchor.body == f.body,
                  "Without a named transfer the anchor is not re-bound");
}
} // namespace
int main() {
    try {
        edges();
        faces();
        validation();
        makeComponentOfBody();
        groupSelection();
        ambiguousTransfersBreak();
        std::cout << "Annotation anchor references, split lineage, holes, transforms and broken "
                     "states passed\n";
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
