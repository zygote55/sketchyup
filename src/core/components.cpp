#include "core/components.hpp"
#include "core/document_limits.hpp"
#include <algorithm>
#include <set>
namespace sketchy {
namespace {
Id allocate(Id &next) {
    if (!next || next == UINT64_MAX)
        throw std::runtime_error("Component identity space exhausted");
    return next++;
}
void emptyGeometry(Body &body) {
    body.textSource.reset();
    body.referenceImage.reset();
    body.surface.vertices.clear();
    body.surface.faces.clear();
    body.surface.wires.clear();
    body.topology.edges.clear();
    body.edgeAppearances.clear();
    body.faceColors.clear();
    body.faceMaterials.clear();
    body.faceTextureMappings.clear();
    body.curves.clear();
    body.guides.clear();
}
void placementState(Body &body, const Body &placement) {
    body.id = placement.id;
    body.parent = placement.parent;
    body.transform = placement.transform;
    body.name = placement.name;
    body.properties = placement.properties;
    body.hidden = placement.hidden;
    body.locked = placement.locked;
    body.tag = placement.tag;
}
void bodyDifference(Edit &edit, const Document &doc, const std::map<Id, BodyPtr> &scene) {
    std::set<Id> ids;
    for (const auto &[id, body] : doc.bodies())
        ids.insert(id);
    for (const auto &[id, body] : scene)
        ids.insert(id);
    for (auto id : ids) {
        const auto before = doc.bodies().contains(id) ? doc.bodies().at(id) : nullptr;
        const auto after = scene.contains(id) ? scene.at(id) : nullptr;
        if (before != after && (!before || !after || *before != *after))
            edit.changes.push_back({id, before, after, {}, {}, {}, true});
    }
}
template <class Records, class Changes>
void recordDifference(const Records &before, const Records &after, Changes &changes) {
    std::set<Id> ids;
    for (const auto &[id, record] : before)
        ids.insert(id);
    for (const auto &[id, record] : after)
        ids.insert(id);
    for (auto id : ids) {
        const auto a = before.contains(id) ? before.at(id) : nullptr;
        const auto b = after.contains(id) ? after.at(id) : nullptr;
        if (a != b)
            changes.push_back({id, a, b});
    }
}
Id normalizeRoot(Document &draft, Id root) {
    const auto old = draft.bodies().at(root);
    auto frame = std::make_shared<Body>(*old);
    frame->kind = BodyKind::Group;
    Edit edit{"Normalize component frame", {}};
    Id geometry = 0;
    if (!old->surface.vertices.empty() || !old->guides.empty() || old->referenceImage) {
        geometry = draft.nextId();
        auto member = std::make_shared<Body>(*old);
        member->id = geometry;
        member->parent = root;
        member->transform = {};
        member->kind = old->referenceImage ? BodyKind::ReferenceImage : BodyKind::Geometry;
        member->hidden = member->locked = false;
        member->tag = 0;
        member->name = "Geometry";
        member->properties.clear();
        edit.changes.push_back({geometry, nullptr, member});
        emptyGeometry(*frame);
    }
    if (*frame != *old)
        edit.changes.push_back({root, old, frame});
    if (!edit.changes.empty())
        draft.apply(std::move(edit), draft.revision());
    return geometry;
}
std::pair<DefinitionPtr, InstancePtr> capture(const Document &draft, Id root, Id definitionId,
                                              std::string name, bool includeDraftRoots = false,
                                              Transform rootDelta = {}, Transform fromWorld = {},
                                              std::optional<ComponentGlue> glue = {}) {
    auto definition = std::make_shared<ComponentDefinition>();
    definition->id = definitionId;
    definition->root = root;
    definition->name = std::move(name);
    definition->glue = std::move(glue);
    definition->nextMemberId = draft.nextId();
    auto instance = std::make_shared<ComponentInstance>();
    instance->definition = definitionId;
    std::map<Id, std::vector<Id>> children;
    for (const auto &[id, body] : draft.bodies())
        children[body->parent].push_back(id);
    if (includeDraftRoots)
        for (auto id : children[0])
            if (id != root)
                children[root].push_back(id);
    auto visit = [&](auto &&self, Id id) -> void {
        auto body = std::make_shared<Body>(*draft.bodies().at(id));
        if (includeDraftRoots && id != root && !body->parent) {
            body->parent = root;
            body->transform = fromWorld * body->transform;
        } else if (body->parent == root)
            body->transform = rootDelta * body->transform;
        instance->members[id] = id;
        if (id == root) {
            body->kind = BodyKind::Group;
            body->parent = 0;
            body->transform = {};
            body->hidden = body->locked = false;
            body->tag = 0;
        } else if (draft.instances().contains(id)) {
            emptyGeometry(*body);
            definition->references[id] = draft.instances().at(id)->definition;
            definition->members[id] = body;
            return;
        }
        definition->members[id] = body;
        for (auto child : children[id])
            self(self, child);
    };
    visit(visit, root);
    return {definition, instance};
}
struct Projection {
    const Document &before;
    const ComponentDefinitions &definitions;
    const ComponentInstances &bindings;
    const std::map<Id, BodyPtr> &seed;
    std::map<Id, BodyPtr> scene;
    ComponentInstances instances;
    Id next;
    void resolve(Id root, Id definitionId, const Body &placement) {
        const auto &definition = *definitions.at(definitionId);
        const auto reuse = bindings.contains(root) && bindings.at(root)->definition == definitionId
                               ? bindings.at(root)
                               : nullptr;
        // A leaf has no transitive definition dependencies. Reuse its validated,
        // immutable projection only when its definition, binding, placement and
        // every seeded member are unchanged. Nested assemblies still resolve.
        if (definition.references.empty() && reuse && before.instances().contains(root) &&
            before.instances().at(root) == reuse && before.definitions().contains(definitionId) &&
            before.definitions().at(definitionId) == definitions.at(definitionId) &&
            before.bodies().contains(root) && before.bodies().at(root).get() == &placement &&
            std::all_of(reuse->members.begin(), reuse->members.end(), [&](const auto &member) {
                const auto id = member.second;
                return seed.contains(id) && before.bodies().contains(id) &&
                       seed.at(id) == before.bodies().at(id);
            })) {
            instances[root] = reuse;
            for (const auto &[member, id] : reuse->members)
                scene[id] = before.bodies().at(id);
            return;
        }
        auto binding = std::make_shared<ComponentInstance>();
        binding->definition = definitionId;
        binding->members[definition.root] = root;
        for (const auto &[member, body] : definition.members) {
            if (member == definition.root)
                continue;
            binding->members[member] = reuse && reuse->members.contains(member)
                                           ? reuse->members.at(member)
                                           : allocate(next);
        }
        instances[root] = binding;
        for (const auto &[member, prototype] : definition.members) {
            auto body = std::make_shared<Body>(*prototype);
            const auto id = binding->members.at(member);
            body->id = id;
            if (member == definition.root)
                placementState(*body, placement);
            else
                body->parent = binding->members.at(prototype->parent);
            if (definition.references.contains(member)) {
                resolve(id, definition.references.at(member), *body);
                continue;
            }
            if (before.bodies().contains(id)) {
                const auto &old = *before.bodies().at(id);
                body->surface.nextId = std::max(body->surface.nextId, old.surface.nextId);
                body->topology.nextId = std::max(body->topology.nextId, old.topology.nextId);
            }
            scene[id] = body;
        }
    }
};
ChangeReport publish(Document &doc, const ComponentDefinitions &definitions,
                     const ComponentInstances &bindings, const std::map<Id, BodyPtr> &seed, Id next,
                     Id nextDefinition, std::string label, Id editedDefinition = 0,
                     const ChangeReport &lineage = {}) {
    const auto sizes = validateComponentDefinitions(
        definitions, nextDefinition, doc.tags(), doc.nextTagId(), doc.materials(),
        doc.nextMaterialId(), doc.assets(), doc.nextAssetId());
    std::set<Id> owned, nestedRoots;
    for (const auto &[root, binding] : doc.instances())
        for (const auto &[member, id] : binding->members) {
            owned.insert(id);
            if (id != root)
                nestedRoots.insert(id);
        }
    for (const auto &[root, binding] : bindings)
        for (const auto &[member, id] : binding->members) {
            owned.insert(id);
            if (id != root)
                nestedRoots.insert(id);
        }
    Projection projection{doc, definitions, bindings, seed, seed, {}, next};
    for (auto id : owned)
        projection.scene.erase(id);
    ComponentSize total;
    auto count = [&](ComponentSize size) {
        total.records += size.records;
        total.vertices += size.vertices;
        total.faces += size.faces;
        total.wires += size.wires;
        total.edges += size.edges;
        total.curves += size.curves;
        total.guides += size.guides;
        if (total.records > DocumentLimits::bodies || total.vertices > DocumentLimits::vertices ||
            total.faces > DocumentLimits::faces || total.wires > DocumentLimits::wires ||
            total.edges > DocumentLimits::edges || total.curves > DocumentLimits::curves ||
            total.guides > DocumentLimits::guides)
            throw std::runtime_error("Component placement exceeds document editing limits");
    };
    for (const auto &[id, body] : projection.scene)
        count({1, body->surface.vertices.size(), body->surface.faces.size(),
               body->surface.wires.size(), body->topology.edges.size(), body->curves.size(),
               body->guides.size(), 1});
    for (const auto &[root, binding] : bindings)
        if (!nestedRoots.contains(root))
            count(sizes.at(binding->definition));
    for (const auto &[root, binding] : bindings)
        if (!nestedRoots.contains(root))
            projection.resolve(root, binding->definition, *seed.at(root));
    for (const auto &[root, binding] : bindings)
        if ((!doc.instances().contains(root) ||
             doc.instances().at(root)->definition != binding->definition) &&
            (!projection.instances.contains(root) ||
             projection.instances.at(root)->definition != binding->definition))
            throw std::runtime_error(
                "Nested component changes require their definition's edit scope");
    Edit edit{std::move(label), {}};
    edit.nextIdFloor = projection.next;
    edit.nextDefinitionFloor = nextDefinition;
    bodyDifference(edit, doc, projection.scene);
    if (editedDefinition) {
        std::map<Id, size_t> changes;
        for (size_t i = 0; i < edit.changes.size(); ++i)
            changes[edit.changes[i].id] = i;
        for (const auto &[root, instance] : projection.instances) {
            if (instance->definition != editedDefinition || !doc.instances().contains(root) ||
                doc.instances().at(root)->definition != editedDefinition)
                continue;
            const auto old = doc.instances().at(root);
            for (auto [member, body] : instance->members) {
                if (!old->members.contains(member) || old->members.at(member) != body ||
                    !lineage.contains(member) || !changes.contains(body))
                    continue;
                auto &change = edit.changes[changes.at(body)];
                const auto &report = lineage.at(member);
                auto copy = [](const auto &source, auto &target, const auto &before,
                               const auto &after) {
                    for (const auto &[id, descendants] : source) {
                        if (!before.contains(id))
                            continue;
                        auto &mapped = target[id];
                        for (auto next : descendants)
                            if (after.contains(next))
                                mapped.push_back(next);
                    }
                };
                copy(report.faces.descendants, change.faceDescendants, change.before->surface.faces,
                     change.after->surface.faces);
                copy(report.vertices.descendants, change.vertexDescendants,
                     change.before->surface.vertices, change.after->surface.vertices);
                copy(report.edges.descendants, change.edgeDescendants,
                     change.before->topology.edges, change.after->topology.edges);
            }
        }
    }
    recordDifference(doc.definitions(), definitions, edit.definitions);
    // Preserve pointer identity for unchanged bindings, avoiding spurious locked
    // instance edits and unnecessary history entries during another definition edit.
    for (auto &[root, binding] : projection.instances)
        if (doc.instances().contains(root) && *doc.instances().at(root) == *binding)
            binding = doc.instances().at(root);
    recordDifference(doc.instances(), projection.instances, edit.instances);
    if (edit.changes.empty() && edit.definitions.empty() && edit.instances.empty())
        return {};
    return doc.apply(std::move(edit), doc.revision());
}
} // namespace
ComponentResult createComponent(Document &doc, Id root, std::string name) {
    if (doc.instances().contains(root))
        throw std::runtime_error("This group is already a component instance");
    Document draft = doc;
    const auto geometry = normalizeRoot(draft, root);
    if (draft.bodies().at(root)->name != name) {
        const auto old = draft.bodies().at(root);
        auto named = std::make_shared<Body>(*old);
        named->name = name;
        draft.apply({"Name component placement", {{root, old, named}}}, draft.revision());
    }
    Id nextDefinition = doc.nextDefinitionId();
    const auto id = allocate(nextDefinition);
    const auto [definition, binding] = capture(draft, root, id, std::move(name));
    auto definitions = doc.definitions();
    auto instances = doc.instances();
    definitions[id] = definition;
    instances[root] = binding;
    ComponentResult result{id, root, {}, {}};
    result.changes = publish(doc, definitions, instances, draft.bodies(), draft.nextId(),
                             nextDefinition, "Make component");
    if (geometry)
        result.movedGeometry[root] = geometry;
    return result;
}
ComponentResult placeComponent(Document &doc, Id definition, Transform local, Id parent,
                               std::string name) {
    local.validate();
    if (parent && doc.bodies().at(parent)->kind != BodyKind::Group)
        throw std::runtime_error("Component placement parent must be a group or the model");
    const auto prototype = doc.definitions().at(definition);
    Id next = doc.nextId();
    const auto root = allocate(next);
    auto placement = std::make_shared<Body>();
    placement->id = root;
    placement->kind = BodyKind::Group;
    placement->parent = parent;
    placement->transform = local;
    placement->name = name.empty() ? prototype->name : std::move(name);
    auto binding = std::make_shared<ComponentInstance>();
    binding->definition = definition;
    binding->members[prototype->root] = root;
    const auto sizes = validateComponentDefinitions(
        doc.definitions(), doc.nextDefinitionId(), doc.tags(), doc.nextTagId(), doc.materials(),
        doc.nextMaterialId(), doc.assets(), doc.nextAssetId());
    auto total = sizes.at(definition);
    for (const auto &[id, body] : doc.bodies()) {
        total.records += 1;
        total.vertices += body->surface.vertices.size();
        total.faces += body->surface.faces.size();
        total.wires += body->surface.wires.size();
        total.edges += body->topology.edges.size();
        total.curves += body->curves.size();
        total.guides += body->guides.size();
    }
    if (total.records > DocumentLimits::bodies || total.vertices > DocumentLimits::vertices ||
        total.faces > DocumentLimits::faces || total.wires > DocumentLimits::wires ||
        total.edges > DocumentLimits::edges || total.curves > DocumentLimits::curves ||
        total.guides > DocumentLimits::guides)
        throw std::runtime_error("Component placement exceeds document editing limits");
    // A new placement has fresh IDs and cannot alter an existing definition or
    // binding. Resolve only its expansion; Document::apply still validates the
    // complete candidate, including ownership, locks and editing scopes.
    const std::map<Id, BodyPtr> seed{{root, placement}};
    const ComponentInstances newBindings{{root, binding}};
    Projection projection{doc, doc.definitions(), newBindings, seed, seed, {}, next};
    projection.resolve(root, definition, *placement);
    Edit edit{"Place component", {}};
    edit.nextIdFloor = projection.next;
    edit.nextDefinitionFloor = doc.nextDefinitionId();
    for (const auto &[id, body] : projection.scene)
        edit.changes.push_back({id, nullptr, body, {}, {}, {}, true});
    for (const auto &[id, instance] : projection.instances)
        edit.instances.push_back({id, nullptr, instance});
    return {definition, root, doc.apply(std::move(edit), doc.revision()), {}};
}
ComponentResult replaceComponent(Document &doc, Id root, Id definition) {
    const auto old = doc.instances().at(root);
    if (old->definition == definition)
        return {definition, root, {}, {}};
    auto binding = std::make_shared<ComponentInstance>();
    binding->definition = definition;
    binding->members[doc.definitions().at(definition)->root] = root;
    auto instances = doc.instances();
    instances[root] = binding;
    auto definitions = doc.definitions();
    std::map<Id, std::pair<Id, Id>> owners;
    for (const auto &[instance, record] : doc.instances())
        for (auto [member, target] : record->members)
            if (target != instance)
                owners[target] = {instance, member};
    Id nextDefinition = doc.nextDefinitionId(), current = root, replacement = definition;
    while (owners.contains(current)) {
        const auto [parent, member] = owners.at(current);
        const auto previous = instances.at(parent);
        auto unique = std::make_shared<ComponentDefinition>(*definitions.at(previous->definition));
        unique->id = allocate(nextDefinition);
        unique->references.at(member) = replacement;
        definitions[unique->id] = unique;
        auto rebound = std::make_shared<ComponentInstance>(*previous);
        rebound->definition = unique->id;
        instances[parent] = rebound;
        current = parent;
        replacement = unique->id;
    }
    return {definition,
            root,
            publish(doc, definitions, instances, doc.bodies(), doc.nextId(), nextDefinition,
                    "Replace component"),
            {}};
}
ComponentResult makeComponentUnique(Document &doc, Id root) {
    // Ancestor ownership must also become unique when a nested placement changes
    // its definition reference; otherwise that reference belongs to shared data.
    std::map<Id, std::pair<Id, Id>> owners;
    for (const auto &[instance, binding] : doc.instances())
        for (auto [member, target] : binding->members)
            if (target != instance)
                owners[target] = {instance, member};
    auto definitions = doc.definitions();
    auto instances = doc.instances();
    Id nextDefinition = doc.nextDefinitionId(), selectedDefinition = 0;
    Id current = root, replaced = 0, replacement = 0;
    while (true) {
        const auto old = instances.at(current);
        auto definition = std::make_shared<ComponentDefinition>(*definitions.at(old->definition));
        definition->id = allocate(nextDefinition);
        if (definition->name.size() <= 1017)
            definition->name += " unique";
        if (replaced)
            definition->references.at(replaced) = replacement;
        definitions[definition->id] = definition;
        auto binding = std::make_shared<ComponentInstance>(*old);
        binding->definition = definition->id;
        instances[current] = binding;
        if (current == root)
            selectedDefinition = definition->id;
        if (!owners.contains(current))
            break;
        const auto [parent, member] = owners.at(current);
        current = parent;
        replaced = member;
        replacement = definition->id;
    }
    return {selectedDefinition,
            root,
            publish(doc, definitions, instances, doc.bodies(), doc.nextId(), nextDefinition,
                    "Make component unique"),
            {}};
}
ComponentResult editComponentDefinition(Document &doc, Id id,
                                        const std::function<ChangeReport(Document &)> &edit,
                                        Transform editingFrame) {
    editingFrame.validate();
    const auto original = doc.definitions().at(id);
    auto members = original->members;
    auto placedRoot = std::make_shared<Body>(*members.at(original->root));
    placedRoot->transform = editingFrame;
    members[original->root] = placedRoot;
    Document draft;
    draft.restore(draft.identity(), original->nextMemberId, members, 0, doc.definitions(), {},
                  doc.nextDefinitionId(), doc.tags(), doc.nextTagId(), doc.materials(),
                  doc.nextMaterialId(), doc.assets(), doc.nextAssetId(), doc.displayUnits(),
                  std::make_shared<const HostedComponents>(), doc.style(), doc.scenes(),
                  doc.nextSceneId(), doc.sections(), doc.nextSectionId(), {}, {}, doc.nextAnnotationId(), doc.solar());
    ComponentInstances references;
    for (auto [member, definition] : original->references) {
        auto binding = std::make_shared<ComponentInstance>();
        binding->definition = definition;
        binding->members[doc.definitions().at(definition)->root] = member;
        references[member] = binding;
    }
    if (!references.empty())
        publish(draft, draft.definitions(), references, draft.bodies(), draft.nextId(),
                draft.nextDefinitionId(), "Resolve definition references");
    std::map<Id, std::map<Id, Id>> existingMembers;
    for (const auto &[root, instance] : doc.instances())
        if (instance->definition == id)
            existingMembers[root] = componentScopeMembers(doc, draft, root);
    const auto baselineDefinitions = draft.definitions();
    const auto baselineTags = draft.tags();
    const auto baselineMaterials = draft.materials();
    const auto baselineAssets = draft.assets();
    const auto baselineScenes = draft.scenes();
    const auto baselineSections = draft.sections();
    const auto baseline = draft.saveStamp();
    const auto report = edit(draft);
    if (draft.isCurrentSnapshot(baseline))
        return {id, 0, {}, {}};
    if (!draft.bodies().contains(original->root))
        throw std::runtime_error("A definition edit cannot remove its root frame");
    const auto root = draft.bodies().at(original->root);
    const auto previousRoot = original->members.at(original->root);
    if (root->parent || root->hidden || root->locked || root->kind != BodyKind::Group ||
        root->name != previousRoot->name || root->properties != previousRoot->properties ||
        root->tag != previousRoot->tag)
        throw std::runtime_error("Instance root state is outside shared geometry edit scope");
    if (draft.displayUnits() != doc.displayUnits())
        throw std::runtime_error("Edit document units outside a shared geometry scope");
    if (draft.solar() != doc.solar())
        throw std::runtime_error("Edit sun study outside a shared geometry scope");
    if (draft.style() != doc.style())
        throw std::runtime_error("Edit model style outside a shared geometry scope");
    if (draft.sections() != baselineSections || draft.nextSectionId() != doc.nextSectionId() ||
        !draft.activeSections().empty())
        throw std::runtime_error("Shared component edits cannot modify document section planes");
    if (!draft.annotations().empty() || draft.nextAnnotationId() != doc.nextAnnotationId())
        throw std::runtime_error("Shared component edits cannot modify document annotations");
    if (draft.scenes() != baselineScenes || draft.nextSceneId() != doc.nextSceneId())
        throw std::runtime_error("Shared component edits cannot modify document scenes");
    if (draft.assets() != baselineAssets)
        throw std::runtime_error("Edit document assets outside a shared geometry scope");
    if (draft.materials() != baselineMaterials)
        throw std::runtime_error("Edit document materials outside a shared geometry scope");
    if (draft.tags() != baselineTags)
        throw std::runtime_error("Edit document tags outside a shared geometry scope");
    for (const auto &[other, definition] : baselineDefinitions)
        if (other != id &&
            (!draft.definitions().contains(other) || draft.definitions().at(other) != definition))
            throw std::runtime_error("Edit another shared definition with its own explicit scope");
    for (const auto &[member, body] : draft.bodies())
        if (member != original->root && !body->parent && original->members.contains(member))
            throw std::runtime_error("A shared edit cannot move a member out of its definition");
    const auto moved = normalizeRoot(draft, original->root);
    // Keep the temporary editing placement out of canonical records. Real root
    // movement is baked into its children; newly created world-root records are
    // re-expressed in the definition frame. Avoid numerical drift for no movement.
    const auto inverse = editingFrame.inverse();
    const auto delta = root->transform == editingFrame ? Transform{} : inverse * root->transform;
    const auto [definition, unused] =
        capture(draft, original->root, id, original->name, true, delta, inverse, original->glue);
    auto definitions = doc.definitions();
    for (const auto &[created, record] : draft.definitions())
        if (!definitions.contains(created))
            definitions[created] = record;
    definitions[id] = definition;
    auto bindings = doc.instances();
    for (const auto &[root, members] : existingMembers) {
        auto binding = std::make_shared<ComponentInstance>(*bindings.at(root));
        for (const auto &[member, body] : definition->members)
            if (!binding->members.contains(member) && members.contains(member))
                binding->members[member] = members.at(member);
        bindings[root] = binding;
    }
    ComponentResult result{id, 0, {}, {}};
    result.changes =
        publish(doc, definitions, bindings, doc.bodies(), doc.nextId(), draft.nextDefinitionId(),
                "Edit shared component definition", id, report);
    if (moved)
        result.movedGeometry[original->root] = moved;
    return result;
}
std::map<Id, Id> componentScopeMembers(const Document &doc, const Document &draft, Id instance) {
    auto members = doc.instances().at(instance)->members;
    auto nested = [&](auto &&self, Id sceneRoot, Id draftRoot) -> void {
        const auto scene = doc.instances().at(sceneRoot),
                   canonical = draft.instances().at(draftRoot);
        for (auto [member, id] : scene->members) {
            const auto target = canonical->members.at(member);
            members[target] = id;
            if (id != sceneRoot && doc.instances().contains(id))
                self(self, id, target);
        }
    };
    const auto definition = doc.definitions().at(doc.instances().at(instance)->definition);
    for (auto [member, target] : definition->references)
        nested(nested, doc.instances().at(instance)->members.at(member), member);
    return members;
}
ComponentResult setComponentAxes(Document &doc, Id id, Transform axes) {
    axes.validate();
    const auto original = doc.definitions().at(id);
    if (axes == Transform{})
        return {id, 0, {}, {}};
    const auto inverse = axes.inverse();
    auto definitions = doc.definitions();
    auto edited = std::make_shared<ComponentDefinition>(*original);
    for (auto &[member, body] : edited->members)
        if (body->parent == edited->root) {
            auto moved = std::make_shared<Body>(*body);
            moved->transform = inverse * body->transform;
            body = moved;
        }
    definitions[id] = edited;
    for (auto &[other, definition] : definitions) {
        std::shared_ptr<ComponentDefinition> adjusted;
        for (auto [member, target] : definition->references)
            if (target == id) {
                if (!adjusted)
                    adjusted = std::make_shared<ComponentDefinition>(*definition);
                auto reference = std::make_shared<Body>(*adjusted->members.at(member));
                reference->transform = reference->transform * axes;
                adjusted->members[member] = reference;
            }
        if (adjusted)
            definition = adjusted;
    }
    std::set<Id> nested;
    for (const auto &[root, instance] : doc.instances())
        for (auto [member, body] : instance->members)
            if (body != root)
                nested.insert(body);
    auto scene = doc.bodies();
    for (const auto &[root, instance] : doc.instances())
        if (instance->definition == id && !nested.contains(root)) {
            auto placement = std::make_shared<Body>(*scene.at(root));
            placement->transform = placement->transform * axes;
            scene[root] = placement;
        }
    return {id,
            0,
            publish(doc, definitions, doc.instances(), scene, doc.nextId(), doc.nextDefinitionId(),
                    "Change component local axes"),
            {}};
}
ComponentResult setComponentGlue(Document &doc, Id id, std::optional<ComponentGlue> glue) {
    const auto original = doc.definitions().at(id);
    if (original->glue == glue)
        return {id, 0, {}, {}};
    auto edited = std::make_shared<ComponentDefinition>(*original);
    edited->glue = std::move(glue);
    Edit edit{edited->glue ? "Set component glue face" : "Clear component glue face", {}};
    edit.definitions.push_back({id, original, edited});
    return {id, 0, doc.apply(std::move(edit), doc.revision()), {}};
}
} // namespace sketchy
