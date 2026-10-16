#pragma once
#include "core/component_parameter_records.hpp"
#include "core/components.hpp"
#include <QJsonArray>
#include <QJsonObject>
namespace sketchy {
// Component parameter commands and inspection (ADR 0165 §4, §6; R093.d). Every
// component_parameter.* operation goes through the R093.b edit builders on the batch's
// staged document, so each is one ordinary model edit and a batch publishes one
// history entry. Definition-side operations are shared-definition edits; override and
// reset are instance-scoped. Errors keep the core's codes (PARAMETER_*, LIMIT_EXCEEDED,
// LOCKED_ENTITY, NOT_FOUND, INVALID_TARGET); executeBatch reports them as
// InspectionError so sessions, MCP and staging see the code.
bool isComponentParameterCommand(const QString &name);
// The structured fields of a parameter error (ADR 0165 §6, ADR 0166 §5): definition,
// `body` (the placement it names, e.g. the locked one), parameter, driver, offset,
// excerpt, cycle, referrers and, for a bound, {bound, limit, value} in SI units. The
// refusal surfaces report it as the error's `details`.
QJsonObject componentParameterErrorDetails(const ComponentParameterError &error);
// Runs one component_parameter.* command against the batch's working document.
// component_parameter.promote is two edits (make unique, then promote) and goes
// through executeComponentParameterPromotion instead.
ComponentResult executeComponentParameterCommand(Document &doc, const QJsonObject &command);
// component_parameter.promote {body} (ADR 0166 §4.2): makes the top-level placement
// unique when its definition has other placements, then makes its current values the
// definition's defaults and clears its overrides. `unique` is set when a copy was
// made; the caller composes both results into the batch's one history entry.
struct ComponentParameterPromotion {
    std::optional<ComponentResult> unique;
    ComponentResult promoted;
};
ComponentParameterPromotion executeComponentParameterPromotion(Document &doc,
                                                               const QJsonObject &command);
// The ten command catalogue entries.
QJsonArray componentParameterCommandSpecs();
// ---- Batch result fields (ADR 0165 §6) ----
// Driver IDs present after the edit and absent before it, ascending.
QJsonArray createdComponentDrivers(const ComponentDefinitions &before,
                                   const ComponentDefinitions &after);
// Drivers bound before and changedSinceBound after, ascending.
QJsonArray componentParameterWarnings(const ComponentDefinitions &before,
                                      const ComponentDefinitions &after);

// ---- Inspection (component_parameters.describe, .values, .evaluate) ----
QJsonObject componentParametersDescription(const Document &doc, Id definition);
QJsonObject componentParameterValues(const Document &doc, Id placement);
// A read-only dry run; `request` holds the query's optional overrides, expression and
// type. Never changes the document. Failures are returned as data, not thrown.
QJsonObject componentParametersEvaluation(const Document &doc, Id definition,
                                          const QJsonObject &request);
} // namespace sketchy
