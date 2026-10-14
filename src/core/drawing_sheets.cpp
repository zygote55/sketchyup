#include "core/drawing_sheets.hpp"
#include <algorithm>
#include <cmath>
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
DrawingItemPtr requireDrawingItem(const Document &doc, Id id) {
    const auto found = doc.drawingItems().find(id);
    if (found == doc.drawingItems().end())
        throw std::runtime_error("Drawing item does not exist");
    return found->second;
}
Edit sheetEdit(std::string label) {
    Edit edit{std::move(label), {}};
    edit.drawingSheetScope = true;
    return edit;
}
// Appends item changes so each listed sheet holds exactly its order, bottom first.
// Items absent from every order are left alone; the caller erases them.
void restack(const Document &doc, Edit &edit, const std::map<Id, std::vector<Id>> &orders) {
    for (const auto &[sheet, order] : orders)
        for (size_t z = 0; z < order.size(); ++z) {
            const auto before = requireDrawingItem(doc, order[z]);
            if (before->sheet == sheet && before->z == z)
                continue;
            auto after = std::make_shared<DrawingItemRecord>(*before);
            after->sheet = sheet;
            after->z = static_cast<std::uint32_t>(z);
            edit.drawingItems.push_back({before->id, before, after});
        }
}
// Items bound to a view (ADR 0164 cascades), bottom first.
std::vector<Id> boundDrawingItems(const Document &doc, Id view) {
    std::vector<DrawingItemPtr> bound;
    for (const auto &[id, item] : doc.drawingItems())
        if (drawingItemView(*item) == view)
            bound.push_back(item);
    std::sort(bound.begin(), bound.end(), [](const auto &a, const auto &b) {
        return a->sheet != b->sheet ? a->sheet < b->sheet : a->z < b->z;
    });
    std::vector<Id> ids;
    for (const auto &item : bound)
        ids.push_back(item->id);
    return ids;
}
std::vector<Id> without(std::vector<Id> order, const std::vector<Id> &removed) {
    std::erase_if(order, [&](Id id) {
        return std::find(removed.begin(), removed.end(), id) != removed.end();
    });
    return order;
}
// A view moving from one sheet to another takes its bound items to the top of the
// target sheet's z order, keeping their relative order.
void appendBoundItemMove(const Document &doc, Edit &edit, Id view, Id from, Id to) {
    const auto bound = boundDrawingItems(doc, view);
    if (bound.empty())
        return;
    auto target = drawingItemsOnSheet(doc, to);
    target.insert(target.end(), bound.begin(), bound.end());
    restack(doc, edit, {{from, without(drawingItemsOnSheet(doc, from), bound)}, {to, target}});
}
} // namespace
Id createDrawingSheet(Document &doc, std::string name, DrawingPaper paper, std::string number,
                      std::optional<double> marginMm) {
    const auto id = doc.nextDrawingSheetId();
    auto sheet = std::make_shared<DrawingSheetRecord>(DrawingSheetRecord{
        id, std::move(name), static_cast<std::uint32_t>(doc.drawingSheets().size()), paper,
        std::move(number), marginMm ? *marginMm : defaultDrawingSheetMarginMm(paper)});
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
    if (std::isfinite(paper.widthMm) && std::isfinite(paper.heightMm))
        after->marginMm = std::min(after->marginMm, std::min(paper.widthMm, paper.heightMm) / 4);
    auto edit = sheetEdit("Resize sheet");
    edit.drawingSheets.push_back({id, before, after});
    doc.apply(std::move(edit), doc.revision());
}
void updateDrawingSheetPage(Document &doc, Id id, std::string number, double marginMm) {
    const auto before = requireDrawingSheet(doc, id);
    if (before->number == number && before->marginMm == marginMm)
        return;
    auto after = std::make_shared<DrawingSheetRecord>(*before);
    after->number = std::move(number);
    after->marginMm = marginMm;
    auto edit = sheetEdit("Update sheet");
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
    for (const auto &[item, record] : doc.drawingItems())
        if (record->sheet == id)
            edit.drawingItems.push_back({item, record, nullptr});
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
    const auto sheet = record.sheet;
    auto edit = sheetEdit("Update sheet view");
    edit.drawingViews.push_back(
        {id, before, std::make_shared<DrawingViewRecord>(std::move(record))});
    if (sheet != before->sheet && doc.drawingSheets().contains(sheet))
        appendBoundItemMove(doc, edit, id, before->sheet, sheet);
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
    appendBoundItemMove(doc, edit, id, before->sheet, sheet);
    doc.apply(std::move(edit), doc.revision());
}
void eraseDrawingView(Document &doc, Id id) {
    const auto before = requireDrawingView(doc, id);
    auto edit = sheetEdit("Delete sheet view");
    edit.drawingViews.push_back({id, before, nullptr});
    const auto bound = boundDrawingItems(doc, id);
    for (const auto item : bound)
        edit.drawingItems.push_back({item, doc.drawingItems().at(item), nullptr});
    if (!bound.empty())
        restack(doc, edit,
                {{before->sheet, without(drawingItemsOnSheet(doc, before->sheet), bound)}});
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
std::vector<Id> drawingItemsOnSheet(const Document &doc, Id sheet) {
    std::vector<DrawingItemPtr> items;
    for (const auto &[id, item] : doc.drawingItems())
        if (item->sheet == sheet)
            items.push_back(item);
    std::sort(items.begin(), items.end(), [](const auto &a, const auto &b) { return a->z < b->z; });
    std::vector<Id> ids;
    for (const auto &item : items)
        ids.push_back(item->id);
    return ids;
}
Id createDrawingItem(Document &doc, Id sheet, DrawingItemData data) {
    requireDrawingSheet(doc, sheet);
    const auto id = doc.nextDrawingItemId();
    auto item = std::make_shared<DrawingItemRecord>(DrawingItemRecord{
        id, sheet, static_cast<std::uint32_t>(drawingItemsOnSheet(doc, sheet).size()),
        std::move(data)});
    auto edit = sheetEdit("Create sheet item");
    edit.drawingItems.push_back({id, nullptr, item});
    doc.apply(std::move(edit), doc.revision());
    return id;
}
void updateDrawingItem(Document &doc, Id id, DrawingItemData data) {
    const auto before = requireDrawingItem(doc, id);
    if (before->data == data)
        return;
    auto after = std::make_shared<DrawingItemRecord>(*before);
    after->data = std::move(data);
    auto edit = sheetEdit("Update sheet item");
    edit.drawingItems.push_back({id, before, after});
    doc.apply(std::move(edit), doc.revision());
}
void moveDrawingItem(Document &doc, Id id, Id sheet) {
    const auto before = requireDrawingItem(doc, id);
    requireDrawingSheet(doc, sheet);
    if (before->sheet == sheet)
        return;
    auto target = drawingItemsOnSheet(doc, sheet);
    target.push_back(id);
    auto edit = sheetEdit("Move sheet item");
    restack(
        doc, edit,
        {{before->sheet, without(drawingItemsOnSheet(doc, before->sheet), {id})}, {sheet, target}});
    doc.apply(std::move(edit), doc.revision());
}
void reorderDrawingItems(Document &doc, Id sheet, const std::vector<Id> &order) {
    requireDrawingSheet(doc, sheet);
    auto current = drawingItemsOnSheet(doc, sheet);
    auto sorted = order;
    std::sort(sorted.begin(), sorted.end());
    std::sort(current.begin(), current.end());
    if (sorted != current)
        throw std::runtime_error(
            "Drawing item order must contain every item on the sheet exactly once");
    auto edit = sheetEdit("Reorder sheet items");
    restack(doc, edit, {{sheet, order}});
    if (!edit.drawingItems.empty())
        doc.apply(std::move(edit), doc.revision());
}
void eraseDrawingItem(Document &doc, Id id) {
    const auto before = requireDrawingItem(doc, id);
    auto edit = sheetEdit("Delete sheet item");
    edit.drawingItems.push_back({id, before, nullptr});
    restack(doc, edit, {{before->sheet, without(drawingItemsOnSheet(doc, before->sheet), {id})}});
    doc.apply(std::move(edit), doc.revision());
}
} // namespace sketchy
