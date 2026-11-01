#pragma once
#include "core/component_parameter_records.hpp"
#include "core/model.hpp"
#include <optional>
#include <set>
#include <stdexcept>
#include <string>
#include <vector>
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
// D-075: the definitions among `candidates` that nothing still refers to. `placed` names the
// definitions that keep a placement, and a definition that a remaining definition's nested
// reference names, or that a library assembly identifies, is kept. A dropped definition can
// release the definitions it references, so the test repeats until no candidate drops.
std::set<Id> unreferencedDefinitions(const ComponentDefinitions &definitions,
                                     const std::set<Id> &placed, std::set<Id> candidates);
// R111: what keeps one definition in use. A definition with none of these is unused and may be
// removed with deleteComponentDefinition.
struct DefinitionUse {
    std::vector<Id> placements; // top-level and nested placements, ascending by root
    std::vector<Id> nestedBy;   // other definitions whose nested references name it, ascending
    std::optional<ComponentAssembly> assembly; // its library assembly identity (ADR 0165)
};
DefinitionUse definitionUse(const Document &doc, Id definition);
// R111: a delete refused because something still uses the definition. `code` is
// DEFINITION_PLACED, DEFINITION_NESTED or DEFINITION_ASSEMBLY; `use` names the blockers.
class DefinitionInUseError : public std::runtime_error {
  public:
    DefinitionInUseError(std::string code, const std::string &message, Id definition,
                         DefinitionUse use)
        : std::runtime_error(message), code_(std::move(code)), definition_(definition),
          use_(std::move(use)) {}
    const std::string &code() const { return code_; }
    Id definition() const { return definition_; }
    const DefinitionUse &use() const { return use_; }

  private:
    std::string code_;
    Id definition_{};
    DefinitionUse use_;
};
struct DefinitionRemoval {
    Id definition{};
    // Definitions this removal leaves unused: the ones it released and nothing else refers to.
    // They are kept; a removal does not cascade.
    std::vector<Id> nowUnused;
    ChangeReport changes;
};
// R111: removes exactly the named definition, in one edit, when definitionUse finds nothing.
// Otherwise it throws DefinitionInUseError and changes nothing.
DefinitionRemoval deleteComponentDefinition(Document &doc, Id definition);
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
