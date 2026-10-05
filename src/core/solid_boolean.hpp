#pragma once
#include "core/model.hpp"
#include "geometry/boolean.hpp"
namespace sketchy {
struct SolidBooleanPart {
    Id body{};
    std::map<Id, BooleanFaceSource> sources;
    double generatedVolume{};
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
} // namespace sketchy
