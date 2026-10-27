#pragma once
// R101.e1 (UX_DESIGN §19.4 and §19.11 item 8): the words that say why a solid has no known volume.
// The Entity info panel and the report's invalid table both use this function, so the two cannot
// give different words for one status. It was `unavailable()` in src/app/entity_info_panel.cpp.
#include <string>

namespace sketchy {

// The words for a solid status from inspectSolid (ADR 0175 §2.4). A status with no words of its own
// reads "No closed surface".
inline std::string solidStatusWords(const std::string &status) {
    if (status == "open_boundary")
        return "Not a solid: open boundary";
    if (status == "non_manifold")
        return "Not a solid: non-manifold boundary";
    if (status == "inconsistent_winding")
        return "Not a solid: inconsistent face winding";
    if (status == "loose_geometry")
        return "Not a solid: loose geometry";
    if (status == "self_intersection")
        return "Not a solid: intersecting faces";
    if (status == "degenerate")
        return "Not a solid: degenerate shell";
    if (status == "multiple_records")
        return "Unavailable: multiple geometry records";
    if (status == "multiple_shells")
        return "Unavailable: multiple shells";
    if (status == "ambiguous_containment")
        return "Unavailable: ambiguous shell containment";
    if (status == "analysis_limit")
        return "Unavailable: analysis limit";
    if (status == "not_a_context")
        return "Select a whole entity for volume";
    return "No closed surface";
}

} // namespace sketchy
