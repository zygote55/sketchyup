#include "core/components.hpp"
#include "core/component_parameters.hpp"
#include "core/component_publish.hpp"
#include "core/component_regeneration.hpp"
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
std::pair<std::shared_ptr<ComponentDefinition>, InstancePtr>
capture(const Document &draft, Id root, Id definitionId,
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
            // Nested placements hold no overrides (ADR 0165 §1); dropping them would
            // silently change the captured geometry.
            if (!draft.instances().at(id)->overrides.empty())
                throw ComponentParameterError(
                    "INVALID_TARGET",
                    "Placement #" + std::to_string(id) +
                        " has parameter overrides; reset them before nesting it in a component",
                    {.definition = draft.instances().at(id)->definition, .placement = id});
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
    // Persisted scene allocator floors above the canonical member floor (native load).
    const std::map<Id, std::pair<Id, Id>> *floors;
    // Projects one placement of `definitionId` through `variant` (null: the template).
    // A top-level placement keeps `overrides` in its new binding.
    void resolve(Id root, Id definitionId, const Body &placement, const VariantPtr &variant = {},
                 const ComponentOverrides &overrides = {}) {
        const auto &definition = *definitions.at(definitionId);
        const auto &members = variant ? variant->members : definition.members;
        const auto &references = variant ? variant->references : definition.references;
        const auto reuse = bindings.contains(root) && bindings.at(root)->definition == definitionId
                               ? bindings.at(root)
                               : nullptr;
        const auto &previous = before.componentVariants().placements;
        const auto previousVariant = previous.find(root);
        const bool sameVariant =
            previousVariant == previous.end() ? !variant : previousVariant->second == variant;
        // A leaf has no transitive definition dependencies. Reuse its validated,
        // immutable projection only when its definition, binding, variant, placement
        // and every seeded member are unchanged. Nested assemblies still resolve.
        if (references.empty() && reuse && sameVariant && reuse->overrides == overrides &&
            before.instances().contains(root) && before.instances().at(root) == reuse &&
            before.definitions().contains(definitionId) &&
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
        binding->overrides = overrides;
        binding->members[definition.root] = root;
        for (const auto &[member, body] : members) {
            if (member == definition.root)
                continue;
            binding->members[member] = reuse && reuse->members.contains(member)
                                           ? reuse->members.at(member)
                                           : allocate(next);
        }
        instances[root] = binding;
        for (const auto &[member, prototype] : members) {
            auto body = std::make_shared<Body>(*prototype);
            const auto id = binding->members.at(member);
            body->id = id;
            if (member == definition.root)
                placementState(*body, placement);
            else
                body->parent = binding->members.at(prototype->parent);
            if (const auto reference = references.find(member); reference != references.end()) {
                VariantPtr nested;
                if (variant)
                    if (const auto found = variant->nested.find(member);
                        found != variant->nested.end())
                        nested = found->second;
                resolve(id, reference->second, *body, nested);
                continue;
            }
            if (before.bodies().contains(id)) {
                const auto &old = *before.bodies().at(id);
                body->surface.nextId = std::max(body->surface.nextId, old.surface.nextId);
                body->topology.nextId = std::max(body->topology.nextId, old.topology.nextId);
            }
            if (floors && floors->contains(id)) {
                const auto [surface, edges] = floors->at(id);
                if (surface < body->surface.nextId || edges < body->topology.nextId ||
                    (surface == body->surface.nextId && edges == body->topology.nextId))
                    throw std::runtime_error(
                        "Placement allocator floor must exceed its definition floor");
                body->surface.nextId = surface;
                body->topology.nextId = edges;
            }
            scene[id] = body;
        }
    }
};
// Parameter operations name the locked placement (ADR 0165 §4).
void checkParameterLocks(const Document &doc, const Edit &edit) {
    auto locked = [&](Id id) -> Id {
        Id placement = 0;
        for (size_t depth = 0; id && doc.bodies().contains(id) && depth <= 128; ++depth) {
            if (!placement && doc.instances().contains(id))
                placement = id;
            if (doc.bodies().at(id)->locked)
                return placement ? placement : id;
            id = doc.bodies().at(id)->parent;
        }
        return 0;
    };
    auto refuse = [&](Id id) {
        if (const auto placement = locked(id))
            throw ComponentParameterError(
                "LOCKED_ENTITY",
                "Placement #" + std::to_string(placement) +
                    " is locked; unlock it before changing its parameters",
                {.placement = placement});
    };
    for (const auto &change : edit.changes)
        refuse(change.before ? change.id : change.after->parent);
    for (const auto &change : edit.instances)
        refuse(change.root);
}
ChangeReport publish(Document &doc, const ComponentDefinitions &definitions,
                     const ComponentInstances &bindings, const std::map<Id, BodyPtr> &seed, Id next,
                     Id nextDefinition, Id nextDriver, std::string label,
                     const ComponentPublishOptions &options = {}) {
    const auto editedDefinition = options.editedDefinition;
    const auto &lineage = options.lineage;
    const auto sizes = validateComponentDefinitions(
        definitions, nextDefinition, doc.tags(), doc.nextTagId(), doc.materials(),
        doc.nextMaterialId(), doc.assets(), doc.nextAssetId());
    nextDriver = std::max(nextDriver, doc.nextComponentDriverId());
    validateComponentParameterRecords(definitions, &doc.definitions(), nextDriver);
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
    // Each top-level placement projects its variant, computed before any record is
    // built (ADR 0165 §3: evaluation and regeneration fail closed).
    ComponentVariantEngine engine(definitions, &doc.componentVariants());
    std::map<Id, VariantPtr> variants;
    for (const auto &[root, binding] : bindings) {
        if (nestedRoots.contains(root) ||
            (binding->overrides.empty() && !engine.parametric(binding->definition)))
            continue;
        try {
            engine.plan(binding->definition, binding->overrides);
        } catch (const ComponentParameterError &error) {
            throw error.withPlacement(root);
        }
    }
    engine.checkPlanned();
    for (const auto &[root, binding] : bindings) {
        if (nestedRoots.contains(root) ||
            (binding->overrides.empty() && !engine.parametric(binding->definition)))
            continue;
        try {
            variants[root] = engine.variant(binding->definition, binding->overrides);
        } catch (const ComponentParameterError &error) {
            throw error.withPlacement(root);
        }
    }
    auto variantOf = [&](Id root) {
        const auto found = variants.find(root);
        return found == variants.end() ? VariantPtr{} : found->second;
    };
    Projection projection{doc, definitions, bindings, seed, seed, {}, next, nullptr};
    for (auto id : owned)
        projection.scene.erase(id);
    // Materialized totals, each running sum bounded before the next addition.
    ComponentSize total;
    auto count = [&](ComponentSize size, auto context) {
        total.records += size.records;
        total.vertices += size.vertices;
        total.faces += size.faces;
        total.wires += size.wires;
        total.edges += size.edges;
        total.curves += size.curves;
        total.guides += size.guides;
        total.placements += size.placements;
        if (!exceededLimit(limitCounts(total), LimitClass::Placement).empty())
            checkLimits(limitCounts(total), LimitClass::Placement, "Component placement",
                        context());
    };
    for (const auto &[id, body] : projection.scene)
        count({1, body->surface.vertices.size(), body->surface.faces.size(),
               body->surface.wires.size(), body->topology.edges.size(), body->curves.size(),
               body->guides.size(), 1, 0},
              [id] { return "body #" + std::to_string(id); });
    for (const auto &[root, binding] : bindings)
        if (!nestedRoots.contains(root))
            count(componentVariantSize(*definitions.at(binding->definition), variantOf(root),
                                       sizes),
                  [&, root] {
                const auto &definition = *definitions.at(binding->definition);
                return "placement #" + std::to_string(root) + " of " +
                       definitionLimitContext(definition.name, definition.id);
            });
    for (const auto &[root, binding] : bindings)
        if (!nestedRoots.contains(root))
            projection.resolve(root, binding->definition, *seed.at(root), variantOf(root),
                               binding->overrides);
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
    edit.nextComponentDriverFloor = nextDriver;
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
    if (options.parameterLocks)
        checkParameterLocks(doc, edit);
    return doc.apply(std::move(edit), doc.revision());
}
std::vector<Id> subtreeMembers(const ComponentDefinition &definition, Id member) {
    std::map<Id, std::vector<Id>> children;
    for (const auto &[id, body] : definition.members)
        if (id != definition.root)
            children[body->parent].push_back(id);
    std::vector<Id> result, stack{member};
    while (!stack.empty() && result.size() <= definition.members.size()) {
        const auto id = stack.back();
        stack.pop_back();
        result.push_back(id);
        if (const auto found = children.find(id); found != children.end())
            stack.insert(stack.end(), found->second.begin(), found->second.end());
    }
    std::sort(result.begin(), result.end());
    return result;
}
} // namespace
ChangeReport publishComponentRecords(Document &doc, const ComponentDefinitions &definitions,
                                     const ComponentInstances &bindings,
                                     const std::map<Id, BodyPtr> &seed, Id next,
                                     Id nextDefinition, Id nextDriver, std::string label,
                                     const ComponentPublishOptions &options) {
    return publish(doc, definitions, bindings, seed, next, nextDefinition, nextDriver,
                   std::move(label), options);
}
ComponentRegeneration componentRegeneration(const Document &doc,
                                            const ComponentDefinitions &beforeDefinitions,
                                            const ComponentVariantIndex &beforeVariants) {
    ComponentRegeneration result;
    const auto &after = doc.componentVariants();
    auto variantIn = [](const ComponentVariantIndex &index, Id root) {
        const auto found = index.placements.find(root);
        return found == index.placements.end() ? VariantPtr{} : found->second;
    };
    for (const auto &[root, instance] : doc.instances()) {
        const auto before = beforeDefinitions.find(instance->definition);
        if (before == beforeDefinitions.end() ||
            before->second != doc.definitions().at(instance->definition) ||
            variantIn(beforeVariants, root) != variantIn(after, root))
            ++result.placements;
    }
    for (const auto &[key, variant] : after.variants) {
        const auto found = beforeVariants.variants.find(key);
        if (found == beforeVariants.variants.end() || found->second != variant)
            ++result.variants;
    }
    return result;
}
ComponentParametersPtr renumberComponentDrivers(const ComponentParametersPtr &record,
                                                Id &nextDriver) {
    if (!record)
        return nullptr;
    auto copy = std::make_shared<ComponentParameters>(*record);
    for (auto &driver : copy->drivers)
        driver.id = allocate(nextDriver);
    return copy;
}
void reserveRepeatBlocks(ComponentDefinition &definition, ComponentParameters &record,
                         const ComponentDefinition *previousTemplate,
                         const ComponentParameters *previousRecord) {
    for (auto &driver : record.drivers) {
        if (driver.kind != ComponentDriverKind::Repeat)
            continue;
        const ComponentDriver *previous = nullptr;
        if (previousRecord)
            for (const auto &candidate : previousRecord->drivers)
                if (candidate.id == driver.id)
                    previous = &candidate;
        const auto members = definition.members.contains(driver.member)
                                 ? subtreeMembers(definition, driver.member)
                                 : std::vector<Id>{};
        if (previous && previous->kind == ComponentDriverKind::Repeat &&
            previous->member == driver.member && previous->maxCount == driver.maxCount &&
            previous->firstMember == driver.firstMember && previousTemplate &&
            previousTemplate->members.contains(driver.member) &&
            subtreeMembers(*previousTemplate, driver.member) == members)
            continue;
        // Copy k's members take the block in order: copy, then template member ID.
        const auto size = Id(driver.maxCount ? driver.maxCount - 1 : 0) * members.size();
        if (size > ComponentParameterLimits::reservedMembers)
            throw ComponentParameterError(
                "LIMIT_EXCEEDED",
                "Repeat copies would reserve more than 4,096 member IDs (definition #" +
                    std::to_string(definition.id) + ")",
                {.definition = definition.id, .driver = driver.id});
        if (definition.nextMemberId > UINT64_MAX - size - 1)
            throw std::runtime_error("Component member identity space exhausted");
        driver.firstMember = definition.nextMemberId;
        definition.nextMemberId += size;
    }
}
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
                             nextDefinition, doc.nextComponentDriverId(), "Make component");
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
    // A parametric definition places its variant at the defaults (the template plus
    // any repeat copies at the default counts).
    ComponentVariantEngine engine(doc.definitions(), &doc.componentVariants());
    const auto variant = engine.parametric(definition) ? engine.variant(definition, {}) : nullptr;
    // The document's running totals equal a sum over every materialized record.
    // A placement adds only placement-owned records, so unique geometry is unchanged.
    auto total = componentVariantSize(*prototype, variant, sizes);
    const auto &current = doc.materializedTotals();
    total.records += current.records;
    total.vertices += current.vertices;
    total.faces += current.faces;
    total.wires += current.wires;
    total.edges += current.edges;
    total.curves += current.curves;
    total.guides += current.guides;
    total.placements += doc.instances().size();
    if (!exceededLimit(limitCounts(total), LimitClass::Placement).empty())
        checkLimits(limitCounts(total), LimitClass::Placement, "Component placement",
                    "placing " + definitionLimitContext(prototype->name, definition) + " into " +
                        (parent ? "group #" + std::to_string(parent) : std::string("the model")));
    // A new placement has fresh IDs and cannot alter an existing definition or
    // binding. Resolve only its expansion; Document::apply still validates the
    // complete candidate, including ownership, locks and editing scopes.
    const std::map<Id, BodyPtr> seed{{root, placement}};
    const ComponentInstances newBindings{{root, binding}};
    Projection projection{doc, doc.definitions(), newBindings, seed, seed, {}, next, nullptr};
    projection.resolve(root, definition, *placement, variant);
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
    // The replacement starts at its own defaults: overrides are dropped and reported.
    std::vector<std::string> dropped;
    for (const auto &[name, value] : old->overrides)
        dropped.push_back(name);
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
    Id nextDriver = doc.nextComponentDriverId();
    while (owners.contains(current)) {
        const auto [parent, member] = owners.at(current);
        const auto previous = instances.at(parent);
        auto unique = std::make_shared<ComponentDefinition>(*definitions.at(previous->definition));
        unique->id = allocate(nextDefinition);
        unique->references.at(member) = replacement;
        unique->parameters = renumberComponentDrivers(unique->parameters, nextDriver);
        definitions[unique->id] = unique;
        auto rebound = std::make_shared<ComponentInstance>(*previous);
        rebound->definition = unique->id;
        instances[parent] = rebound;
        current = parent;
        replacement = unique->id;
    }
    ComponentResult result{definition, root, {}, {}};
    result.changes = publish(doc, definitions, instances, doc.bodies(), doc.nextId(),
                             nextDefinition, nextDriver, "Replace component");
    result.droppedOverrides = std::move(dropped);
    return result;
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
    Id nextDriver = doc.nextComponentDriverId();
    Id current = root, replaced = 0, replacement = 0;
    while (true) {
        const auto old = instances.at(current);
        auto definition = std::make_shared<ComponentDefinition>(*definitions.at(old->definition));
        definition->id = allocate(nextDefinition);
        // The copy keeps its parameters under new driver IDs; bindings keep overrides.
        definition->parameters = renumberComponentDrivers(definition->parameters, nextDriver);
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
    ComponentResult result{selectedDefinition, root, {}, {}};
    const auto firstDriver = doc.nextComponentDriverId();
    result.changes = publish(doc, definitions, instances, doc.bodies(), doc.nextId(),
                             nextDefinition, nextDriver, "Make component unique");
    for (Id id = firstDriver; id < nextDriver; ++id)
        result.createdDrivers.push_back(id);
    return result;
}
ComponentResult editComponentDefinition(Document &doc, Id id,
                                        const std::function<ChangeReport(Document &)> &edit,
                                        Transform editingFrame,
                                        const ComponentTemplateParameters &parameterStep) {
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
                  doc.nextSceneId(), doc.sections(), doc.nextSectionId(), {}, {}, doc.nextAnnotationId(), doc.solar(),
                  doc.displayPrecision(), {}, 1, {}, 1, {}, 1, doc.nextComponentDriverId());
    ComponentInstances references;
    for (auto [member, definition] : original->references) {
        auto binding = std::make_shared<ComponentInstance>();
        binding->definition = definition;
        binding->members[doc.definitions().at(definition)->root] = member;
        references[member] = binding;
    }
    if (!references.empty())
        publish(draft, draft.definitions(), references, draft.bodies(), draft.nextId(),
                draft.nextDefinitionId(), draft.nextComponentDriverId(),
                "Resolve definition references");
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
    if (draft.displayUnits() != doc.displayUnits() ||
        draft.displayPrecision() != doc.displayPrecision())
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
    // The draft starts with empty drawing-sheet tables; sheet edits are never part
    // of a shared component scope (ADR 0163).
    if (!draft.drawingSheets().empty() || !draft.drawingViews().empty() ||
        !draft.drawingItems().empty() || draft.nextDrawingSheetId() != 1 ||
        draft.nextDrawingViewId() != 1 || draft.nextDrawingItemId() != 1)
        throw std::runtime_error("Shared component edits cannot modify drawing sheets");
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
    // A parametric definition's members are its template (ADR 0165 §4). The record
    // carries over, or the caller's parameter step replaces it in the same edit.
    auto parameters = original->parameters;
    if (parameterStep) {
        parameters = parameterStep(*definition, parameters);
        if (parameters && parameters->parameters.empty() && parameters->drivers.empty())
            parameters = nullptr;
        std::set<Id> known;
        if (original->parameters)
            for (const auto &driver : original->parameters->drivers)
                known.insert(driver.id);
        if (parameters)
            for (const auto &driver : parameters->drivers)
                if (!known.contains(driver.id))
                    throw ComponentParameterError(
                        "INVALID_TARGET", "A template edit cannot create drivers",
                        {.definition = id, .driver = driver.id});
    }
    if (parameters) {
        auto record = *parameters;
        reserveRepeatBlocks(*definition, record, original.get(), original->parameters.get());
        definition->parameters = std::make_shared<const ComponentParameters>(std::move(record));
    }
    auto definitions = doc.definitions();
    for (const auto &[created, record] : draft.definitions())
        if (!definitions.contains(created))
            definitions[created] = record;
    definitions[id] = definition;
    // Drivers whose targets this edit changed (R094's incompatible-change warning).
    std::vector<Id> warnings;
    if (definition->parameters)
        for (const auto &driver : definition->parameters->drivers) {
            const ComponentDriver *before = nullptr;
            if (original->parameters)
                for (const auto &candidate : original->parameters->drivers)
                    if (candidate.id == driver.id)
                        before = &candidate;
            if (before &&
                componentDriverState(*original, doc.definitions(), *before) ==
                    ComponentDriverState::Bound &&
                componentDriverState(*definition, definitions, driver) ==
                    ComponentDriverState::ChangedSinceBound)
                warnings.push_back(driver.id);
        }
    auto bindings = doc.instances();
    for (const auto &[root, members] : existingMembers) {
        auto binding = std::make_shared<ComponentInstance>(*bindings.at(root));
        for (const auto &[member, body] : definition->members)
            if (!binding->members.contains(member) && members.contains(member))
                binding->members[member] = members.at(member);
        bindings[root] = binding;
    }
    ComponentResult result{id, 0, {}, {}};
    const auto beforeDefinitions = doc.definitions();
    const auto beforeVariants = doc.componentVariantRecords();
    result.changes = publish(doc, definitions, bindings, doc.bodies(), doc.nextId(),
                             draft.nextDefinitionId(), doc.nextComponentDriverId(),
                             "Edit shared component definition", {id, report, false});
    if (moved)
        result.movedGeometry[original->root] = moved;
    result.parameterWarnings = std::move(warnings);
    result.regenerated = componentRegeneration(doc, beforeDefinitions, *beforeVariants);
    return result;
}
std::map<Id, Id> componentScopeMembers(const Document &doc, const Document &draft, Id instance) {
    auto members = doc.instances().at(instance)->members;
    // The draft shows the template; a placement's repeat copies have no draft record.
    auto nested = [&](auto &&self, Id sceneRoot, Id draftRoot) -> void {
        const auto found = draft.instances().find(draftRoot);
        if (found == draft.instances().end())
            return;
        const auto scene = doc.instances().at(sceneRoot);
        const auto &canonical = found->second;
        for (auto [member, id] : scene->members) {
            const auto target = canonical->members.find(member);
            if (target == canonical->members.end())
                continue;
            members[target->second] = id;
            if (id != sceneRoot && doc.instances().contains(id))
                self(self, id, target->second);
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
                    doc.nextComponentDriverId(), "Change component local axes"),
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
std::map<Id, BodyPtr> projectComponentPlacement(const ComponentDefinitions &definitions,
                                                const ComponentInstances &instances, Id root,
                                                const Body &placement,
                                                const std::map<Id, std::pair<Id, Id>> &floors) {
    if (!instances.contains(root) || !instances.at(root))
        throw std::runtime_error("Missing component placement binding");
    const auto &top = *instances.at(root);
    if (!definitions.contains(top.definition) || !definitions.at(top.definition))
        throw std::runtime_error("Missing or mismatched component placement binding");
    ComponentVariantEngine engine(definitions);
    const auto variant = top.overrides.empty() && !engine.parametric(top.definition)
                             ? VariantPtr{}
                             : engine.variant(top.definition, top.overrides);
    // Every binding must already name each member, so projection never allocates.
    auto complete = [&](auto &&self, Id placed, Id definitionId, const VariantPtr &shown,
                        size_t depth) -> void {
        if (depth > 128 || !instances.contains(placed) || !instances.at(placed) ||
            instances.at(placed)->definition != definitionId ||
            !definitions.contains(definitionId))
            throw std::runtime_error("Missing or mismatched component placement binding");
        const auto &definition = *definitions.at(definitionId);
        const auto &binding = *instances.at(placed);
        const auto &members = shown ? shown->members : definition.members;
        const auto &references = shown ? shown->references : definition.references;
        if (binding.members.size() != members.size() ||
            !binding.members.contains(definition.root) ||
            binding.members.at(definition.root) != placed)
            throw std::runtime_error("Incomplete component placement member map");
        for (const auto &[member, body] : members) {
            if (!binding.members.contains(member))
                throw std::runtime_error("Incomplete component placement member map");
            if (const auto reference = references.find(member); reference != references.end()) {
                VariantPtr nested;
                if (shown)
                    if (const auto found = shown->nested.find(member); found != shown->nested.end())
                        nested = found->second;
                self(self, binding.members.at(member), reference->second, nested, depth + 1);
            }
        }
    };
    complete(complete, root, top.definition, variant, 1);
    const Document empty;
    const std::map<Id, BodyPtr> seed;
    Projection projection{empty, definitions, instances, seed, {}, {}, 0, &floors};
    projection.resolve(root, top.definition, placement, variant, top.overrides);
    return std::move(projection.scene);
}
} // namespace sketchy
