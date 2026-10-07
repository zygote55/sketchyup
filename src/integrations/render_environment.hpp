#pragma once
#include <QByteArray>
#include <QJsonObject>
#include <QString>
namespace sketchy {
// Immutable, bounded Radiance RGBE equirectangular image captured for one render.
struct RenderEnvironment {
    QByteArray image;
    int width{}, height{};
    double strength{1}, rotationDegrees{};
};
RenderEnvironment readRenderEnvironment(const QString &path, double strength = 1,
                                        double rotationDegrees = 0);
void validateRenderEnvironment(const RenderEnvironment &environment);
QJsonObject describeRenderEnvironment(const RenderEnvironment &environment);
} // namespace sketchy
