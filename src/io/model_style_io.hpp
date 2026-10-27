#pragma once
#include "core/model_style.hpp"
#include <QJsonObject>
#include <string>
namespace sketchy {
QJsonObject encodeModelStyle(const ModelStyle &style);
// `edgeStrokes` admits the schema-31 `edgeStroke` field; a schema-30 or older payload refuses it.
ModelStyle decodeModelStyle(const QJsonValue &value, bool edgeStrokes = false);
QJsonObject encodeEdgeStroke(const EdgeStroke &stroke);
EdgeStroke decodeEdgeStroke(const QJsonValue &value);

// One labelled edit per edge stroke control (UX_DESIGN §18.3, owner choice 3). Each returns the
// whole style with exactly that control changed, and the history label that names the edit. A
// `document.style` command carries the style and the label (history.label), so each control is one
// undo step. The field edits refuse a style whose stroke is off: turn the stroke on first.
struct StyleEdit {
    ModelStyle style;
    std::string label;
};
// Turning the stroke on copies the profile width into the stroke width, at opacity 1, solid, no
// texture. Turning it off removes the sub-record, which returns the record to its 14 fields.
StyleEdit turnOnEdgeStroke(const ModelStyle &style);
StyleEdit turnOffEdgeStroke(const ModelStyle &style);
// Width in logical pixels (1 to 8) and opacity as a fraction (0.01 to 1): the label reads "%".
StyleEdit setEdgeStrokeWidth(const ModelStyle &style, double width);
StyleEdit setEdgeStrokeOpacity(const ModelStyle &style, double opacity);
StyleEdit setEdgeStrokeDash(const ModelStyle &style, EdgeDash dash);
// A texture of 0 is "None". `name` is the asset's name, quoted in the label.
StyleEdit setEdgeStrokeTexture(const ModelStyle &style, Id texture, const std::string &name);
} // namespace sketchy
