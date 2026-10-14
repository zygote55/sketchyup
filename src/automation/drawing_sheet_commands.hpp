#pragma once
#include "core/drawing_sheets.hpp"
#include <QJsonArray>
#include <QJsonObject>
#include <cstdint>
namespace sketchy {
// Shared drawing-sheet commands and inspection (ADR 0163, R090.d). Every
// drawing_sheet.* and drawing_view.* operation goes through the core edit builders,
// which tag their edits with the drawing-sheet scope, so Document::apply enforces
// isDrawingSheetOnlyEdit for each operation and again for the whole batch.
bool isDrawingSheetCommand(const QString &name);
// Runs one command against the batch's working document. stampRevision is the
// document revision whose model content the batch evaluates (its base revision);
// accepted stamps record it with the evaluated fingerprint.
void executeDrawingSheetCommand(Document &doc, const QJsonObject &command,
                                std::uint64_t stampRevision);
// Parameter schemas shared by the command catalogue.
QJsonObject drawingPaperSchema();
QJsonObject drawingFrameSchema();
QJsonObject drawingViewSourceSchema();
QJsonObject drawingViewOffsetSchema();
QJsonObject drawingViewScaleSchema();
QJsonObject drawingViewContentSchema();
// Inspection payloads.
QJsonObject drawingSheetSummary(const Document &doc, Id sheet);
QJsonObject drawingSheetDescription(const Document &doc, Id sheet);
// One evaluated status row; costs one bounded capture.
QJsonObject drawingViewStatusDescription(const Document &doc, Id view);
// Record, status and, for a measurable view, its world <-> sheet projection.
QJsonObject drawingViewDescription(const Document &doc, Id view);
// Views in sheet order, then by ID; optionally only one sheet's views.
std::vector<Id> orderedDrawingViews(const Document &doc, Id sheet = 0);
} // namespace sketchy
