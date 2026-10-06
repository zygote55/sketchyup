#pragma once
#include "core/selection.hpp"
#include "geometry/orientation.hpp"
namespace sketchy {
// Change only winding and the corresponding material-side assignments, preserving
// physical-side appearance, topology IDs, curves and metadata in one document edit.
ChangeReport reverseSelectedFaces(Document &doc, const SelectionSet &faces, Id context);
ChangeReport orientConnectedFaces(Document &doc, Id body, Id referenceFace, Id context);
} // namespace sketchy
