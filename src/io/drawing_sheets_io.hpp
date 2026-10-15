#pragma once
#include "core/drawing_sheet_records.hpp"
#include <QJsonArray>
#include <QJsonObject>
#include <QJsonValue>
namespace sketchy {
// Schema 27 drawing-sheet tables (ADR 0163). Records are written in ID order; every
// field is required, unknown fields are rejected and identities are canonical
// nonzero decimal strings.
QJsonArray encodeDrawingSheets(const DrawingSheetRecords &sheets);
QJsonArray encodeDrawingViews(const DrawingViewRecords &views);
// Decodes both tables and validates them together against their allocators.
std::pair<DrawingSheetRecords, DrawingViewRecords>
decodeDrawingSheets(const QJsonValue &sheets, Id nextSheet, const QJsonValue &views, Id nextView);
// The sheet's dimension format (R091.g): {unit: "mm" | "cm" | "m" | "ftIn", decimals: 0–4,
// showUnit}, every field required.
QJsonObject encodeDrawingDimensionFormat(const DrawingDimensionFormat &format);
DrawingDimensionFormat decodeDrawingDimensionFormat(const QJsonValue &value);
} // namespace sketchy
