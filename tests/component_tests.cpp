#include "core/components.hpp"
#include "core/groups.hpp"
#include "core/selection.hpp"
#include <iostream>
using namespace sketchy;
namespace {
void check(bool ok, const char *message) {
    if (!ok)
        throw std::runtime_error(message);
}
template <class F> void rejects(F run, const char *message) {
    bool rejected = false;
    try {
        run();
    } catch (const std::exception &) {
        rejected = true;
    }
    check(rejected, message);
}
Id square(Document &doc, double size = 1) {
    return doc.addFace({{{0, 0, 0}, {size, 0, 0}, {size, size, 0}, {0, size, 0}}});
}
void creationPlacementAndReplacement() {
    Document doc;
    const auto raw = square(doc);
    doc.addGuide(raw, guidePoint({.5, .5, 0}));
    doc.transform(raw, Transform::translation({2, 3, 0}));
    const auto original = *doc.bodies().at(raw);
    const auto created = createComponent(doc, raw, "Panel");
    const auto member = created.movedGeometry.at(raw);
    check(created.instance == raw && doc.instances().at(raw)->definition == created.definition &&
              doc.bodies().at(raw)->surface.vertices.empty() &&
              doc.bodies().at(member)->surface.faces.contains(5),
          "Raw geometry becomes a component with a stable placement and transferred geometry");
    check(doc.worldTransform(member) == original.transform && doc.worldArea(member, 5) == 1 &&
              doc.bodies().at(member)->guides.size() == 1,
          "Component creation preserves world geometry and guides");
    doc.undo();
    check(doc.instances().empty() && doc.definitions().empty() && *doc.bodies().at(raw) == original,
          "Creation is one undo including canonical and placed records");
    doc.redo();
    const auto second =
        placeComponent(doc, created.definition,
                       Transform::translation({5, 0, 0}) * Transform::scaling({-2, 1, 1}));
    const auto secondMember = doc.instances().at(second.instance)->members.at(member);
    check(doc.worldArea(secondMember, 5) == 2 && doc.instances().size() == 2,
          "Placed mirror/scale instances share canonical geometry");
    const auto beforeUnique = doc.bodies();
    const auto unique = makeComponentUnique(doc, second.instance);
    check(unique.definition != created.definition &&
              doc.instances().at(raw)->definition == created.definition,
          "Make unique changes exactly the selected binding");
    for (const auto &[id, body] : beforeUnique)
        check(*doc.bodies().at(id) == *body,
              "Make unique preserves scene identities and appearance");
    doc.undo();
    check(doc.instances().at(second.instance)->definition == created.definition,
          "Make unique undoes to the original sharing relationship");
    const auto other = createComponent(doc, square(doc, 2), "Large panel");
    const auto placement = doc.worldTransform(second.instance);
    const auto replacement = replaceComponent(doc, second.instance, other.definition);
    const auto replacementMember =
        doc.instances().at(second.instance)->members.at(other.movedGeometry.begin()->second);
    check(replacement.instance == second.instance &&
              doc.worldTransform(second.instance) == placement &&
              !doc.bodies().contains(secondMember) && replacementMember > secondMember &&
              doc.worldArea(replacementMember, 5) == 8,
          "Replacement preserves placement and retires old geometry contexts");
    doc.undo();
    check(doc.bodies().contains(secondMember) &&
              doc.instances().at(second.instance)->definition == created.definition,
          "Replacement undo restores member contexts and definition binding");
    setEntityState(doc, second.instance, {}, true);
    const auto snapshot = doc.bodies();
    const auto definitions = doc.definitions();
    rejects([&] { makeComponentUnique(doc, second.instance); },
            "Locked instance cannot become unique");
    rejects([&] { replaceComponent(doc, second.instance, other.definition); },
            "Locked instance cannot be replaced");
    check(doc.bodies() == snapshot && doc.definitions() == definitions,
          "Rejected component operations are atomic");
}
void leafEditsBesideUnchangedInstances() {
    Document doc;
    const auto leaf = createComponent(doc, square(doc), "Leaf");
    const auto member = leaf.movedGeometry.at(leaf.instance);
    const auto peer = placeComponent(doc, leaf.definition, Transform::translation({5, 0, 0}));
    const auto peerMember = doc.instances().at(peer.instance)->members.at(member);
    const auto unrelated = createComponent(doc, square(doc, 3), "Unrelated");
    setEntityState(doc, unrelated.instance, {}, true);
    const auto unrelatedMember = unrelated.movedGeometry.at(unrelated.instance);
    const auto preserved = *doc.bodies().at(unrelatedMember);
    editComponentDefinition(doc, leaf.definition, [&](Document &draft) {
        draft.transform(member, Transform::scaling({2, 1, 1}));
        return ChangeReport{};
    });
    check(doc.worldArea(member, 5) == 2 && doc.worldArea(peerMember, 5) == 2 &&
              *doc.bodies().at(unrelatedMember) == preserved,
          "Changed leaf definitions update all peers beside an unchanged locked component");
    const auto added = placeComponent(doc, leaf.definition, Transform::translation({10, 0, 0}));
    const auto addedMember = doc.instances().at(added.instance)->members.at(member);
    check(doc.worldArea(addedMember, 5) == 2 && doc.worldArea(peerMember, 5) == 2,
          "New placements use the edited definition without disturbing existing peers");
    doc.undo();
    doc.undo();
    check(doc.worldArea(member, 5) == 1 && doc.worldArea(peerMember, 5) == 1 &&
              !doc.bodies().contains(addedMember),
          "Placement and shared edit undo restore every affected leaf");
    doc.redo();
    doc.redo();
    check(doc.worldArea(addedMember, 5) == 2 && *doc.bodies().at(unrelatedMember) == preserved,
          "Redo restores edited placements while preserving unrelated geometry");
    validateComponentInstances(doc.definitions(), doc.instances(), doc.bodies());
}
void nestedUniqueness() {
    Document doc;
    const auto leaf = createComponent(doc, square(doc), "Leaf");
    const auto assemblyRoot = createGroup(doc, {leaf.instance}, "Assembly");
    const auto assembly = createComponent(doc, assemblyRoot, "Assembly");
    const auto peer = placeComponent(doc, assembly.definition, Transform::translation({4, 0, 0}));
    const auto peerLeaf = doc.instances().at(peer.instance)->members.at(leaf.instance);
    check(doc.instances().at(peerLeaf)->definition == leaf.definition,
          "Nested placement retains leaf sharing");
    const auto peerBinding = doc.instances().at(peer.instance);
    const auto peerLeafBinding = doc.instances().at(peerLeaf);
    const auto before = doc.bodies();
    const auto unique = makeComponentUnique(doc, leaf.instance);
    check(doc.instances().at(leaf.instance)->definition == unique.definition &&
              doc.instances().at(assemblyRoot)->definition != assembly.definition &&
              doc.instances().at(peer.instance) == peerBinding &&
              doc.instances().at(peerLeaf) == peerLeafBinding,
          "Nested uniqueness clones the ownership path while preserving peer instances");
    for (const auto &[id, body] : before)
        check(*doc.bodies().at(id) == *body, "Nested uniqueness preserves all scene geometry");
    doc.undo();
    const auto other = createComponent(doc, square(doc, 2), "Replacement");
    replaceComponent(doc, leaf.instance, other.definition);
    check(doc.instances().at(leaf.instance)->definition == other.definition &&
              doc.instances().at(peerLeaf)->definition == leaf.definition &&
              doc.instances().at(peer.instance)->definition == assembly.definition,
          "Nested replacement isolates the selected ownership path");
    validateComponentDefinitions(doc.definitions(), doc.nextDefinitionId());
    validateComponentInstances(doc.definitions(), doc.instances(), doc.bodies());
}
void sharedEditsAndAxes() {
    Document doc;
    const auto leaf = createComponent(doc, square(doc, 2), "Leaf");
    const auto member = leaf.movedGeometry.at(leaf.instance);
    const auto assembly = createComponent(doc, createGroup(doc, {leaf.instance}), "Assembly");
    const auto peer =
        placeComponent(doc, assembly.definition,
                       Transform::translation({5, 0, 0}) * Transform::scaling({-1, 2, 1}));
    const auto peerLeaf = doc.instances().at(peer.instance)->members.at(leaf.instance);
    const auto peerMember = doc.instances().at(peerLeaf)->members.at(member);
    const auto revision = doc.revision();
    const auto edit = editComponentDefinition(doc, leaf.definition, [&](Document &draft) {
        return draft.insertEdges(member, {}, {0, 0, 1}, {{Vec3{1, 0, 0}, Vec3{1, 2, 0}}});
    });
    check(doc.revision() == revision + 1 && doc.bodies().at(member)->surface.faces.size() == 2 &&
              doc.bodies().at(peerMember)->surface.faces.size() == 2 &&
              edit.changes.at(member).faces.descendants.at(5).size() == 2 &&
              edit.changes.at(peerMember).faces.descendants.at(5).size() == 2,
          "Shared nested edits propagate geometry and lineage to all instances in one revision");
    doc.undo();
    check(doc.bodies().at(member)->surface.faces.size() == 1 &&
              doc.bodies().at(peerMember)->surface.faces.size() == 1,
          "Shared edit undo restores every instance and its definition");
    doc.redo();
    setEntityState(doc, peer.instance, {}, true);
    const auto before = doc.bodies();
    const auto definitions = doc.definitions();
    rejects(
        [&] {
            editComponentDefinition(doc, leaf.definition, [&](Document &draft) {
                draft.move(member, {1, 0, 0});
                return ChangeReport{};
            });
        },
        "A locked peer rejects the whole shared edit");
    check(doc.bodies() == before && doc.definitions() == definitions,
          "Shared rejection leaves all canonical and resolved records untouched");
    setEntityState(doc, peer.instance, {}, false);
    std::map<Id, std::map<Id, Vec3>> world;
    for (const auto &[id, body] : doc.bodies())
        for (const auto &[vertex, point] : body->surface.vertices)
            world[id][vertex] = doc.worldTransform(id).point(point);
    setComponentAxes(doc, leaf.definition,
                     Transform::translation({.4, .7, 0}) * Transform::rotation({0, 0, 1}, .6));
    for (const auto &[id, vertices] : world)
        for (const auto &[vertex, point] : vertices)
            check(length(doc.worldTransform(id).point(
                             doc.bodies().at(id)->surface.vertices.at(vertex)) -
                         point) < tolerance,
                  "Local axes preserve all nested and mirrored instance geometry in world space");
    doc.undo();
    const auto stable = doc.bodies();
    rejects(
        [&] {
            editComponentDefinition(doc, assembly.definition, [&](Document &draft) {
                placeComponent(draft, assembly.definition, {},
                               doc.definitions().at(assembly.definition)->root);
                return ChangeReport{};
            });
        },
        "A shared edit cannot insert its own definition recursively");
    check(doc.bodies() == stable, "Cyclic shared edits reject atomically");
    const auto unique = makeComponentUnique(doc, leaf.instance);
    const auto beforePeer = doc.bodies().at(peerMember);
    editComponentDefinition(doc, unique.definition, [&](Document &draft) {
        draft.move(member, {0, 0, 1});
        return ChangeReport{};
    });
    check(doc.bodies().at(peerMember) == beforePeer,
          "Editing a unique nested definition leaves the original peer untouched");
    const auto root = doc.definitions().at(unique.definition)->root;
    const auto added = editComponentDefinition(doc, unique.definition, [&](Document &draft) {
        return draft.insertEdges(root, {0, 0, 3}, {0, 0, 1},
                                 {{Vec3{0, 0, 3}, Vec3{1, 0, 3}},
                                  {Vec3{1, 0, 3}, Vec3{1, 1, 3}},
                                  {Vec3{1, 1, 3}, Vec3{0, 1, 3}},
                                  {Vec3{0, 1, 3}, Vec3{0, 0, 3}}});
    });
    check(added.movedGeometry.contains(root) &&
              doc.definitions().at(unique.definition)->members.at(root)->surface.vertices.empty(),
          "Drawing in the definition root creates a geometry member and preserves the placement "
          "frame");
}
void sharedHierarchyEdits() {
    Document doc;
    const auto created = createComponent(doc, square(doc));
    const auto peer = placeComponent(doc, created.definition, Transform::translation({3, 0, 0}));
    const auto root = doc.definitions().at(created.definition)->root;
    Id added = 0;
    editComponentDefinition(doc, created.definition, [&](Document &draft) {
        added = square(draft, 2);
        setEntityState(draft, added, {}, true);
        return ChangeReport{};
    });
    check(doc.definitions().at(created.definition)->members.at(added)->parent == root &&
              doc.bodies().at(doc.instances().at(peer.instance)->members.at(added))->locked,
          "New model-root geometry in shared scope joins every instance, including lock state");
    doc.undo();
    check(!doc.definitions().at(created.definition)->members.contains(added),
          "Adding shared locked geometry undoes atomically");
    const auto leaf = createComponent(doc, square(doc, 2), "Nested leaf");
    Id nested = 0;
    editComponentDefinition(doc, created.definition, [&](Document &draft) {
        nested = placeComponent(draft, leaf.definition).instance;
        return ChangeReport{};
    });
    check(doc.definitions().at(created.definition)->references.at(nested) == leaf.definition &&
              doc.instances().size() == 5,
          "New nested instances in shared scope attach to all placements");
    std::set<Id> promotedGeometry;
    for (const auto &[id, body] : doc.bodies())
        if (!body->surface.faces.empty())
            promotedGeometry.insert(id);
    editComponentDefinition(doc, created.definition,
                            [&](Document &draft) { return explodeGroup(draft, nested); });
    check(doc.definitions().at(created.definition)->references.empty() &&
              doc.instances().size() == 3,
          "Shared nested explode converts every placement into ordinary member geometry");
    for (auto id : promotedGeometry)
        check(doc.bodies().contains(id),
              "Shared boundary-only explode preserves existing scene geometry IDs");
    doc.undo();
    editComponentDefinition(doc, created.definition, [&](Document &draft) {
        Selection selection;
        selection.enter(draft, root);
        selection.apply(draft, {{nested, SelectionKind::Body, 0}}, SelectionMode::Replace);
        return eraseSelected(draft, selection);
    });
    check(doc.definitions().at(created.definition)->references.empty() &&
              doc.instances().size() == 3,
          "Shared nested deletion removes every placement and binding");
    doc.undo();
    check(doc.instances().size() == 5, "Shared nested deletion restores on undo");
    const auto snapshot = doc.bodies();
    rejects([&] { placeComponent(doc, leaf.definition, {}, created.instance); },
            "Unscoped placement cannot add an unbound component member");
    rejects([&] { setComponentAxes(doc, 9999, {}); },
            "Identity axes still validate the requested definition");
    check(doc.bodies() == snapshot, "Rejected hierarchy operations leave scene unchanged");
}
} // namespace
int main() {
    try {
        creationPlacementAndReplacement();
        leafEditsBesideUnchangedInstances();
        nestedUniqueness();
        sharedEditsAndAxes();
        sharedHierarchyEdits();
        std::cout
            << "Component creation, placement, replacement, nested uniqueness and undo passed\n";
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
