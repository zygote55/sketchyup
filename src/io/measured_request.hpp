#pragma once
#include "io/measured_export.hpp"
namespace sketchy {
struct MeasuredRequest {
    MeasuredPage page;
    MeasuredFormat format{MeasuredFormat::Svg};
    RenderOptions render;
};
// Explicit technical-line export only. Appearance raster capture needs the native viewport.
MeasuredRequest parseMeasuredRequest(const QJsonObject &request);
} // namespace sketchy
