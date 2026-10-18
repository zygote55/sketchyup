#include "core/drawing_sheet_records.hpp"
#include "core/drawing_item_records.hpp"
#include "core/model.hpp"
#include <algorithm>
#include <cmath>
namespace sketchy {
namespace {
bool validName(const std::string &name) {
    return !name.empty() && name.size() <= 1024 && name.front() != ' ' && name.back() != ' ' &&
           std::none_of(name.begin(), name.end(),
                        [](unsigned char c) { return c < 32 || c == 127; });
}
bool paperLength(double value) { return std::isfinite(value) && value >= 10 && value <= 2000; }
bool validNumber(const std::string &number) {
    return number.size() <= drawingSheetNumberLimit &&
           std::none_of(number.begin(), number.end(),
                        [](unsigned char c) { return c < 32 || c == 127; });
}
} // namespace
void DrawingPaper::validate() const {
    if (!paperLength(widthMm) || !paperLength(heightMm))
        throw std::runtime_error("Drawing sheet paper must be from 10 to 2000 millimetres");
}
void DrawingViewSource::validate() const {
    switch (kind) {
    case DrawingViewSourceKind::Scene:
        if (!scene)
            throw std::runtime_error("Drawing view source requires a nonzero scene identity");
        return;
    }
    throw std::runtime_error("Unknown drawing view source kind");
}
void DrawingFrame::validate(const DrawingPaper &paper) const {
    for (const auto value : {xMm, yMm, widthMm, heightMm})
        if (!std::isfinite(value))
            throw std::runtime_error("Drawing view frame must be finite");
    if (widthMm < 10 || heightMm < 10)
        throw std::runtime_error("Drawing view frame must be at least 10 millimetres on each side");
    if (xMm < 0 || yMm < 0 || xMm + widthMm > paper.widthMm || yMm + heightMm > paper.heightMm)
        throw std::runtime_error("Drawing view frame must lie inside its sheet paper");
}
void DrawingViewAcceptance::validate() const {
    if (fingerprint.size() != 64 ||
        !std::all_of(fingerprint.begin(), fingerprint.end(),
                     [](char c) { return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'); }))
        throw std::runtime_error(
            "Drawing view acceptance requires a lowercase SHA-256 fingerprint");
}
size_t drawingSheetBytes(const DrawingSheetPtr &sheet) {
    return sheet ? sizeof(DrawingSheetRecord) + sheet->name.size() + sheet->number.size() + 64 : 0;
}
void DrawingDimensionFormat::validate() const {
    if (unit != DrawingDimensionUnit::Millimetres && unit != DrawingDimensionUnit::Centimetres &&
        unit != DrawingDimensionUnit::Metres && unit != DrawingDimensionUnit::FeetInches)
        throw std::runtime_error("Unknown drawing dimension unit");
    if (decimals < 0 || decimals > 4)
        throw std::runtime_error("Drawing dimension decimals must be from 0 to 4");
}
double defaultDrawingSheetMarginMm(const DrawingPaper &paper) {
    return std::min(drawingSheetDefaultMarginMm, std::min(paper.widthMm, paper.heightMm) / 4);
}
size_t drawingViewBytes(const DrawingViewPtr &view) {
    return view ? sizeof(DrawingViewRecord) + view->name.size() +
                      (view->accepted ? view->accepted->fingerprint.size() : 0) + 64
                : 0;
}
void validateDrawingSheetRecords(const DrawingSheetRecords &sheets, Id nextSheet,
                                 const DrawingViewRecords &views, Id nextView,
                                 const DrawingItemRecords &items, Id nextItem) {
    if (!nextSheet || !nextView || sheets.size() > drawingSheetCountLimit ||
        views.size() > drawingViewCountLimit)
        throw std::runtime_error("Invalid drawing sheet allocator or count");
    std::set<std::string> names, numbers;
    std::set<std::uint32_t> positions;
    size_t bytes = 0;
    for (const auto &[id, sheet] : sheets) {
        if (!sheet || !id || id >= nextSheet || sheet->id != id || !validName(sheet->name))
            throw std::runtime_error("Invalid drawing sheet identity or name");
        if (!names.insert(sheet->name).second)
            throw std::runtime_error("Drawing sheet names must be unique");
        if (sheet->position >= sheets.size() || !positions.insert(sheet->position).second)
            throw std::runtime_error("Drawing sheet order must contain each position exactly once");
        sheet->paper.validate();
        if (!validNumber(sheet->number))
            throw std::runtime_error(
                "Drawing sheet number must be at most 32 bytes without control characters");
        if (!sheet->number.empty() && !numbers.insert(sheet->number).second)
            throw std::runtime_error("Drawing sheet numbers must be unique");
        if (!std::isfinite(sheet->marginMm) || sheet->marginMm < 0 ||
            sheet->marginMm > drawingSheetMarginLimitMm ||
            sheet->marginMm > std::min(sheet->paper.widthMm, sheet->paper.heightMm) / 4)
            throw std::runtime_error("Drawing sheet margin must be from 0 to 50 millimetres and at "
                                     "most a quarter of the shorter paper side");
        sheet->dimensionFormat.validate();
        bytes += drawingSheetBytes(sheet);
    }
    names.clear();
    std::map<Id, size_t> perSheet;
    for (const auto &[id, view] : views) {
        if (!view || !id || id >= nextView || view->id != id || !validName(view->name))
            throw std::runtime_error("Invalid drawing view identity or name");
        if (!names.insert(view->name).second)
            throw std::runtime_error("Drawing view names must be unique");
        const auto sheet = sheets.find(view->sheet);
        if (sheet == sheets.end())
            throw std::runtime_error("Drawing view must belong to an existing drawing sheet");
        if (++perSheet[view->sheet] > drawingViewsPerSheetLimit)
            throw std::runtime_error("A drawing sheet allows at most 64 views");
        view->source.validate();
        view->frame.validate(sheet->second->paper);
        for (const auto value : view->offsetMm)
            if (!std::isfinite(value) || std::abs(value) > drawingViewOffsetLimitMm)
                throw std::runtime_error("Drawing view offset must be finite and bounded");
        if (!std::isfinite(view->scaleDenominator) || view->scaleDenominator < .001 ||
            view->scaleDenominator > 1e9)
            throw std::runtime_error("Drawing view scale denominator must be from 0.001 to 1e9");
        switch (view->content) {
        case DrawingViewContent::TechnicalLines:
        case DrawingViewContent::RasterAppearance:
            break;
        default:
            throw std::runtime_error("Unknown drawing view content mode");
        }
        if (view->accepted)
            view->accepted->validate();
        bytes += drawingViewBytes(view);
    }
    bytes += validateDrawingItemRecords(sheets, views, items, nextItem);
    if (bytes > drawingSheetBytesLimit)
        throw std::runtime_error("Drawing sheets exceed the eight MiB record budget");
}
bool drawingViewSourceExists(const SceneRecords &scenes, const DrawingViewSource &source) {
    return source.kind == DrawingViewSourceKind::Scene && scenes.contains(source.scene);
}
std::set<Id> drawingViewsWithMissingSource(const Document &doc) {
    std::set<Id> missing;
    for (const auto &[id, view] : doc.drawingViews())
        if (!drawingViewSourceExists(doc.scenes(), view->source))
            missing.insert(id);
    return missing;
}
bool isDrawingSheetOnlyEdit(const Edit &edit) {
    return (!edit.drawingSheets.empty() || !edit.drawingViews.empty() ||
            !edit.drawingItems.empty()) &&
           edit.changes.empty() && edit.definitions.empty() && edit.instances.empty() &&
           edit.tags.empty() && edit.materials.empty() && edit.assets.empty() &&
           edit.scenes.empty() && edit.sections.empty() && edit.annotations.empty() &&
           !edit.displayUnits && !edit.displayPrecision && !edit.hosted && !edit.style &&
           !edit.activeSections && !edit.solar && !edit.site;
}
} // namespace sketchy
