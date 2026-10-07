#pragma once
#include "io/measured_drawing.hpp"
#include <QImage>
namespace sketchy {
enum class MeasuredFormat { Svg, Pdf };
struct MeasuredExport {
    QByteArray bytes;
    QJsonObject report;
};
// Requires QGuiApplication for font shaping. The caller explicitly selects technical
// lines, or supplies a full-page appearance raster captured at this exact framing.
MeasuredExport exportMeasuredDrawing(const MeasuredDrawing &drawing, MeasuredFormat format,
                                     const std::optional<QImage> &pageRaster = {});
void writeMeasuredExport(const MeasuredExport &output, const QString &path);
} // namespace sketchy
