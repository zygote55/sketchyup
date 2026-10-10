#pragma once
#include "core/asset_records.hpp"
#include "core/body.hpp"
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
};
using DefinitionPtr = std::shared_ptr<const ComponentDefinition>;
using ComponentDefinitions = std::map<Id, DefinitionPtr>;
struct ComponentInstance {
    Id definition{};
    std::map<Id, Id> members; // definition member -> resolved scene record
    bool operator==(const ComponentInstance &) const = default;
};
using InstancePtr = std::shared_ptr<const ComponentInstance>;
using ComponentInstances = std::map<Id, InstancePtr>; // keyed by scene root
struct ComponentSize {
    size_t records{}, vertices{}, faces{}, wires{}, edges{}, curves{}, guides{}, depth{};
};
// Validate canonical records and the acyclic reference graph, returning bounded
// expanded sizes for allocation preflight. Does not mutate caller-owned records.
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
void validateComponentInstances(const ComponentDefinitions &definitions,
                                const ComponentInstances &instances,
                                const std::map<Id, BodyPtr> &scene);
// Include staged definitions, bindings and tags when publishing a compound edit.
void appendSceneMetadataChanges(Edit &edit, const Document &before, const Document &after);
} // namespace sketchy
