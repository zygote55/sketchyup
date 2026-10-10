#include "core/component_records.hpp"
#include "core/component_glue.hpp"
#include "core/component_validation.hpp"
#include "core/document_limits.hpp"
#include "core/model.hpp"
#include "core/record_overlay.hpp"
#include <algorithm>
#include <set>
namespace sketchy {
void appendSceneMetadataChanges(Edit &edit, const Document &before, const Document &after) {
    auto difference = [](const auto &old, const auto &next, auto &changes) {
        std::set<Id> ids;
        for (const auto &[id, record] : old)
            ids.insert(id);
        for (const auto &[id, record] : next)
            ids.insert(id);
        for (auto id : ids) {
            const auto a = old.contains(id) ? old.at(id) : nullptr;
            const auto b = next.contains(id) ? next.at(id) : nullptr;
            if (a != b)
                changes.push_back({id, a, b});
        }
    };
    difference(before.definitions(), after.definitions(), edit.definitions);
    difference(before.instances(), after.instances(), edit.instances);
    difference(before.tags(), after.tags(), edit.tags);
    edit.nextDefinitionFloor = after.nextDefinitionId();
    edit.nextTagFloor = after.nextTagId();
    difference(before.materials(), after.materials(), edit.materials);
    edit.nextMaterialFloor = after.nextMaterialId();
    difference(before.assets(), after.assets(), edit.assets);
    edit.nextAssetFloor = after.nextAssetId();
    difference(before.scenes(), after.scenes(), edit.scenes);
    std::erase_if(edit.scenes, [](const auto &change) {
        return change.before && change.after && *change.before == *change.after;
    });
    edit.nextSceneFloor = after.nextSceneId();
    difference(before.annotations(), after.annotations(), edit.annotations);
    std::erase_if(edit.annotations, [](const auto &change) {
        return change.before && change.after && *change.before == *change.after;
    });
    edit.nextAnnotationFloor = after.nextAnnotationId();
    edit.annotationsResolved = true;
    difference(before.sections(), after.sections(), edit.sections);
    std::erase_if(edit.sections, [](const auto &change) {
        return change.before && change.after && *change.before == *change.after;
    });
    edit.nextSectionFloor = after.nextSectionId();
    if (before.activeSections() != after.activeSections())
        edit.activeSections = std::pair{before.activeSections(), after.activeSections()};
    if (before.hostedComponents() != after.hostedComponents())
        edit.hosted = HostedChange{before.hostedRecords(), after.hostedRecords()};
    edit.hostedResolved = true;
    if (before.displayUnits() != after.displayUnits())
        edit.displayUnits = std::pair{before.displayUnits(), after.displayUnits()};
    if (before.solar() != after.solar())
        edit.solar = std::pair{before.solar(), after.solar()};
    if (before.style() != after.style())
        edit.style = std::pair{before.style(), after.style()};
}
namespace {
void bounded(const ComponentSize &size) {
    if (size.records > DocumentLimits::bodies || size.vertices > DocumentLimits::vertices ||
        size.faces > DocumentLimits::faces || size.wires > DocumentLimits::wires ||
        size.edges > DocumentLimits::edges || size.curves > DocumentLimits::curves ||
        size.guides > DocumentLimits::guides || size.depth > 128)
        throw std::runtime_error("Component expansion exceeds document editing limits");
}
void add(ComponentSize &a, const ComponentSize &b) {
    // Every operand is bounded before addition, so sums cannot overflow size_t.
    a.records += b.records;
    a.vertices += b.vertices;
    a.faces += b.faces;
    a.wires += b.wires;
    a.edges += b.edges;
    a.curves += b.curves;
    a.guides += b.guides;
    a.depth = std::max(a.depth, b.depth);
    bounded(a);
}
ComponentSize sizeOf(const Body &body) {
    return {1,
            body.surface.vertices.size(),
            body.surface.faces.size(),
            body.surface.wires.size(),
            body.topology.edges.size(),
            body.curves.size(),
            body.guides.size(),
            1};
}
void rootState(Body &body, const Body &placement) {
    body.id = placement.id;
    body.parent = placement.parent;
    body.transform = placement.transform;
    body.name = placement.name;
    body.properties = placement.properties;
    body.hidden = placement.hidden;
    body.locked = placement.locked;
    body.tag = placement.tag;
}
bool projected(const Body &expected, const Body &actual) {
    // Scene allocator floors can exceed the canonical floor after undo. They
    // never license a geometry identity mismatch or a lower allocator floor.
    if (actual.surface.nextId < expected.surface.nextId ||
        actual.topology.nextId < expected.topology.nextId)
        return false;
    auto comparable = actual;
    comparable.surface.nextId = expected.surface.nextId;
    comparable.topology.nextId = expected.topology.nextId;
    return comparable == expected;
}
} // namespace
std::map<Id, ComponentSize>
validateComponentDefinitions(const ComponentDefinitions &definitions, Id nextDefinitionId,
                             const TagRecords &tags, Id nextTagId, const MaterialRecords &materials,
                             Id nextMaterialId, const AssetRecords &assets, Id nextAssetId) {
    if (!nextDefinitionId || definitions.size() > 1024)
        throw std::runtime_error("Invalid component definition allocator or count");
    ComponentSize stored;
    for (const auto &[id, definition] : definitions) {
        if (!definition || id != definition->id || !id || id >= nextDefinitionId ||
            definition->name.size() > 1024 || !definition->members.contains(definition->root))
            throw std::runtime_error("Invalid component definition identity or root");
        const auto &root = definition->members.at(definition->root);
        if (!root || root->kind != BodyKind::Group || root->parent ||
            root->transform != Transform{} || root->hidden || root->locked || root->tag ||
            definition->references.contains(definition->root))
            throw std::runtime_error("Component definition root must be an unplaced group");
        if (!root->surface.vertices.empty() || !root->surface.faces.empty() ||
            !root->surface.wires.empty() || !root->curves.empty() || !root->guides.empty())
            throw std::runtime_error(
                "Component root is a placement frame; geometry belongs to members");
        // Reuse authoritative geometry, topology, curve, guide, hierarchy and
        // world-bound validation instead of accepting a looser prototype format.
        Document canonical;
        canonical.restore("00000000000000000000000000000000", definition->nextMemberId,
                          definition->members, 0, {}, {}, 1, tags, nextTagId, materials,
                          nextMaterialId, assets, nextAssetId);
        if (definition->glue)
            (void)resolveComponentGlue(*definition);
        for (const auto &[member, body] : definition->members) {
            if (canonical.bodies().at(member)->topology != body->topology)
                throw std::runtime_error("Component prototype requires explicit valid topology");
            auto ancestor = member;
            size_t depth = 1;
            while (ancestor != definition->root) {
                ancestor = definition->members.at(ancestor)->parent;
                if (!ancestor || ++depth > 128)
                    throw std::runtime_error("Component members must descend from one root");
            }
            auto size = sizeOf(*body);
            size.depth = depth;
            add(stored, size);
            if (definition->references.contains(body->parent))
                throw std::runtime_error("Nested component reference must be a leaf node");
        }
        for (const auto &[member, target] : definition->references) {
            if (!definition->members.contains(member) || !definitions.contains(target))
                throw std::runtime_error("Dangling nested component reference");
            const auto &body = *definition->members.at(member);
            if (body.kind != BodyKind::Group || !body.surface.vertices.empty() ||
                !body.guides.empty() || !body.curves.empty())
                throw std::runtime_error(
                    "Nested component reference cannot own independent geometry");
        }
    }
    std::map<Id, ComponentSize> sizes;
    std::set<Id> visiting;
    auto visit = [&](auto &&self, Id id) -> ComponentSize {
        if (sizes.contains(id))
            return sizes.at(id);
        if (visiting.size() >= 128 || !visiting.insert(id).second)
            throw std::runtime_error("Cyclic or excessively deep component reference graph");
        ComponentSize total;
        const auto &definition = *definitions.at(id);
        for (const auto &[member, body] : definition.members) {
            size_t depth = 1;
            for (auto parent = body->parent; parent; parent = definition.members.at(parent)->parent)
                ++depth;
            auto size = sizeOf(*body);
            if (definition.references.contains(member)) {
                size = self(self, definition.references.at(member));
                size.depth += depth - 1;
            } else
                size.depth = depth;
            add(total, size);
        }
        visiting.erase(id);
        sizes[id] = total;
        return total;
    };
    for (const auto &[id, definition] : definitions)
        visit(visit, id);
    return sizes;
}
namespace {
// Validate one binding against its definition. Scene and Instances need only
// contains/at, so the same checks serve whole-map and overlay candidates.
template <class Scene, class Instances>
void validateInstance(const ComponentDefinitions &definitions, const Instances &instances,
                      const Scene &scene, const Document *baseline, Id root,
                      const InstancePtr &instance, std::set<Id> &owned) {
    if (!instance || !definitions.contains(instance->definition) || !scene.contains(root))
        throw std::runtime_error("Dangling component instance");
    const auto &definition = *definitions.at(instance->definition);
    if (instance->members.size() != definition.members.size() ||
        !instance->members.contains(definition.root) ||
        instance->members.at(definition.root) != root || scene.at(root)->kind != BodyKind::Group)
        throw std::runtime_error("Invalid component instance root or member map");
    const bool unchangedLeaf = baseline && definition.references.empty() &&
                               baseline->instances().contains(root) &&
                               baseline->instances().at(root) == instance &&
                               baseline->definitions().contains(instance->definition) &&
                               baseline->definitions().at(instance->definition) ==
                                   definitions.at(instance->definition);
    std::set<Id> unique;
    for (const auto &[member, target] : instance->members) {
        if (!definition.members.contains(member) || !scene.contains(target) ||
            !unique.insert(target).second)
            throw std::runtime_error("Invalid or duplicated component member binding");
        if (member != definition.root) {
            if (!owned.insert(target).second)
                throw std::runtime_error("Scene record belongs to multiple component members");
            if (definition.references.contains(member)) {
                const auto nested = definition.references.at(member);
                if (!instances.contains(target) || instances.at(target)->definition != nested)
                    throw std::runtime_error("Missing nested component instance binding");
            } else if (instances.contains(target))
                throw std::runtime_error("Unreferenced nested component instance");
        }
        // Ownership/binding checks still run across the entire candidate.
        // Only the expensive canonical-body comparison can be reused.
        if (unchangedLeaf && baseline->bodies().contains(target) &&
            baseline->bodies().at(target) == scene.at(target))
            continue;
        auto expected = *definition.members.at(member);
        const auto &actual = *scene.at(target);
        if (member == definition.root)
            rootState(expected, actual);
        else {
            expected.id = target;
            expected.parent = instance->members.at(expected.parent);
            if (definition.references.contains(member)) {
                const auto nested = definition.references.at(member);
                auto geometry = *definitions.at(nested)->members.at(definitions.at(nested)->root);
                rootState(geometry, expected);
                expected = std::move(geometry);
            }
        }
        if (!projected(expected, actual))
            throw std::runtime_error("Resolved component member disagrees with its definition");
    }
}
void validateInstances(const ComponentDefinitions &definitions, const ComponentInstances &instances,
                       const std::map<Id, BodyPtr> &scene, const Document *baseline) {
    if (instances.size() > 10000)
        throw std::runtime_error("Too many component instances");
    std::set<Id> owned, roots;
    for (const auto &[root, instance] : instances) {
        validateInstance(definitions, instances, scene, baseline, root, instance, owned);
        roots.insert(root);
    }
    auto parents = owned;
    parents.insert(roots.begin(), roots.end());
    for (const auto &[id, body] : scene)
        if (parents.contains(body->parent) && !owned.contains(id))
            throw std::runtime_error("Unbound geometry inside a component instance");
}
} // namespace
bool componentBound(const Document &doc, Id id) {
    if (!id || !doc.bodies().contains(id))
        return false;
    if (doc.instances().contains(id))
        return true;
    // Every member target descends from its owning instance root, so only the
    // bounded ancestor chain can own this record.
    size_t depth = 0;
    for (Id ancestor = doc.bodies().at(id)->parent; ancestor && ++depth <= 128;) {
        const auto found = doc.bodies().find(ancestor);
        if (found == doc.bodies().end())
            return false;
        if (const auto instance = doc.instances().find(ancestor);
            instance != doc.instances().end())
            for (const auto &[member, target] : instance->second->members)
                if (target == id)
                    return true;
        ancestor = found->second->parent;
    }
    return false;
}
void validateComponentInstanceInsertions(const ComponentDefinitions &definitions,
                                         const ComponentInstances &added,
                                         const std::map<Id, BodyPtr> &changedBodies,
                                         const Document &baseline) {
    if (baseline.instances().size() + added.size() > 10000)
        throw std::runtime_error("Too many component instances");
    const RecordOverlay<BodyPtr> scene{baseline.bodies(), changedBodies};
    const RecordOverlay<InstancePtr> instances{baseline.instances(), added};
    std::set<Id> owned, roots;
    for (const auto &[root, instance] : added) {
        validateInstance(definitions, instances, scene, &baseline, root, instance, owned);
        roots.insert(root);
    }
    // Unchanged records keep their parent and ownership. Inserted bindings own
    // only inserted records, so only changed records can become unbound children.
    for (const auto &[id, body] : changedBodies) {
        if (!body)
            continue;
        const auto parent = body->parent;
        const bool boundParent =
            owned.contains(parent) || roots.contains(parent) || componentBound(baseline, parent);
        if (boundParent && !owned.contains(id))
            throw std::runtime_error("Unbound geometry inside a component instance");
    }
}
void validateComponentInstances(const ComponentDefinitions &definitions,
                                const ComponentInstances &instances,
                                const std::map<Id, BodyPtr> &scene) {
    validateInstances(definitions, instances, scene, nullptr);
}
void validateComponentInstanceEdits(const ComponentDefinitions &definitions,
                                    const ComponentInstances &instances,
                                    const std::map<Id, BodyPtr> &scene, const Document &baseline) {
    validateInstances(definitions, instances, scene, &baseline);
}
} // namespace sketchy
