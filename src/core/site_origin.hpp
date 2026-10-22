#pragma once
// The document site origin, apart from the terrain records (core/terrain_records.hpp) so that
// core/model.hpp needs only this (build fan-out, ADR 0170).
#include <cstddef>
#include <string>
namespace sketchy {
// Document site origin (ADR 0168 §4): projected metres; the CRS is a label only.
struct SiteProvenance {
    std::string source, sha256;
    bool operator==(const SiteProvenance &) const = default;
};
struct SiteOrigin {
    double easting{}, northing{}, elevation{};
    std::string crs;
    SiteProvenance established;
    bool operator==(const SiteOrigin &) const = default;
};
// Site coordinates are bounded so that model = site - origin stays exact to well
// below the 1e-7 m tolerance; projected CRSs stay far inside this range.
inline constexpr double siteCoordinateLimit = 1e8;
inline constexpr std::size_t siteCrsBytes = 64, siteSourceBytes = 256;
void validateSiteOrigin(const SiteOrigin &origin);
} // namespace sketchy
