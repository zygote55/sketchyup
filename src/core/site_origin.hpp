#pragma once
// The document site origin, apart from the terrain records (core/terrain_records.hpp) so that
// core/model.hpp needs only this (build fan-out, ADR 0170).
#include <array>
#include <cstddef>
#include <optional>
#include <string>
#include <string_view>

namespace sketchy {

// Document site origin (ADR 0168 §4): projected metres; the CRS is a label only.
struct SiteProvenance {
    std::string source, sha256;
    bool operator==(const SiteProvenance &) const = default;
};

// The optional geographic record (ADR 0181 §2.1, schema 32): where the site origin is on the
// Earth. Its UTM position in its zone is the origin's easting and northing, to within
// siteGeographicToleranceMetres (validateSiteOrigin checks it), so the record and the origin
// cannot disagree.
struct SiteGeographicProvenance {
    std::string kind, source, sha256; // kind: "typed", "geocoded" or "imported"
    bool operator==(const SiteGeographicProvenance &) const = default;
};

struct SiteGeographic {
    double latitude{}, longitude{}; // WGS84 decimal degrees, within the UTM latitude band
    std::string datum;              // "WGS84", or "NAD83" as a labelled approximation
    int utmZone{};                  // 1 to 60
    bool utmNorth{true};            // the hemisphere of the zone grid
    double convergence{};           // grid convergence at the origin, degrees (ADR 0181 §2.6)
    std::string verticalReference;  // one of siteVerticalReferences
    SiteGeographicProvenance provenance;
    bool operator==(const SiteGeographic &) const = default;
};

inline constexpr std::array<std::string_view, 5> siteVerticalReferences{
    "unknown", "ellipsoid", "EGM96", "EGM2008", "NAVD88"};
inline constexpr std::array<std::string_view, 3> siteProvenanceKinds{"typed", "geocoded",
                                                                     "imported"};
// The origin and its geographic position agree to this distance on the ground (ADR 0181 §2.1).
inline constexpr double siteGeographicToleranceMetres = 1;

struct SiteOrigin {
    double easting{}, northing{}, elevation{};
    std::string crs;
    SiteProvenance established;
    std::optional<SiteGeographic> geographic; // schema 32 (ADR 0181 §2.1) when present
    bool operator==(const SiteOrigin &) const = default;
};

// Site coordinates are bounded so that model = site - origin stays exact to well
// below the 1e-7 m tolerance; projected CRSs stay far inside this range.
inline constexpr double siteCoordinateLimit = 1e8;
inline constexpr std::size_t siteCrsBytes = 64, siteSourceBytes = 256;

void validateSiteOrigin(const SiteOrigin &origin);

} // namespace sketchy
