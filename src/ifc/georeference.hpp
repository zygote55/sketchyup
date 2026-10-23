// Georeferencing (ADR 0171 section 3.6, R097.c): the Model context's IfcMapConversion and
// IfcProjectedCRS, and what of them the document's translation-only site origin can hold. The worker
// decides; it never guesses: whatever cannot be held exactly is reported with its reason, and the
// project axes are kept (decision 6). The application re-validates everything it receives.
#pragma once

#include <functional>
#include <optional>
#include <string>
#include <vector>

#include "ifc/access.hpp"
#include "ifc/report.hpp"
#include "ifc/units.hpp"

namespace sketchyup::ifc {

// The map conversion that was read (exactly one, on the project's one Model context).
struct MapConversionFacts {
  int step{};
  double eastings{}, northings{}, orthogonalHeight{};
  double xAxisAbscissa{1}, xAxisOrdinate{0};
  double rotationDegrees{0};  // atan2(ordinate, abscissa), anticlockwise about Z
  double scale{1};
  std::string crsName;  // IfcProjectedCRS.Name, shortened to 64 bytes without control characters
};

struct Georeference {
  // Set when the file is refused (a non-identity WorldCoordinateSystem on the Model context).
  std::string refusalCode, refusalMessage;
  // The report's "georeference" object, a complete JSON object.
  std::string json;
  std::optional<MapConversionFacts> facts;
  // Grid mode with a rotation to apply to every world matrix: [cosine -sine; sine cosine] about Z.
  bool rotate{false};
  double cosine{1}, sine{0};
  // The site origin the document gets (easting, northing, elevation in metres, CRS label).
  bool setsSiteOrigin{false};
  // Instances whose data went into the report (the accounting counts them as supporting).
  std::vector<Inst> consumed;
};

// `texts` turns a file string into valid UTF-8 (the import's latin-1 fallback). Notices go to `report`.
Georeference readGeoreference(ifcopenshell::file& file, const Inst& project, const Units& units, bool gridAligned,
                              const std::string& schema, Report& report,
                              const std::function<std::string(const std::string&)>& texts);

}  // namespace sketchyup::ifc
