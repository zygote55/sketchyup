#pragma once
#include "core/document_limits.hpp"
#include <cstddef>
#include <stdexcept>
#include <string>
#include <string_view>
namespace sketchy {
// Count limits of the file formats (R113, D-102). An export takes the materialized limits of the
// document, since the model it writes is the materialized one. An import takes the unique limits.
// Going over refuses by name, with the quantity, the attempted count and the maximum, in the style
// of checkLimits, and the operation writes nothing:
// "OBJ import exceeds the document unique geometry limit: faces would be 300,001 of 300,000".
// `limit` names the class: "materialized" or "unique geometry".
inline void checkFormatCount(std::string_view operation, std::string_view quantity,
                             std::size_t attempted, std::size_t maximum, std::string_view limit) {
    if (attempted <= maximum)
        return;
    throw std::runtime_error(std::string(operation) + " exceeds the document " +
                             std::string(limit) + " limit: " + std::string(quantity) +
                             " would be " + groupedCount(attempted) + " of " +
                             groupedCount(maximum));
}
// A byte bound in MiB for a message. Messages print the constant they check, so a changed limit
// cannot leave a stale number in a refusal (R114 items 9 and 10; R115).
inline std::string mibBound(std::size_t bytes) {
    return std::to_string(bytes / (1024 * 1024)) + " MiB";
}
// The most face corners a model's geometry can have (R113, D-102). A manifold model uses each edge
// in at most two faces, so its corners are at most twice its edges, and the edge limit bounds
// them. An exported model with non-manifold edges and more corners is refused by name. Triangles
// are at most corners, so the same bound holds for the triangles an export writes.
inline constexpr std::size_t materializedCornerLimit = 2 * DocumentLimits::materialized.edges;
inline constexpr std::size_t uniqueCornerLimit = 2 * DocumentLimits::unique.edges;
} // namespace sketchy
