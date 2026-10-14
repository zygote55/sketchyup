#pragma once
#include "core/drawing_item_records.hpp"
#include <QJsonArray>
#include <QJsonValue>
namespace sketchy {
// The schema 27 drawingItems table (ADR 0164 §3). Records are written in ID order as
// {id, sheet, z, kind, data}; every field is required, unknown fields are rejected,
// identities are canonical nonzero decimal strings and anchors use the ADR 0080
// anchor object.
QJsonArray encodeDrawingItems(const DrawingItemRecords &items);
// Decodes the table's shape. The records are validated against the sheets, views and
// allocator by validateDrawingSheetRecords when the document is restored.
DrawingItemRecords decodeDrawingItems(const QJsonValue &items);
} // namespace sketchy
