#pragma once
#include "automation/commands.hpp"
#include <set>
namespace sketchy {
// An explicit shared edit addressed through one placement's scene IDs and world frame.
QJsonObject componentScopeCommand(const Document &doc, Id instance, const QJsonArray &commands);
QJsonArray canonicalComponentCommands(const Document &doc, const Document &draft, Id instance,
                                      const QJsonArray &commands);
// Native selection follows the initiating placement while geometry reports remain global.
QJsonObject componentScopeResult(QJsonObject result, Id instance);
// Inside component.edit and component.edit_instance, parameter operations are refused:
// one on another definition needs its own explicit scope, and one on the scoped
// definition belongs in the outer batch beside the template edit (ADR 0165 §4).
void rejectScopedParameterCommands(const QJsonArray &commands, Id scopedDefinition);
// The scene records a scoped edit's nested commands name (body, context, parent, tool,
// members and entity bodies), for the template-scope check.
std::set<Id> scopedCommandRecords(const QJsonArray &commands);
} // namespace sketchy
