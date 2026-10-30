#include "app/terrain_dem_window.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace sketchy {

namespace {

// Whether a position lies in the file's extent (the post centres, both edges included).
bool within(const GeoTiffFacts &facts, const GeoPosition &position) {
    return position.latitude >= facts.minLatitude && position.latitude <= facts.maxLatitude &&
           position.longitude >= facts.minLongitude && position.longitude <= facts.maxLongitude;
}

// A UTM position of a window as degrees, or nothing when it does not convert (outside the zone's
// band, the latitude range or the window's hemisphere).
std::optional<GeoPosition> toGeographic(const DemWindow &window, double easting, double northing) {
    try {
        return utmToGeo(UtmPosition{window.zone, window.north, easting, northing});
    } catch (const std::exception &) {
        return std::nullopt;
    }
}

// The window with each edge moved `inset` metres inward.
DemWindow insetWindow(const DemWindow &box, double inset) {
    return DemWindow{box.zone, box.north, box.e0 + inset, box.n0 + inset, box.e1 - inset,
                     box.n1 - inset};
}

} // namespace

std::vector<GeoPosition> demFootprint(const GeoTiffFacts &facts, int perEdge) {
    std::vector<GeoPosition> out;
    const int steps = std::max(perEdge, 1);
    out.reserve(std::size_t(4 * (steps + 1)));
    for (int i = 0; i <= steps; ++i) {
        const double t = double(i) / steps;
        const double longitude = facts.minLongitude + t * (facts.maxLongitude - facts.minLongitude);
        const double latitude = facts.minLatitude + t * (facts.maxLatitude - facts.minLatitude);
        out.push_back({facts.minLatitude, longitude});
        out.push_back({facts.maxLatitude, longitude});
        out.push_back({latitude, facts.minLongitude});
        out.push_back({latitude, facts.maxLongitude});
    }
    return out;
}

std::vector<std::array<double, 2>> demFootprintUtm(const GeoTiffFacts &facts, int zone, bool north,
                                                   int perEdge) {
    std::vector<std::array<double, 2>> out;
    for (const auto &position : demFootprint(facts, perEdge)) {
        try {
            const auto utm = geoToUtm(position, zone);
            if (utm.north == north)
                out.push_back({utm.easting, utm.northing});
        } catch (const std::exception &) {
            // Outside the zone's band: the file's part in another zone is not drawn here.
        }
    }
    return out;
}

bool demWindowInside(const GeoTiffFacts &facts, const DemWindow &window, int perEdge) {
    if (!(window.width() > 0 && window.height() > 0))
        return false;
    const int steps = std::max(perEdge, 1);
    for (int i = 0; i <= steps; ++i) {
        const double t = double(i) / steps;
        const double easting = window.e0 + t * window.width();
        const double northing = window.n0 + t * window.height();
        const std::array<std::array<double, 2>, 4> edge{{{easting, window.n0},
                                                         {easting, window.n1},
                                                         {window.e0, northing},
                                                         {window.e1, northing}}};
        for (const auto &point : edge) {
            const auto position = toGeographic(window, point[0], point[1]);
            if (!position || !within(facts, *position))
                return false;
        }
    }
    return true;
}

std::optional<DemWindow> demFitWindow(const GeoTiffFacts &facts, int zone, bool north) {
    const auto footprint = demFootprintUtm(facts, zone, north, 64);
    if (footprint.empty())
        return std::nullopt;
    constexpr double infinity = std::numeric_limits<double>::infinity();
    DemWindow box{zone, north, infinity, infinity, -infinity, -infinity};
    for (const auto &point : footprint) {
        box.e0 = std::min(box.e0, point[0]);
        box.n0 = std::min(box.n0, point[1]);
        box.e1 = std::max(box.e1, point[0]);
        box.n1 = std::max(box.n1, point[1]);
    }
    if (demWindowInside(facts, box, 16))
        return box;
    // The bounding box's corners lie outside a footprint rotated by grid convergence, so the box
    // is inset. The inside test is monotone in the inset, so a bisection finds the least one.
    const double largest = std::min(box.width(), box.height()) / 2 - 1;
    if (!(largest > 0) || !demWindowInside(facts, insetWindow(box, largest), 16))
        return std::nullopt;
    double outside = 0, inside = largest;
    for (int i = 0; i < 48; ++i) {
        const double middle = (outside + inside) / 2;
        if (demWindowInside(facts, insetWindow(box, middle), 16))
            inside = middle;
        else
            outside = middle;
    }
    // A metre more than the least inset: the dialog's spin boxes round to a tenth of a metre, and a
    // corner rounded outward would fall just outside the file.
    return insetWindow(box, std::min(inside + 1, largest));
}

UtmPosition demCentre(const GeoTiffFacts &facts) {
    const GeoPosition centre{(facts.minLatitude + facts.maxLatitude) / 2,
                             (facts.minLongitude + facts.maxLongitude) / 2};
    return geoToUtm(centre);
}

double demNominalSpacing(const GeoTiffFacts &facts) {
    return std::max(facts.postMetresNorthSouth, facts.postMetresEastWest);
}

QString demZoneLabel(int zone, bool north) {
    return QString::number(zone) + (north ? QLatin1Char('N') : QLatin1Char('S'));
}

QString demProductLabel(DemProduct product) {
    return product == DemProduct::Dep3 ? QStringLiteral("USGS 3DEP")
                                       : QStringLiteral("Copernicus GLO-30");
}

QString demArcSecondLabel(double arcSeconds) {
    if (std::abs(arcSeconds - 1.0 / 3) < 1e-3)
        return QStringLiteral("1/3 arc-second");
    if (std::abs(arcSeconds - 1) < 1e-3)
        return QStringLiteral("1 arc-second");
    if (std::abs(arcSeconds - 1.5) < 1e-3)
        return QStringLiteral("1.5 arc-seconds");
    return QString::number(arcSeconds, 'g', 4) + QStringLiteral(" arc-seconds");
}

QStringList demNextSteps(const std::string &refusal, double nominal) {
    if (refusal == "not-tiff")
        return {"Choose another file"};
    if (refusal == "bigtiff")
        return {"Save the tile as a classic GeoTIFF with a GeoTIFF tool"};
    if (refusal == "compression")
        return {"Download the product's standard GeoTIFF (3DEP is LZW, GLO-30 Deflate), or convert "
                "the file"};
    if (refusal == "image-layout")
        return {"Keep one full-resolution image, with a GeoTIFF tool"};
    if (refusal == "bands" || refusal == "sample-type")
        return {"Choose the product's elevation tile"};
    if (refusal == "projected" || refusal == "coordinate-system")
        return {"Download the geographic tile (WGS84 or NAD83)"};
    if (refusal == "national-grid")
        return {"None in this version"};
    if (refusal == "vertical-units" || refusal == "vertical-reference")
        return {"Choose a file whose heights are in metres"};
    if (refusal == "post-size")
        return {"Choose the matching product, or a supported resolution"};
    if (refusal == "not-georeferenced")
        return {"Choose a georeferenced tile"};
    if (refusal == "damaged" || refusal == "unreadable")
        return {"Download the file again, and compare its size with the source's listing"};
    if (refusal == "file-changed")
        return {"Choose the file again"};
    if (refusal == "window-zone")
        return {"Choose a zone from 1 to 60, with N or S"};
    if (refusal == "window-place")
        return {"Choose a window in one zone without an exception"};
    if (refusal == "window-order" || refusal == "window-coordinates")
        return {"Correct the corners, or enlarge the window"};
    if (refusal == "window-small")
        return {"Enlarge the window"};
    if (refusal == "window-outside")
        return {"Move the window inside the file's outline", "Or choose Fit to the file"};
    if (refusal == "window-void")
        return {"Move the window clear of the no-data area"};
    if (refusal == "spacing" || refusal == "spacing-fine")
        return {QStringLiteral("Enter %1 m or more").arg(nominal)};
    if (refusal == "triangle-room" || refusal == "triangle-budget")
        return {"Choose a smaller window, or a coarser spacing"};
    if (refusal == "vertical-choice" || refusal == "vertical-choice-differs")
        return {"Choose the file's reference"};
    if (refusal == "vertical-note" || refusal == "vertical-note-length")
        return {"Enter the note, or shorten it"};
    if (refusal == "vertical-offset-zero" || refusal == "vertical-offset-value")
        return {"Leave the offset out, or enter the difference"};
    if (refusal == "vertical-mismatch")
        return {"Enter an offset and a note, or change the site's reference in Site settings"};
    if (refusal == "zone-mismatch")
        return {"Choose a window in the site's zone"};
    if (refusal == "origin-outside-zone")
        return {"Use an origin inside the file's zone"};
    return {};
}

bool demFileRefusal(const std::string &refusal) {
    static const std::vector<std::string> codes{
        "not-tiff",     "bigtiff",          "damaged",       "compression",   "image-layout",
        "bands",        "sample-type",      "projected",     "national-grid", "coordinate-system",
        "vertical-units", "vertical-reference", "not-georeferenced", "unsupported", "post-size",
        "unreadable"};
    return std::find(codes.begin(), codes.end(), refusal) != codes.end();
}

QString demPlainMessage(const QString &message) {
    QString text = message;
    if (const auto citation = text.indexOf(QStringLiteral(" (ADR ")); citation >= 0)
        text.truncate(citation);
    text = text.trimmed();
    if (!text.isEmpty() && !text.endsWith(QLatin1Char('.')))
        text += QLatin1Char('.');
    return text;
}

QString decimatedNotice(const QString &requested, const QString &spacing) {
    return QStringLiteral("Too many triangles at %1 m, so the spacing is %2 m.").arg(requested, spacing);
}

QString demNoticeText(const std::string &kind, const QString &message, const QString &vertical,
                      const QString &offset, const QString &note, const QString &requested,
                      const QString &spacing) {
    if (kind == "datum-approximation")
        return QStringLiteral("The file is NAD83. It is taken as WGS84, with the label kept.");
    if (kind == "surface-elevation")
        return QStringLiteral("GLO-30 heights are a surface model: buildings and trees are included.");
    if (kind == "vertical-from-option")
        return QStringLiteral("The file names no vertical reference. The %1 label is recorded.")
            .arg(vertical);
    if (kind == "vertical-offset")
        return QStringLiteral("Heights are offset by %1 m to the site's reference. %2")
            .arg(offset, note);
    if (kind == "site-vertical-unlabelled")
        return QStringLiteral("The site's elevation names no vertical reference. The terrain is "
                              "labelled %1, and the label is recorded, not checked.")
            .arg(vertical);
    // The room the other terrains leave sets the spacing in the engine's words; the terrain's own
    // limits are the amber line of §20.2.
    if (kind == "spacing-decimated" && !message.contains(QStringLiteral("room")))
        return decimatedNotice(requested, spacing);
    return demPlainMessage(message);
}

} // namespace sketchy
