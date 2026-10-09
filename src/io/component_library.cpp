#include "core/components.hpp"
#include "io/library_bundle.hpp"
#include <set>
namespace sketchy {
Document captureLibraryComponent(const Document &source, Id definition) {
    if (!source.definitions().contains(definition))
        throw std::runtime_error("Choose an existing component definition");
    ComponentDefinitions definitions;
    TagRecords tags;
    MaterialRecords materials;
    AssetRecords assets;
    std::vector<Id> pending{definition};
    auto asset = [&](Id id) {
        if (id) {
            const auto record = source.assets().at(id);
            if (!record->payload)
                throw std::runtime_error("Restore missing component assets before saving a bundle");
            assets[id] = record;
        }
    };
    auto material = [&](Id id) {
        if (id) {
            const auto record = source.materials().at(id);
            materials[id] = record;
            asset(record->asset);
        }
    };
    while (!pending.empty()) {
        const auto id = pending.back();
        pending.pop_back();
        if (definitions.contains(id))
            continue;
        const auto record = source.definitions().at(id);
        definitions[id] = record;
        for (const auto &[member, target] : record->references) {
            (void)member;
            pending.push_back(target);
        }
        for (const auto &[member, body] : record->members) {
            (void)member;
            for (auto tag = body->tag; tag && !tags.contains(tag);
                 tag = source.tags().at(tag)->parent)
                tags[tag] = source.tags().at(tag);
            material(body->materials.front);
            material(body->materials.back);
            for (const auto &[face, sides] : body->faceMaterials) {
                (void)face;
                material(sides.front);
                material(sides.back);
            }
            if (body->referenceImage)
                asset(body->referenceImage->asset);
        }
    }
    Document result;
    result.restore(result.identity(), 1, {}, 0, definitions, {}, source.nextDefinitionId(), tags,
                   source.nextTagId(), materials, source.nextMaterialId(), assets,
                   source.nextAssetId(), source.displayUnits());
    placeComponent(result, definition);
    return result;
}
} // namespace sketchy
