#pragma once
#include "core/model.hpp"
#include <cstddef>
#include <string>
#include <vector>
namespace sketchy {
// R111 (ADR 0005, "Removing unused definitions"): the records document.purge_unused may remove.
// Each category defaults to on.
struct PurgeOptions {
    bool definitions{true};
    bool materials{true};
    bool assets{true};
    bool dryRun{};
};
struct PurgedRecord {
    Id id{};
    std::string name; // untrusted model data; callers truncate it before they report it
};
struct PurgedDefinition {
    Id id{};
    std::string name;
    // The unique vertices the definition's members store, which removing it frees.
    std::size_t uniqueVertices{};
};
struct PurgeReport {
    std::vector<PurgedDefinition> definitions;
    std::vector<PurgedRecord> materials, assets;
    std::size_t uniqueVerticesFreed{};
    // The changes of the one edit. Empty for a dry run and when nothing is unused.
    ChangeReport changes;
};
// Removes, in one edit (one undo step), every unused definition, then every material that no body
// and no remaining definition member names, then every asset that no remaining material, reference
// image or edge stroke names. Each step reads what the earlier one left, so a removed definition
// can free a material and a removed material can free an asset. With `dryRun` the report is the
// same and the document does not change. Throws std::runtime_error inside a component edit.
PurgeReport purgeUnused(Document &doc, const PurgeOptions &options);
} // namespace sketchy
