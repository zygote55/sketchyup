#pragma once
#include "core/component_parameters.hpp"
#include <QJsonObject>
#include "core/components.hpp"
#include <optional>
#include "io/library_bundle.hpp"
namespace sketchy {
struct LibraryInsertion {
    ComponentResult component;
    size_t reusedAssets{}, reusedMaterials{}, renamedResources{};
};
// One undoable edit. Imported definitions remain independent of prior insertions.
LibraryInsertion insertLibraryComponent(Document &destination, const ComponentBundle &bundle,
                                        Transform placement = {}, Id parent = 0);

// ADR 0166 §6 (R094.f): placing a configurable assembly from the library.
// The library assembly identity of a bundle's definition, if it has one.
std::optional<ComponentAssembly> libraryAssemblyIdentity(const ComponentBundle &bundle);
// The definition in `doc` that carries `identity` (the lowest ID when several do), or 0.
Id findLibraryAssembly(const Document &doc, const ComponentAssembly &identity);
// A gluing assembly placed on a host face, with component.attach's parameters: the anchor
// and tangent are host-local, the inset a signed distance from the host plane.
struct AssemblyHostFace {
    Id host{}, face{};
    Vec3 anchor{}, tangent{1, 0, 0};
    double inset{};
};
struct AssemblyPlacement {
    Transform transform{};               // free placement (ignored on a host face)
    std::optional<AssemblyHostFace> host; // a gluing assembly attached to a face
    Id parent{};
};
struct AssemblyPlacementResult {
    Id placement{}, definition{};
    bool reused{}; // the in-model definition with the bundle's assembly identity was reused
    ChangeReport changes;
};
// One undoable edit, labelled `label` ("Place Interior door" by default): reuses the
// in-model definition with the bundle's assembly identity (§6.2) or inserts the bundle
// (new independent definitions, as insertLibraryComponent), places it, sets `overrides`
// on the placement and, for a host face, attaches it with the builders component.attach
// uses, so a cutting assembly's opening is cut in the same edit. `overrides` are
// component_parameter.override values (override literals: SI numbers, quantity strings,
// booleans and choice values) and go through that command's builder. Throws, leaving the
// document unchanged, on any refusal (bounds, regeneration, host placement or limits);
// parameter refusals are ComponentParameterError with the core's code.
AssemblyPlacementResult placeLibraryAssembly(Document &destination, const ComponentBundle &bundle,
                                             const AssemblyPlacement &placement,
                                             const QJsonObject &overrides = {},
                                             std::string label = {});
} // namespace sketchy
