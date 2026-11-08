#pragma once
#include "geometry/surface.hpp"
#include <QByteArray>
#include <QJsonObject>
#include <QString>
#include <cstddef>
#include <map>
#include <optional>
namespace sketchy {
// The most bytes of a DXF file (R113, D-102). An export of 893,345 segments at the materialized
// edge limit measured 68 MB, so 64 MiB refused a model within the published limits. R113 set
// 256 MiB; R115 sets 2 GiB, eight times that.
inline constexpr std::size_t dxfByteLimit = 2048ull * 1024 * 1024;
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
