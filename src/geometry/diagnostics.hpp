#pragma once
#include "geometry/solid.hpp"
#include <compare>
namespace sketchy {
enum class DiagnosticKind { Face, Edge, Vertex, None };
enum class DiagnosticSeverity { Information, Warning, Error };
struct DiagnosticReference {
    DiagnosticKind kind;
    Id id{};
    auto operator<=>(const DiagnosticReference &) const = default;
};
struct GeometryFinding {
    std::string code, message;
    DiagnosticSeverity severity{DiagnosticSeverity::Error};
    DiagnosticKind countedKind{DiagnosticKind::None};
    size_t count{};
    bool countExact{true};
    std::vector<DiagnosticReference> references;
    bool truncated{};
    // Only a complete, geometrically validated set of inverted shell faces can
    // be offered as a whole-shell reversal. Never repair a truncated sample.
    bool reverseShells{};
};
struct GeometryDiagnostics {
    std::vector<GeometryFinding> findings;
    std::string solidStatus;
    std::optional<double> materialVolume;
    bool analysisComplete{};
    static constexpr size_t findingLimit = 16, referenceLimit = 64, cornerLimit = 2000000;
};
// Read-only bounded findings for an authoritative valid surface/topology pair.
// Topological defects prevent deeper solid analysis; no clean-volume claim is
// made on an incomplete or budget-limited analysis. Samples never imply repair.
GeometryDiagnostics diagnoseGeometry(const Surface &surface, const Topology &topology);
} // namespace sketchy
