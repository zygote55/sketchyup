#include "app/viewport.hpp"
#include <QVariantAnimation>
namespace sketchy {
MeasuredExport Viewport::renderMeasuredView(MeasuredPage page, MeasuredFormat format, bool raster,
                                            int dpi) {
    page.validate();
    if (inspectionBusy() || inspectionRenderOverrides() || selection_.showingHidden() ||
        !rasterSize_.isEmpty() || measuredRasterMatrix_ ||
        (sceneAnimation_ && sceneAnimation_->state() == QAbstractAnimation::Running))
        throw std::runtime_error("Finish the active interaction and turn off temporary render "
                                 "overrides before measured export");
    RenderOptions options;
    options.camera = renderCamera();
    options.camera->orthographic = true;
    const auto snapshot = RenderSnapshot::capture(doc_, options, selection_.hiddenEntities());
    const auto drawing = captureMeasuredDrawing(snapshot, page);
    if (!raster)
        return exportMeasuredDrawing(drawing, format);
    if (dpi < 72 || dpi > 300)
        throw std::runtime_error("Raster print resolution must be 72–300 DPI");
    page = drawing.page;
    const QSize pixels(qRound(page.widthMm * dpi / 25.4), qRound(page.heightMm * dpi / 25.4));
    if (pixels.width() < 1 || pixels.height() < 1 || pixels.width() > 8192 ||
        pixels.height() > 8192 || qint64(pixels.width()) * pixels.height() > 16000000)
        throw std::runtime_error(
            "Reduce raster DPI or page size to stay within 8192 pixels per side and 16 megapixels");
    double minimum{}, maximum{};
    size_t points{};
    for (const auto &[id, body] : doc_.bodies())
        if (snapshot.visible(id)) {
            const auto world = doc_.worldTransform(id);
            auto depth = [&](Vec3 p) {
                if (++points > 140000)
                    throw std::runtime_error(
                        "Measured raster exceeds depth-framing geometry budget");
                const auto z = page.project(world.point(p)).z;
                minimum = std::min(minimum, z);
                maximum = std::max(maximum, z);
            };
            for (const auto &[vertex, p] : body->surface.vertices) {
                (void)vertex;
                depth(p);
            }
            if (body->referenceImage)
                for (auto p : referenceImageCorners(*body->referenceImage))
                    depth(p);
        }
    for (const auto &[id, annotation] : doc_.annotations()) {
        (void)id;
        const auto measurement = measureAnnotation(doc_, *annotation);
        for (const auto &anchor : measurement.anchors) {
            const auto z = page.project(anchor.point).z;
            minimum = std::min(minimum, z);
            maximum = std::max(maximum, z);
        }
        const auto z = page.project(measurement.textPoint).z;
        minimum = std::min(minimum, z);
        maximum = std::max(maximum, z);
    }
    const auto relative = page.origin - renderOrigin_;
    const auto x = page.right, y = page.up, z = page.towardEye;
    QMatrix4x4 view(float(x.x), float(x.y), float(x.z), float(-dot(x, relative)), float(y.x),
                    float(y.y), float(y.z), float(-dot(y, relative)), float(z.x), float(z.y),
                    float(z.z), float(-dot(z, relative)), 0, 0, 0, 1);
    QMatrix4x4 projection;
    const auto halfWidth = page.widthMm * page.scaleDenominator / 2000;
    const auto halfHeight = page.heightMm * page.scaleDenominator / 2000;
    const auto padding = std::max(1., (maximum - minimum) * .01);
    projection.ortho(float(-halfWidth), float(halfWidth), float(-halfHeight), float(halfHeight),
                     float(-maximum - padding), float(-minimum + padding));
    measuredRasterMatrix_ = projection * view;
    measuredAnnotationScale_ = double(pixels.height()) / page.heightMm * 25.4 / 96;
    auto restore = [&] {
        measuredRasterMatrix_.reset();
        measuredAnnotationScale_ = 1;
        profilesDirty_ = true;
        update();
    };
    try {
        const auto image = renderRaster(pixels);
        restore();
        return exportMeasuredDrawing(drawing, format, image);
    } catch (...) {
        restore();
        throw;
    }
}
} // namespace sketchy
