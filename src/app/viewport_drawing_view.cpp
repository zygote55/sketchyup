#include "app/viewport.hpp"
#include <QVariantAnimation>
#include <cmath>
namespace sketchy {
namespace {
QVector3D vector(Vec3 p) { return {float(p.x), float(p.y), float(p.z)}; }
} // namespace
QImage Viewport::renderDrawingViewRaster(const DrawingViewEvaluationInput &input, QSize pixels) {
    if (!input.capture.snapshot)
        throw std::runtime_error("This view has no scene to draw");
    const auto &snapshot = *input.capture.snapshot;
    // The GL renderer draws the live document; the capture must describe the same state.
    if (snapshot.sourceIdentity() != doc_.identity() ||
        snapshot.sourceRevision() != doc_.revision())
        throw std::runtime_error("The model changed since this view was captured");
    if (inspectionBusy() || measuredRasterMatrix_ || !rasterSize_.isEmpty() ||
        (sceneAnimation_ && sceneAnimation_->state() == QAbstractAnimation::Running))
        throw std::runtime_error("Finish the active interaction before drawing sheet views");
    QMatrix4x4 projection, view;
    if (input.projection) {
        const auto &page = input.projection->page;
        // Frame the depth of what the scene shows, as measured raster export does.
        double minimum{}, maximum{};
        size_t points{};
        for (const auto &[id, body] : doc_.bodies()) {
            if (!snapshot.visible(id))
                continue;
            const auto world = doc_.worldTransform(id);
            for (const auto &[vertex, p] : body->surface.vertices) {
                (void)vertex;
                if (++points > 140000)
                    throw std::runtime_error("This view exceeds the raster depth-framing budget");
                const auto z = page.project(world.point(p)).z;
                minimum = std::min(minimum, z);
                maximum = std::max(maximum, z);
            }
        }
        const auto relative = page.origin - renderOrigin_;
        const auto x = page.right, y = page.up, z = page.towardEye;
        view = QMatrix4x4(float(x.x), float(x.y), float(x.z), float(-dot(x, relative)), float(y.x),
                          float(y.y), float(y.z), float(-dot(y, relative)), float(z.x), float(z.y),
                          float(z.z), float(-dot(z, relative)), 0, 0, 0, 1);
        const auto halfWidth = page.widthMm * page.scaleDenominator / 2000;
        const auto halfHeight = page.heightMm * page.scaleDenominator / 2000;
        const auto padding = std::max(1., (maximum - minimum) * .01);
        projection.ortho(float(-halfWidth), float(halfWidth), float(-halfHeight), float(halfHeight),
                         float(-maximum - padding), float(-minimum + padding));
    } else {
        const auto &camera = snapshot.camera();
        const auto aspect = float(pixels.width()) / float(std::max(1, pixels.height()));
        if (camera.orthographic) {
            const auto half = float(camera.yMag);
            projection.ortho(-half * aspect, half * aspect, -half, half, float(camera.nearClip),
                             float(camera.farClip));
        } else
            projection.perspective(float(camera.verticalFov * 180 / M_PI), aspect,
                                   float(std::max(1e-4, camera.nearClip)),
                                   float(std::max(camera.nearClip * 2, camera.farClip)));
        view.lookAt(vector(camera.position - renderOrigin_), vector(camera.target - renderOrigin_),
                    vector(camera.up));
    }
    measuredRasterMatrix_ = projection * view;
    drawingViewRaster_ = true;
    measuredAnnotationScale_ =
        input.projection ? double(pixels.height()) / input.projection->page.heightMm * 25.4 / 96
                         : 1.;
    auto restore = [&] {
        measuredRasterMatrix_.reset();
        drawingViewRaster_ = false;
        measuredAnnotationScale_ = 1;
        profilesDirty_ = true;
        update();
    };
    try {
        auto image = renderRaster(pixels, true);
        restore();
        return image;
    } catch (...) {
        restore();
        throw;
    }
}
} // namespace sketchy
