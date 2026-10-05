#pragma once
#include "core/selection.hpp"
namespace sketchy {
enum class IntersectionMode { Selected, Context, Model };
// Only bodies containing explicit face targets are edited; incident boundary
// splits propagate within each body. Context respects group boundaries;
// Model reads visible geometry throughout the scene without changing references.
// An optional outer scene supplies read-only references for component drafts;
// its excluded instance is represented by the current draft instead.
ChangeReport intersectSelected(Document &doc, const SelectionSet &targets, IntersectionMode mode,
                               Id context, const Document *outerScene = nullptr,
                               Id excludedInstance = 0);
} // namespace sketchy
