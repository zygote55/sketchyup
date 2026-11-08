#pragma once
#include "core/asset_records.hpp"
#include "core/body.hpp"
#include "core/component_parameter_types.hpp"
#include "core/document_limits.hpp"
#include "core/material_records.hpp"
#include "core/tag_records.hpp"
#include <cstddef>
#include <map>
#include <optional>
#include <set>
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
// The most component definitions a document holds besides its root (R113, D-102). The format
// refuses more; an import of a larger file omits the excess, and counts it. Scaled only in
// benchmark builds (R114). Raised from 1,024 to 4,096 by R117: the largest tier measured within the
// 100 ms edit budget on the GPU PC (the record says what stops 8,192).
inline constexpr std::size_t componentDefinitionCountLimit = 4096 * DocumentLimits::scale;
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
// The validation of an edit that changes the definitions in `changed` and no tag, material or
// asset record. An ID absent from `definitions` was erased. Every definition keeps its cheap checks
// (identity, sizes and nested references), so a reference to an erased definition is refused. The
// geometry of each changed definition is checked as validateComponentDefinitions checks it, and
// the expansion graph is checked for the changed definitions and every definition that places one.
// Any other definition was validated when it was stored and is unchanged, so the result equals
// validateComponentDefinitions on the same candidate. Does not mutate caller-owned records.
void validateComponentDefinitionChanges(const ComponentDefinitions &definitions,
                                        const std::set<Id> &changed, Id nextDefinitionId,
                                        const TagRecords &tags, Id nextTagId,
                                        const MaterialRecords &materials, Id nextMaterialId,
                                        const AssetRecords &assets, Id nextAssetId);
// Expanded sizes of `roots` and of the definitions they reach, with the graph checks (acyclic,
// nesting depth, expansion budgets) and no geometry checks. Callers use it for stored definitions,
// whose records are already validated.
std::map<Id, ComponentSize> componentExpansionSizes(const ComponentDefinitions &definitions,
                                                    const std::set<Id> &roots);
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
