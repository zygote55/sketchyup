#pragma once
#include "core/model.hpp"
#include "geometry/boolean.hpp"
namespace sketchy {
struct SolidBooleanPart {
    Id body{};
    std::map<Id, BooleanFaceSource> sources;
    double generatedVolume{};
    std::string portion{"result"};
};
struct SolidBooleanResult {
    std::vector<SolidBooleanPart> parts;
    ChangeReport changes;
};
// Two editable raw solid bodies in one context. Operate in world coordinates,
// create siblings in the target's frame, and retain or consume both operands as
// explicitly requested. All publication is one edit; invalid inputs stay intact.
SolidBooleanResult booleanBodies(Document &doc, Id body, Id tool, BooleanOperation operation,
                                 Id context, bool keepOperands);
enum class SolidAction { Union, Subtract, Intersect, Trim, Split, OuterShell };
// Trim always retains the tool. Split's tool-only region inherits the tool frame;
// all other regions inherit the target frame. Retention is explicit and atomic.
SolidBooleanResult solidBodies(Document &doc, Id body, Id tool, SolidAction action, Id context,
                               bool keepOriginals);
} // namespace sketchy
