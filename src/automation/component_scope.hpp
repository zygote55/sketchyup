#pragma once
#include "automation/commands.hpp"
namespace sketchy {
// An explicit shared edit addressed through one placement's scene IDs and world frame.
QJsonObject componentScopeCommand(const Document &doc, Id instance, const QJsonArray &commands);
QJsonArray canonicalComponentCommands(const Document &doc, const Document &draft, Id instance,
                                      const QJsonArray &commands);
// Native selection follows the initiating placement while geometry reports remain global.
QJsonObject componentScopeResult(QJsonObject result, Id instance);
} // namespace sketchy
