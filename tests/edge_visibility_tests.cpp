#include "core/edge_appearance.hpp"
#include "core/groups.hpp"
#include <iostream>
using namespace sketchy;
namespace {
void check(bool value, const char *message) {
    if (!value)
        throw std::runtime_error(message);
}
void test() {
    Document doc;
    const auto body = doc.addFace({{{0, 0, 0}, {2, 0, 0}, {2, 2, 0}, {0, 2, 0}}});
    const auto edge = doc.bodies().at(body)->topology.edges.begin()->first;
    const SelectedEntity ref{body, SelectionKind::Edge, edge}, face{body, SelectionKind::Face, 5};
    Selection selection;
    selection.sync(doc);
    setEdgeAppearance(doc, {ref}, 0, {}, {}, true);
    check(selection.selectable(doc, ref) && !selection.hidden(doc, ref),
          "Smooth alone leaves ordinary edges visible and selectable");
    selection.apply(doc, {ref, face}, SelectionMode::Replace);
    setEdgeAppearance(doc, {ref}, 0, {}, true, {});
    selection.sync(doc);
    check(selection.hidden(doc, ref) && !selection.selectable(doc, ref) &&
              !selection.pickTarget(doc, ref) && selection.entities() == SelectionSet{face},
          "Soft stroke suppression prunes only the edge, preserving face selection");
    selection.reveal(doc);
    check(selection.hidden(doc, ref), "Transient reveal never clears persistent edge flags");
    const auto revision = doc.revision(), history = doc.history().total;
    selection.showHidden(doc, true);
    check(selection.selectable(doc, ref) && selection.pickTarget(doc, ref) == ref,
          "Explicit hidden-geometry mode exposes the same authoritative edge");
    selection.apply(doc, {ref}, SelectionMode::Replace);
    selection.showHidden(doc, false);
    check(selection.entities().empty() && doc.revision() == revision &&
              doc.history().total == history,
          "Leaving reveal mode prunes suppressed selection without a document edit");
    setEdgeAppearance(doc, {ref}, 0, true, false, {});
    check(selection.hidden(doc, ref), "Hidden alone suppresses a hard edge");
    setEdgeAppearance(doc, {ref}, 0, false, {}, {});
    check(!selection.hidden(doc, ref) && edgeAppearance(*doc.bodies().at(body), edge).smooth,
          "Explicit hidden-ID command clears only hidden, preserving smooth");
    setEdgeAppearance(doc, {ref}, 0, true, true, {});
    const auto group = createGroup(doc, {body});
    selection.sync(doc);
    selection.showHidden(doc, true);
    check(!selection.selectable(doc, ref), "Reveal does not bypass a group editing boundary");
    selection.enter(doc, group);
    check(selection.selectable(doc, ref), "Entered group exposes revealed edges");
    selection.lock(doc, group, true);
    check(!selection.selectable(doc, ref), "Transient lock overrides reveal");
    selection.unlockAll(doc);
    selection.enter(doc, group);
    setEntityState(doc, group, {}, true);
    selection.sync(doc);
    check(!selection.selectable(doc, ref) && !selection.pickTarget(doc, ref),
          "Persistent ancestor lock overrides reveal and group fallback picking");
}
} // namespace
int main() {
    try {
        test();
        std::cout
            << "Persistent edge visibility, reveal, selection pruning, context and locks passed\n";
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
