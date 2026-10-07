#include "io/component_library.hpp"
#include "core/components.hpp"
#include <QCryptographicHash>
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
namespace {
Id allocateLibraryId(Id &next) {
    if (!next || next == UINT64_MAX)
        throw std::runtime_error("Library resource identity space exhausted");
    return next++;
}
std::string uniqueLibraryName(const std::string &name, std::set<std::string> &used,
                              size_t &renamed) {
    if (used.insert(name).second)
        return name;
    // Bound the prefix in Unicode characters without cutting a UTF-8 sequence.
    const auto prefix = QString::fromStdString(name).left(200).toStdString();
    for (size_t suffix = 2; suffix <= 2049; ++suffix) {
        auto candidate = prefix + " (library " + std::to_string(suffix) + ")";
        if (used.insert(candidate).second) {
            ++renamed;
            return candidate;
        }
    }
    throw std::runtime_error("Library resource name space exhausted");
}
QByteArray assetDigest(const AssetRecord &asset) {
    if (!asset.payload)
        return {};
    const auto &bytes = asset.payload->bytes();
    return QCryptographicHash::hash(
        QByteArrayView(reinterpret_cast<const char *>(bytes.data()), qsizetype(bytes.size())),
        QCryptographicHash::Sha256);
}
} // namespace
LibraryInsertion insertLibraryComponent(Document &destination, const ComponentBundle &bundle,
                                        Transform placement, Id parent) {
    placement.validate();
    // Recapture validates public, caller-constructed bundle records as well as decoded bundles.
    const auto source = captureLibraryComponent(bundle.document, bundle.definition);
    if (destination.readSnapshotBytes() > 256 * 1024 * 1024 ||
        source.readSnapshotBytes() > 256 * 1024 * 1024)
        throw std::runtime_error("Library insertion exceeds snapshot budget");
    auto draft = destination.readSnapshot();
    LibraryInsertion result;
    Edit resources{"Import library resources", {}};
    std::map<Id, Id> assetIds{{0, 0}}, materialIds{{0, 0}}, tagIds{{0, 0}}, definitionIds;
    auto nextAsset = draft.nextAssetId(), nextMaterial = draft.nextMaterialId(),
         nextTag = draft.nextTagId(), nextDefinition = draft.nextDefinitionId();
    auto allAssets = draft.assets();
    auto allMaterials = draft.materials();
    std::set<std::string> assetNames, materialNames, definitionNames;
    std::map<Id, std::set<std::string>> tagNames;
    std::map<std::pair<std::string, QByteArray>, std::vector<Id>> assetHashes;
    for (const auto &[id, asset] : allAssets) {
        assetNames.insert(asset->name);
        if (asset->payload)
            assetHashes[{asset->mediaType, assetDigest(*asset)}].push_back(id);
    }
    for (const auto &[id, material] : allMaterials) {
        (void)id;
        materialNames.insert(material->name);
    }
    for (const auto &[id, tag] : draft.tags()) {
        (void)id;
        tagNames[tag->parent].insert(tag->name);
    }
    for (const auto &[id, def] : draft.definitions()) {
        (void)id;
        definitionNames.insert(def->name);
    }
    for (const auto &[id, asset] : source.assets()) {
        const auto key = std::pair{asset->mediaType, assetDigest(*asset)};
        Id reused{};
        for (auto candidate : assetHashes[key])
            if (allAssets.at(candidate)->payload->bytes() == asset->payload->bytes()) {
                reused = candidate;
                break;
            }
        if (reused) {
            assetIds[id] = reused;
            ++result.reusedAssets;
            continue;
        }
        auto record = std::make_shared<AssetRecord>(*asset);
        record->id = allocateLibraryId(nextAsset);
        record->name = uniqueLibraryName(record->name, assetNames, result.renamedResources);
        assetIds[id] = record->id;
        allAssets[record->id] = record;
        assetHashes[key].push_back(record->id);
        resources.assets.push_back({record->id, nullptr, record});
    }
    for (const auto &[id, material] : source.materials()) {
        auto record = std::make_shared<MaterialRecord>(*material);
        record->asset = assetIds.at(record->asset);
        Id reused{};
        for (const auto &[candidate, old] : allMaterials)
            if (old->name == record->name && old->color == record->color &&
                old->opacity == record->opacity && old->asset == record->asset) {
                reused = candidate;
                break;
            }
        if (reused) {
            materialIds[id] = reused;
            ++result.reusedMaterials;
            continue;
        }
        record->id = allocateLibraryId(nextMaterial);
        record->name = uniqueLibraryName(record->name, materialNames, result.renamedResources);
        materialIds[id] = record->id;
        allMaterials[record->id] = record;
        resources.materials.push_back({record->id, nullptr, record});
    }
    for (const auto &[id, tag] : source.tags()) {
        (void)tag;
        tagIds[id] = allocateLibraryId(nextTag);
    }
    for (const auto &[id, tag] : source.tags()) {
        auto record = std::make_shared<TagRecord>(*tag);
        record->id = tagIds.at(id);
        record->parent = tagIds.at(record->parent);
        record->name =
            uniqueLibraryName(record->name, tagNames[record->parent], result.renamedResources);
        resources.tags.push_back({record->id, nullptr, record});
    }
    for (const auto &[id, def] : source.definitions()) {
        (void)def;
        definitionIds[id] = allocateLibraryId(nextDefinition);
    }
    auto remapSides = [&](MaterialSides &sides) {
        sides.front = materialIds.at(sides.front);
        sides.back = materialIds.at(sides.back);
    };
    for (const auto &[id, def] : source.definitions()) {
        auto record = std::make_shared<ComponentDefinition>(*def);
        record->id = definitionIds.at(id);
        record->name = uniqueLibraryName(record->name, definitionNames, result.renamedResources);
        for (auto &[member, reference] : record->references) {
            (void)member;
            reference = definitionIds.at(reference);
        }
        for (auto &[member, body] : record->members) {
            (void)member;
            auto mapped = std::make_shared<Body>(*body);
            mapped->tag = tagIds.at(mapped->tag);
            remapSides(mapped->materials);
            for (auto &[face, sides] : mapped->faceMaterials) {
                (void)face;
                remapSides(sides);
            }
            if (mapped->referenceImage)
                mapped->referenceImage->asset = assetIds.at(mapped->referenceImage->asset);
            body = std::move(mapped);
        }
        resources.definitions.push_back({record->id, nullptr, record});
    }
    resources.nextAssetFloor = nextAsset;
    resources.nextMaterialFloor = nextMaterial;
    resources.nextTagFloor = nextTag;
    resources.nextDefinitionFloor = nextDefinition;
    draft.apply(std::move(resources), draft.revision());
    result.component = placeComponent(draft, definitionIds.at(bundle.definition), placement, parent,
                                      bundle.metadata.name.toStdString());
    if (draft.readSnapshotBytes() > 256 * 1024 * 1024)
        throw std::runtime_error("Library insertion result exceeds snapshot budget");
    Edit insertion{"Insert library component", {}};
    for (const auto &[id, body] : draft.bodies()) {
        const auto old = destination.bodies().contains(id) ? destination.bodies().at(id) : nullptr;
        if (!old || *old != *body)
            insertion.changes.push_back({id, old, body, {}, {}, {}, true});
    }
    insertion.nextIdFloor = draft.nextId();
    appendSceneMetadataChanges(insertion, destination, draft);
    result.component.changes = destination.apply(std::move(insertion), destination.revision());
    return result;
}
} // namespace sketchy
