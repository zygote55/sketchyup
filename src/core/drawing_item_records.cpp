#include "core/drawing_item_records.hpp"
#include <algorithm>
#include <cmath>
#include <set>
#include <stdexcept>
#include <vector>
namespace sketchy {
namespace {
void require(bool condition, const char *message) {
    if (!condition)
        throw std::runtime_error(message);
}
bool control(unsigned char c) { return c < 32 || c == 127; }
// Short single-line strings: prefixes and title-block fields.
bool validShortText(const std::string &text) {
    return text.size() <= drawingItemShortTextLimit &&
           std::none_of(text.begin(), text.end(), control);
}
// Item text may span lines; other control characters are refused.
bool validItemText(const std::string &text) {
    return !text.empty() && text.size() <= drawingItemTextLimit &&
           std::none_of(text.begin(), text.end(),
                        [](unsigned char c) { return control(c) && c != '\n' && c != '\t'; });
}
bool insidePaper(const DrawingPointMm &point, const DrawingPaper &paper) {
    return std::isfinite(point[0]) && std::isfinite(point[1]) && point[0] >= 0 && point[1] >= 0 &&
           point[0] <= paper.widthMm && point[1] <= paper.heightMm;
}
// Derived paper positions are only required to be finite and bounded.
bool boundedDerived(const DrawingPointMm &point) {
    return std::all_of(point.begin(), point.end(), [](double value) {
        return std::isfinite(value) && std::abs(value) <= drawingViewOffsetLimitMm;
    });
}
void validateStroke(double strokeMm, DrawingStrokeDash dash) {
    require(std::isfinite(strokeMm) && strokeMm >= drawingStrokeMinMm &&
                strokeMm <= drawingStrokeMaxMm,
            "Drawing item stroke must be from 0.1 to 2 millimetres");
    require(dash == DrawingStrokeDash::Solid || dash == DrawingStrokeDash::Dashed,
            "Unknown drawing item dash style");
}
// R091 binds paper references to model vertices and edges only. Anchors are
// stored resolved; their broken state is derived at evaluation, never stored.
void validateViewAnchor(const AnnotationAnchor &anchor) {
    validateAnnotationAnchor(anchor);
    require(anchor.kind == AnchorKind::Vertex || anchor.kind == AnchorKind::Edge,
            "Drawing item anchors must be vertex or edge anchors");
    require(anchor.state == AnchorState::Resolved, "Drawing item anchors are stored resolved");
}
void validateBoundView(const DrawingViewRecords &views, Id view, Id sheet) {
    const auto found = views.find(view);
    require(found != views.end(), "Drawing item must reference an existing drawing view");
    require(found->second->sheet == sheet,
            "Drawing item must reference a drawing view on its own sheet");
}
struct DataValidator {
    const DrawingPaper &paper;
    const DrawingViewRecords &views;
    Id sheet;
    void operator()(const DrawingTextItem &item) const {
        item.frame.validate(paper);
        require(validItemText(item.text),
                "Drawing text must be 1 to 4096 bytes without control characters");
        item.font.validate();
        require(item.align == DrawingTextAlign::Left || item.align == DrawingTextAlign::Center ||
                    item.align == DrawingTextAlign::Right,
                "Unknown drawing text alignment");
    }
    void operator()(const DrawingLabelItem &item) const {
        require(validItemText(item.text),
                "Drawing label text must be 1 to 4096 bytes without control characters");
        item.font.validate();
        require(insidePaper(item.textAt, paper), "Drawing label text must lie inside its paper");
        const auto &target = item.target;
        switch (target.kind) {
        case DrawingLabelTargetKind::Paper:
            require(insidePaper(target.pointMm, paper),
                    "Drawing label target must lie inside its paper");
            require(!target.view && target.anchor == AnnotationAnchor{} &&
                        target.lastMm == DrawingPointMm{},
                    "A paper label target has no view reference");
            return;
        case DrawingLabelTargetKind::View:
            require(target.pointMm == DrawingPointMm{},
                    "A view label target has no fixed paper point");
            validateBoundView(views, target.view, sheet);
            validateViewAnchor(target.anchor);
            require(boundedDerived(target.lastMm),
                    "Drawing label last position must be finite and bounded");
            return;
        }
        throw std::runtime_error("Unknown drawing label target kind");
    }
    void operator()(const DrawingDimensionItem &item) const {
        validateBoundView(views, item.view, sheet);
        for (const auto &anchor : item.anchors)
            validateViewAnchor(anchor);
        require(item.orientation == DrawingDimensionOrientation::Aligned ||
                    item.orientation == DrawingDimensionOrientation::Horizontal ||
                    item.orientation == DrawingDimensionOrientation::Vertical,
                "Unknown drawing dimension orientation");
        require(std::isfinite(item.offsetMm) && std::abs(item.offsetMm) <= drawingViewOffsetLimitMm,
                "Drawing dimension offset must be finite and bounded");
        require(validShortText(item.prefix),
                "Drawing dimension prefix must be at most 256 bytes without control characters");
        item.font.validate();
        for (const auto &point : item.lastMm)
            require(boundedDerived(point),
                    "Drawing dimension last position must be finite and bounded");
    }
    void operator()(const DrawingLineItem &item) const {
        require(insidePaper(item.from, paper) && insidePaper(item.to, paper),
                "Drawing line must lie inside its paper");
        require(item.from != item.to, "Drawing line endpoints must differ");
        validateStroke(item.strokeMm, item.dash);
    }
    void operator()(const DrawingRectangleItem &item) const {
        item.frame.validate(paper);
        validateStroke(item.strokeMm, item.dash);
    }
    void operator()(const DrawingTitleBlockItem &item) const {
        item.frame.validate(paper);
        item.font.validate();
        for (const auto *field : {&item.fields.project, &item.fields.drawingTitle,
                                  &item.fields.date, &item.fields.revision})
            require(validShortText(*field), "Drawing title block fields must be at most 256 bytes "
                                            "without control characters");
    }
};
size_t dataBytes(const DrawingItemData &data) {
    return std::visit(
        [](const auto &item) -> size_t {
            using T = std::decay_t<decltype(item)>;
            if constexpr (std::is_same_v<T, DrawingTextItem>)
                return item.text.size() + item.font.family.size();
            else if constexpr (std::is_same_v<T, DrawingLabelItem>)
                return item.text.size() + item.font.family.size();
            else if constexpr (std::is_same_v<T, DrawingDimensionItem>)
                return item.prefix.size() + item.font.family.size();
            else if constexpr (std::is_same_v<T, DrawingTitleBlockItem>)
                return item.font.family.size() + item.fields.project.size() +
                       item.fields.drawingTitle.size() + item.fields.date.size() +
                       item.fields.revision.size();
            else
                return 0;
        },
        data);
}
} // namespace
void DrawingFont::validate() const {
    require(!family.empty() && family.size() <= drawingFontFamilyLimit && family.front() != ' ' &&
                family.back() != ' ' &&
                std::none_of(family.begin(), family.end(),
                             [](unsigned char c) { return control(c) || c == '/' || c == '\\'; }),
            "Drawing font family must be a 1 to 128 byte family name, not a path");
    require(std::isfinite(sizePt) && sizePt >= drawingFontMinPt && sizePt <= drawingFontMaxPt,
            "Drawing font size must be from 4 to 72 points");
}
void DrawingItemFrame::validate(const DrawingPaper &paper) const {
    for (const auto value : {xMm, yMm, widthMm, heightMm})
        require(std::isfinite(value), "Drawing item frame must be finite");
    require(widthMm > 0 && heightMm > 0, "Drawing item frame must have a positive size");
    require(xMm >= 0 && yMm >= 0 && xMm + widthMm <= paper.widthMm &&
                yMm + heightMm <= paper.heightMm,
            "Drawing item frame must lie inside its sheet paper");
}
size_t drawingItemBytes(const DrawingItemPtr &item) {
    return item ? sizeof(DrawingItemRecord) + dataBytes(item->data) + 64 : 0;
}
Id drawingItemView(const DrawingItemRecord &item) {
    if (const auto *dimension = std::get_if<DrawingDimensionItem>(&item.data))
        return dimension->view;
    if (const auto *label = std::get_if<DrawingLabelItem>(&item.data))
        return label->target.kind == DrawingLabelTargetKind::View ? label->target.view : 0;
    return 0;
}
size_t validateDrawingItemRecords(const DrawingSheetRecords &sheets,
                                  const DrawingViewRecords &views, const DrawingItemRecords &items,
                                  Id nextItem) {
    require(nextItem && items.size() <= drawingItemCountLimit,
            "Invalid drawing item allocator or count");
    std::map<Id, std::vector<bool>> stacks;
    size_t bytes = 0;
    for (const auto &[id, item] : items) {
        require(item && id && id < nextItem && item->id == id, "Invalid drawing item identity");
        const auto sheet = sheets.find(item->sheet);
        require(sheet != sheets.end(), "Drawing item must belong to an existing drawing sheet");
        require(item->data.index() != std::variant_npos, "Unknown drawing item kind");
        std::visit(DataValidator{sheet->second->paper, views, item->sheet}, item->data);
        auto &stack = stacks[item->sheet];
        require(stack.size() < drawingItemsPerSheetLimit,
                "A drawing sheet allows at most 512 items");
        stack.push_back(false);
        bytes += drawingItemBytes(item);
    }
    // z is a permutation of 0..n-1 on each sheet.
    for (const auto &[id, item] : items) {
        auto &stack = stacks.at(item->sheet);
        require(item->z < stack.size() && !stack[item->z],
                "Drawing item order must contain each position exactly once per sheet");
        stack[item->z] = true;
    }
    return bytes;
}
} // namespace sketchy
