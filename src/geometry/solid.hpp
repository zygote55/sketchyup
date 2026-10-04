#pragma once
#include "geometry/topology.hpp"
#include <optional>
#include <string>
namespace sketchy {
struct SolidReport {
    std::string status;
    std::optional<double> volume;
    std::vector<Id> faces{}, edges{}, vertices{};
};
// Conservative single-shell classification. A volume is returned only after
// manifold, orientation and geometric intersection checks complete within budget.
SolidReport inspectSolid(const Surface &surface, const Topology &topology);
} // namespace sketchy
