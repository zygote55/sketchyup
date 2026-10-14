#include "core/drawing_sheets.hpp"
#include <set>
namespace sketchy {
namespace {
DrawingSheetPtr requireDrawingSheet(const Document &doc, Id id) {
    const auto found = doc.drawingSheets().find(id);
    if (found == doc.drawingSheets().end())
        throw std::runtime_error("Drawing sheet does not exist");
    return found->second;
}
DrawingViewPtr requireDrawingView(const Document &doc, Id id) {
    const auto found = doc.drawingViews().find(id);
    if (found == doc.drawingViews().end())
        throw std::runtime_error("Drawing view does not exist");
    return found->second;
}
Edit sheetEdit(std::string label) {
    Edit edit{std::move(label), {}};
    edit.drawingSheetScope = true;
    return edit;
}
} // namespace
Id createDrawingSheet(Document &doc, std::string name, DrawingPaper paper) {
    const auto id = doc.nextDrawingSheetId();
    auto sheet = std::make_shared<DrawingSheetRecord>(DrawingSheetRecord{
        id, std::move(name), static_cast<std::uint32_t>(doc.drawingSheets().size()), paper});
    auto edit = sheetEdit("Create sheet");
    edit.drawingSheets.push_back({id, nullptr, sheet});
    doc.apply(std::move(edit), doc.revision());
    return id;
}
void renameDrawingSheet(Document &doc, Id id, std::string name) {
    const auto before = requireDrawingSheet(doc, id);
    if (before->name == name)
        return;
    auto after = std::make_shared<DrawingSheetRecord>(*before);
    after->name = std::move(name);
    auto edit = sheetEdit("Rename sheet");
    edit.drawingSheets.push_back({id, before, after});
    doc.apply(std::move(edit), doc.revision());
}
void resizeDrawingSheet(Document &doc, Id id, DrawingPaper paper) {
    const auto before = requireDrawingSheet(doc, id);
    if (before->paper == paper)
        return;
    auto after = std::make_shared<DrawingSheetRecord>(*before);
    after->paper = paper;
    auto edit = sheetEdit("Resize sheet");
    edit.drawingSheets.push_back({id, before, after});
    doc.apply(std::move(edit), doc.revision());
}
std::vector<Id> orderedDrawingSheets(const Document &doc) {
    std::vector<Id> order(doc.drawingSheets().size());
    for (const auto &[id, sheet] : doc.drawingSheets())
        order.at(sheet->position) = id;
    return order;
}
void reorderDrawingSheets(Document &doc, const std::vector<Id> &order) {
    if (order.size() != doc.drawingSheets().size() ||
        std::set<Id>(order.begin(), order.end()).size() != order.size())
        throw std::runtime_error("Drawing sheet order must contain every sheet exactly once");
    auto edit = sheetEdit("Reorder sheets");
    for (size_t i = 0; i < order.size(); ++i) {
        const auto before = requireDrawingSheet(doc, order[i]);
        if (before->position == i)
            continue;
        auto after = std::make_shared<DrawingSheetRecord>(*before);
        after->position = static_cast<std::uint32_t>(i);
        edit.drawingSheets.push_back({before->id, before, after});
    }
    if (!edit.drawingSheets.empty())
        doc.apply(std::move(edit), doc.revision());
}
void eraseDrawingSheet(Document &doc, Id id) {
    const auto before = requireDrawingSheet(doc, id);
    auto edit = sheetEdit("Delete sheet");
    edit.drawingSheets.push_back({id, before, nullptr});
    for (const auto &[other, sheet] : doc.drawingSheets()) {
        if (sheet->position <= before->position)
            continue;
        auto after = std::make_shared<DrawingSheetRecord>(*sheet);
        --after->position;
        edit.drawingSheets.push_back({other, sheet, after});
    }
    for (const auto &[view, record] : doc.drawingViews())
        if (record->sheet == id)
            edit.drawingViews.push_back({view, record, nullptr});
    doc.apply(std::move(edit), doc.revision());
}
std::vector<Id> drawingViewsOnSheet(const Document &doc, Id sheet) {
    std::vector<Id> views;
    for (const auto &[id, view] : doc.drawingViews())
        if (view->sheet == sheet)
            views.push_back(id);
    return views;
}
Id createDrawingView(Document &doc, DrawingViewRecord view) {
    const auto id = doc.nextDrawingViewId();
    view.id = id;
    auto edit = sheetEdit("Create sheet view");
    edit.drawingViews.push_back(
        {id, nullptr, std::make_shared<DrawingViewRecord>(std::move(view))});
    doc.apply(std::move(edit), doc.revision());
    return id;
}
void updateDrawingView(Document &doc, Id id, DrawingViewRecord record) {
    const auto before = requireDrawingView(doc, id);
    record.id = id;
    if (*before == record)
        return;
    auto edit = sheetEdit("Update sheet view");
    edit.drawingViews.push_back(
        {id, before, std::make_shared<DrawingViewRecord>(std::move(record))});
    doc.apply(std::move(edit), doc.revision());
}
void moveDrawingView(Document &doc, Id id, Id sheet) {
    const auto before = requireDrawingView(doc, id);
    requireDrawingSheet(doc, sheet);
    if (before->sheet == sheet)
        return;
    auto after = std::make_shared<DrawingViewRecord>(*before);
    after->sheet = sheet;
    auto edit = sheetEdit("Move sheet view");
    edit.drawingViews.push_back({id, before, after});
    doc.apply(std::move(edit), doc.revision());
}
void eraseDrawingView(Document &doc, Id id) {
    const auto before = requireDrawingView(doc, id);
    auto edit = sheetEdit("Delete sheet view");
    edit.drawingViews.push_back({id, before, nullptr});
    doc.apply(std::move(edit), doc.revision());
}
void acceptDrawingViews(Document &doc, const std::map<Id, DrawingViewAcceptance> &stamps) {
    auto edit = sheetEdit(stamps.size() == 1 ? "Accept sheet view" : "Accept sheet views");
    for (const auto &[id, stamp] : stamps) {
        const auto before = requireDrawingView(doc, id);
        if (before->accepted == stamp)
            continue;
        auto after = std::make_shared<DrawingViewRecord>(*before);
        after->accepted = stamp;
        edit.drawingViews.push_back({id, before, after});
    }
    if (!edit.drawingViews.empty())
        doc.apply(std::move(edit), doc.revision());
}
} // namespace sketchy
