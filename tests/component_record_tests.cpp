#include "core/component_records.hpp"
#include "core/copy_array.hpp"
#include "core/groups.hpp"
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
std::shared_ptr<ComponentDefinition> fixture(Id id = 1) {
    Document doc;
    doc.addFace({{{0, 0, 0}, {1, 0, 0}, {1, 1, 0}, {0, 1, 0}}});
    doc.addGuide(1, guideLine({}, {1, 0, 0}));
    const auto root = createGroup(doc, {1}, "Window");
    auto definition = std::make_shared<ComponentDefinition>();
    definition->id = id;
    definition->name = "Window";
    definition->root = root;
    definition->nextMemberId = doc.nextId();
    definition->members = doc.bodies();
    return definition;
}
std::shared_ptr<ComponentDefinition> reference(Id id, Id target, size_t count = 1) {
    auto definition = std::make_shared<ComponentDefinition>();
    definition->id = id;
    definition->root = 1;
    definition->nextMemberId = count + 2;
    for (Id node = 1; node <= count + 1; ++node) {
        auto body = std::make_shared<Body>();
        body->id = node;
        body->kind = BodyKind::Group;
        body->parent = node == 1 ? 0 : 1;
        definition->members[node] = body;
        if (node != 1)
            definition->references[node] = target;
    }
    return definition;
}
// Fixture projection only. Production instantiation will publish these records
// and bindings through the document transaction, with allocation preflight.
Id project(const ComponentDefinitions &definitions, Id definitionId, Id &next,
           std::map<Id, BodyPtr> &scene, ComponentInstances &instances, Body placement = {},
           Id existingRoot = 0) {
    const auto &definition = *definitions.at(definitionId);
    const auto root = existingRoot ? existingRoot : next++;
    auto instance = std::make_shared<ComponentInstance>();
    instance->definition = definitionId;
    instance->members[definition.root] = root;
    for (const auto &[id, source] : definition.members) {
        if (id == definition.root)
            continue;
        instance->members[id] = next++;
    }
    for (const auto &[id, source] : definition.members) {
        auto body = std::make_shared<Body>(*source);
        body->id = instance->members.at(id);
        body->parent =
            id == definition.root ? placement.parent : instance->members.at(source->parent);
        if (id == definition.root) {
            body->name = placement.name;
            body->transform = placement.transform;
            body->properties = placement.properties;
            body->hidden = placement.hidden;
            body->locked = placement.locked;
        }
        scene[body->id] = body;
    }
    instances[root] = instance;
    return root;
}
void recordsAndProjection() {
    ComponentDefinitions definitions{{1, fixture()}};
    const auto sizes = validateComponentDefinitions(definitions, 2);
    check(sizes.at(1).records == 2 && sizes.at(1).vertices == 4 && sizes.at(1).faces == 1 &&
              sizes.at(1).guides == 1,
          "Canonical definition counts include raw geometry and guides");
    std::map<Id, BodyPtr> scene;
    ComponentInstances instances;
    Id next = 1;
    Body placement;
    placement.name = "Window A";
    const auto a = project(definitions, 1, next, scene, instances, placement);
    placement.name = "Window B";
    placement.transform = Transform::translation({4, 0, 0}) * Transform::scaling({-2, 1, 1});
    placement.locked = true;
    const auto b = project(definitions, 1, next, scene, instances, placement);
    validateComponentInstances(definitions, instances, scene);
    Document doc;
    doc.restore(doc.identity(), next, scene);
    check(doc.worldArea(instances.at(b)->members.at(1), 5) == 2,
          "Resolved instance placement supports mirroring and nonuniform scale");
    auto changed = scene;
    const auto member = instances.at(a)->members.at(1);
    auto edited = std::make_shared<Body>(*scene.at(member));
    edited->color = {.9f, .1f, .1f};
    changed[member] = edited;
    rejects([&] { validateComponentInstances(definitions, instances, changed); },
            "Resolved scene cannot silently diverge from shared geometry appearance");
    edited = std::make_shared<Body>(*scene.at(member));
    edited->surface.nextId += 20;
    edited->topology.nextId += 20;
    changed[member] = edited;
    validateComponentInstances(definitions, instances, changed);
    auto extra = std::make_shared<Body>();
    extra->id = next;
    extra->parent = a;
    changed[next] = extra;
    rejects([&] { validateComponentInstances(definitions, instances, changed); },
            "Unbound geometry cannot enter a placed component");
    auto bad = std::make_shared<ComponentInstance>(*instances.at(a));
    bad->members[1] = a;
    auto bindings = instances;
    bindings[a] = bad;
    rejects([&] { validateComponentInstances(definitions, bindings, scene); },
            "Duplicate projected identities reject");
}
void documentTransactions() {
    auto definition = fixture();
    ComponentDefinitions definitions{{1, definition}};
    ComponentInstances instances;
    std::map<Id, BodyPtr> scene;
    Id next = 1;
    const auto first = project(definitions, 1, next, scene, instances);
    Body placement;
    placement.transform = Transform::translation({3, 0, 0});
    const auto second = project(definitions, 1, next, scene, instances, placement);
    Edit creation{"Create component fixtures", {}};
    creation.definitions.push_back({1, nullptr, definition});
    for (const auto &[id, body] : scene)
        creation.changes.push_back({id, nullptr, body});
    for (const auto &[root, instance] : instances)
        creation.instances.push_back({root, nullptr, instance});
    Document doc;
    doc.apply(creation, doc.revision());
    check(doc.revision() == 1 && doc.definitions().size() == 1 && doc.instances().size() == 2 &&
              doc.nextDefinitionId() == 2,
          "Definition and placed records publish in one transaction");
    definition->name = "Mutated caller";
    definition->members.clear();
    check(doc.definitions().at(1)->name == "Window" && doc.definitions().at(1)->members.size() == 2,
          "Document freezes caller-owned component records");
    const auto before = doc.bodies();
    rejects([&] { doc.paint(doc.instances().at(first)->members.at(1), {.9f, .1f, .1f}); },
            "Independent edits cannot diverge a placed member from its definition");
    check(doc.bodies() == before && doc.revision() == 1, "Divergence rejection is atomic");
    const auto secondWorld = doc.worldTransform(second);
    doc.move(first, {0, 2, 0});
    check(doc.worldTransform(second) == secondWorld, "Moving one instance preserves its peer");
    doc.undo();
    auto changed = std::make_shared<ComponentDefinition>(*doc.definitions().at(1));
    auto colored = std::make_shared<Body>(*changed->members.at(1));
    colored->color = {.9f, .1f, .2f};
    changed->members[1] = colored;
    Edit shared{"Shared definition color", {}};
    shared.definitions.push_back({1, doc.definitions().at(1), changed});
    for (const auto &[root, instance] : doc.instances()) {
        const auto member = instance->members.at(1);
        const auto original = doc.bodies().at(member);
        auto updated = std::make_shared<Body>(*original);
        updated->color = colored->color;
        shared.changes.push_back({member, original, updated});
    }
    const auto revision = doc.revision();
    doc.apply(shared, revision);
    check(doc.revision() == revision + 1 &&
              doc.definitions().at(1)->members.at(1)->color == colored->color,
          "Shared definition and every resolved peer commit together");
    doc.undo();
    check(doc.definitions().at(1)->members.at(1)->color != colored->color,
          "Undo restores canonical definition geometry");
    validateComponentInstances(doc.definitions(), doc.instances(), doc.bodies());
    doc.redo();
    validateComponentInstances(doc.definitions(), doc.instances(), doc.bodies());
    const auto copied =
        transformSelected(doc, {{first, TransformKind::Context, 0}},
                          Transform::translation({0, 3, 0}), {}, TransformSpace::World, true);
    check(doc.instances().size() == 3 &&
              doc.instances().at(copied.copies.at(first))->definition == 1,
          "Whole-context copies retain shared definition bindings");
    doc.undo();
    CopyArray array;
    array.copies = 2;
    array.delta = {0, 4, 0};
    copyArraySelected(doc, {{first, TransformKind::Context, 0}}, array);
    check(doc.instances().size() == 4, "Copy arrays publish every instance binding");
    const auto stamp = doc.amendmentStamp();
    doc.amendLast(
        stamp,
        [&](Document &candidate) {
            array.copies = 3;
            copyArraySelected(candidate, {{first, TransformKind::Context, 0}}, array);
        },
        Document::AmendPolicy::CopyArray);
    check(doc.instances().size() == 5, "Array amendment replaces the component binding count");
    doc.undo();
    check(doc.instances().size() == 2, "One array undo restores the original shared placements");
    auto variant = [&](Document &candidate, const char *name) {
        const auto previous = candidate.instances().at(second);
        auto unique = std::make_shared<ComponentDefinition>(
            *candidate.definitions().at(previous->definition));
        unique->id = candidate.nextDefinitionId();
        unique->name = name;
        auto rebound = std::make_shared<ComponentInstance>(*previous);
        rebound->definition = unique->id;
        Edit replacement{"Variant binding fixture", {}};
        replacement.definitions.push_back({unique->id, nullptr, unique});
        replacement.instances.push_back({second, previous, rebound});
        candidate.apply(replacement, candidate.revision());
    };
    variant(doc, "Variant A");
    const auto variantStamp = doc.amendmentStamp();
    doc.amendLast(variantStamp, [&](Document &candidate) { variant(candidate, "Variant B"); });
    check(doc.definitions().size() == 2 &&
              doc.definitions().at(doc.instances().at(second)->definition)->name == "Variant B",
          "Binding-only amendment retains its original context scope");
    doc.undo();
    check(doc.definitions().size() == 1 && doc.instances().at(second)->definition == 1,
          "One binding amendment undo restores the shared definition");
    Document reopened;
    reopened.restore(doc.identity(), doc.nextId(), doc.bodies(), doc.revision(), doc.definitions(),
                     doc.instances(), doc.nextDefinitionId());
    check(reopened.instances().size() == 2 && reopened.definitions().size() == 1,
          "Restore freezes and validates canonical and placed records together");
    setEntityState(doc, second, {}, true);
    Edit invalid{"Drop locked binding", {}};
    invalid.instances.push_back({second, doc.instances().at(second), nullptr});
    rejects([&] { doc.apply(invalid, doc.revision()); }, "Locked instance cannot lose its binding");
    Document allocator;
    auto unused = fixture();
    Edit add{"Unused definition", {}};
    add.definitions.push_back({1, nullptr, unused});
    allocator.apply(add, 0);
    allocator.undo();
    rejects([&] { allocator.apply(add, allocator.revision()); },
            "Retired definition ID cannot be reused");
    allocator.redo();
    const auto original = allocator.definitions().at(1);
    Document canonical;
    canonical.restore(canonical.identity(), original->nextMemberId, original->members);
    canonical.addGuide(1, guidePoint({2, 0, 0}));
    auto extended = std::make_shared<ComponentDefinition>(*original);
    extended->members = canonical.bodies();
    Edit extend{"Extend definition", {}};
    extend.definitions.push_back({1, original, extended});
    allocator.apply(extend, allocator.revision());
    const auto floor = allocator.definitions().at(1)->members.at(1)->surface.nextId;
    allocator.undo();
    check(allocator.definitions().at(1)->members.at(1)->surface.nextId == floor,
          "Definition geometry allocator floor survives undo");
    extend.definitions[0].before = allocator.definitions().at(1);
    rejects([&] { allocator.apply(extend, allocator.revision()); },
            "A fresh edit cannot reuse an undone definition geometry identity");
    allocator.redo();
    check(allocator.definitions().at(1)->members.at(1)->guides.size() == 2,
          "Redo may restore historical definition geometry identities");
}
void graphAndLimits() {
    ComponentDefinitions definitions{{1, fixture()}, {2, reference(2, 1, 2)}};
    const auto sizes = validateComponentDefinitions(definitions, 3);
    check(sizes.at(2).records == 5 && sizes.at(2).vertices == 8 && sizes.at(2).depth == 3,
          "Nested expansion counts substitute referenced definitions at leaf nodes");
    std::map<Id, BodyPtr> scene;
    ComponentInstances instances;
    Id next = 1;
    const auto outer = project(definitions, 2, next, scene, instances);
    for (const auto &[member, target] : definitions.at(2)->references) {
        const auto root = instances.at(outer)->members.at(member);
        project(definitions, target, next, scene, instances, *scene.at(root), root);
    }
    validateComponentInstances(definitions, instances, scene);
    check(instances.size() == 3 && scene.size() == 5,
          "Nested reference bindings resolve canonical nodes without duplicate scene identities");
    auto missing = instances;
    missing.erase(instances.at(outer)->members.at(2));
    rejects([&] { validateComponentInstances(definitions, missing, scene); },
            "Missing nested bindings reject");
    auto cyclic = reference(1, 2);
    auto graph = definitions;
    graph[1] = cyclic;
    rejects([&] { validateComponentDefinitions(graph, 3); }, "Reference cycles reject");
    graph[1] = reference(1, 999);
    rejects([&] { validateComponentDefinitions(graph, 3); }, "Dangling definitions reject");
    auto malformed = std::make_shared<ComponentDefinition>(*definitions.at(1));
    auto root = std::make_shared<Body>(*malformed->members.at(malformed->root));
    root->transform = Transform::translation({1, 0, 0});
    malformed->members[root->id] = root;
    graph = definitions;
    graph[1] = malformed;
    rejects([&] { validateComponentDefinitions(graph, 3); }, "Definition roots cannot be placed");
    graph = {{1, fixture()}};
    for (Id i = 2; i <= 20; ++i)
        graph[i] = reference(i, i - 1, 2);
    rejects([&] { validateComponentDefinitions(graph, 21); },
            "Exponential reference expansion rejects before allocating resolved records");
    graph = {{1, fixture()}};
    for (Id i = 2; i <= 129; ++i)
        graph[i] = reference(i, i - 1);
    rejects([&] { validateComponentDefinitions(graph, 130); },
            "Expanded hierarchy depth remains bounded");
}
} // namespace
int main() {
    try {
        recordsAndProjection();
        graphAndLimits();
        documentTransactions();
        std::cout << "Component definition records, projections, graph cycles and expansion "
                     "budgets passed\n";
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
