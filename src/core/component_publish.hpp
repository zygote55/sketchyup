#pragma once
// Internal: the shared projection and publication step of component edits.
#include "core/components.hpp"
namespace sketchy {
struct ComponentPublishOptions {
    Id editedDefinition{};
    ChangeReport lineage;
    // Parameter operations refuse with LOCKED_ENTITY before publishing when the edit
    // would change a locked placement (ADR 0165 §4).
    bool parameterLocks{};
    // Geometry that moved to another context with its IDs unchanged (Edit::transfers).
    std::map<Id, std::set<Id>> transfers;
};
// Validate `definitions`, project every binding (each top-level placement through
// its variant) and publish one edit raising the allocators to the given floors.
// Returns the change report; publishes nothing when no record changes.
ChangeReport publishComponentRecords(Document &doc, const ComponentDefinitions &definitions,
                                     const ComponentInstances &bindings,
                                     const std::map<Id, BodyPtr> &seed, Id next,
                                     Id nextDefinition, Id nextDriver, std::string label,
                                     const ComponentPublishOptions &options = {});
// A unique copy of a parameter record: fresh driver IDs taken from `nextDriver` and
// no assembly identity (ADR 0166 §6.2).
ComponentParametersPtr renumberComponentDrivers(const ComponentParametersPtr &record,
                                                Id &nextDriver);
// The record with each repeat block (re)reserved when its target subtree changed
// size against `previousTemplate`, or when the block is new or its maxCount changed
// against `previousRecord`. Raises `definition.nextMemberId` above new blocks.
void reserveRepeatBlocks(ComponentDefinition &definition, ComponentParameters &record,
                         const ComponentDefinition *previousTemplate,
                         const ComponentParameters *previousRecord);
// Placements whose definition record or variant changed since `beforeDefinitions` /
// `beforeVariants`, and the variants that are new in the document's index.
ComponentRegeneration componentRegeneration(const Document &doc,
                                            const ComponentDefinitions &beforeDefinitions,
                                            const ComponentVariantIndex &beforeVariants);
} // namespace sketchy
