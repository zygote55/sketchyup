#pragma once
#include "core/drawing_item_records.hpp"
#include "core/model.hpp"
#include "io/drawing_items.hpp"
#include <QJsonObject>
#include <QPointF>
#include <QRectF>
#include <QString>
namespace sketchy {
// Shared helpers of the R091.g page-item tools, Tray and window: kind names, the command
// form of an item's data, and moves that keep stored geometry on the paper.
QString sheetItemKindName(DrawingItemKind kind);     // "Text", "Title block"
const char *sheetItemKindCode(DrawingItemKind kind); // "text", "titleBlock"
// The item's data in its stored JSON form.
QJsonObject sheetItemData(const DrawingItemRecord &record);
// The drawing_item.update data that turns `before` into `after`: only the fields that
// changed, title-block fields one by one. A dimension's view and anchors and a label's
// view target are never written here (they change only through drawing_item.reattach).
QJsonObject sheetItemChanges(const DrawingItemRecord &before, const DrawingItemRecord &after);
// The command form of a vertex or edge anchor ({kind, body, vertex} or {kind, body, edge,
// fraction}).
QJsonObject sheetAnchorInput(const AnnotationAnchor &anchor);
// The item moved by a paper delta, clamped so its stored geometry stays on the paper. A
// dimension's line moves by changing its offset (its anchors stay); a label bound to a view
// moves its text (the leader tip stays on the model point).
DrawingItemRecord movedSheetItem(const Document &doc, const DrawingItemRecord &record,
                                 QPointF deltaMm, const DrawingPaper &paper);
// The top-left of an item's stored geometry, for typed [x,y] moves.
QPointF sheetItemOrigin(const DrawingItemRecord &record);
// "Text · General notes", "Dimension · 6000", "Title block · House extension".
QString sheetItemSummary(const DrawingItemRecord &record, const DrawingItemEvaluation *evaluation);
// "Current", or "Broken: reference missing".
QString sheetItemStateText(const DrawingItemEvaluation &evaluation);
} // namespace sketchy
