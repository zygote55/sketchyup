#pragma once
#include "core/drawing_sheet_records.hpp"
#include "io/measured_drawing.hpp"
#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <utility>
#include <vector>
namespace sketchy {
// Linked drawing-view evaluation (ADR 0163, R090.c). Everything here is pure: it
// reads a Document (live or a readSnapshot copy) and never edits it, so a worker
// can evaluate views from an immutable snapshot.

// World <-> sheet mapping of one orthographic view. Sheet coordinates are paper
// millimetres with the origin at the paper's top-left corner, x right and y down
// (DrawingFrame's convention); z stays world depth in metres, as in
// MeasuredPage::project. The declared scale comes only from the view record.
struct DrawingViewProjection {
    DrawingFrame frame;
    // Frame-local page: widthMm/heightMm = frame size, marginMm = 0, the declared
    // scaleDenominator and includeHidden = hiddenLines. Its origin is the world point
    // drawn at the frame centre.
    MeasuredPage page;
    Vec3 worldToSheet(Vec3 world) const;
    // Inverse of worldToSheet; depth is the world-depth z of the result.
    Vec3 sheetToWorld(Vec3 sheet) const;
};
// Maps a view to its MeasuredPage for an orthographic scene camera. offsetMm pans
// the scene target inside the frame: the target is drawn at the frame centre plus
// offsetMm (paper x right, y down). Throws for a perspective camera: perspective
// views are not to scale and have no measured mapping.
DrawingViewProjection drawingViewProjection(const DrawingViewRecord &view,
                                            const RenderCamera &sceneCamera);

// SHA-256 (64 lowercase hex digits) over a canonical serialization of what the view
// draws: page parameters, frame-millimetre line segments quantized to 0.001 mm with
// hidden flags (endpoint-ordered, sorted), and the annotations that reach the frame
// (text, kind, measured value, broken state, quantized positions) with the display
// units and precision. Independent of record IDs, map order and pointer values.
// For rasterAppearance views the appearance digest is appended.
std::string drawingFingerprint(const MeasuredDrawing &drawing,
                               const std::string &appearanceDigest = {});
// Appearance inputs of a rasterAppearance view: the model style, the surface
// appearance (colour, opacity, texture asset SHA-256) of visible faces, and the
// visible reference images (asset SHA-256, opacity, quantized sheet corners).
std::string drawingAppearanceDigest(const RenderSnapshot &snapshot, const MeasuredPage &page);
// Fingerprint of a perspective (not-to-scale) view, which has no measured drawing.
// SHA-256 over the captured snapshot inputs the illustration depends on, never over
// rendered pixels: the camera, the frame size, the model style, and every visible
// face (world triangles quantized to 0.001 mm, colour, opacity, texture asset
// SHA-256), visible edge (endpoint-ordered segment, soft/smooth flags), section cut
// and reference image of the scene. Items are sorted, so record IDs and map order do
// not matter. The whole scene counts, not only what falls inside the frame.
std::string drawingIllustrationFingerprint(const RenderSnapshot &snapshot,
                                           const DrawingViewRecord &view);

enum class DrawingViewState { Current, Stale, Broken, Failed };
const char *drawingViewStateName(DrawingViewState state);
struct DrawingViewStatus {
    Id view{};
    std::uint64_t revision{}; // Document revision the status was evaluated at.
    DrawingViewState state{DrawingViewState::Current};
    // Stable reason codes. broken: sceneMissing, sceneHasNoCamera, temporaryClipPlane,
    // showHidden. stale: notAccepted or changedSinceAccepted. failed: captureFailed
    // (detail holds the message). current has none.
    std::vector<std::string> reasons;
    // The scene camera is perspective: the view is current or stale like any other,
    // but draws as an unscaled illustration with no measured mapping (no projection,
    // no measured drawing, no dimensions). ADR 0163 decision 5.
    bool notToScale{};
    size_t warnings{};       // Missing references inside the scene.
    std::string fingerprint; // Empty for broken and failed views.
    std::string detail;
    bool operator==(const DrawingViewStatus &) const = default;
};
// Full evaluation, for export and dimension placement: the status plus, when the
// view is to scale and captured, its projection and measured drawing.
struct DrawingViewEvaluation {
    DrawingViewStatus status;
    std::optional<DrawingViewProjection> projection;
    std::optional<MeasuredDrawing> drawing;
};
// Owner-thread half of evaluateDrawingViewDrawing (ADR 0164 §2): everything read
// from the Document, as immutable values. It holds no reference to the live
// document, so the document may change while a worker computes from it.
struct DrawingViewEvaluationInput {
    Id view{};
    std::uint64_t revision{};
    DrawingViewRecord record;
    // The view's mapping when the scene camera is orthographic.
    std::optional<DrawingViewProjection> projection;
    // The scene capture (snapshot or broken reasons). Default when capturing threw.
    DrawingViewCapture capture;
    // The capture threw (for example a work limit or an out-of-bounds offset).
    std::optional<std::string> captureError;
};
// Reads the document: the view record, the scene and a RenderSnapshot of it. Run
// on the document's owner thread. Throws only when the view record does not exist.
DrawingViewEvaluationInput captureDrawingViewEvaluation(const Document &doc, Id view);
// Pure worker half: status, fingerprint, projection and measured drawing from the
// captured input alone. Safe on any thread; the result is identical to
// evaluateDrawingViewDrawing at the capture's revision.
DrawingViewEvaluation computeDrawingViewEvaluation(const DrawingViewEvaluationInput &input);
// Precedence: broken, then failed, then stale/current. Throws only
// when the view record does not exist. The composition of the two halves above.
DrawingViewEvaluation evaluateDrawingViewDrawing(const Document &doc, Id view);
DrawingViewStatus evaluateDrawingView(const Document &doc, Id view);

// Pure revision-keyed status cache: (document identity, view) -> the status at one
// revision. A lookup at the revision a status was stored for is free; any other
// revision misses and is re-evaluated. Storing a newer status replaces the view's
// older one, so the cache holds at most one entry per view. No threads, no timers.
class DrawingViewStatusCache {
  public:
    const DrawingViewStatus *find(const std::string &identity, Id view,
                                  std::uint64_t revision) const;
    void store(const std::string &identity, DrawingViewStatus status);
    // Cached status at doc.revision(), or evaluate and store it.
    DrawingViewStatus status(const Document &doc, Id view);
    // Drops entries for views that no longer exist in doc.
    void prune(const Document &doc);
    size_t size() const { return entries_.size(); }
    size_t evaluations() const { return evaluations_; }

  private:
    std::map<std::pair<std::string, Id>, DrawingViewStatus> entries_;
    size_t evaluations_{};
};
} // namespace sketchy
