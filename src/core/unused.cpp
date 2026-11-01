#include "core/unused.hpp"
#include "core/components.hpp"
#include "core/material_records.hpp"
#include "core/reference_image.hpp"
#include <set>
#include <stdexcept>
namespace sketchy {
namespace {
// Each of these names an asset that validation refuses to drop, so the purge keeps it.
void addAssetReference(std::set<Id> &assets, const Body &body) {
    if (body.referenceImage && body.referenceImage->asset)
        assets.insert(body.referenceImage->asset);
}
void addEdgeStrokeReference(std::set<Id> &assets, const ModelStyle &style) {
    if (style.edgeStroke && style.edgeStroke->texture)
        assets.insert(style.edgeStroke->texture);
}
} // namespace
PurgeReport purgeUnused(Document &doc, const PurgeOptions &options) {
    if (doc.componentDraft())
        throw std::runtime_error("Remove unused records in the document, not in a component edit");
    PurgeReport report;
    Edit edit{"Remove unused definitions, materials and assets", {}};
    // Definitions. unreferencedDefinitions is the rule that group.explode and make-unique apply,
    // here to every definition: a placed, library-assembly or still-nested definition stays.
    auto definitions = doc.definitions();
    std::set<Id> dropped;
    if (options.definitions) {
        std::set<Id> placed, candidates;
        for (const auto &[root, instance] : doc.instances())
            placed.insert(instance->definition);
        for (const auto &[id, definition] : definitions)
            candidates.insert(id);
        dropped = unreferencedDefinitions(definitions, placed, candidates);
    }
    for (const auto id : dropped) {
        const auto definition = definitions.at(id);
        PurgedDefinition item{id, definition->name, 0};
        for (const auto &[member, body] : definition->members)
            item.uniqueVertices += body->surface.vertices.size();
        report.uniqueVerticesFreed += item.uniqueVertices;
        report.definitions.push_back(item);
        edit.definitions.push_back({id, definition, nullptr});
        definitions.erase(id);
    }
    // Materials. A body names a material through its sides, its face overrides or its terrain paint
    // (addMaterialReferences), so a material no remaining body or definition member names is unused.
    std::set<Id> inUse;
    for (const auto &[root, body] : doc.bodies())
        addMaterialReferences(*body, inUse);
    for (const auto &[id, definition] : definitions)
        for (const auto &[member, body] : definition->members)
            addMaterialReferences(*body, inUse);
    std::set<Id> removedMaterials;
    if (options.materials)
        for (const auto &[id, material] : doc.materials())
            if (!inUse.contains(id)) {
                report.materials.push_back({id, material->name});
                edit.materials.push_back({id, material, nullptr});
                removedMaterials.insert(id);
            }
    // Assets. The Clean files rule keeps an asset that a remaining material names. A reference
    // image and an edge stroke texture also name assets, and validation refuses to drop those, so
    // the purge keeps them too.
    std::set<Id> kept;
    for (const auto &[id, material] : doc.materials())
        if (!removedMaterials.contains(id) && material->asset)
            kept.insert(material->asset);
    for (const auto &[root, body] : doc.bodies())
        addAssetReference(kept, *body);
    for (const auto &[id, definition] : definitions)
        for (const auto &[member, body] : definition->members)
            addAssetReference(kept, *body);
    addEdgeStrokeReference(kept, doc.style());
    for (const auto &[id, scene] : doc.scenes())
        if (scene->snapshot.style)
            addEdgeStrokeReference(kept, *scene->snapshot.style);
    if (options.assets)
        for (const auto &[id, asset] : doc.assets())
            if (!kept.contains(id)) {
                report.assets.push_back({id, asset->name});
                edit.assets.push_back({id, asset, nullptr});
            }
    // An empty edit is not applied, so it leaves no history item.
    if (!options.dryRun && !(edit.definitions.empty() && edit.materials.empty() &&
                             edit.assets.empty()))
        report.changes = doc.apply(std::move(edit), doc.revision());
    return report;
}
} // namespace sketchy
