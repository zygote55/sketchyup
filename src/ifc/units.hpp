// The project's units (ADR 0171 section 3.5), read from IfcProject.UnitsInContext.
#pragma once

#include <map>
#include <optional>
#include <string>
#include <vector>

#include "ifc/access.hpp"

namespace sketchyup::ifc {

// One unit of the assignment as far as the import converts it: metres per unit for a length, square
// metres for an area, cubic metres for a volume (SI base per one unit of the file).
struct UnitMagnitude {
  std::string name;  // MILLIMETRE, FOOT, SQUARE_METRE, ...
  double perUnit{1};
  bool exact{true};
};

struct Units {
  bool hasLength{false};
  UnitMagnitude length{"METRE", 1, true};
  UnitMagnitude area{"SQUARE_METRE", 1, true};
  UnitMagnitude volume{"CUBIC_METRE", 1, true};
  std::string planeAngle{"RADIAN"};
  bool overridden{false};
  bool areaDefaulted{false};
  bool volumeDefaulted{false};
  // The assigned unit of each other unit type (MASSUNIT -> "GRAM"), for the report's table of
  // numbers that are stored as written.
  std::map<std::string, std::string> others;
  std::vector<std::string> notices;  // codes: unitFactorSnapped, ...
};

struct UnitsOutcome {
  Units units;
  std::string refusalCode, refusalMessage;  // set when the file cannot be imported
};

// `override` is "mm", "cm", "m", "in" or "ft" (or empty). Never guesses: a missing, mixed or
// unrecognised length unit refuses unless the override names one for a file that has none.
UnitsOutcome readUnits(const Inst& project, const std::string& overrideUnit);

// The magnitude of a unit given as an IfcSIUnit or IfcConversionBasedUnit instance, or nullopt for
// a unit that is neither or whose dimension is not the expected one. `dimension` is LENGTHUNIT,
// AREAUNIT or VOLUMEUNIT.
std::optional<UnitMagnitude> unitMagnitude(const Inst& unit, const std::string& dimension, std::vector<std::string>* notices);

}  // namespace sketchyup::ifc
