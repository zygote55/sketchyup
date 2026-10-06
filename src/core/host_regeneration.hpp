#pragma once
#include "core/body.hpp"
#include "geometry/hosted_opening.hpp"
namespace sketchy {
struct OpeningCorner {
    Id key{}; // Stable source-profile vertex identity, scoped to this opening.
    Vec3 point;
    bool operator==(const OpeningCorner &) const = default;
};
struct OpeningProfile {
    Id face{}; // Original uncut host face.
    std::vector<OpeningCorner> corners;
    bool operator==(const OpeningProfile &) const = default;
};
struct HostOpening {
    OpeningProfile profile;
    Id exit{};
    std::map<Id, std::array<Id, 2>> vertices; // Source corner -> entry/exit native vertices.
    std::map<std::array<Id, 2>, Id> jambs;    // Unordered source edge -> native reveal face.
    bool operator==(const HostOpening &) const = default;
};
using HostOpenings = std::map<Id, HostOpening>; // Keyed by the owning component placement.
struct RegeneratedHost {
    Body body;
    HostOpenings openings;
    std::map<Id, std::vector<Id>> faceDescendants;
};
// Immutable maintenance of one host against its uncut surface and prior records.
// Verifies the old cuts match current geometry before rebuilding requested cuts.
// Retains native identities/appearance for matching source corners and edges;
// allocates new geometry only above current floors. Does not publish a document.
// A caller publishing the body must mark Change.edgeAppearancesResolved true.
RegeneratedHost regenerateHost(const Surface &uncut, const Body &current,
                               const HostOpenings &previous,
                               const std::map<Id, OpeningProfile> &requested);
} // namespace sketchy
