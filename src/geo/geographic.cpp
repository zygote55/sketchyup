#include "geo/geographic.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <complex>
#include <stdexcept>
#include <string>
#include <utility>

namespace sketchy {
namespace {

constexpr double pi = 3.14159265358979323846;
constexpr double degree = pi / 180;
// The WGS84 ellipsoid (ADR 0181 §2.2 and §2.3).
constexpr double semiMajor = 6378137;
constexpr double inverseFlattening = 298.257223563;
// A longitude may be this far outside a zone's band before the refusal, for rounding at an edge.
constexpr double bandTolerance = 1e-9;

// The Krüger series to order six, in the flattening n = f / (2 - f), in the form Karney (2011)
// evaluates to nanometres: alpha for the forward series and beta for the inverse. The rectifying
// radius is A, in metres, before the central scale.
struct Series {
    double eccentricity{}, rectifying{};
    std::array<double, 6> alpha{}, beta{};
};

const Series &series() {
    static const Series result = [] {
        Series s;
        const double f = 1 / inverseFlattening;
        const double n = f / (2 - f);
        const double n2 = n * n, n3 = n2 * n, n4 = n3 * n, n5 = n4 * n, n6 = n5 * n;
        s.eccentricity = std::sqrt(f * (2 - f));
        s.rectifying = semiMajor / (1 + n) * (1 + n2 / 4 + n4 / 64 + n6 / 256);
        // The forward series, zeta = zeta' + sum alpha_j sin(2 j zeta') (Krüger's α).
        s.alpha = {
            n / 2 - 2 * n2 / 3 + 5 * n3 / 16 + 41 * n4 / 180 - 127 * n5 / 288 + 7891 * n6 / 37800,
            13 * n2 / 48 - 3 * n3 / 5 + 557 * n4 / 1440 + 281 * n5 / 630 - 1983433 * n6 / 1935360,
            61 * n3 / 240 - 103 * n4 / 140 + 15061 * n5 / 26880 + 167603 * n6 / 181440,
            49561 * n4 / 161280 - 179 * n5 / 168 + 6601661 * n6 / 7257600,
            34729 * n5 / 80640 - 3418889 * n6 / 1995840,
            212378941 * n6 / 319334400};
        // The inverse series, zeta' = zeta - sum beta_j sin(2 j zeta) (Krüger's β).
        s.beta = {n / 2 - 2 * n2 / 3 + 37 * n3 / 96 - n4 / 360 - 81 * n5 / 512 +
                      96199 * n6 / 604800,
                  n2 / 48 + n3 / 15 - 437 * n4 / 1440 + 46 * n5 / 105 - 1118711 * n6 / 3870720,
                  17 * n3 / 480 - 37 * n4 / 840 - 209 * n5 / 4480 + 5569 * n6 / 90720,
                  4397 * n4 / 161280 - 11 * n5 / 504 - 830251 * n6 / 7257600,
                  4583 * n5 / 161280 - 108847 * n6 / 3991680,
                  20648693 * n6 / 638668800};
        return s;
    }();
    return result;
}

std::string number(double value) {
    std::string text = std::to_string(value);
    text.erase(text.find_last_not_of('0') + 1);
    if (!text.empty() && text.back() == '.')
        text.pop_back();
    return text;
}

// Wraps a difference of longitudes into [-180, 180).
double wrapDegrees(double degrees) { return degrees - 360 * std::floor((degrees + 180) / 360); }

double centralMeridian(int zone) { return -183 + 6.0 * zone; }

void requirePosition(const GeoPosition &position) {
    if (!std::isfinite(position.latitude) || !std::isfinite(position.longitude))
        throw std::runtime_error("Latitude and longitude must be finite numbers");
    if (position.latitude < utmMinLatitude || position.latitude > utmMaxLatitude)
        throw std::runtime_error(
            "Latitude " + number(position.latitude) +
            "° is outside the UTM latitude band, 80°S to 84°N (ADR 0181 §2.2)");
    if (position.longitude < -180 || position.longitude > 180)
        throw std::runtime_error("Longitude " + number(position.longitude) +
                                 "° must be between -180° and 180°");
}

// The Norway and Svalbard zone exceptions. Their zone boundaries are not verified, so a position
// in either is refused by name in both directions (ADR 0181 §2.2).
void refuseZoneException(double latitude, double longitude) {
    if (latitude >= 56 && latitude < 64 && longitude >= 3 && longitude < 12)
        throw UtmRangeRefusal(UtmRangeRefusal::Kind::Exception,
                              "This position is in the Norway zone exception (56° to 64°N, 3° "
                              "to 12°E), which SketchyUp refuses: its zone boundaries are not "
                              "verified (ADR 0181 §2.2)",
                              "Norway");
    if (latitude >= 72 && latitude < 84 && longitude >= 0 && longitude < 42)
        throw UtmRangeRefusal(UtmRangeRefusal::Kind::Exception,
                              "This position is in the Svalbard zone exception (72° to 84°N, 0° "
                              "to 42°E), which SketchyUp refuses: its zone boundaries are not "
                              "verified (ADR 0181 §2.2)",
                              "Svalbard");
}

// The zone to project in.
int resolveZone(const GeoPosition &position, int zone) {
    requirePosition(position);
    if (zone == 0)
        zone = utmZoneOfLongitude(position.longitude);
    if (zone < 1 || zone > 60)
        throw std::runtime_error("UTM zone must be between 1 and 60");
    refuseZoneException(position.latitude, position.longitude);
    const double longitude = position.longitude;
    const auto offset = wrapDegrees(longitude - centralMeridian(zone));
    if (std::abs(offset) > utmZoneHalfWidth + bandTolerance)
        throw std::runtime_error("Longitude " + number(longitude) +
                                 "° is outside the 6° band of UTM zone " + std::to_string(zone));
    return zone;
}

// The Krüger series for the northern hemisphere, in units of the rectifying radius. The
// latitude is in [0, 84°] and the longitude from the central meridian is within 3°.
struct NorthSeries {
    double xi{}, eta{}, convergence{}; // convergence in radians
};

NorthSeries projectNorth(double phi, double lambda) {
    const auto &s = series();
    const double e = s.eccentricity, e2 = e * e;
    const double tau = std::tan(phi);
    const double sqrtTau = std::sqrt(1 + tau * tau);
    const double sigma = std::sinh(e * std::atanh(e * tau / sqrtTau));
    // tau' is tan of the conformal latitude.
    const double tauPrime = tau * std::sqrt(1 + sigma * sigma) - sigma * sqrtTau;
    const double sqrtTauPrime = std::sqrt(1 + tauPrime * tauPrime);
    const double cosLambda = std::cos(lambda), sinLambda = std::sin(lambda);
    const double xiPrime = std::atan2(tauPrime, cosLambda);
    const double etaPrime = std::asinh(sinLambda / std::hypot(tauPrime, cosLambda));
    NorthSeries result{xiPrime, etaPrime, 0};
    for (int j = 1; j <= 6; ++j) {
        const double alpha = s.alpha[std::size_t(j - 1)];
        result.xi += alpha * std::sin(2 * j * xiPrime) * std::cosh(2 * j * etaPrime);
        result.eta += alpha * std::cos(2 * j * xiPrime) * std::sinh(2 * j * etaPrime);
    }
    // The convergence is the direction of the meridian in the grid: d(xi + i eta)/d phi, by the
    // chain rule through zeta = zeta' + sum alpha_j sin(2 j zeta') and through tau'(tau).
    using Complex = std::complex<double>;
    const Complex zetaPrime(xiPrime, etaPrime);
    Complex dZetaDZetaPrime = 1.0;
    for (int j = 1; j <= 6; ++j)
        dZetaDZetaPrime += 2.0 * j * s.alpha[std::size_t(j - 1)] * std::cos(2.0 * j * zetaPrime);
    const double hypotSquared = tauPrime * tauPrime + cosLambda * cosLambda;
    const Complex dZetaPrimeDTauPrime(cosLambda / hypotSquared,
                                      -sinLambda * tauPrime / (sqrtTauPrime * hypotSquared));
    // d tau' / d tau is (1 - e²) sqrt(1 + tau²) sqrt(1 + tau'²) / (1 + (1 - e²) tau²).
    const double oneMinusE2 = 1 - e2;
    const double dTauPrimeDPhi =
        oneMinusE2 * (1 + tau * tau) * sqrtTau * sqrtTauPrime / (1 + oneMinusE2 * tau * tau);
    const Complex dZeta = dZetaDZetaPrime * dZetaPrimeDTauPrime * dTauPrimeDPhi;
    result.convergence = std::atan2(-dZeta.imag(), dZeta.real());
    return result;
}

// The inverse of projectNorth: the latitude in [0, 84°] and the longitude from the central
// meridian, from xi and eta in units of the rectifying radius.
std::pair<double, double> unprojectNorth(double xi, double eta) {
    const auto &s = series();
    const double e = s.eccentricity, oneMinusE2 = 1 - e * e;
    double xiPrime = xi, etaPrime = eta;
    for (int j = 1; j <= 6; ++j) {
        const double beta = s.beta[std::size_t(j - 1)];
        xiPrime -= beta * std::sin(2 * j * xi) * std::cosh(2 * j * eta);
        etaPrime -= beta * std::cos(2 * j * xi) * std::sinh(2 * j * eta);
    }
    const double tauPrime = std::sin(xiPrime) / std::hypot(std::sinh(etaPrime), std::cos(xiPrime));
    const double lambda = std::atan2(std::sinh(etaPrime), std::cos(xiPrime));
    // Newton's method on tau for the known tau'. The step is exact to rounding in a few passes.
    double tau = tauPrime;
    for (int iteration = 0; iteration < 16; ++iteration) {
        const double sqrtTau = std::sqrt(1 + tau * tau);
        const double sigma = std::sinh(e * std::atanh(e * tau / sqrtTau));
        const double candidate = tau * std::sqrt(1 + sigma * sigma) - sigma * sqrtTau;
        const double step = (tauPrime - candidate) * (1 + oneMinusE2 * tau * tau) /
                            (oneMinusE2 * sqrtTau * std::sqrt(1 + candidate * candidate));
        tau += step;
        if (std::abs(step) <= 1e-15 * std::max(1.0, std::abs(tau)))
            break;
    }
    return {std::atan(tau), lambda};
}

struct Projection {
    UtmPosition position;
    double convergence{}; // degrees
};

Projection project(const GeoPosition &position, int zone) {
    zone = resolveZone(position, zone);
    const double offset = wrapDegrees(position.longitude - centralMeridian(zone)) * degree;
    const bool north = position.latitude >= 0;
    const auto grid = projectNorth(std::abs(position.latitude) * degree, offset);
    const double scale = utmCentralScale * series().rectifying;
    // The southern grid is the mirror image of the northern one across the equator.
    const double convergence = grid.convergence / degree;
    return {{zone, north, utmFalseEasting + scale * grid.eta,
             north ? scale * grid.xi : utmSouthernFalseNorthing - scale * grid.xi},
            north ? convergence : -convergence};
}

} // namespace

std::string_view geoDatumLabel(std::string_view label) {
    if (label == geoDatumWgs84)
        return geoDatumWgs84;
    if (label == geoDatumNad83)
        return geoDatumNad83;
    if (label == "OSGB36")
        throw std::runtime_error("Datum OSGB36 is refused: SketchyUp has no national grid "
                                 "transformation for it (ADR 0181 §2.3)");
    if (label == "ETRS89")
        throw std::runtime_error("Datum ETRS89 is refused: no transformation has been read and "
                                 "approved for it (ADR 0181 §2.3)");
    throw std::runtime_error("Datum must be WGS84, or NAD83 as a labelled approximation "
                             "(ADR 0181 §2.3)");
}

int utmZoneOfLongitude(double longitude) {
    if (!std::isfinite(longitude) || longitude < -180 || longitude > 180)
        throw std::runtime_error("Longitude " + number(longitude) +
                                 "° must be between -180° and 180°");
    return int(std::floor((longitude + 180) / 6)) % 60 + 1;
}

UtmPosition geoToUtm(const GeoPosition &position, int zone) {
    return project(position, zone).position;
}

double utmGridConvergence(const GeoPosition &position, int zone) {
    return project(position, zone).convergence;
}

GeoPosition utmToGeo(const UtmPosition &position) {
    if (position.zone < 1 || position.zone > 60)
        throw std::runtime_error("UTM zone must be between 1 and 60");
    if (!std::isfinite(position.easting) || !std::isfinite(position.northing))
        throw std::runtime_error("UTM coordinates must be finite numbers");
    const double scale = utmCentralScale * series().rectifying;
    const double northing =
        position.north ? position.northing : utmSouthernFalseNorthing - position.northing;
    if (northing < 0)
        throw std::runtime_error(position.north
                                     ? "A northern UTM northing cannot be negative"
                                     : "A southern UTM northing cannot exceed 10,000,000 m");
    const auto [phi, lambda] =
        unprojectNorth(northing / scale, (position.easting - utmFalseEasting) / scale);
    const double offsetDegrees = lambda / degree;
    if (std::abs(offsetDegrees) > utmZoneHalfWidth + bandTolerance)
        throw UtmRangeRefusal(UtmRangeRefusal::Kind::Band,
                              "UTM position is outside the 6° band of zone " +
                                  std::to_string(position.zone));
    const double latitude = (position.north ? phi : -phi) / degree;
    if (latitude < utmMinLatitude || latitude > utmMaxLatitude)
        throw UtmRangeRefusal(UtmRangeRefusal::Kind::Latitude,
                              "UTM position is outside the UTM latitude band, 80°S to 84°N "
                              "(ADR 0181 §2.2)");
    const double longitude = wrapDegrees(centralMeridian(position.zone) + offsetDegrees);
    refuseZoneException(latitude, longitude);
    return {latitude, longitude};
}

double geoGroundDistance(const GeoPosition &a, const GeoPosition &b) {
    const double e2 = series().eccentricity * series().eccentricity;
    const double phiMid = (a.latitude + b.latitude) / 2 * degree;
    const double w2 = 1 - e2 * std::sin(phiMid) * std::sin(phiMid);
    const double meridional = semiMajor * (1 - e2) / (w2 * std::sqrt(w2));
    const double prime = semiMajor / std::sqrt(w2);
    const double dPhi = (b.latitude - a.latitude) * degree;
    const double dLambda = wrapDegrees(b.longitude - a.longitude) * degree;
    return std::hypot(dPhi * meridional, dLambda * prime * std::cos(phiMid));
}

} // namespace sketchy
