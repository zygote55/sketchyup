#pragma once
// R107.b4 (UX_DESIGN §20.2 to §20.5): the rules of the GeoTIFF page of the terrain import dialog,
// without widgets: the file's footprint in UTM, the window that fits it, and the texts the dialog
// shows for a refusal, a notice and an arc-second step. The engine (R107.b1 to R107.b3) decides
// every import; these helpers only choose what the person sees and where the window starts.
#include "geo/geographic.hpp"
#include "io/geotiff_terrain.hpp"
#include <QString>
#include <QStringList>
#include <array>
#include <optional>
#include <string>
#include <vector>

namespace sketchy {

// A UTM window: its zone and hemisphere, and two corners in metres (south-west and north-east).
struct DemWindow {
    int zone{};
    bool north{true};
    double e0{}, n0{}, e1{}, n1{};
    double width() const { return e1 - e0; }
    double height() const { return n1 - n0; }
};

// The file's extent (its post centres, as geoTiffFacts reports it) on its boundary, in degrees:
// `perEdge` samples along each of the four edges.
std::vector<GeoPosition> demFootprint(const GeoTiffFacts &facts, int perEdge);
// The footprint in one UTM zone and hemisphere. A sample outside the zone's band, or in the other
// hemisphere, is left out, so a file that crosses a zone edge is clipped to the zone.
std::vector<std::array<double, 2>> demFootprintUtm(const GeoTiffFacts &facts, int zone, bool north,
                                                   int perEdge);
// Whether a window lies inside the file's extent: its perimeter, sampled `perEdge` times along each
// edge, converts to degrees inside the extent. A position that does not convert is outside.
bool demWindowInside(const GeoTiffFacts &facts, const DemWindow &window, int perEdge);
// Fit to the file (UX §20.2): the window is the bounding box of the file's extent in the zone,
// inset just enough that its perimeter lies inside the file, since a UTM rectangle's corners
// fall outside a footprint that is rotated by grid convergence. Nullopt when the zone holds no
// part of the file.
std::optional<DemWindow> demFitWindow(const GeoTiffFacts &facts, int zone, bool north);
// The UTM position of the file's centre, in the zone the centre lies in (the zone the window starts in).
UtmPosition demCentre(const GeoTiffFacts &facts);
// The nominal post size in whole metres: the larger of the two steps, as the reader rounds it.
double demNominalSpacing(const GeoTiffFacts &facts);

// The zone's label, such as "11N".
QString demZoneLabel(int zone, bool north);
// The product's name as the report and the caption give it.
QString demProductLabel(DemProduct product);
// The post step in words: "1/3 arc-second", "1 arc-second", "1.5 arc-seconds".
QString demArcSecondLabel(double arcSeconds);
// A refusal's next steps (UX §20.5, the table's last column), numbered by the caller. `nominal`
// is the file's post size, for the spacing refusal.
QStringList demNextSteps(const std::string &refusal, double nominal);
// Whether a refusal stops the file itself (not the window, the spacing or the labels): the file
// is then shown as refused, and the settings grey out (UX §20.5).
bool demFileRefusal(const std::string &refusal);
// A notice's wording (UX §20.2, §20.4): the plain texts for the known kinds, and the engine's
// message without its ADR citation for the rest. `requested` and `spacing` are the spacings of a
// decimated grid, in metres.
QString demNoticeText(const std::string &kind, const QString &message, const QString &vertical,
                      const QString &offset, const QString &note, const QString &requested,
                      const QString &spacing);
// The amber notice of a decimated grid, without its mark: "Too many triangles at 31 m, so the
// spacing is 34 m." (UX §20.2). A window the room leaves less of says so in the engine's words.
QString decimatedNotice(const QString &requested, const QString &spacing);
// A message with its ADR citation removed, as the dialog shows it.
QString demPlainMessage(const QString &message);

} // namespace sketchy
