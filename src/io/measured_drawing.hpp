#pragma once
#include "geometry/measured_view.hpp"
#include "integrations/render_snapshot.hpp"
namespace sketchy {
struct MeasuredSource {
    Id body{}, edge{}, section{};
};
struct MeasuredAnnotation {
    AnnotationRecord record;
    AnnotationMeasurement measurement; // World-space values and associative state.
    std::vector<Vec3> anchors;         // Page millimetres; z is world depth.
    Vec3 textPoint;
};
struct MeasuredDrawing {
    MeasuredPage page;
    MeasuredLines geometry;
    std::vector<MeasuredSource> sources;
    std::vector<MeasuredAnnotation> annotations;
    DisplayUnit units{DisplayUnit::Meters};
    QJsonObject report;
};
// Orthographic technical line drawing. Page frame comes from the snapshot camera;
// physical scale and page dimensions come from page. No camera near/far crop.
// Appearance requiring raster output is explicitly described in the report.
MeasuredDrawing captureMeasuredDrawing(const RenderSnapshot &snapshot, MeasuredPage page = {});
} // namespace sketchy
