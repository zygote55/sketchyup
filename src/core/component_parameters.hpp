#pragma once
// R093.b: the parameter graph, evaluation and the parameter edit builders
// (ADR 0165 §1, §3, §4). Every builder is one ordinary model edit: it stages the
// new records, regenerates every affected placement and publishes one history entry,
// or throws ComponentParameterError and leaves the document unchanged. A batch may
// call several builders on a staged document and compose them as one entry.
#include "core/components.hpp"
#include "core/component_parameter_records.hpp"
#include <set>
namespace sketchy {
// ---- Graph and evaluation ----
// The checked form of one definition's parameter record. Nodes are the parameters
// and the driver expressions; edges are name references.
struct CompiledComponentParameters {
    DefinitionPtr definition;
    std::map<std::string, std::size_t, std::less<>> index; // parameter name -> record position
    std::vector<ParameterSignature> signatures;            // per parameter
    std::vector<CheckedParameterExpression> expressions;   // per parameter
    // Kahn's order: the ready set ordered by name bytes (ADR 0165 §3).
    std::vector<std::size_t> order;
    struct Driver {
        ParameterSignature expected;
        CheckedParameterExpression value;
        std::optional<CheckedParameterExpression> spacing; // repeat only
        std::optional<ComponentParameter> child;           // childParameter: the target input
    };
    std::vector<Driver> drivers; // record order (ascending ID)
};
using CompiledParametersPtr = std::shared_ptr<const CompiledComponentParameters>;
// Static checks of one definition's record against its template and the referenced
// definitions: names, labels, types, bounds, options, canonical expressions, the
// acyclic graph and every driver target. Throws ComponentParameterError.
CompiledParametersPtr compileComponentParameters(const DefinitionPtr &definition,
                                                 const ComponentDefinitions &definitions);
struct ComponentEvaluation {
    std::vector<ParameterValue> parameters; // record order
    std::vector<ParameterValue> drivers;    // record order
    std::vector<double> spacing;            // repeat spacing per driver, 0 for other kinds
    std::size_t visits{};                   // node visits this evaluation used
};
// Evaluate one placement: each input takes its value from `inputs` or its default,
// derived parameters follow in graph order, then the drivers in ID order. The budget
// is min(definitionVisits, documentRemaining), checked before every node visit.
ComponentEvaluation evaluateComponentParameters(
    const CompiledComponentParameters &compiled, const ComponentOverrides &inputs,
    std::size_t documentRemaining = ComponentParameterLimits::documentVisits,
    std::size_t definitionBudget = ComponentParameterLimits::definitionVisits);
// Validate and canonicalize override values for `compiled`'s inputs: names must be
// input parameters (PARAMETER_NAME), values typed (PARAMETER_TYPE) and inside the
// type range, bounds and step (PARAMETER_DOMAIN). Quantities are quantized.
ComponentOverrides canonicalComponentOverrides(const CompiledComponentParameters &compiled,
                                               const ComponentOverrides &values);
// Check a final value against one parameter's bounds and step (PARAMETER_DOMAIN).
void checkComponentParameterBounds(const ComponentParameter &parameter, const ParameterValue &value,
                                   ComponentParameterErrorContext context);
// The canonical literal for a value of `type`; evaluating it gives the value exactly.
std::string componentParameterLiteral(const ParameterValue &value, ParameterType type);

// Document-wide checks of every parameter record: the parametric definition count,
// record bytes, unique driver IDs below `nextDriverId`, parametric nesting depth and,
// for each definition whose record or referenced definitions changed relative to
// `baseline` (all when null), compileComponentParameters.
void validateComponentParameterRecords(const ComponentDefinitions &definitions,
                                       const ComponentDefinitions *baseline, Id nextDriverId);

// ---- Driver state (ADR 0165 §4) ----
enum class ComponentDriverState { Bound, ChangedSinceBound };
ComponentDigest componentDriverDigest(const ComponentDefinition &definition,
                                      const ComponentDefinitions &definitions,
                                      const ComponentDriver &driver);
ComponentDriverState componentDriverState(const ComponentDefinition &definition,
                                          const ComponentDefinitions &definitions,
                                          const ComponentDriver &driver);

// ---- Edit builders (ADR 0165 §4, §6) ----
struct ComponentParameterSpec {
    std::string name, label;
    ParameterType type{ParameterType::Length};
    ComponentParameterMode mode{ComponentParameterMode::Input};
    std::string expression; // any valid spelling; stored as canonical text
    std::optional<double> min, max, step;
    std::vector<ComponentParameterOption> options;
};
// Optional fields are left unchanged; `clearMin` etc. remove a bound. Type and mode
// are fixed.
struct ComponentParameterUpdate {
    std::optional<std::string> label, expression;
    std::optional<double> min, max, step;
    bool clearMin{}, clearMax{}, clearStep{};
    std::optional<std::vector<ComponentParameterOption>> options;
    std::optional<std::size_t> position;
};
struct ComponentDriverSpec {
    ComponentDriverKind kind{ComponentDriverKind::Stretch};
    Id member{};
    std::string expression;
    ComponentAxis axis{ComponentAxis::X};
    double at{};
    std::uint32_t maxCount{1};
    std::string spacing;
    std::string parameter;
};
ComponentResult defineComponentParameter(Document &doc, Id definition, ComponentParameterSpec spec,
                                         std::optional<std::size_t> position = {});
ComponentResult updateComponentParameter(Document &doc, Id definition, const std::string &name,
                                         const ComponentParameterUpdate &update);
// Rewrites references in the definition, overrides on its placements and parent
// childParameter drivers.
ComponentResult renameComponentParameter(Document &doc, Id definition, const std::string &name,
                                         const std::string &newName);
// Refused while referenced (the referrers are listed); overrides of it are dropped.
ComponentResult removeComponentParameter(Document &doc, Id definition, const std::string &name);
// Creates a driver (driver == 0) or updates one; stamps `bound`.
ComponentResult driveComponent(Document &doc, Id definition, Id driver,
                               const ComponentDriverSpec &spec);
ComponentResult undriveComponent(Document &doc, Id definition, Id driver);
// Restamps the listed drivers, or every driver when `drivers` is empty.
ComponentResult rebindComponentDrivers(Document &doc, Id definition,
                                       const std::vector<Id> &drivers = {});
// Sets or clears the definition's library assembly identity (ADR 0166 §6.2), a
// shared-definition edit that changes no geometry. Requires a parameter record;
// a malformed identity is PARAMETER_NAME.
ComponentResult setComponentAssembly(Document &doc, Id definition,
                                     std::optional<ComponentAssembly> assembly);
// Instance scope: sets override values on a top-level placement.
ComponentResult overrideComponentParameters(Document &doc, Id instance,
                                            const ComponentOverrides &values);
// Removes the named overrides, or every override when `names` is empty.
ComponentResult resetComponentParameters(Document &doc, Id instance,
                                         const std::vector<std::string> &names = {});
// component.edit_instance on a parametric placement (ADR 0165 §4): after make-unique,
// the placement's current values become its definition's defaults (re-baselining the
// template) and its overrides clear. Reports parametersPromoted.
ComponentResult promoteComponentParameters(Document &doc, Id instance);
// component.edit scoped through `instance` is allowed only when the placement's values
// equal its defaults (its variant shows the template) and none of `sceneRecords` is a
// repeat copy. Throws PARAMETER_TEMPLATE_SCOPE otherwise.
void requireComponentTemplateScope(const Document &doc, Id instance,
                                   const std::set<Id> &sceneRecords = {});
// The input values a placement evaluates with: its overrides (top-level) or the
// values its parent's childParameter drivers supply (nested).
ComponentOverrides componentPlacementInputs(const Document &doc, Id instance);
} // namespace sketchy
