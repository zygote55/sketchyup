#include "core/drawing_sheet_records.hpp"
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
    return sheet ? sizeof(DrawingSheetRecord) + sheet->name.size() + 64 : 0;
}
size_t drawingViewBytes(const DrawingViewPtr &view) {
    return view ? sizeof(DrawingViewRecord) + view->name.size() +
                      (view->accepted ? view->accepted->fingerprint.size() : 0) + 64
                : 0;
}
void validateDrawingSheetRecords(const DrawingSheetRecords &sheets, Id nextSheet,
                                 const DrawingViewRecords &views, Id nextView) {
    if (!nextSheet || !nextView || sheets.size() > drawingSheetCountLimit ||
        views.size() > drawingViewCountLimit)
        throw std::runtime_error("Invalid drawing sheet allocator or count");
    std::set<std::string> names;
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
    return (!edit.drawingSheets.empty() || !edit.drawingViews.empty()) && edit.changes.empty() &&
           edit.definitions.empty() && edit.instances.empty() && edit.tags.empty() &&
           edit.materials.empty() && edit.assets.empty() && edit.scenes.empty() &&
           edit.sections.empty() && edit.annotations.empty() && !edit.displayUnits &&
           !edit.displayPrecision && !edit.hosted && !edit.style && !edit.activeSections &&
           !edit.solar;
}
} // namespace sketchy
