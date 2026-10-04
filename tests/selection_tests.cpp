#include "core/selection.hpp"
#include <iostream>
using namespace sketchy;
void check(bool ok, const char *message) {
    if (!ok)
        throw std::runtime_error(message);
}
template <class F> void rejects(F action) {
    bool rejected = false;
    try {
        action();
    } catch (const std::exception &) {
        rejected = true;
    }
    check(rejected, "Expected selection rejection");
}
int main() {
    try {
        Document doc;
        doc.addFace({{{0, 0, 0}, {4, 0, 0}, {4, 4, 0}, {0, 4, 0}},
                     {{1, 1, 0}, {1, 2, 0}, {2, 2, 0}, {2, 1, 0}}});
        const auto face = doc.bodies().at(1)->surface.faces.begin()->first;
        const auto edge = doc.bodies().at(1)->topology.edges.begin()->first;
        doc.addWire(1, {5, 0, 0}, {6, 0, 0});
        doc.addGuide(1, guidePoint({.5, .5, 0}));
        const auto guide = doc.bodies().at(1)->guides.begin()->first;
        doc.addFace({{{0, 0, 1}, {1, 0, 1}, {0, 1, 1}}});
        doc.transform(2, Transform::translation({0, 0, 1}), 1);
        const auto revision = doc.revision(), history = doc.historyBytes();
        const auto dirty = doc.dirty();
        Selection selection;
        selection.sync(doc);
        const SelectedEntity f{1, SelectionKind::Face, face}, e{1, SelectionKind::Edge, edge},
            g{1, SelectionKind::Guide, guide}, b{1, SelectionKind::Body, 0},
            child{2, SelectionKind::Body, 0};
        selection.apply(doc, {f}, SelectionMode::Replace);
        selection.apply(doc, {e, g}, SelectionMode::Add);
        check(selection.entities().size() == 3 && selection.summary() == "1 face, 1 edge, 1 guide",
              "Typed additive selection has an accessible summary");
        selection.apply(doc, selection.entities(), SelectionMode::Replace);
        check(selection.entities().size() == 3, "Replacing with the current set is alias safe");
        selection.apply(doc, {e, g}, SelectionMode::Toggle);
        check(selection.entities() == SelectionSet{f}, "Toggle removes only specified entities");
        selection.apply(doc, {b}, SelectionMode::Add);
        selection.apply(doc, {child}, SelectionMode::Add);
        check(selection.entities() == SelectionSet{b}, "Whole context subsumes its geometry");
        selection.apply(doc, {{1, SelectionKind::Face, edge}}, SelectionMode::Replace);
        check(selection.entities().empty(), "IDs cannot cross entity namespaces");
        const auto boundary = selection.boundary(doc, f);
        check(boundary.size() == 9 && boundary.contains(f),
              "Face boundary includes outer and hole edges");
        check(selection.connected(doc, e) == boundary,
              "Connected edge traversal follows faces across hole loops and excludes loose "
              "islands/guides");
        check(selection.connected(doc, g) == SelectionSet{g},
              "Construction guides do not join model topology");
        selection.hide(doc, {e});
        check(!selection.selectable(doc, e) && selection.connected(doc, f).size() == 8,
              "Hidden geometry never leaks through connected selection");
        selection.showHidden(doc, true);
        check(selection.selectable(doc, e) && selection.hidden(doc, e),
              "Hidden mode exposes hidden entities explicitly");
        selection.apply(doc, {e}, SelectionMode::Replace);
        selection.showHidden(doc, false);
        check(selection.entities().empty(), "Leaving hidden mode prunes hidden selections");
        selection.reveal(doc);
        selection.enter(doc, 1);
        selection.apply(doc, {child, f}, SelectionMode::Add);
        check(selection.entities() == SelectionSet{f},
              "Inactive contexts cannot be selected additively");
        for (auto entity : selection.ordered(doc))
            check(entity.body == 1, "Keyboard order respects active context");
        selection.enter(doc, 0);
        selection.hide(doc, {b});
        check(selection.hidden(doc, child) && !selection.selectable(doc, child),
              "Context hiding includes descendants");
        rejects([&] { selection.enter(doc, 2); });
        selection.showHidden(doc, true);
        selection.enter(doc, 2);
        selection.lock(doc, 1, true);
        check(selection.context() == 0 && !selection.selectable(doc, child) &&
                  !selection.selectable(doc, f),
              "Parent locks override hidden mode and close a locked editing context");
        rejects([&] { selection.enter(doc, 2); });
        selection.apply(doc, {child, f}, SelectionMode::Toggle);
        check(selection.entities().empty() && selection.ordered(doc).empty(),
              "Toggle and keyboard traversal cannot bypass a lock");
        selection.unlockAll(doc);
        selection.lock(doc, 2, true);
        check(!selection.selectable(doc, b) && selection.selectable(doc, f),
              "A whole-context selection cannot indirectly include a locked descendant");
        selection.unlockAll(doc);
        selection.reveal(doc);
        selection.apply(doc, {f, e, g}, SelectionMode::Replace);
        check(doc.revision() == revision && doc.historyBytes() == history && doc.dirty() == dirty,
              "All selection and view-state operations leave document state untouched");
        doc.eraseGuide(1, guide);
        selection.sync(doc);
        check(!selection.entities().contains(g) && selection.entities().contains(e),
              "Deleted entities are pruned by typed identity");
        doc.undo();
        selection.sync(doc);
        check(!selection.entities().contains(g), "Undo does not resurrect stale selection state");
        Document reopened;
        reopened.restore(doc.identity(), doc.nextId(), doc.bodies());
        selection.sync(reopened);
        check(selection.entities().empty() && selection.hiddenEntities().empty() &&
                  selection.lockedBodies().empty(),
              "Reopen with identical document identity clears session selection and view state");
        rejects([&] { selection.apply(reopened, {}, static_cast<SelectionMode>(99)); });
        rejects([&] { selection.lock(reopened, 999, true); });
        rejects([&] { selection.enter(reopened, 999); });
        Selection deletion;
        deletion.apply(reopened, {f, e, g}, SelectionMode::Replace);
        const auto beforeDelete = reopened.bodies();
        const auto deleteRevision = reopened.revision();
        eraseSelected(reopened, deletion);
        check(
            reopened.revision() == deleteRevision + 1 && deletion.entities().empty() &&
                !reopened.bodies().at(1)->surface.faces.contains(face) &&
                !reopened.bodies().at(1)->topology.edges.contains(edge) &&
                !reopened.bodies().at(1)->guides.contains(guide) && reopened.bodies().contains(2),
            "Mixed face/edge/guide deletion is one typed atomic edit and preserves other contexts");
        reopened.undo();
        for (const auto &[id, body] : beforeDelete)
            check(*reopened.bodies().at(id) == *body,
                  "One undo restores the entire mixed selection edit");
        deletion.apply(reopened, {b, child}, SelectionMode::Replace);
        eraseSelected(reopened, deletion);
        check(reopened.bodies().empty(), "Whole context deletion includes its descendants");
        reopened.undo();
        deletion.lock(reopened, 1, true);
        deletion.apply(reopened, {f, child}, SelectionMode::Replace);
        const auto lockedRevision = reopened.revision();
        eraseSelected(reopened, deletion);
        check(reopened.revision() == lockedRevision, "Delete cannot bypass context locks");
        Document healing;
        healing.addFace({{{0, 0, 0}, {4, 0, 0}, {4, 4, 0}, {0, 4, 0}}});
        healing.insertEdges(1, {}, {0, 0, 1}, {{{2, 0, 0}, {2, 4, 0}}});
        const auto divided = healing.bodies().at(1);
        const auto adjacency = divided->topology.adjacency(divided->surface);
        Id seam = 0;
        for (const auto &[id, incident] : adjacency.edgeFaces)
            if (incident.size() == 2)
                seam = id;
        Selection healSelection;
        healSelection.apply(healing, {{1, SelectionKind::Edge, seam}}, SelectionMode::Replace);
        const auto healed = eraseSelected(healing, healSelection);
        check(healing.bodies().at(1)->surface.faces.size() == 1,
              "Selected seam deletion heals adjacent faces");
        const auto joinedFace = healing.bodies().at(1)->surface.faces.begin()->first;
        for (const auto &[id, record] : divided->surface.faces)
            check(healed.at(1).faces.descendants.at(id) == std::vector<Id>{joinedFace},
                  "Atomic selection deletion preserves face-healing lineage");
        Document many;
        for (int i = 0; i < 101; ++i)
            many.addGuide(i ? 1 : 0, guidePoint({double(i), 0, 0}));
        Selection manySelection;
        SelectionSet points;
        for (const auto &[id, guide] : many.bodies().at(1)->guides)
            points.insert({1, SelectionKind::Guide, id});
        manySelection.apply(many, points, SelectionMode::Replace);
        const auto manyRevision = many.revision();
        rejects([&] { eraseSelected(many, manySelection); });
        check(many.revision() == manyRevision && many.bodies().at(1)->guides.size() == 101,
              "Bounded subentity deletion rejects before any mutation");
        manySelection.apply(many, {{1, SelectionKind::Body, 0}}, SelectionMode::Replace);
        eraseSelected(many, manySelection);
        check(many.bodies().empty(), "Whole-context deletion supports bulk geometry");
        std::cout << "Typed selection, modes, boundary/connected topology, context and visibility "
                     "guards, keyboard order, session reset and immutability passed\n";
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
