// R107.a (ADR 0181 §2.11, V3 and V4): the in-house WGS84 and UTM conversion. Reference values are
// Karney's published transverse Mercator test data (tests/fixtures/geo/karney-tm-reference.txt).
#include "geo/geographic.hpp"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

using namespace sketchy;

namespace {

// ADR 0181 §2.2 states 5 nm for the series within 3,900 km of the central meridian (Karney 2011).
constexpr double referenceMetres = 5e-9;
// ADR 0181 §2.11: the round trip within 1 µm.
constexpr double roundTripMetres = 1e-6;
constexpr double convergenceDegrees = 1e-9;
constexpr double pi = 3.14159265358979323846;

void check(bool value, const std::string &message) {
    if (!value)
        throw std::runtime_error(message);
}

void near(double actual, double expected, double tolerance, const std::string &message) {
    if (!(std::abs(actual - expected) <= tolerance)) {
        std::cerr << message << ": " << actual << " != " << expected << '\n';
        throw std::runtime_error(message);
    }
}

// Refusals are checked by their named reason, so a wrong refusal fails the test.
template <class F> std::string refusal(F action, const std::string &message) {
    try {
        action();
    } catch (const std::runtime_error &error) {
        return error.what();
    }
    throw std::runtime_error(message);
}

// The Norway and Svalbard exception areas, widened by a margin in degrees.
bool inExceptionArea(double latitude, double longitude, double margin) {
    return (latitude >= 56 - margin && latitude < 64 + margin && longitude >= 3 - margin &&
            longitude < 12 + margin) ||
           (latitude >= 72 - margin && latitude < 84 + margin && longitude >= -margin &&
            longitude < 42 + margin);
}

bool contains(const std::string &text, const std::string &part) {
    return text.find(part) != std::string::npos;
}

struct Reference {
    double latitude, longitude, easting, northing, convergence, scale;
};

// The published rows: longitude is from the central meridian, the grid is on the 0.9996 scale.
std::vector<Reference> references() {
    std::ifstream file(GEO_FIXTURE_PATH);
    check(bool(file), "The Karney reference fixture is readable");
    std::vector<Reference> rows;
    std::string line;
    while (std::getline(file, line)) {
        if (line.empty() || line.front() == '#')
            continue;
        std::istringstream fields(line);
        Reference row{};
        fields >> row.latitude >> row.longitude >> row.easting >> row.northing >> row.convergence >>
            row.scale;
        check(bool(fields), "Each reference row has six values");
        rows.push_back(row);
    }
    check(rows.size() >= 20, "The reference fixture covers the latitude range");
    return rows;
}

// The published northern row in a zone, and its mirror in zone 56 (153°E). Zone 20 (63°W) keeps the
// rows clear of the Norway and Svalbard exceptions, which the rows at 3°E would fall into.
void projectReference(const Reference &row, bool south, int zone, double &maxMetres,
                      double &maxConvergence) {
    const double centralMeridian = -183 + 6.0 * zone;
    const double latitude = south ? -row.latitude : row.latitude;
    const UtmPosition utm = geoToUtm({latitude, centralMeridian + row.longitude}, zone);
    check(utm.zone == zone && utm.north == !south,
          "The reference point is in its zone and hemisphere");
    const double easting = utmFalseEasting + row.easting;
    const double northing = south ? utmSouthernFalseNorthing - row.northing : row.northing;
    maxMetres =
        std::max({maxMetres, std::abs(utm.easting - easting), std::abs(utm.northing - northing)});
    const double convergence = south ? -row.convergence : row.convergence;
    maxConvergence =
        std::max(maxConvergence,
                 std::abs(utmGridConvergence({latitude, centralMeridian + row.longitude}, zone) -
                          convergence));
}

void zoneRules() {
    near(utmZoneOfLongitude(-180), 1, 0, "Longitude -180 is zone 1");
    near(utmZoneOfLongitude(180), 1, 0, "Longitude 180 wraps to zone 1");
    near(utmZoneOfLongitude(-174), 2, 0,
         "A longitude on the boundary belongs to the zone east of it");
    near(utmZoneOfLongitude(-0.0001), 30, 0, "London is in zone 30");
    near(utmZoneOfLongitude(0), 31, 0, "The Greenwich meridian is in zone 31");
    near(utmZoneOfLongitude(-104.99), 13, 0, "Denver is in zone 13");
    near(utmZoneOfLongitude(179.9999), 60, 0, "The last zone is 60");
    check(contains(refusal([] { utmZoneOfLongitude(180.5); }, "A longitude beyond 180 is refused"),
                   "-180"),
          "A longitude beyond 180 is refused by name");
    // A zone the user picks is checked against its 6° band, closed at both edges.
    geoToUtm({0, 3.0}, 31);
    geoToUtm({0, 6.0}, 31);
    check(contains(refusal([] { geoToUtm({0, 6.0001}, 31); }, "Outside the band is refused"),
                   "6° band of UTM zone 31"),
          "A point outside the chosen zone's band is refused by name");
    check(contains(refusal([] { geoToUtm({0, 3.5}, 30); }, "Zone 30 refuses 3.5°E"), "zone 30"),
          "A point outside a chosen zone is refused");
    check(geoToUtm({0, 3.0}, 0).zone == 31, "The automatic zone of 3°E is 31");
    check(geoToUtm({0, 2.9999}, 0).zone == 31, "The automatic zone just west of 3° is 31");
    check(geoToUtm({10, 8.0}, 32).zone == 32, "A user zone is kept as chosen");
    check(contains(refusal([] { geoToUtm({0, 3}, 61); }, "Zone 61 is refused"), "between 1 and 60"),
          "A zone above 60 is refused by name");
    check(contains(refusal([] { geoToUtm({0, 3}, -1); }, "Zone -1 is refused"), "between 1 and 60"),
          "A negative zone is refused by name");
}

void rangeRules() {
    check(geoToUtm({84, 3}).north && geoToUtm({84, 3}).zone == 31, "84°N is inside the band");
    check(!geoToUtm({-80, 3}).north, "80°S is inside the band");
    check(contains(refusal([] { geoToUtm({84.0001, 3}); }, "84.0001 is refused"), "80°S to 84°N"),
          "A latitude above 84°N is refused by name");
    check(contains(refusal([] { geoToUtm({-80.0001, 3}); }, "-80.0001 is refused"), "80°S to 84°N"),
          "A latitude below 80°S is refused by name");
    check(contains(refusal([] { geoToUtm({0, 180.5}); }, "180.5 is refused"), "between -180"),
          "A longitude beyond 180° is refused by name");
    check(contains(refusal([] { geoToUtm({std::nan(""), 3}); }, "NaN is refused"), "finite"),
          "A NaN latitude is refused by name");
    // Inverse range: a northing on the wrong side of the equator, and a result outside the band.
    check(contains(refusal([] { utmToGeo({31, true, 500000, -1}); }, "Negative north"), "negative"),
          "A negative northern northing is refused by name");
    check(contains(refusal([] { utmToGeo({31, false, 500000, 10000001}); }, "South overflow"),
                   "10,000,000"),
          "A southern northing above 10,000,000 m is refused by name");
    check(contains(refusal([] { utmToGeo({31, true, 500000, 9.5e6}); }, "North above 84"),
                   "80°S to 84°N"),
          "A northing beyond 84°N is refused by name");
    check(
        contains(refusal([] { utmToGeo({31, true, 900000, 5e6}); }, "East of the band"), "6° band"),
        "An easting outside the zone's 6° band is refused by name");
}

void zoneExceptions() {
    check(contains(refusal([] { geoToUtm({60, 5}); }, "Norway refused"), "Norway zone exception"),
          "A point in the Norway zone exception is refused by name");
    check(contains(refusal([] { geoToUtm({78, 15}); }, "Svalbard refused"), "Svalbard"),
          "A point in the Svalbard zone exception is refused by name");
    check(geoToUtm({60, 2}).zone == 31, "A point west of the Norway exception keeps its zone");
}

void datums() {
    check(geoDatumLabel("WGS84") == "WGS84", "WGS84 is accepted");
    check(geoDatumLabel("NAD83") == "NAD83", "NAD83 is accepted as a labelled approximation");
    check(contains(refusal([] { geoDatumLabel("OSGB36"); }, "OSGB36 refused"), "OSGB36 is refused"),
          "OSGB36 is refused with its reason named");
    check(contains(refusal([] { geoDatumLabel("ETRS89"); }, "ETRS89 refused"), "ETRS89 is refused"),
          "ETRS89 is refused with its reason named");
    check(
        contains(refusal([] { geoDatumLabel("wgs84"); }, "lower case refused"), "WGS84, or NAD83"),
        "An unknown datum label is refused by name");
    check(contains(refusal([] { geoDatumLabel(""); }, "empty refused"), "WGS84, or NAD83"),
          "An empty datum label is refused by name");
}

void referencePoints() {
    const auto rows = references();
    double metres = 0, convergence = 0;
    for (const auto &row : rows) {
        // Zone 20 for the published values, and their mirror in zone 56 (central meridian 153°E),
        // which is the southern grid UTM defines.
        if (row.latitude > 0) {
            projectReference(row, false, 20, metres, convergence);
            // The southern mirror exists only down to 80°S.
            if (row.latitude <= 80)
                projectReference(row, true, 56, metres, convergence);
        }
    }
    std::cout << "Karney reference points: largest grid error " << metres << " m, largest "
              << "convergence error " << convergence << " degrees\n";
    check(metres <= referenceMetres,
          "Forward UTM agrees with Karney's published values within 5 nm");
    check(convergence <= convergenceDegrees,
          "Grid convergence agrees with Karney's published values within 1e-9 degrees");
    // The central meridian has no convergence and no offset from the false easting.
    const auto onMeridian = geoToUtm({46.407609426344, 3.0}, 31);
    near(onMeridian.easting, 500000, referenceMetres,
         "The central meridian lies on the false easting");
    near(onMeridian.northing, 5139337.3167534275931, referenceMetres,
         "The central meridian northing matches the published value");
    near(utmGridConvergence({46.407609426344, 3.0}, 31), 0, 1e-12,
         "Zero convergence on the meridian");
}

void roundTrips() {
    const auto rows = references();
    double worst = 0;
    for (const auto &row : rows) {
        const GeoPosition point{row.latitude, -63.0 + row.longitude};
        const auto utm = geoToUtm(point, 20);
        const auto back = utmToGeo(utm);
        worst = std::max(worst, geoGroundDistance(point, back));
        const auto again = geoToUtm(back, 20);
        worst = std::max({worst, std::abs(again.easting - utm.easting),
                          std::abs(again.northing - utm.northing)});
    }
    std::cout << "Round trips over the reference rows: largest error " << worst << " m\n";
    check(worst <= roundTripMetres, "Each round trip returns within 1 µm");

    // A grid over zones, hemispheres, the band edges and the zone edges.
    const std::vector<double> latitudes{-79.9, -60, -30.5, -0.5, 0, 0.5, 33.3, 60, 83.9};
    const std::vector<double> offsets{-3, -2.9999, -1.5, 0, 0.7, 2.9999, 3};
    double grid = 0;
    for (const int zone : {1, 31, 32, 33, 56, 60}) {
        for (const double latitude : latitudes)
            for (const double offset : offsets) {
                const double centralMeridian = -183 + 6.0 * zone;
                double longitude = centralMeridian + offset;
                longitude = longitude > 180 ? longitude - 360 : longitude;
                const GeoPosition point{latitude, longitude};
                // Norway and Svalbard are refused by name, and a point on their edge can round
                // into them, so the grid leaves a margin there.
                if (inExceptionArea(latitude, longitude, 1e-6))
                    continue;
                const auto utm = geoToUtm(point, zone);
                const auto back = utmToGeo(utm);
                const auto again = geoToUtm(back, zone);
                grid = std::max({grid, std::abs(again.easting - utm.easting),
                                 std::abs(again.northing - utm.northing),
                                 geoGroundDistance(point, back)});
            }
    }
    check(grid <= roundTripMetres, "The zone grid round trips within 1 µm");
    std::cout << "Zone grid round trips: largest error " << grid << " m\n";

    // Southern zone: the false northing is 10,000,000 m, and the grid runs south of the equator.
    const auto south = geoToUtm({-33.8688, 151.2093}, 56);
    check(!south.north && south.northing > 6e6 && south.northing < 6.5e6,
          "Sydney is in the southern grid near 6.2 million metres");
    // A point on the equator is in the northern grid, and at the equator the northing is zero.
    const auto equator = geoToUtm({0, 3.0}, 31);
    check(equator.north && std::abs(equator.northing) < 1e-6,
          "The equator is in the north at zero");
}

void distances() {
    near(geoGroundDistance({0, 0}, {0, 1}), 111319.4908, 0.01,
         "One degree of longitude at the equator");
    // A short meridian step at the equator: a (1 - e²) per radian, from the WGS84 constants.
    near(geoGroundDistance({0, 0}, {0.0001, 0}), 11.057427582159436, 1e-6,
         "A short step of latitude at the equator");
    // A short step of longitude at 50°N: N(50°) cos(50°) per radian.
    const double radians = pi / 180;
    const double prime50 =
        6378137 / std::sqrt(1 - 0.00669437999014 * std::pow(std::sin(50 * radians), 2));
    near(geoGroundDistance({50, 0}, {50, 0.0001}),
         0.0001 * radians * prime50 * std::cos(50 * radians), 1e-4,
         "Longitude shrinks with latitude");
    near(geoGroundDistance({50, 0}, {50, 0}), 0, 0, "A zero distance");
}

void convergence() {
    // Zone 31 (central meridian 3°E): at 50°N and 3° east of the meridian the sphere gives 2.30°.
    const double east = utmGridConvergence({50, 6}, 31);
    near(east, 2.30, 0.02, "Convergence at 50°N, 3° east of the meridian (sphere: 2.30°)");
    check(east > 0, "Grid north is east of true north east of the meridian");
    check(utmGridConvergence({50, 0}, 31) < 0,
          "Grid north is west of true north west of the meridian");
    check(utmGridConvergence({-50, 6}, 31) < 0, "The southern convergence has the opposite sign");
    near(utmGridConvergence({30, 6}, 31),
         std::atan(std::tan(3 * pi / 180) * std::sin(30 * pi / 180)) * 180 / pi, 0.02,
         "Convergence at 30°N matches the spherical value");
}

void geoDistanceMatchesNumber() {
    // The ground distance agrees with the grid distance, to the scale of the grid (ADR 0181 §2.5).
    const GeoPosition a{50, 4}, b{50.00001, 4.00002};
    const auto ua = geoToUtm(a, 31), ub = geoToUtm(b, 31);
    const double grid = std::hypot(ub.easting - ua.easting, ub.northing - ua.northing);
    const double ground = geoGroundDistance(a, b);
    check(std::abs(grid / ground - 1) < 2e-3, "Grid and ground distances agree at a short range");
}

} // namespace

int main() {
    try {
        zoneRules();
        rangeRules();
        zoneExceptions();
        datums();
        referencePoints();
        roundTrips();
        distances();
        convergence();
        geoDistanceMatchesNumber();
    } catch (const std::exception &error) {
        std::cerr << "geo coordinate test failed: " << error.what() << '\n';
        return 1;
    }
    std::cout << "geo coordinate tests passed\n";
    return 0;
}
