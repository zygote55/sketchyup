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
struct SolidShell {
    std::vector<Id> faces;
    std::optional<size_t> parent;
    unsigned depth{};
    double signedVolume{};
};
struct SolidShellAnalysis {
    SolidReport report;
    std::vector<SolidShell> shells;
};
// Validated closed, nonintersecting shell hierarchy with alternating cavity winding.
// Success is "validated_shells" and its volume sums material across all roots/islands.
// A failure returns no hierarchy or volume. Surface/topology must already be valid.
SolidShellAnalysis analyzeSolidShells(const Surface &surface, const Topology &topology);
// Conservative single-shell classification. A volume is returned only after
// manifold, orientation and geometric intersection checks complete within budget.
SolidReport inspectSolid(const Surface &surface, const Topology &topology);
} // namespace sketchy
