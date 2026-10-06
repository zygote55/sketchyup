#include "app/viewport.hpp"
#include <QNativeGestureEvent>
#include <QWheelEvent>
#include <algorithm>
#include <numbers>
namespace sketchy {
RenderCamera Viewport::renderCamera() const {
    const double yaw = yaw_ * std::numbers::pi / 180;
    const double pitch = pitch_ * std::numbers::pi / 180;
    const Vec3 direction{std::cos(pitch) * std::cos(yaw), std::cos(pitch) * std::sin(yaw),
                         std::sin(pitch)};
    RenderCamera camera;
    camera.target = target_;
    camera.position = camera.target + direction * distance_;
    camera.up = std::abs(pitch_) > 89.999f ? Vec3{0, pitch_ > 0 ? 1. : -1., 0} : Vec3{0, 0, 1};
    camera.orthographic = ortho_;
    camera.verticalFov = fov_ * std::numbers::pi / 180;
    camera.yMag = distance_ * .45;
    camera.nearClip = ortho_ ? .01 : std::max(.001f, distance_ / 10000);
    camera.farClip = std::max(1000.f, distance_ * 10);
    return camera;
}
void Viewport::cameraChanged() {
    // Keep floats near the view without re-uploading geometry for every small pan.
    constexpr double cell = 16;
    const Vec3 origin{std::round(target_.x / cell) * cell,
                      std::round(target_.y / cell) * cell,
                      std::round(target_.z / cell) * cell};
    if (origin != renderOrigin_) {
        renderOrigin_ = origin;
        cacheDirty_ = gridDirty_ = transparentDirty_ = true;
        pickDirty_ = overlayDirty_ = assistantPreviewDirty_ = benchmarkDirty_ = true;
    }
    inference_ = {};
    directions_.clear();
    hoverReference_.reset();
    if (hover_) {
        hover_.reset();
        overlayDirty_ = true;
    }
    update();
    emit navigationChanged();
}
void Viewport::setOrthographic(bool enabled) {
    if (ortho_ == enabled)
        return;
    // Keep the target-plane scale when changing projection.
    const auto scale = std::tan(fov_ * std::numbers::pi / 360);
    distance_ = std::clamp(double(distance_) * (enabled ? scale / .45 : .45 / scale), .05, 1e7);
    ortho_ = enabled;
    cameraChanged();
}
void Viewport::setFieldOfView(double degrees) {
    if (!std::isfinite(degrees) || degrees < 5 || degrees > 120)
        throw std::runtime_error("Field of view must be between 5 and 120 degrees");
    fov_ = degrees;
    cameraChanged();
}
void Viewport::setTrackpadNavigation(bool enabled) {
    trackpad_ = enabled;
    emit navigationChanged();
    emit message(enabled
                     ? "Trackpad: two fingers pan · Alt-scroll orbits · Ctrl-scroll or pinch zooms"
                     : "Mouse: middle drag orbits · Shift/right drag pans · Wheel zooms");
}
void Viewport::standardView(int view) {
    if (view < 0 || view > 7)
        throw std::runtime_error("Unknown standard view");
    setOrthographic(view != 0);
    static constexpr float angles[8][2] = {{-45, 35}, {-90, 90}, {-90, 0},   {0, 0},
                                           {90, 0},   {180, 0},  {-90, -90}, {-45, 35}};
    yaw_ = angles[view][0];
    pitch_ = angles[view][1];
    cameraChanged();
}
void Viewport::panCamera(QPointF position, QPointF delta) {
    const auto [origin, direction] = ray(position);
    const auto [previous, previousDirection] = ray(position - delta);
    const auto [center, normal] = ray(QPointF(width() * .5, height() * .5));
    const Vec3 target = target_;
    auto onPlane = [&](Vec3 point, Vec3 vector) {
        return point + vector * (dot(target - point, normal) / dot(vector, normal));
    };
    const auto shift = onPlane(previous, previousDirection) - onPlane(origin, direction);
    target_ = target_ + shift;
    cameraChanged();
}
void Viewport::orbitCamera(QPointF delta) {
    yaw_ = std::remainder(yaw_ - delta.x() * .4, 360.);
    pitch_ = std::clamp(pitch_ + delta.y() * .4, -89., 89.);
    cameraChanged();
}
void Viewport::zoomCamera(QPointF position, double factor) {
    if (!std::isfinite(factor) || factor <= 0)
        return;
    const auto [center, normal] = ray(QPointF(width() * .5, height() * .5));
    const Vec3 target = target_;
    auto onPlane = [&] {
        const auto [origin, direction] = ray(position);
        return origin + direction * (dot(target - origin, normal) / dot(direction, normal));
    };
    const auto before = onPlane();
    distance_ = std::clamp(double(distance_) * factor, .05, 1e7);
    const auto shift = before - onPlane();
    target_ = target_ + shift;
    cameraChanged();
}
void Viewport::wheelEvent(QWheelEvent *event) {
    const bool pixel = !event->pixelDelta().isNull();
    const QPointF delta = pixel ? QPointF(event->pixelDelta()) : QPointF(event->angleDelta()) / 8;
    if (trackpad_ && event->modifiers().testFlag(Qt::AltModifier))
        orbitCamera(delta);
    else if (trackpad_ && !event->modifiers().testFlag(Qt::ControlModifier))
        panCamera(event->position(), delta);
    else {
        const auto steps = pixel ? event->pixelDelta().y() / 15. : event->angleDelta().y() / 120.;
        zoomCamera(event->position(), std::exp(std::clamp(-steps * .12, -2., 2.)));
    }
    toolPressed_ = dragCommit_ = false;
    selectionPressed_ = selectingBox_ = false;
    event->accept();
}
bool Viewport::nativeNavigation(QEvent *event) {
    if (event->type() != QEvent::NativeGesture)
        return false;
    const auto *gesture = static_cast<QNativeGestureEvent *>(event);
    switch (gesture->gestureType()) {
    case Qt::BeginNativeGesture:
    case Qt::EndNativeGesture:
        break;
    case Qt::ZoomNativeGesture:
        zoomCamera(gesture->position(), 1 / std::max(.01, 1 + gesture->value()));
        break;
    case Qt::PanNativeGesture:
        panCamera(gesture->position(), gesture->delta());
        break;
    case Qt::RotateNativeGesture:
        orbitCamera(QPointF(-gesture->value() / .4, 0));
        break;
    default:
        return false;
    }
    // Camera gestures preserve the tool anchor, but cannot finish a button drag.
    toolPressed_ = dragCommit_ = false;
    selectionPressed_ = selectingBox_ = false;
    event->accept();
    return true;
}
} // namespace sketchy
