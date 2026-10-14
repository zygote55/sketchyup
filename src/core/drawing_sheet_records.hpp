#pragma once
#include "core/scene_records.hpp"
#include <array>
#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <string>
namespace sketchy {
// Drawing sheets (ADR 0163). Lengths are paper millimetres. Paper and frame
// coordinates have their origin at the paper's top-left corner, x to the right
// and y down, matching MeasuredPage::project.
struct DrawingPaper {
    double widthMm{420}, heightMm{297};
    bool operator==(const DrawingPaper &) const = default;
    void validate() const;
};
struct DrawingSheetRecord {
    Id id{};
    std::string name;
    std::uint32_t position{};
    DrawingPaper paper;
    // ADR 0164: the sheet number ("A-101"), 0–32 bytes and unique when nonempty,
    // and the page-margin frame, 0–50 mm and at most a quarter of the shorter side.
    std::string number;
    double marginMm{10};
    bool operator==(const DrawingSheetRecord &) const = default;
};
// A tagged source. R090 has only saved-scene sources; a future external-model kind
// gets its own required feature.
enum class DrawingViewSourceKind { Scene };
struct DrawingViewSource {
    DrawingViewSourceKind kind{DrawingViewSourceKind::Scene};
    Id scene{};
    bool operator==(const DrawingViewSource &) const = default;
    void validate() const;
};
// The viewport rectangle on the paper. It is also the crop, and it maps to a
// MeasuredPage of the same size, so each side is at least 10 mm.
struct DrawingFrame {
    double xMm{}, yMm{}, widthMm{100}, heightMm{100};
    bool operator==(const DrawingFrame &) const = default;
    void validate(const DrawingPaper &paper) const;
};
enum class DrawingViewContent { TechnicalLines, RasterAppearance };
// The user's last accepted evaluation. Staleness is derived, never stored.
struct DrawingViewAcceptance {
    std::uint64_t revision{};
    std::string fingerprint; // 64 lowercase hexadecimal SHA-256 digits.
    bool operator==(const DrawingViewAcceptance &) const = default;
    void validate() const;
};
struct DrawingViewRecord {
    Id id{};
    std::string name;
    Id sheet{};
    DrawingViewSource source;
    DrawingFrame frame;
    std::array<double, 2> offsetMm{}; // Paper-space pan of the scene target inside the frame.
    double scaleDenominator{100};     // Declared; never derived from camera zoom or geometry.
    DrawingViewContent content{DrawingViewContent::TechnicalLines};
    bool hiddenLines{};
    std::optional<DrawingViewAcceptance> accepted;
    bool operator==(const DrawingViewRecord &) const = default;
};
using DrawingSheetPtr = std::shared_ptr<const DrawingSheetRecord>;
using DrawingSheetRecords = std::map<Id, DrawingSheetPtr>;
using DrawingViewPtr = std::shared_ptr<const DrawingViewRecord>;
using DrawingViewRecords = std::map<Id, DrawingViewPtr>;
// Page items (ADR 0164), declared in core/drawing_item_records.hpp.
struct DrawingItemRecord;
using DrawingItemPtr = std::shared_ptr<const DrawingItemRecord>;
using DrawingItemRecords = std::map<Id, DrawingItemPtr>;
inline constexpr size_t drawingSheetCountLimit = 256, drawingViewCountLimit = 1024,
                        drawingViewsPerSheetLimit = 64, drawingSheetBytesLimit = 8 * 1024 * 1024;
inline constexpr double drawingViewOffsetLimitMm = 1e6;
size_t drawingSheetBytes(const DrawingSheetPtr &sheet);
size_t drawingViewBytes(const DrawingViewPtr &view);
// The default page margin: 10 mm, reduced to a quarter of the shorter paper side
// on small paper so that every valid paper has a valid default.
double defaultDrawingSheetMarginMm(const DrawingPaper &paper);
// Validates the tables together: views name a live sheet and fit its paper, and
// items (ADR 0164) name a live sheet and fit it. All records share the eight MiB
// budget. A view whose scene no longer exists stays valid (a diagnosed broken view).
void validateDrawingSheetRecords(const DrawingSheetRecords &sheets, Id nextSheet,
                                 const DrawingViewRecords &views, Id nextView,
                                 const DrawingItemRecords &items = {}, Id nextItem = 1);
// Creating a view, or changing its source, requires the source scene to exist.
bool drawingViewSourceExists(const SceneRecords &scenes, const DrawingViewSource &source);
class Document;
// Views whose source scene is missing, in ID order.
std::set<Id> drawingViewsWithMissingSource(const Document &doc);
struct Edit;
// True when the edit changes drawing-sheet records and nothing else: every model
// change vector and optional field is empty. Allocator floors are checked against
// the document by Document::apply.
bool isDrawingSheetOnlyEdit(const Edit &edit);
} // namespace sketchy
