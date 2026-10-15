#pragma once
#include "core/drawing_item_records.hpp"
#include "io/drawing_view.hpp"
#include <limits>
#include <optional>
#include <string>
#include <vector>
namespace sketchy {
// Page-item evaluation (ADR 0164 §3, R091.c). Everything here is pure: it reads a
// Document (live or a readSnapshot copy) and never edits it. evaluateDrawingItem is
// cheap: it needs no capture, only the bound view's scene camera and
// drawingViewProjection. Anchors are resolved lazily by ID; model edits never
// rewrite them, so a missing ID gives a derived broken state, never a silent rebind.

enum class DrawingItemState { Current, Broken };
const char *drawingItemStateName(DrawingItemState state);

// One referenced anchor of a dimension or a view-target label.
struct DrawingItemAnchorEvaluation {
    AnchorState state{AnchorState::Resolved};
    Vec3 world{};                        // Resolved world point, or the stored fallback.
    std::optional<DrawingPointMm> paper; // Sheet millimetres when the view projects.
};

// Derived title-block fields; never stored.
struct DrawingTitleBlockDerived {
    std::string sheetNumber; // The sheet's number, possibly empty.
    std::string page;        // "n of N" from the sheet position and count.
    // "1:50 @ A3" when every measurable view on the page shares one scale and the
    // paper is a named size, "1:50" when it is not, "As indicated" when scales
    // differ, "Not to scale" when the page has only perspective views, and "—"
    // when it has no views that can be drawn.
    std::string scale;
};

struct DrawingItemEvaluation {
    Id item{};
    std::uint64_t revision{};
    DrawingItemState state{DrawingItemState::Current};
    // broken: sceneMissing, sceneHasNoCamera, notToScale (the view cannot project)
    // and referenceMissing (an anchor's body or entity no longer exists). View
    // reasons come first.
    std::vector<std::string> reasons;
    std::vector<DrawingItemAnchorEvaluation> anchors; // Dimension (2) or view label (1).
    // Dimensions: the projected measured points, the dimension line placed from the
    // orientation and offset, the text position (the line's midpoint), the paper
    // length and the model value = paper length x scaleDenominator / 1000 metres.
    std::optional<std::array<DrawingPointMm, 2>> measuredMm, lineMm;
    std::optional<DrawingPointMm> textMm;
    std::optional<double> paperLengthMm, valueMetres;
    // Display text: a dimension's prefix and value in the document's display units
    // and precision, "[Missing reference]" when broken; a text or label's text.
    std::string text;
    // Labels: the leader tip (the paper target, or the projected view anchor).
    std::optional<DrawingPointMm> tipMm;
    std::optional<DrawingTitleBlockDerived> titleBlock;
};
// Throws only when the item does not exist.
DrawingItemEvaluation evaluateDrawingItem(const Document &doc, Id item);
// Evaluates a record that need not be in the document yet, such as a tool's preview or
// a dragged copy (R091.g): the record's sheet and bound view are read from the document.
DrawingItemEvaluation evaluateDrawingItem(const Document &doc, const DrawingItemRecord &record);
// A dimension line placed from its measured paper points, orientation and offset (the
// R091.c placement): the line's two ends, its midpoint (the text point) and the paper
// length it measures.
struct DrawingDimensionPlacement {
    std::array<DrawingPointMm, 2> lineMm{};
    DrawingPointMm textMm{};
    double paperLengthMm{};
};
DrawingDimensionPlacement placeDrawingDimension(DrawingPointMm p0, DrawingPointMm p1,
                                                DrawingDimensionOrientation orientation,
                                                double offsetMm);
// The signed offset that puts a dimension line through a paper point: the inverse of
// placeDrawingDimension for the point's distance from the measured points.
double drawingDimensionOffset(DrawingPointMm p0, DrawingPointMm p1,
                              DrawingDimensionOrientation orientation, DrawingPointMm through);
// The projection of a view whose scene camera is orthographic, or the reason it has
// none: sceneMissing, sceneHasNoCamera or notToScale. No capture.
struct DrawingViewMapping {
    std::optional<DrawingViewProjection> projection;
    std::string reason;
};
DrawingViewMapping drawingViewMapping(const Document &doc, Id view);
DrawingTitleBlockDerived drawingTitleBlockFields(const Document &doc, Id sheet);
// "A3" for a named paper size in either orientation (ISO A0–A5, Letter, Legal,
// Tabloid, within 0.5 mm), or empty.
std::string drawingPaperName(const DrawingPaper &paper);

// The nearest model vertex or point on a model edge that an evaluated view draws, to a
// paper point q (sheet millimetres), from the view's MeasuredSource lines (ADR 0164 §3):
// a drawn line end that matches a projected edge end within 0.001 mm is a vertex, any
// other point on a drawn line an edge point at the projected parameter. Section-cut
// lines and lines not drawn from a model edge are never offered. A vertex wins when it
// lies within vertexPreferenceMm of q or is no farther than the nearest edge point;
// nothing farther than maxDistanceMm is returned. Used by Reattach and by the desktop
// Dimension and Label tools (R091.g).
struct DrawingViewAnchorHit {
    AnnotationAnchor anchor;
    DrawingPointMm paperMm{};
    double distanceMm{};
    bool vertex{};
};
std::optional<DrawingViewAnchorHit>
nearestDrawingViewAnchor(const Document &doc, const DrawingViewProjection &projection,
                         const MeasuredDrawing &drawing, DrawingPointMm q,
                         double vertexPreferenceMm,
                         double maxDistanceMm = std::numeric_limits<double>::infinity());
// Reattach proposal for one anchor (ADR 0164 decision 2): the nearest model vertex
// or edge point drawn in the view to the anchor's old paper position (lastMm). A
// vertex is preferred when it lies within drawingReattachVertexSnapMm of lastMm or
// is no farther than the nearest edge point. Section-cut lines are never proposed.
// The caller (the user) confirms before anything is committed.
inline constexpr double drawingReattachVertexSnapMm = 2;
struct DrawingReattachCandidate {
    size_t index{};          // The anchor's index in the item.
    DrawingPointMm lastMm{}; // The old paper position searched from.
    AnchorState state{AnchorState::Resolved};
    // Present when the anchor is missing and the view draws a candidate.
    std::optional<AnnotationAnchor> anchor;
    std::optional<DrawingPointMm> paperMm;
    std::optional<double> distanceMm;
};
struct DrawingReattachProposal {
    DrawingItemEvaluation evaluation;
    std::vector<DrawingReattachCandidate> anchors;
    // Empty, or why no candidate can be searched for (the view's broken or
    // failed state reasons, or notToScale).
    std::vector<std::string> unavailable;
};
// Costs one bounded capture of the bound view. Throws when the item does not exist
// or is not bound to a view.
DrawingReattachProposal drawingReattachCandidates(const Document &doc, Id item);
} // namespace sketchy
