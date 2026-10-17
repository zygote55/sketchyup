#pragma once
#include "core/model.hpp"
namespace sketchy {
struct ComponentRegeneration {
    std::size_t placements{}, variants{};
};
struct ComponentResult {
    Id definition{}, instance{};
    ChangeReport changes;
    std::map<Id, Id> movedGeometry;
    // R093.b (ADR 0165 §4, §6).
    std::vector<Id> createdDrivers{};
    // Placements whose variant changed and distinct variants built by this edit.
    ComponentRegeneration regenerated{};
    // Drivers this edit moved from bound to changedSinceBound.
    std::vector<Id> parameterWarnings{};
    // component.replace: the overrides the placement dropped.
    std::vector<std::string> droppedOverrides{};
    bool parametersPromoted{};
};
ComponentResult createComponent(Document &doc, Id root, std::string name = "Component");
ComponentResult placeComponent(Document &doc, Id definition, Transform local = {}, Id parent = 0,
                               std::string name = {});
// Drops the placement's overrides (reported in droppedOverrides).
ComponentResult replaceComponent(Document &doc, Id instance, Id definition);
// Copies parameter records with new driver IDs and keeps the placement's overrides.
ComponentResult makeComponentUnique(Document &doc, Id instance);
// The optional parameter step of a template edit (ADR 0165 §4): given the new
// template and the current record, return the record to store. It may update or
// remove parameters and drivers, so a template edit that deletes a driver's target
// can retarget or remove that driver in the same edit; it cannot create drivers.
using ComponentTemplateParameters =
    std::function<ComponentParametersPtr(const ComponentDefinition &, const ComponentParametersPtr &)>;
// Edit canonical member IDs privately, returning composed topology lineage. For a
// parametric definition this edits the template: every placement regenerates, a
// driver whose target the edit breaks refuses it (PARAMETER_DRIVER_TARGET), and
// parameterWarnings lists drivers whose targets changed.
ComponentResult editComponentDefinition(Document &doc, Id definition,
                                        const std::function<ChangeReport(Document &)> &edit,
                                        Transform editingFrame = {},
                                        const ComponentTemplateParameters &parameters = {});
ComponentResult setComponentAxes(Document &doc, Id definition, Transform axes);
ComponentResult setComponentGlue(Document &doc, Id definition, std::optional<ComponentGlue> glue);
// Map a materialized definition-edit draft to one live placement, including references.
std::map<Id, Id> componentScopeMembers(const Document &doc, const Document &draft, Id instance);
// Rebuild one persisted top-level placement exactly as editing projects it, from
// already validated definitions and complete member bindings, including nested
// bindings. `placement` supplies the root's instance-owned state. `floors` restores
// scene allocator floors that exceed the canonical member floor; each must be higher.
// Returns the root and every resolved member, each a fresh unshared record. When
// `variants` is given it supplies the variant each placement shows (absent: the
// template), so a caller that evaluated the whole document once (native load, ADR 0165
// §5) does not evaluate again; otherwise the placement is evaluated here.
std::map<Id, BodyPtr>
projectComponentPlacement(const ComponentDefinitions &definitions,
                          const ComponentInstances &instances, Id root, const Body &placement,
                          const std::map<Id, std::pair<Id, Id>> &floors = {},
                          const ComponentVariantIndex *variants = nullptr);
} // namespace sketchy
