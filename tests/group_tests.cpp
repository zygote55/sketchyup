#include "core/groups.hpp"
#include "core/selection.hpp"
#include "core/transform_selection.hpp"
#include <algorithm>
#include <iostream>
using namespace sketchy;
namespace {
void check(bool value, const char *message) {
    if (!value)
        throw std::runtime_error(message);
}
template <class F> void rejects(Document &doc, F operation) {
    const auto before = doc.bodies();
    const auto revision = doc.revision(), next = doc.nextId();
    try {
        operation();
    } catch (const std::exception &) {
        check(doc.bodies() == before && doc.revision() == revision && doc.nextId() == next,
              "Rejected group edit is atomic including allocation floors");
        return;
    }
    throw std::runtime_error("Expected rejection");
}
bool near(Vec3 a, Vec3 b) { return length(a - b) < 1e-9; }
} // namespace
int main() {
    try {
        Document doc;
        const auto a = doc.addFace({{{0, 0, 0}, {1, 0, 0}, {1, 1, 0}, {0, 1, 0}}}, "Panel");
        const auto b = doc.addFace({{{0, 0, 0}, {2, 0, 0}, {0, 2, 0}}}, "Bracket");
        doc.transform(a, Transform::translation({3, 2, 1}) * Transform::scaling({-2, 3, 1}));
        doc.paint(b, {.1f, .2f, .3f});
        doc.addGuide(a, guideLine({0, 0, 0}, {1, 0, 0}));
        Curve curve;
        curve.center = {3, 3, 0};
        curve.radius = .25;
        curve.segments = 12;
        doc.addCurve(a, curve);
        const auto originalA = *doc.bodies().at(a), originalB = *doc.bodies().at(b);
        const auto worldA = doc.worldTransform(a), worldB = doc.worldTransform(b);
        const auto group = createGroup(doc, {a, b}, "Assembly");
        check(doc.bodies().at(group)->kind == BodyKind::Group &&
                  doc.bodies().at(a)->parent == group && doc.bodies().at(b)->parent == group,
              "Typed group contains its members");
        check(doc.worldTransform(a) == worldA && doc.worldTransform(b) == worldB &&
                  doc.bodies().at(a)->surface == originalA.surface &&
                  doc.bodies().at(a)->topology == originalA.topology &&
                  doc.bodies().at(a)->guides == originalA.guides &&
                  doc.bodies().at(a)->curves == originalA.curves &&
                  doc.bodies().at(b)->color == originalB.color,
              "Grouping preserves geometry and metadata");
        doc.undo();
        check(*doc.bodies().at(a) == originalA && *doc.bodies().at(b) == originalB &&
                  !doc.bodies().contains(group),
              "Group is one undo item");
        doc.redo();
        Selection selection;
        selection.sync(doc);
        const auto face = doc.bodies().at(a)->surface.faces.begin()->first;
        check(!selection.selectable(doc, {a, SelectionKind::Face, face}) &&
                  selection.selectable(doc, {group, SelectionKind::Body, 0}),
              "Closed group isolates raw contents");
        selection.enter(doc, group);
        check(selection.selectable(doc, {a, SelectionKind::Face, face}) &&
                  !selection.selectable(doc, {group, SelectionKind::Body, 0}),
              "Open group exposes its geometry");
        selection.enter(doc, 0);
        const auto ordered = selection.ordered(doc);
        check(std::find(ordered.begin(), ordered.end(),
                        SelectedEntity{group, SelectionKind::Body, 0}) != ordered.end(),
              "Keyboard selection includes a closed group");
        selection.enter(doc, group);
        const auto nested = createGroup(doc, {a}, "Nested");
        check(enclosingGroup(doc, a) == nested && enclosingGroup(doc, nested) == group &&
                  !selection.selectable(doc, {a, SelectionKind::Face, face}) &&
                  selection.selectable(doc, {nested, SelectionKind::Body, 0}),
              "Nested group isolates one level");
        selection.enter(doc, nested);
        check(selection.selectable(doc, {a, SelectionKind::Face, face}),
              "Nested geometry becomes editable");
        doc.transform(group,
                      Transform::translation({4, 5, 6}) * Transform::rotation({0, 0, 1}, .7));
        const auto beforeReparent = doc.worldTransform(a).point({.3, .4, 0});
        reparentPreservingWorld(doc, a, group);
        check(near(doc.worldTransform(a).point({.3, .4, 0}), beforeReparent),
              "Reparent preserves transformed world placement");
        rejects(doc, [&] { reparentPreservingWorld(doc, group, nested); });
        rejects(doc, [&] { reparentPreservingWorld(doc, b, a); });
        rejects(doc, [&] { createGroup(doc, {group, b}); });
        setEntityState(doc, a, {}, true);
        rejects(doc, [&] { doc.move(a, {1, 0, 0}); });
        rejects(doc, [&] { doc.move(group, {1, 0, 0}); });
        rejects(doc, [&] { reparentPreservingWorld(doc, a, 0); });
        rejects(doc, [&] { createGroup(doc, {a, b}); });
        rejects(doc, [&] { explodeGroup(doc, group); });
        rejects(doc, [&] { doc.addWire(a, {0, 0, 1}, {1, 0, 1}); });
        rejects(doc, [&] {
            transformSelected(doc, {{a, TransformKind::Face, face}},
                              Transform::translation({1, 0, 0}));
        });
        selection.enter(doc, 0);
        check(!selection.selectable(doc, {group, SelectionKind::Body, 0}),
              "Locked descendants prevent indirect whole selection");
        setEntityState(doc, a, true, {});
        check(selection.hidden(doc, {a, SelectionKind::Face, face}),
              "Persistent hiding is reflected in selection");
        setEntityState(doc, a, false, false);
        check(!persistentlyLocked(doc, a), "Locked records can be explicitly unlocked");
        setEntityState(doc, group, {}, true);
        rejects(doc, [&] { doc.addWire(a, {0, 0, 1}, {1, 0, 1}); });
        rejects(doc, [&] { reparentPreservingWorld(doc, a, 0); });
        setEntityState(doc, group, {}, false);
        const auto beforeExplode = doc.worldTransform(a).point({.3, .4, 0});
        explodeGroup(doc, group);
        check(!doc.bodies().contains(group) && doc.bodies().at(a)->parent == 0 &&
                  doc.bodies().at(nested)->parent == 0 &&
                  near(doc.worldTransform(a).point({.3, .4, 0}), beforeExplode),
              "Explode preserves transformed child placement");
        doc.undo();
        check(doc.bodies().at(a)->parent == group && doc.bodies().contains(group),
              "Explode is one undo item");
        doc.addWire(group, {0, 0, 2}, {1, 0, 2});
        const auto groupGeometry = doc.bodies().at(group)->surface;
        explodeGroup(doc, group);
        check(doc.bodies().at(group)->kind == BodyKind::Geometry &&
                  doc.bodies().at(group)->surface == groupGeometry &&
                  doc.bodies().at(a)->parent == 0,
              "Explode retains directly drawn geometry with stable IDs");
        Document raw;
        const auto rawId = raw.addFace({{{0, 0, 0}, {1, 0, 0}, {1, 1, 0}, {0, 1, 0}}});
        auto rawBefore = raw.bodies().at(rawId);
        auto joined = std::make_shared<Body>(*rawBefore);
        const auto neighbor =
            joined->surface.addFace({{{1, 0, 0}, {2, 0, 0}, {2, 1, 0}, {1, 1, 0}}});
        raw.apply({"Adjacent fixture", {{rawId, rawBefore, joined}}}, raw.revision());
        raw.addGuide(rawId, guidePoint({.5, .5, 2}));
        const auto original = *raw.bodies().at(rawId);
        Selection rawSelection;
        rawSelection.apply(raw,
                           {{rawId, SelectionKind::Face, 5},
                            {rawId, SelectionKind::Guide, original.guides.begin()->first}},
                           SelectionMode::Replace);
        const auto transferred = groupSelected(raw, rawSelection);
        const auto member = transferred.movedGeometry.at(rawId);
        check(raw.bodies().at(rawId)->surface.faces.contains(neighbor) &&
                  !raw.bodies().at(rawId)->surface.faces.contains(5) &&
                  raw.bodies().at(member)->surface.faces.contains(5) &&
                  raw.bodies().at(member)->guides == original.guides &&
                  raw.bodies().at(rawId)->guides.empty(),
              "Grouping transfers only selected raw entities");
        std::optional<Id> boundary;
        for (const auto &[id, edge] : original.topology.edges)
            if (edge.a == 2 && edge.b == 3)
                boundary = id;
        check(boundary && raw.bodies().at(rawId)->topology.edges.contains(*boundary) &&
                  raw.bodies().at(member)->topology.edges.contains(*boundary),
              "Shared boundary remains with the unselected adjacent face and the new group");
        rawSelection.sync(raw);
        check(rawSelection.pickTarget(raw, {member, SelectionKind::Face, 5}) ==
                  SelectedEntity{transferred.group, SelectionKind::Body, 0},
              "Closed group resolves a raw hit to its container");
        raw.undo();
        check(*raw.bodies().at(rawId) == original && raw.bodies().size() == 1,
              "Raw transfer and new group undo together");
        rawSelection.enter(raw, rawId);
        rawSelection.apply(raw, {{rawId, SelectionKind::Face, 5}}, SelectionMode::Replace);
        const auto nestedRaw = groupSelected(raw, rawSelection);
        check(
            raw.bodies().at(rawId)->kind == BodyKind::Group &&
                raw.bodies().at(nestedRaw.group)->parent == rawId &&
                rawSelection.selectable(raw, {nestedRaw.group, SelectionKind::Body, 0}),
            "Grouping in a legacy raw context promotes its boundary without changing its identity");
        const auto moved = nestedRaw.movedGeometry.at(rawId);
        rawSelection.enter(raw, nestedRaw.group);
        rawSelection.apply(raw, {{moved, SelectionKind::Face, 5}}, SelectionMode::Replace);
        eraseSelected(raw, rawSelection);
        check(raw.bodies().at(moved)->surface.faces.empty() &&
                  raw.bodies().at(rawId)->surface.faces.contains(neighbor),
              "Deleting inside a nested group leaves inactive surrounding geometry intact");
        std::cout << "Groups, context isolation, nested transforms, authoritative locks and undo "
                     "passed\n";
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
