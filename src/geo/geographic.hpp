#pragma once
// R107.a (ADR 0181 §2): offline WGS84 ↔ UTM coordinates. The Krüger series of Karney (2011),
// to order 6, is written in this repository: no PROJ, no grid files and no network (ADR 0181
// §2.2, owner decision 13). Qt-free, so the document, the file format and the tests use it alone.
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>

namespace sketchy {

// A position on the WGS84 ellipsoid, in decimal degrees.
struct GeoPosition {
    double latitude{}, longitude{};
    bool operator==(const GeoPosition &) const = default;
};

// A position the UTM range checks refuse (ADR 0181 §2.2). It is a std::runtime_error whose message
// names the refusal; kind() says which range it left, so a caller can name it to the person. The
// name of an exception is "Norway" or "Svalbard".
class UtmRangeRefusal : public std::runtime_error {
  public:
    enum class Kind { Band, Latitude, Exception };
    UtmRangeRefusal(Kind kind, const std::string &message, std::string name = {})
        : std::runtime_error(message), kind_(kind), name_(std::move(name)) {}
    Kind kind() const { return kind_; }
    const std::string &name() const { return name_; }

  private:
    Kind kind_;
    std::string name_;
};

// A UTM position: the zone (1 to 60), the hemisphere, and the grid coordinates in metres with
// the false easting (500,000 m) and the false northing (0 m north, 10,000,000 m south).
struct UtmPosition {
    int zone{};
    bool north{true};
    double easting{}, northing{};
    bool operator==(const UtmPosition &) const = default;
};

// Datum labels (ADR 0181 §2.3, owner decision 5). WGS84 is the only datum typed in. NAD83 is
// accepted as a labelled approximation, which the caller warns about. Any other label is refused
// with its reason named.
inline constexpr std::string_view geoDatumWgs84 = "WGS84", geoDatumNad83 = "NAD83";
// The canonical label of an accepted datum; throws std::runtime_error naming any refusal.
std::string_view geoDatumLabel(std::string_view label);

// UTM constants. The central scale is applied to the grid only; model geometry is never scaled
// by it (ADR 0181 §2.5, owner decision 13).
inline constexpr double utmCentralScale = 0.9996;
inline constexpr double utmFalseEasting = 500000, utmSouthernFalseNorthing = 10000000;
// The UTM latitude range (ADR 0181 §2.2): 80°S to 84°N.
inline constexpr double utmMinLatitude = -80, utmMaxLatitude = 84;
// A zone is a 6° band: a longitude is in zone z when it is within 3° of the zone's central
// meridian. The band is closed for a zone the user chooses; the automatic zone is half-open.
inline constexpr double utmZoneHalfWidth = 3;

// The zone of a longitude, floor((longitude + 180) / 6) + 1 (ADR 0181 §2.2).
int utmZoneOfLongitude(double longitude);

// Geographic to UTM. A zone of 0 takes the zone of the longitude; any other zone must hold the
// longitude within its 6° band. Refuses (throws) a latitude outside 80°S to 84°N, a longitude
// outside ±180°, a zone outside 1 to 60, a point in the Norway or Svalbard zone exceptions
// (their boundaries are not verified, ADR 0181 §2.2) and a longitude outside the chosen band.
UtmPosition geoToUtm(const GeoPosition &position, int zone = 0);

// UTM to geographic. Refuses a zone outside 1 to 60, a northing on the wrong side of the
// equator for its hemisphere, a result outside the UTM latitude band and a result outside the
// zone's 6° band.
GeoPosition utmToGeo(const UtmPosition &position);

// Grid convergence γ in degrees, at a position in the zone (0 for the automatic zone). Grid
// north lies γ clockwise (east) of true north, so the value is positive east of the central
// meridian in the northern hemisphere. The model is never rotated by it (ADR 0181 §2.6).
double utmGridConvergence(const GeoPosition &position, int zone = 0);

// Distance in metres on the WGS84 ellipsoid between two nearby positions, from the local
// meridional and prime-vertical radii. It is for comparing locations (a 1 m tolerance), not
// for surveying across a large region.
double geoGroundDistance(const GeoPosition &a, const GeoPosition &b);

} // namespace sketchy
