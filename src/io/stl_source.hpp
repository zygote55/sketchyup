#pragma once
#include "geometry/surface.hpp"
#include <QByteArray>
#include <QJsonObject>
#include <QStringList>
namespace sketchy {
enum class StlUpAxis { Y, Z };
struct StlCoordinateOptions {
    double metresPerUnit{1};
    StlUpAxis up{StlUpAxis::Z};
    void validate() const;
};
struct StlFacet {
    std::array<Vec3, 3> vertices;
    Vec3 normal;
    size_t solid{};
    std::uint16_t attribute{};
};
struct StlSource {
    std::vector<StlFacet> facets;
    QStringList solids;
    QJsonObject report;
};
// Strict, bounded binary/ASCII parsing. Units and axis are supplied explicitly.
StlSource parseStl(const QByteArray &bytes, StlCoordinateOptions options);
} // namespace sketchy
