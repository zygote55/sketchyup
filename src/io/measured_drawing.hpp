#pragma once
#include "core/source_credits.hpp"
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
    int precision{fullDisplayPrecision};
    QJsonObject report;
    // R107.c2 (ADR 0181 §3.3): the DEM terrain the page shows. `credits` is what a vector output
    // draws: the technical lines that reach the page. `appearanceCredits` is what a raster
    // appearance shows: the surfaces or lines that reach it.
    SourceCredits credits;
    SourceCredits appearanceCredits;
};
// Orthographic technical line drawing. Page frame comes from the snapshot camera;
// physical scale and page dimensions come from page. No camera near/far crop.
// Appearance requiring raster output is explicitly described in the report.
MeasuredDrawing captureMeasuredDrawing(const RenderSnapshot &snapshot, MeasuredPage page = {});
} // namespace sketchy
