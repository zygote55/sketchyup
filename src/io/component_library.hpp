#pragma once
#include "core/components.hpp"
#include "io/library_bundle.hpp"
namespace sketchy {
struct LibraryInsertion {
    ComponentResult component;
    size_t reusedAssets{}, reusedMaterials{}, renamedResources{};
};
// One undoable edit. Imported definitions remain independent of prior insertions.
LibraryInsertion insertLibraryComponent(Document &destination, const ComponentBundle &bundle,
                                        Transform placement = {}, Id parent = 0);
} // namespace sketchy
