#pragma once
#include "core/model.hpp"
#include <QJsonObject>
#include <QString>
#include <vector>

namespace sketchy {

// The camera path and render preset commands (ADR 0173 §7, decision 12). Each one is one undoable
// document edit. Presentation execution is not a command: a batch runs from --export-presentation
// or the application, never over MCP or the assistant.
bool isPresentationCommand(const QString &name);
void executePresentationCommand(Document &doc, const QJsonObject &command);

// The read-only descriptions behind camera_path.describe, render_preset.describe and
// presentation.describe. An unknown identity throws InspectionError NOT_FOUND. A plan that cannot
// run is reported in `refusals`, not thrown, so the query shows every reason at once.
QJsonObject cameraPathDescription(const Document &doc, Id path);
QJsonObject renderPresetDescription(const Document &doc, Id preset);
QJsonObject presentationDescription(const Document &doc, Id path, const std::vector<Id> &presets,
                                    bool video);

} // namespace sketchy
