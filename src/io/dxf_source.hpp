#pragma once
#include "geometry/surface.hpp"
#include <QByteArray>
#include <QJsonObject>
#include <QString>
#include <map>
#include <optional>
namespace sketchy {
struct DxfOptions {
    // If absent, a supported non-unitless $INSUNITS is required.
    std::optional<double> metresPerUnit;
};
struct DxfSegment {
    Vec3 start, end, center;
    // Zero denotes a line; otherwise signed radians around +Z.
    double sweep{};
};
struct DxfEntity {
    QString type, layer;
    std::vector<DxfSegment> segments;
    bool closed{};
};
struct DxfLayer {
    QString name;
    bool hidden{}, locked{};
};
struct DxfSource {
    std::vector<DxfEntity> entities;
    std::map<QString, DxfLayer> layers;
    double metresPerUnit{};
    QJsonObject report;
};
// Bounded ASCII DXF, world XY plane only; no blocks, executable content or sidecars.
DxfSource parseDxf(const QByteArray &bytes, DxfOptions options = {});
} // namespace sketchy
