#pragma once
#include "io/measured_drawing.hpp"
#include <QColor>
#include <QGlyphRun>
#include <QImage>
#include <QPainterPath>
#include <vector>
namespace sketchy {
enum class MeasuredFormat { Svg, Pdf };
struct MeasuredExport {
    QByteArray bytes;
    QJsonObject report;
};
// One painted primitive of a measured drawing, in the drawing's page millimetres
// (for a drawing-sheet view: frame-local, origin at the frame's top-left corner).
struct MeasuredPathPrimitive {
    QPainterPath path;
    QColor color{Qt::black};
    double width{.25}; // Stroke width in millimetres; unused when filled.
    bool fill{}, dashed{};
    QString label; // The annotation text a filled glyph path draws.
    // The label as shaped glyph runs; glyphTransform maps them to page millimetres.
    // The sheet compositor paints them as real PDF text (R092); R078 export ignores them.
    QList<QGlyphRun> glyphs;
    QTransform glyphTransform;
};
struct MeasuredDrawingPaths {
    std::vector<MeasuredPathPrimitive> values; // Paint order.
    qsizetype elements{}, textBytes{}, missingGlyphs{};
};
// The vector paths R078 export paints: hidden then visible lines, and per annotation
// its leader/dimension lines, a white text background and its shaped glyph outlines
// ("DejaVu Sans"). Requires QGuiApplication for font shaping; it touches no painter,
// so it may run on a worker thread. Throws when the R078 element or text budgets are
// exceeded. Pulled forward from R092 for the sheet compositor (ADR 0164 decision 7).
MeasuredDrawingPaths measuredDrawingPaths(const MeasuredDrawing &drawing);
// Requires QGuiApplication for font shaping. The caller explicitly selects technical
// lines, or supplies a full-page appearance raster captured at this exact framing.
MeasuredExport exportMeasuredDrawing(const MeasuredDrawing &drawing, MeasuredFormat format,
                                     const std::optional<QImage> &pageRaster = {});
void writeMeasuredExport(const MeasuredExport &output, const QString &path);
} // namespace sketchy
