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
// Page items (ADR 0164, R091.c) are drawing-sheet commands too: drawing_item.* and
// drawing_sheet.update stay inside the sheet-only batch rule.
bool isDrawingSheetCommand(const QString &name);
bool isDrawingItemCommand(const QString &name);
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
QJsonObject drawingSheetNumberSchema();
QJsonObject drawingSheetMarginSchema();
QJsonObject drawingItemAnchorSchema();
QJsonObject drawingItemKindSchema();
// oneOf choices that pair each kind with its data object; on update every data
// field is optional and a dimension's view and anchors are not accepted (they
// change only through drawing_item.reattach).
QJsonArray drawingItemDataSchemas(bool creating);
// Inspection payloads.
QJsonObject drawingSheetSummary(const Document &doc, Id sheet);
QJsonObject drawingSheetDescription(const Document &doc, Id sheet);
// One evaluated status row; costs one bounded capture.
QJsonObject drawingViewStatusDescription(const Document &doc, Id view);
// Record, status and, for a measurable view, its world <-> sheet projection.
QJsonObject drawingViewDescription(const Document &doc, Id view);
// Record plus derived state and reasons; cheap (no capture).
QJsonObject drawingItemSummary(const Document &doc, Id item);
// Record plus the full evaluation: paper geometry, measured value, text and state.
QJsonObject drawingItemDescription(const Document &doc, Id item);
// Reattach proposals for a dimension or view-target label; costs one capture.
QJsonObject drawingItemReattachDescription(const Document &doc, Id item);
// Items in sheet order, then bottom first; optionally only one sheet's items.
std::vector<Id> orderedDrawingItems(const Document &doc, Id sheet = 0);
// Views in sheet order, then by ID; optionally only one sheet's views.
std::vector<Id> orderedDrawingViews(const Document &doc, Id sheet = 0);
} // namespace sketchy
