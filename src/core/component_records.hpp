#pragma once
#include "core/asset_records.hpp"
#include "core/body.hpp"
#include "core/material_records.hpp"
#include "core/tag_records.hpp"
namespace sketchy {
class Document;
struct Edit;
struct ComponentDefinition {
    Id id{}, root{}, nextMemberId{1};
    std::string name{"Component"};
    std::map<Id, BodyPtr> members;
    // A reference occupies a leaf group node; its geometry comes from the
    // referenced definition, and the node provides placement and instance state.
    std::map<Id, Id> references;
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
void validateComponentInstances(const ComponentDefinitions &definitions,
                                const ComponentInstances &instances,
                                const std::map<Id, BodyPtr> &scene);
// Include staged definitions, bindings and tags when publishing a compound edit.
void appendSceneMetadataChanges(Edit &edit, const Document &before, const Document &after);
} // namespace sketchy
