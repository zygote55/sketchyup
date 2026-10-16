#pragma once
#include "core/asset_records.hpp"
#include "core/body.hpp"
#include "core/component_parameter_records.hpp"
#include "core/document_limits.hpp"
#include "core/material_records.hpp"
#include "core/tag_records.hpp"
#include <optional>
namespace sketchy {
class Document;
struct Edit;
struct ComponentGlue {
    Id member{}, face{};
    Vec3 anchor{}, tangent{1, 0, 0}; // Member-local plane anchor and in-plane direction.
    bool cutsOpening{};              // The selected face's outer loop is the cut outline.
    bool operator==(const ComponentGlue &) const = default;
};
struct ComponentDefinition {
    Id id{}, root{}, nextMemberId{1};
    std::string name{"Component"};
    std::map<Id, BodyPtr> members;
    // A reference occupies a leaf group node; its geometry comes from the
    // referenced definition, and the node provides placement and instance state.
    std::map<Id, Id> references;
    std::optional<ComponentGlue> glue;
    // R093.b: at most one parameter record (null when the definition has none). The
    // members are then the template: the geometry at the default values.
    ComponentParametersPtr parameters;
};
using DefinitionPtr = std::shared_ptr<const ComponentDefinition>;
using ComponentDefinitions = std::map<Id, DefinitionPtr>;
struct ComponentInstance {
    Id definition{};
    // Definition member -> resolved scene record. For a parametric placement this is
    // the variant's members: the template's plus each repeat copy that exists.
    std::map<Id, Id> members;
    // R093.b: values for the definition's input parameters; top-level placements only.
    ComponentOverrides overrides;
    bool operator==(const ComponentInstance &) const = default;
};
using InstancePtr = std::shared_ptr<const ComponentInstance>;
using ComponentInstances = std::map<Id, InstancePtr>; // keyed by scene root
struct ComponentVariantIndex;
struct ComponentSize {
    size_t records{}, vertices{}, faces{}, wires{}, edges{}, curves{}, guides{}, depth{};
    // Materialized component instances, the placement itself and nested ones.
    size_t placements{};
};
LimitCounts limitCounts(const ComponentSize &size);
struct ComponentGeometry {
    // Records one placement of each definition owns directly: every member
    // except nested references, whose records belong to the nested placement.
    std::map<Id, ComponentSize> direct;
    // Every definition member counted once (unique geometry).
    ComponentSize stored;
};
ComponentGeometry componentGeometry(const ComponentDefinitions &definitions);
// Validate canonical records and the acyclic reference graph, returning bounded
// expanded sizes for allocation preflight. Definition members together must fit
// the unique geometry limits; each expansion must fit the materialized limits. Does not mutate caller-owned records.
std::map<Id, ComponentSize>
validateComponentDefinitions(const ComponentDefinitions &definitions, Id nextDefinitionId,
                             const TagRecords &tags = {}, Id nextTagId = 1,
                             const MaterialRecords &materials = {}, Id nextMaterialId = 1,
                             const AssetRecords &assets = {}, Id nextAssetId = 1);
// Resolved records must exactly project their canonical definition and binding.
// Root placement/state is instance-owned; inner members are definition-owned.
// The canonical comparison of a resolved record with its projection: exact equality,
// except that scene allocator floors may exceed (never fall below) the projected floors.
bool matchesComponentProjection(const Body &expected, const Body &actual);
// Placements of parametric definitions are checked against their variants, computed
// here (or taken from `variants` when given).
void validateComponentInstances(const ComponentDefinitions &definitions,
                                const ComponentInstances &instances,
                                const std::map<Id, BodyPtr> &scene);
void validateComponentInstances(const ComponentDefinitions &definitions,
                                const ComponentInstances &instances,
                                const std::map<Id, BodyPtr> &scene,
                                const ComponentVariantIndex &variants);
// Include staged definitions, bindings and tags when publishing a compound edit.
void appendSceneMetadataChanges(Edit &edit, const Document &before, const Document &after);
} // namespace sketchy
