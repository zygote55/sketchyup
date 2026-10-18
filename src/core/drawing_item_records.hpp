#pragma once
#include "core/annotation_anchors.hpp"
#include "core/drawing_sheet_records.hpp"
#include <array>
#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <variant>
namespace sketchy {
// Drawing-sheet page items (ADR 0164 §3). Lengths are paper millimetres with the
// origin at the paper's top-left corner, x to the right and y down, as for views.
// Stored geometry must lie inside the sheet's paper; derived geometry (positions
// placed from the model, such as the lastMm caches) is only bounded, and is
// clipped when painted.
using DrawingPointMm = std::array<double, 2>;
// A font family name, never a file path, and a point size.
struct DrawingFont {
    std::string family{"DejaVu Sans"};
    double sizePt{10};
    bool operator==(const DrawingFont &) const = default;
    void validate() const;
};
// A rectangle on paper: a text wrap box, a rectangle item or a title block.
struct DrawingItemFrame {
    double xMm{}, yMm{}, widthMm{10}, heightMm{10};
    bool operator==(const DrawingItemFrame &) const = default;
    void validate(const DrawingPaper &paper) const;
};
enum class DrawingTextAlign { Left, Center, Right };
enum class DrawingStrokeDash { Solid, Dashed };
// In paper axes.
enum class DrawingDimensionOrientation { Aligned, Horizontal, Vertical };
struct DrawingTextItem {
    DrawingItemFrame frame;
    std::string text{"Text"}; // 1–4,096 bytes; newlines and tabs are allowed.
    DrawingFont font;
    DrawingTextAlign align{DrawingTextAlign::Left};
    bool operator==(const DrawingTextItem &) const = default;
};
enum class DrawingLabelTargetKind { Paper, View };
// A paper target uses pointMm only. A view target uses view, anchor and lastMm
// (the anchor's paper position at the last sheet edit that wrote it).
struct DrawingLabelTarget {
    DrawingLabelTargetKind kind{DrawingLabelTargetKind::Paper};
    DrawingPointMm pointMm{};
    Id view{};
    AnnotationAnchor anchor{};
    DrawingPointMm lastMm{};
    bool operator==(const DrawingLabelTarget &) const = default;
};
struct DrawingLabelItem {
    std::string text{"Label"}; // 1–4,096 bytes; newlines and tabs are allowed.
    DrawingFont font;
    DrawingPointMm textAt{};
    bool leader{true};
    DrawingLabelTarget target;
    bool operator==(const DrawingLabelItem &) const = default;
};
// A referenced dimension. Its anchors are resolved lazily by ID against the
// model when evaluated (R091.c). A model edit rewrites only the anchors, inside
// the same edit, when its lineage names one successor (ADR 0164 §3 addendum).
struct DrawingDimensionItem {
    Id view{};
    std::array<AnnotationAnchor, 2> anchors{};
    DrawingDimensionOrientation orientation{DrawingDimensionOrientation::Aligned};
    double offsetMm{};  // Signed distance from the measured points to the dimension line.
    std::string prefix; // 0–256 bytes.
    DrawingFont font;
    std::array<DrawingPointMm, 2> lastMm{};
    bool operator==(const DrawingDimensionItem &) const = default;
};
struct DrawingLineItem {
    DrawingPointMm from{}, to{10, 0};
    double strokeMm{.35};
    DrawingStrokeDash dash{DrawingStrokeDash::Solid};
    bool operator==(const DrawingLineItem &) const = default;
};
struct DrawingRectangleItem {
    DrawingItemFrame frame;
    double strokeMm{.35};
    DrawingStrokeDash dash{DrawingStrokeDash::Solid};
    bool operator==(const DrawingRectangleItem &) const = default;
};
// Stored fields only. The sheet number, "n of N" and the scale are derived.
struct DrawingTitleBlockFields {
    std::string project, drawingTitle, date, revision; // Each 0–256 bytes.
    bool operator==(const DrawingTitleBlockFields &) const = default;
};
struct DrawingTitleBlockItem {
    DrawingItemFrame frame;
    DrawingFont font;
    DrawingTitleBlockFields fields;
    bool operator==(const DrawingTitleBlockItem &) const = default;
};
// The variant index is the item kind, in DrawingItemKind order.
enum class DrawingItemKind { Text, Label, Dimension, Line, Rectangle, TitleBlock };
using DrawingItemData = std::variant<DrawingTextItem, DrawingLabelItem, DrawingDimensionItem,
                                     DrawingLineItem, DrawingRectangleItem, DrawingTitleBlockItem>;
struct DrawingItemRecord {
    Id id{};
    Id sheet{};
    std::uint32_t z{}; // Contiguous per sheet, 0 at the bottom.
    DrawingItemData data;
    DrawingItemKind kind() const { return static_cast<DrawingItemKind>(data.index()); }
    bool operator==(const DrawingItemRecord &) const = default;
};
inline constexpr size_t drawingItemCountLimit = 4096, drawingItemsPerSheetLimit = 512,
                        drawingItemTextLimit = 4096, drawingItemShortTextLimit = 256,
                        drawingFontFamilyLimit = 128, drawingSheetNumberLimit = 32;
inline constexpr double drawingSheetMarginLimitMm = 50, drawingSheetDefaultMarginMm = 10,
                        drawingStrokeMinMm = .1, drawingStrokeMaxMm = 2, drawingFontMinPt = 4,
                        drawingFontMaxPt = 72;
size_t drawingItemBytes(const DrawingItemPtr &item);
// The view an item is bound to: a dimension's view, or a label's view target.
// Zero when the item is not bound to a view.
Id drawingItemView(const DrawingItemRecord &item);
// Validates the item table against the sheets and views. Each item belongs to a
// live sheet, its stored geometry fits that paper, its bound view exists on the
// same sheet, and z is contiguous per sheet. Returns the item byte total.
size_t validateDrawingItemRecords(const DrawingSheetRecords &sheets,
                                  const DrawingViewRecords &views, const DrawingItemRecords &items,
                                  Id nextItem);
} // namespace sketchy
