#pragma once
#include "core/model.hpp"
#include "geometry/sweep.hpp"
namespace sketchy {
struct ProfileSweepResult {
    Id body{};
    std::vector<Id> caps;
    std::map<std::array<Id, 2>, std::vector<Id>> sides;
    std::vector<std::vector<Id>> segments;
    ChangeReport changes;
};
// Keeps the source unchanged and creates a sibling (or a child when the profile
// belongs directly to a group). Path coordinates follow the requested space.
ProfileSweepResult sweepFace(Document &doc, Id source, Id face, const std::vector<Vec3> &path,
                             bool closed = false, bool worldSpace = false);
} // namespace sketchy
