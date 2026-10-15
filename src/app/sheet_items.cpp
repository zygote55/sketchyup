// R091.g: page items on the canvas (ADR 0164 §§3–5, decisions 2 and 11; design §5). The Line,
// Rectangle, Text, Label and Dimension tools, item selection, moving and nudging, in-place text
// editing, Reattach and the Measurements grammar for items. Every change goes through the
// committer as drawing_item.* commands, one labelled sheet-only entry; previews are local canvas
// state and Esc cancels them without an entry.
#include "app/sheet_items.hpp"
#include "app/sheet_canvas.hpp"
#include "app/sheet_text.hpp"
#include "automation/measurements.hpp"
#include "core/drawing_sheets.hpp"
#include "io/drawing_items_io.hpp"
#include "presentation/unit_display.hpp"
#include <QAccessible>
#include <QApplication>
#include <QFocusEvent>
#include <QFontDatabase>
#include <QFontMetricsF>
#include <QKeyEvent>
#include <QLineEdit>
#include <QLocale>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPainterPathStroker>
#include <QPlainTextEdit>
#include <QRegularExpression>
#include <QScopedValueRollback>
#include <algorithm>
#include <cmath>
#include <numbers>
namespace sketchy {
namespace {
constexpr double hitPixels = 4;
constexpr double minimumTextWidthMm = 2, textSlackMm = 1;
constexpr double millimetresPerPoint = 25.4 / 72;
int itemSerial = 0;
QRectF rectOf(const DrawingItemFrame &frame) {
    return {frame.xMm, frame.yMm, frame.widthMm, frame.heightMm};
}
QPointF qpoint(const DrawingPointMm &point) { return {point[0], point[1]}; }
DrawingPointMm mm(QPointF point) { return {point.x(), point.y()}; }
double tidy(double value, double step = .1) { return std::round(value / step) * step; }
QPointF tidy(QPointF value, double step = .1) {
    return {tidy(value.x(), step), tidy(value.y(), step)};
}
QString itemKey(const QString &kind, Id item) {
    return QString("%1:%2#%3").arg(kind).arg(item).arg(++itemSerial);
}
bool itemKeyOf(const QString &key, const QString &kind, Id item) {
    return key.startsWith(QString("%1:%2#").arg(kind).arg(item));
}
QJsonArray pointJson(QPointF point) { return {point.x(), point.y()}; }
QJsonObject frameJson(const QRectF &frame) {
    return {{"xMm", frame.x()},
            {"yMm", frame.y()},
            {"widthMm", frame.width()},
            {"heightMm", frame.height()}};
}
QJsonObject fontJson(const DrawingFont &font) {
    return {{"family", QString::fromStdString(font.family)}, {"sizePt", font.sizePt}};
}
QFont uiFont(const QWidget *widget, int pixels, bool bold = false) {
    QFont font = widget->font();
    font.setPixelSize(pixels);
    if (bold)
        font.setWeight(QFont::DemiBold);
    return font;
}
// Paper millimetres typed in the Measurements box, as for views.
std::vector<double> lengths(QString text) {
    const QLocale locale;
    const auto separator = locale.decimalPoint() == "," ? ';' : ',';
    const auto pieces = text.split(text.contains(';') ? ';' : separator, Qt::KeepEmptyParts);
    std::vector<double> values;
    for (const auto &piece : pieces) {
        if (piece.trimmed().isEmpty())
            throw std::runtime_error("Type a value on each side of the separator");
        values.push_back(std::round(parseLength(piece.trimmed(), "mm", locale) * 1e9) / 1e6);
    }
    return values;
}
// The bounds of an item's stored geometry, used to keep moves on the paper.
std::optional<QRectF> storedBounds(const DrawingItemRecord &record) {
    if (const auto *t = std::get_if<DrawingTextItem>(&record.data))
        return rectOf(t->frame);
    if (const auto *l = std::get_if<DrawingLabelItem>(&record.data)) {
        QRectF bounds(qpoint(l->textAt), QSizeF(0, 0));
        if (l->target.kind == DrawingLabelTargetKind::Paper)
            bounds = QRectF(qpoint(l->textAt), qpoint(l->target.pointMm)).normalized();
        return bounds;
    }
    if (const auto *line = std::get_if<DrawingLineItem>(&record.data))
        return QRectF(qpoint(line->from), qpoint(line->to)).normalized();
    if (const auto *r = std::get_if<DrawingRectangleItem>(&record.data))
        return rectOf(r->frame);
    if (const auto *b = std::get_if<DrawingTitleBlockItem>(&record.data))
        return rectOf(b->frame);
    return std::nullopt;
}
const std::vector<std::pair<QString, QString>> &titleFields() {
    static const std::vector<std::pair<QString, QString>> fields{{"project", "Project"},
                                                                 {"drawingTitle", "Drawing"},
                                                                 {"date", "Date"},
                                                                 {"revision", "Revision"}};
    return fields;
}
std::string titleValue(const DrawingTitleBlockFields &fields, const QString &field) {
    return field == "project"        ? fields.project
           : field == "drawingTitle" ? fields.drawingTitle
           : field == "date"         ? fields.date
                                     : fields.revision;
}
} // namespace

// ---- Shared helpers ----------------------------------------------------------------------
QString sheetItemKindName(DrawingItemKind kind) {
    switch (kind) {
    case DrawingItemKind::Text:
        return "Text";
    case DrawingItemKind::Label:
        return "Label";
    case DrawingItemKind::Dimension:
        return "Dimension";
    case DrawingItemKind::Line:
        return "Line";
    case DrawingItemKind::Rectangle:
        return "Rectangle";
    case DrawingItemKind::TitleBlock:
        return "Title block";
    }
    return "Item";
}
const char *sheetItemKindCode(DrawingItemKind kind) {
    switch (kind) {
    case DrawingItemKind::Text:
        return "text";
    case DrawingItemKind::Label:
        return "label";
    case DrawingItemKind::Dimension:
        return "dimension";
    case DrawingItemKind::Line:
        return "line";
    case DrawingItemKind::Rectangle:
        return "rectangle";
    case DrawingItemKind::TitleBlock:
        return "titleBlock";
    }
    return "text";
}
QJsonObject sheetItemData(const DrawingItemRecord &record) {
    auto copy = std::make_shared<DrawingItemRecord>(record);
    if (!copy->id)
        copy->id = 1;
    if (!copy->sheet)
        copy->sheet = 1;
    DrawingItemRecords items;
    items[copy->id] = copy;
    return encodeDrawingItems(items).first().toObject()["data"].toObject();
}
QJsonObject sheetItemChanges(const DrawingItemRecord &before, const DrawingItemRecord &after) {
    const auto a = sheetItemData(before), b = sheetItemData(after);
    QJsonObject result;
    for (auto it = b.begin(); it != b.end(); ++it) {
        const auto key = it.key();
        if (key == "view" || key == "anchors" || key == "lastMm" || a.value(key) == it.value())
            continue;
        if (key == "fields") {
            QJsonObject fields;
            const auto old = a.value(key).toObject(), now = it.value().toObject();
            for (auto field = now.begin(); field != now.end(); ++field)
                if (old.value(field.key()) != field.value())
                    fields[field.key()] = field.value();
            result[key] = fields;
            continue;
        }
        if (key == "target" && it.value().toObject()["kind"] != "paper")
            continue; // A view target changes only through drawing_item.reattach.
        result[key] = it.value();
    }
    return result;
}
QJsonObject sheetAnchorInput(const AnnotationAnchor &anchor) {
    if (anchor.kind == AnchorKind::Vertex)
        return {{"kind", "vertex"},
                {"body", QString::number(anchor.body)},
                {"vertex", QString::number(anchor.entity)}};
    return {{"kind", "edge"},
            {"body", QString::number(anchor.body)},
            {"edge", QString::number(anchor.entity)},
            {"fraction", anchor.parameter}};
}
QPointF sheetItemOrigin(const DrawingItemRecord &record) {
    if (const auto bounds = storedBounds(record)) {
        if (const auto *l = std::get_if<DrawingLabelItem>(&record.data))
            return qpoint(l->textAt);
        return bounds->topLeft();
    }
    if (const auto *d = std::get_if<DrawingDimensionItem>(&record.data))
        return QRectF(qpoint(d->lastMm[0]), qpoint(d->lastMm[1])).normalized().topLeft();
    return {};
}
DrawingItemRecord movedSheetItem(const Document &doc, const DrawingItemRecord &record,
                                 QPointF delta, const DrawingPaper &paper) {
    auto result = record;
    if (auto *d = std::get_if<DrawingDimensionItem>(&result.data)) {
        // The line moves by its offset; its anchors stay on the model.
        DrawingPointMm p0 = d->lastMm[0], p1 = d->lastMm[1];
        try {
            const auto evaluation = evaluateDrawingItem(doc, record);
            if (evaluation.measuredMm) {
                p0 = (*evaluation.measuredMm)[0];
                p1 = (*evaluation.measuredMm)[1];
            }
        } catch (const std::exception &) {
        }
        const auto through = placeDrawingDimension(p0, p1, d->orientation, d->offsetMm).textMm;
        const auto offset = drawingDimensionOffset(
            p0, p1, d->orientation, {through[0] + delta.x(), through[1] + delta.y()});
        d->offsetMm = std::clamp(std::round(offset * 1e6) / 1e6, -drawingViewOffsetLimitMm,
                                 drawingViewOffsetLimitMm);
        return result;
    }
    const auto bounds = storedBounds(record);
    if (!bounds)
        return result;
    // Keep the stored geometry on the paper.
    delta = {std::round(delta.x() * 1e6) / 1e6, std::round(delta.y() * 1e6) / 1e6};
    delta.setX(std::clamp(delta.x(), -bounds->left(), paper.widthMm - bounds->right()));
    delta.setY(std::clamp(delta.y(), -bounds->top(), paper.heightMm - bounds->bottom()));
    auto shift = [&](DrawingPointMm &point) {
        point = {std::clamp(std::round((point[0] + delta.x()) * 1e6) / 1e6, 0., paper.widthMm),
                 std::clamp(std::round((point[1] + delta.y()) * 1e6) / 1e6, 0., paper.heightMm)};
    };
    auto shiftFrame = [&](DrawingItemFrame &frame) {
        frame.xMm = std::clamp(std::round((frame.xMm + delta.x()) * 1e6) / 1e6, 0.,
                               paper.widthMm - frame.widthMm);
        frame.yMm = std::clamp(std::round((frame.yMm + delta.y()) * 1e6) / 1e6, 0.,
                               paper.heightMm - frame.heightMm);
    };
    if (auto *t = std::get_if<DrawingTextItem>(&result.data))
        shiftFrame(t->frame);
    else if (auto *l = std::get_if<DrawingLabelItem>(&result.data)) {
        shift(l->textAt);
        // A paper target moves with its text; a model target keeps the leader on the model.
        if (l->target.kind == DrawingLabelTargetKind::Paper)
            shift(l->target.pointMm);
    } else if (auto *line = std::get_if<DrawingLineItem>(&result.data)) {
        shift(line->from);
        shift(line->to);
    } else if (auto *r = std::get_if<DrawingRectangleItem>(&result.data))
        shiftFrame(r->frame);
    else if (auto *b = std::get_if<DrawingTitleBlockItem>(&result.data))
        shiftFrame(b->frame);
    return result;
}
QString sheetItemStateText(const DrawingItemEvaluation &evaluation) {
    if (evaluation.state == DrawingItemState::Current)
        return "Current";
    return "Broken: " + (evaluation.reasons.empty() ? QString("reference missing")
                                                    : brokenReasonText(evaluation.reasons.front()));
}
QString sheetItemSummary(const DrawingItemRecord &record, const DrawingItemEvaluation *evaluation) {
    const auto firstLine = [](const std::string &text) {
        return QString::fromStdString(text).section('\n', 0, 0).left(40);
    };
    switch (record.kind()) {
    case DrawingItemKind::Text:
        return "Text · " + firstLine(std::get<DrawingTextItem>(record.data).text);
    case DrawingItemKind::Label:
        return "Label · " + firstLine(std::get<DrawingLabelItem>(record.data).text);
    case DrawingItemKind::Dimension:
        return evaluation && !evaluation->text.empty()
                   ? "Dimension · " + QString::fromStdString(evaluation->text)
                   : QString("Dimension");
    case DrawingItemKind::Line: {
        const auto &line = std::get<DrawingLineItem>(record.data);
        return "Line · " +
               paperMillimetres(tidy(QLineF(qpoint(line.from), qpoint(line.to)).length())) + " mm";
    }
    case DrawingItemKind::Rectangle: {
        const auto &frame = std::get<DrawingRectangleItem>(record.data).frame;
        return "Rectangle · " + paperMillimetres(tidy(frame.widthMm)) + " × " +
               paperMillimetres(tidy(frame.heightMm)) + " mm";
    }
    case DrawingItemKind::TitleBlock: {
        const auto &project = std::get<DrawingTitleBlockItem>(record.data).fields.project;
        return project.empty() ? QString("Title block") : "Title block · " + firstLine(project);
    }
    }
    return "Item";
}

// ---- Records, entries and the live display ----------------------------------------------
const DrawingItemRecord *SheetCanvas::liveItem(Id item) const {
    const auto found = doc_.drawingItems().find(item);
    return found == doc_.drawingItems().end() || found->second->sheet != sheet_
               ? nullptr
               : found->second.get();
}
std::vector<Id> SheetCanvas::items() const {
    return record() ? drawingItemsOnSheet(doc_, sheet_) : std::vector<Id>{};
}
std::vector<SheetCanvas::Entry> SheetCanvas::entries() const {
    std::vector<Entry> result;
    for (const auto view : views())
        result.push_back({false, view});
    for (const auto item : items())
        result.push_back({true, item});
    return result;
}
const SheetCanvas::LiveItems &SheetCanvas::liveItems() const {
    const auto evaluation = cache_.sheet(sheet_);
    if (live_.built && live_.source == evaluation.get() && live_.identity == doc_.identity() &&
        live_.revision == doc_.revision() && live_.sheet == sheet_ && live_.preview == previewItem_)
        return live_;
    LiveItems next;
    next.built = true;
    next.source = evaluation.get();
    next.identity = doc_.identity();
    next.revision = doc_.revision();
    next.sheet = sheet_;
    next.preview = previewItem_;
    const auto *sheet = record();
    if (!sheet) {
        live_ = std::move(next);
        return live_;
    }
    // Items whose record is what was evaluated reuse the evaluated display list.
    std::map<Id, std::vector<DrawingSheetPrimitive>> evaluated;
    std::map<Id, const DrawingItemRecord *> evaluatedRecords;
    if (evaluation && evaluation->error.empty()) {
        for (const auto &prepared : evaluation->prepared.items)
            evaluatedRecords[prepared.record.id] = &prepared.record;
        for (const auto &primitive : evaluation->prepared.primitives)
            if (primitive.layer == DrawingSheetLayer::Item)
                evaluated[primitive.source].push_back(primitive);
    }
    for (const auto id : items()) {
        auto now = *liveItem(id);
        if (previewItem_ && previewItem_->id == id)
            now = *previewItem_;
        std::vector<DrawingSheetPrimitive> primitives;
        const auto found = evaluatedRecords.find(id);
        if (found != evaluatedRecords.end() && found->second->data == now.data &&
            found->second->sheet == now.sheet)
            primitives = evaluated[id];
        else
            try {
                primitives = drawingItemPrimitives(*sheet, {now, evaluateDrawingItem(doc_, now)});
            } catch (const std::exception &) {
                // An item that cannot be shaped is drawn after the next evaluation.
            }
        next.primitives.emplace_back(id, std::move(primitives));
    }
    live_ = std::move(next);
    return live_;
}
bool SheetCanvas::itemsDiffer(const PreparedDrawingSheet &prepared) const {
    if (previewItem_ || (textEditing() && editing_.item))
        return true;
    const auto ids = items();
    if (ids.size() != prepared.items.size())
        return true;
    for (size_t i = 0; i < ids.size(); ++i)
        if (prepared.items[i].record.id != ids[i] ||
            prepared.items[i].record.data != liveItem(ids[i])->data)
            return true;
    return false;
}
void SheetCanvas::replaceItems(PreparedDrawingSheet &sheet) const {
    std::erase_if(sheet.primitives, [](const DrawingSheetPrimitive &primitive) {
        return primitive.layer == DrawingSheetLayer::Item;
    });
    const bool hideText =
        textEditing() && editing_.item && editing_.kind != DrawingItemKind::TitleBlock;
    for (const auto &[id, primitives] : liveItems().primitives)
        for (const auto &primitive : primitives) {
            // The item's own text hides under its in-place editor.
            if (hideText && id == editing_.item && primitive.fill && !primitive.text.isEmpty())
                continue;
            sheet.primitives.push_back(primitive);
        }
}
const PreparedDrawingView *SheetCanvas::preparedView(Id view) const {
    const auto evaluation = cache_.sheet(sheet_);
    if (!evaluation || !evaluation->error.empty())
        return nullptr;
    for (const auto &prepared : evaluation->prepared.views)
        if (prepared.record.id == view)
            return &prepared;
    return nullptr;
}
std::optional<DrawingViewAnchorHit> SheetCanvas::anchorNear(Id view, QPointF point,
                                                            double toleranceMm) const {
    // Keep the evaluation alive while its drawing is searched.
    const auto evaluation = cache_.sheet(sheet_);
    const auto *prepared = preparedView(view);
    if (!prepared || !prepared->evaluation.projection || !prepared->evaluation.drawing ||
        !liveView(view))
        return std::nullopt;
    // A view moved since it was evaluated draws the same lines translated.
    const auto &projection = *prepared->evaluation.projection;
    const auto live = effectiveView(view).frame;
    QPointF shift;
    if (std::abs(live.widthMm - projection.frame.widthMm) < 1e-9 &&
        std::abs(live.heightMm - projection.frame.heightMm) < 1e-9)
        shift = {live.xMm - projection.frame.xMm, live.yMm - projection.frame.yMm};
    auto hit = nearestDrawingViewAnchor(doc_, projection, *prepared->evaluation.drawing,
                                        mm(point - shift), toleranceMm, toleranceMm);
    if (hit)
        hit->paperMm = {hit->paperMm[0] + shift.x(), hit->paperMm[1] + shift.y()};
    return hit;
}

// ---- Selection ---------------------------------------------------------------------------
void SheetCanvas::setSelectedItem(Id item) {
    if (item && !liveItem(item))
        item = 0;
    if (item == selectedItem_ && !selectedView_)
        return;
    if (textEditing())
        endTextEdit();
    cancelReattach();
    if (editView_)
        leaveEditView();
    endRevision();
    const bool hadView = selectedView_ != 0;
    selectedView_ = 0;
    selectedItem_ = item;
    update();
    announce();
    if (hadView)
        emit selectionChanged(0);
    emit itemSelectionChanged(item);
    emit stepChanged();
}
void SheetCanvas::dropItemSelection() {
    if (!selectedItem_)
        return;
    selectedItem_ = 0;
    emit itemSelectionChanged(0);
}
QRectF SheetCanvas::itemRect(Id item) const {
    const auto *live = liveItem(item);
    if (!live)
        return {};
    QRectF bounds;
    for (const auto &[id, primitives] : liveItems().primitives) {
        if (id != item)
            continue;
        for (const auto &primitive : primitives) {
            const auto extent = primitive.fill && !primitive.text.isEmpty()
                                    ? primitive.textBoxMm
                                    : primitive.path.boundingRect().adjusted(
                                          -primitive.strokeMm / 2, -primitive.strokeMm / 2,
                                          primitive.strokeMm / 2, primitive.strokeMm / 2);
            bounds = bounds.isNull() ? extent : bounds.united(extent);
        }
    }
    if (bounds.isNull()) {
        const auto record = previewItem_ && previewItem_->id == item ? *previewItem_ : *live;
        bounds = storedBounds(record).value_or(QRectF(sheetItemOrigin(record), QSizeF(1, 1)));
    }
    return paperToWidget().mapRect(bounds);
}
Id SheetCanvas::itemAt(QPointF point) const {
    const auto slack = hitPixels / pixelsPerMm_;
    const auto &live = liveItems();
    for (auto it = live.primitives.rbegin(); it != live.primitives.rend(); ++it) {
        const auto *record = liveItem(it->first);
        if (!record)
            continue;
        if (const auto *block = std::get_if<DrawingTitleBlockItem>(&record->data)) {
            // A title block is picked anywhere inside its frame.
            if (rectOf(block->frame).adjusted(-slack, -slack, slack, slack).contains(point))
                return it->first;
            continue;
        }
        for (const auto &primitive : it->second) {
            if (primitive.fill && !primitive.text.isEmpty()) {
                if (primitive.textBoxMm.adjusted(-slack, -slack, slack, slack).contains(point))
                    return it->first;
                continue;
            }
            if (primitive.fill) {
                if (primitive.path.contains(point))
                    return it->first;
                continue;
            }
            QPainterPathStroker stroker;
            stroker.setWidth(std::max(primitive.strokeMm, 2 * slack));
            if (stroker.createStroke(primitive.path).contains(point))
                return it->first;
        }
    }
    return 0;
}
QString SheetCanvas::describeItem(Id item) const {
    const auto *live = liveItem(item);
    if (!live)
        return {};
    std::optional<DrawingItemEvaluation> evaluation;
    try {
        evaluation = evaluateDrawingItem(doc_, item);
    } catch (const std::exception &) {
    }
    QStringList parts{sheetItemSummary(*live, evaluation ? &*evaluation : nullptr)};
    if (const auto view = drawingItemView(*live)) {
        const auto list = views();
        const auto at = std::find(list.begin(), list.end(), view);
        if (at != list.end())
            parts << "View " + QString::number(int(at - list.begin()) + 1);
    }
    if (evaluation)
        parts << sheetItemStateText(*evaluation);
    return parts.join(" · ");
}
bool SheetCanvas::nudgeItem(QPointF delta) {
    const auto *live = liveItem(selectedItem_);
    const auto *sheet = record();
    if (!live || !sheet)
        return false;
    const auto after = movedSheetItem(doc_, *live, delta, sheet->paper);
    if (after == *live) {
        emit message("The item is already at the edge of the paper.");
        return false;
    }
    // Consecutive nudges of the same item amend one "Nudge" entry (ADR 0164 §5).
    const auto amendKey = itemKeyOf(revision_.key, "itemnudge", live->id)
                              ? revision_.key
                              : itemKey("itemnudge", live->id);
    return commitItemUpdate(*live, after, "Nudge", amendKey);
}

// ---- Commits -----------------------------------------------------------------------------
bool SheetCanvas::commitItem(const QJsonObject &create, const QString &label, bool amend) {
    amend = amend && revision_.key.startsWith("item:") && !revision_.create.isEmpty();
    const auto kind = create["kind"].toString();
    const auto amendKey = amend ? revision_.key : itemKey("item:" + kind, 0);
    const auto result = commit({create}, label, amendKey);
    if (result.isEmpty())
        return false;
    resetItemTool();
    drag_ = Drag::None;
    // The tool stays active for the next item; a typed value right after revises this one.
    revision_ = {};
    revision_.key = amendKey;
    revision_.create = create;
    update();
    emit stepChanged();
    return true;
}
bool SheetCanvas::commitItemUpdate(const DrawingItemRecord &before, const DrawingItemRecord &after,
                                   const QString &label, const QString &amendKey) {
    const auto data = sheetItemChanges(before, after);
    if (data.isEmpty())
        return false;
    const QJsonObject command{{"command", "drawing_item.update"},
                              {"item", QString::number(after.id)},
                              {"kind", sheetItemKindCode(after.kind())},
                              {"data", data}};
    const auto result = commit({command}, label, amendKey);
    if (result.isEmpty())
        return false;
    revision_.key = amendKey;
    return true;
}
bool SheetCanvas::commitLine(QPointF from, QPointF to, bool amend) {
    const auto *sheet = record();
    if (!sheet)
        return false;
    const double step = amend ? 1e-6 : .1;
    auto clampPoint = [&](QPointF point) {
        return QPointF(std::clamp(tidy(point.x(), step), 0., sheet->paper.widthMm),
                       std::clamp(tidy(point.y(), step), 0., sheet->paper.heightMm));
    };
    from = clampPoint(from);
    to = clampPoint(to);
    if (QLineF(from, to).length() < .1) {
        emit message("A line needs two different points.");
        return false;
    }
    QJsonObject data{{"from", pointJson(from)}, {"to", pointJson(to)}};
    if (amend && !revision_.create.isEmpty()) {
        auto old = revision_.create["data"].toObject();
        for (const auto &key : {"strokeMm", "dash"})
            if (old.contains(key))
                data[key] = old[key];
    }
    return commitItem({{"command", "drawing_item.create"},
                       {"sheet", QString::number(sheet_)},
                       {"kind", "line"},
                       {"data", data}},
                      "Add line", amend);
}
bool SheetCanvas::commitRectangle(QRectF frame, bool amend) {
    const auto *sheet = record();
    if (!sheet)
        return false;
    const double step = amend ? 1e-6 : .1;
    frame = frame.normalized();
    QRectF tidied(tidy(frame.left(), step), tidy(frame.top(), step), tidy(frame.width(), step),
                  tidy(frame.height(), step));
    if (tidied.width() < .1 || tidied.height() < .1) {
        emit message("A rectangle needs a width and a height.");
        return false;
    }
    if (tidied.left() < 0 || tidied.top() < 0 || tidied.right() > sheet->paper.widthMm + 1e-9 ||
        tidied.bottom() > sheet->paper.heightMm + 1e-9) {
        emit message("The rectangle must stay on the paper (" +
                     paperMillimetres(sheet->paper.widthMm) + " × " +
                     paperMillimetres(sheet->paper.heightMm) + " mm).");
        return false;
    }
    return commitItem({{"command", "drawing_item.create"},
                       {"sheet", QString::number(sheet_)},
                       {"kind", "rectangle"},
                       {"data", QJsonObject{{"frame", frameJson(tidied)}}}},
                      "Add rectangle", amend);
}
bool SheetCanvas::commitDimension(double offset, bool amend) {
    QJsonObject create;
    if (amend && revision_.key.startsWith("item:dimension") && !revision_.create.isEmpty()) {
        create = revision_.create;
        auto data = create["data"].toObject();
        data["offsetMm"] = offset;
        create["data"] = data;
    } else {
        const auto &t = itemTool_;
        if (t.step != 2 || !t.anchors[0] || !t.anchors[1]) {
            emit message("Pick two points in a view first.");
            return false;
        }
        amend = false;
        create = {
            {"command", "drawing_item.create"},
            {"sheet", QString::number(sheet_)},
            {"kind", "dimension"},
            {"data", QJsonObject{{"view", QString::number(t.view)},
                                 {"anchors", QJsonArray{sheetAnchorInput(t.anchors[0]->anchor),
                                                        sheetAnchorInput(t.anchors[1]->anchor)}},
                                 {"orientation", "aligned"},
                                 {"offsetMm", std::round(offset * 1e6) / 1e6}}}};
    }
    return commitItem(create, "Add dimension", amend);
}

// ---- Item tools ----------------------------------------------------------------------------
bool SheetCanvas::itemTool() const {
    return tool_ == Tool::Line || tool_ == Tool::Rectangle || tool_ == Tool::Text ||
           tool_ == Tool::Label || tool_ == Tool::Dimension;
}
void SheetCanvas::resetItemTool() {
    const bool active = itemTool_.step > 0;
    itemTool_ = {};
    hoverAnchor_.reset();
    hoverView_ = 0;
    if (active) {
        snapHint_.clear();
        guides_.clear();
    }
}
QPointF SheetCanvas::constrained(QPointF from, QPointF to, bool shift) const {
    if (!shift)
        return to;
    const auto delta = to - from;
    if (tool_ == Tool::Rectangle) {
        // Shift draws a square.
        const auto side = std::max(std::abs(delta.x()), std::abs(delta.y()));
        return from + QPointF(std::copysign(side, delta.x() == 0 ? 1. : delta.x()),
                              std::copysign(side, delta.y() == 0 ? 1. : delta.y()));
    }
    // Shift keeps 45° steps.
    const auto length = std::hypot(delta.x(), delta.y());
    const auto angle = std::round(std::atan2(delta.y(), delta.x()) / (std::numbers::pi / 4)) *
                       (std::numbers::pi / 4);
    return from + QPointF(std::cos(angle), std::sin(angle)) * length;
}
// Dimensions measure only a view drawn to scale that evaluates (DRAWING_ITEM_NOT_MEASURABLE).
static QString unmeasurable(const Document &doc, DrawingViewRenderCache &cache, Id view) {
    const auto found = doc.drawingViews().find(view);
    if (found == doc.drawingViews().end())
        return "Click a model vertex or edge inside a view.";
    const auto name = QString::fromStdString(found->second->name);
    const auto status = cache.status(view);
    if (!status)
        return "“" + name + "” is still updating; try again in a moment.";
    if (status->state == DrawingViewState::Broken || status->state == DrawingViewState::Failed)
        return "“" + name + "” is " + viewStateText(*status).toLower() +
               ", so it cannot be measured. Choose a scene for it first.";
    if (status->notToScale)
        return "Dimension measures only views drawn to scale; “" + name +
               "” is a perspective view (not to scale).";
    return {};
}
void SheetCanvas::pressItemTool(QMouseEvent *event) {
    const auto point = toPaper(event->position());
    const bool snapping = !(event->modifiers() & Qt::AltModifier);
    const bool shift = event->modifiers() & Qt::ShiftModifier;
    auto &t = itemTool_;
    switch (tool_) {
    case Tool::Line:
    case Tool::Rectangle:
        if (t.step == 0) {
            endRevision();
            t.start = t.pointer = snapPoint(point, snapping);
            t.step = 1;
            drag_ = Drag::Item;
            update();
            emit stepChanged();
            return;
        }
        // Click–move–click: the second click places.
        t.pointer = constrained(t.start, snapPoint(point, snapping), shift);
        if (tool_ == Tool::Line)
            commitLine(t.start, t.pointer);
        else
            commitRectangle(QRectF(t.start, t.pointer));
        return;
    case Tool::Text:
        endRevision();
        t.start = t.pointer = snapPoint(point, snapping);
        t.step = 1;
        drag_ = Drag::Item;
        update();
        emit stepChanged();
        return;
    case Tool::Label: {
        if (t.step == 1) {
            // The second click places the text.
            t.pointer = snapPoint(point, snapping);
            drag_ = Drag::None;
            break;
        }
        endRevision();
        // The leader points at a model point in a view drawn to scale, else at the paper.
        const auto view = viewAt(point);
        std::optional<DrawingViewAnchorHit> hit;
        if (view && unmeasurable(doc_, cache_, view).isEmpty())
            hit = anchorNear(view, point, tolerance());
        if (hit) {
            t.view = view;
            t.anchors[0] = hit;
            t.start = qpoint(hit->paperMm);
        } else {
            t.view = 0;
            t.anchors[0].reset();
            t.start = snapPoint(point, snapping);
        }
        t.pointer = t.start;
        t.step = 1;
        drag_ = Drag::Item;
        update();
        emit stepChanged();
        return;
    }
    case Tool::Dimension: {
        if (t.step == 2) {
            commitDimension(t.offsetMm);
            return;
        }
        const auto view = t.step == 1 ? t.view : viewAt(point);
        if (!view) {
            emit message("Click a model vertex or edge inside a view.");
            return;
        }
        if (const auto reason = unmeasurable(doc_, cache_, view); !reason.isEmpty()) {
            emit message(reason);
            return;
        }
        const auto hit = anchorNear(view, point, tolerance());
        if (!hit) {
            emit message(t.step == 1 && viewAt(point) != t.view
                             ? QString("Pick the second point in the same view.")
                             : QString("Click a model vertex or a point on a model edge drawn "
                                       "in the view."));
            return;
        }
        if (t.step == 0) {
            endRevision();
            t.view = view;
            t.anchors[0] = hit;
            t.start = t.pointer = qpoint(hit->paperMm);
            t.step = 1;
            drag_ = Drag::Item;
        } else {
            if (QLineF(qpoint(hit->paperMm), t.start).length() < 1e-6) {
                emit message("Pick a second, different point.");
                return;
            }
            t.anchors[1] = hit;
            t.step = 2;
            t.offsetMm = 0;
            t.pointer = point;
        }
        update();
        emit stepChanged();
        return;
    }
    default:
        return;
    }
    // Label: the second click places its text, which is typed in place next.
    if (tool_ == Tool::Label) {
        const auto *sheet = record();
        DrawingLabelItem label;
        label.text = "Label";
        label.font.sizePt = textSizePt_;
        label.textAt = mm(QPointF(std::clamp(tidy(t.pointer.x()), 0., sheet->paper.widthMm),
                                  std::clamp(tidy(t.pointer.y()), 0., sheet->paper.heightMm)));
        if (t.anchors[0]) {
            label.target.kind = DrawingLabelTargetKind::View;
            label.target.view = t.view;
            label.target.anchor = t.anchors[0]->anchor;
            label.target.lastMm = t.anchors[0]->paperMm;
        } else
            label.target.pointMm =
                mm(QPointF(std::clamp(tidy(t.start.x()), 0., sheet->paper.widthMm),
                           std::clamp(tidy(t.start.y()), 0., sheet->paper.heightMm)));
        editing_ = {};
        editing_.kind = DrawingItemKind::Label;
        editing_.font = label.font;
        editing_.pending = DrawingItemRecord{0, sheet_, 0, label};
        resetItemTool();
        openEditor(
            QRectF(qpoint(label.textAt), QSizeF(30, textSizePt_ * millimetresPerPoint * 1.2)), {},
            true, "Label text");
    }
}
void SheetCanvas::moveItemTool(QMouseEvent *event) {
    const auto point = toPaper(event->position());
    const bool snapping = !(event->modifiers() & Qt::AltModifier);
    const bool shift = event->modifiers() & Qt::ShiftModifier;
    auto &t = itemTool_;
    hoverAnchor_.reset();
    hoverView_ = 0;
    switch (tool_) {
    case Tool::Line:
    case Tool::Rectangle:
    case Tool::Text:
        if (t.step == 1)
            t.pointer = tool_ == Tool::Text
                            ? snapPoint(point, snapping)
                            : constrained(t.start, snapPoint(point, snapping), shift);
        break;
    case Tool::Label:
        if (t.step == 1)
            t.pointer = snapPoint(point, snapping);
        else if (const auto view = viewAt(point);
                 view && unmeasurable(doc_, cache_, view).isEmpty()) {
            hoverAnchor_ = anchorNear(view, point, tolerance());
            hoverView_ = view;
        }
        break;
    case Tool::Dimension:
        if (t.step == 2) {
            t.pointer = point;
            t.offsetMm =
                tidy(drawingDimensionOffset(t.anchors[0]->paperMm, t.anchors[1]->paperMm,
                                            DrawingDimensionOrientation::Aligned, mm(point)));
        } else {
            const auto view = t.step == 1 ? t.view : viewAt(point);
            if (view && unmeasurable(doc_, cache_, view).isEmpty()) {
                hoverAnchor_ = anchorNear(view, point, tolerance());
                hoverView_ = view;
            }
            t.pointer = hoverAnchor_ ? qpoint(hoverAnchor_->paperMm) : point;
        }
        break;
    default:
        return;
    }
    update();
    emit stepChanged();
}
void SheetCanvas::releaseItemTool(QMouseEvent *event) {
    auto &t = itemTool_;
    const auto point = toPaper(event->position());
    switch (tool_) {
    case Tool::Line:
        // Press–drag–release draws; a click anchors the start for click–move–click.
        if (dragMoved_)
            commitLine(t.start, t.pointer);
        return;
    case Tool::Rectangle:
        if (dragMoved_)
            commitRectangle(QRectF(t.start, t.pointer));
        return;
    case Tool::Text: {
        // A click places unwrapped text; a drag sets its wrap width.
        const auto *sheet = record();
        const bool wrap = dragMoved_ && std::abs(t.pointer.x() - t.start.x()) >= minimumTextWidthMm;
        const auto left = wrap ? std::min(t.start.x(), t.pointer.x()) : t.start.x();
        const auto top = wrap ? std::min(t.start.y(), t.pointer.y()) : t.start.y();
        const QPointF at(std::clamp(tidy(left), 0., sheet->paper.widthMm - minimumTextWidthMm),
                         std::clamp(tidy(top), 0., sheet->paper.heightMm - 1));
        DrawingTextItem text;
        text.font.sizePt = textSizePt_;
        const auto width = wrap ? std::min(tidy(std::abs(t.pointer.x() - t.start.x())),
                                           sheet->paper.widthMm - at.x())
                                : minimumTextWidthMm;
        text.frame = {at.x(), at.y(), width, std::max(1., textSizePt_ * millimetresPerPoint)};
        editing_ = {};
        editing_.kind = DrawingItemKind::Text;
        editing_.font = text.font;
        editing_.wrap = wrap;
        editing_.autoWidth = !wrap;
        editing_.pending = DrawingItemRecord{0, sheet_, 0, text};
        resetItemTool();
        openEditor(rectOf(text.frame), {}, true, "Text");
        return;
    }
    case Tool::Label:
        if (dragMoved_ && t.step == 1)
            pressItemTool(event);
        return;
    case Tool::Dimension:
        if (dragMoved_ && t.step == 1) {
            // Press on the first point and release on the second.
            if (const auto hit = anchorNear(t.view, point, tolerance());
                hit && QLineF(qpoint(hit->paperMm), t.start).length() >= 1e-6) {
                t.anchors[1] = hit;
                t.step = 2;
                t.offsetMm = 0;
                update();
                emit stepChanged();
            }
        }
        return;
    default:
        return;
    }
}

// ---- In-place text editing (decision 11) ------------------------------------------------
bool SheetCanvas::beginTextEdit(const QString &requested) {
    const auto *item = liveItem(selectedItem_);
    if (!item)
        return false;
    if (textEditing())
        endTextEdit();
    cancelOperation();
    editing_ = {};
    editing_.item = item->id;
    editing_.kind = item->kind();
    if (const auto *t = std::get_if<DrawingTextItem>(&item->data)) {
        editing_.font = t->font;
        editing_.wrap = true;
        editing_.autoWidth =
            std::abs(t->frame.widthMm -
                     (drawingTextSizeMm(t->text, t->font).width() + textSlackMm)) < .6;
        editing_.wrap = !editing_.autoWidth;
        openEditor(rectOf(t->frame), QString::fromStdString(t->text), true, "Text");
        return true;
    }
    if (const auto *l = std::get_if<DrawingLabelItem>(&item->data)) {
        editing_.font = l->font;
        const auto size = drawingTextSizeMm(l->text, l->font);
        openEditor(QRectF(qpoint(l->textAt), size), QString::fromStdString(l->text), true,
                   "Label text");
        return true;
    }
    if (const auto *b = std::get_if<DrawingTitleBlockItem>(&item->data)) {
        auto field = requested;
        if (std::none_of(titleFields().begin(), titleFields().end(),
                         [&](const auto &entry) { return entry.first == field; }))
            field = "project";
        editing_.font = b->font;
        editing_.field = field;
        for (const auto &cell : drawingTitleBlockCells(b->frame))
            if (QString::fromStdString(cell.field) == field) {
                const auto caption = std::find_if(titleFields().begin(), titleFields().end(),
                                                  [&](const auto &e) { return e.first == field; });
                openEditor(cell.rectMm, QString::fromStdString(titleValue(b->fields, field)), false,
                           caption->second);
            }
        return true;
    }
    editing_ = {};
    return false;
}
void SheetCanvas::openEditor(QRectF paperBox, const QString &text, bool multiline,
                             const QString &name) {
    if (editor_) {
        editor_->removeEventFilter(this);
        editor_->hide();
        editor_->deleteLater();
        editor_ = nullptr;
    }
    editing_.box = paperBox;
    QWidget *editor;
    if (multiline) {
        auto *edit = new QPlainTextEdit(this);
        edit->setPlainText(text);
        edit->setLineWrapMode(editing_.wrap ? QPlainTextEdit::WidgetWidth : QPlainTextEdit::NoWrap);
        edit->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        edit->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        edit->document()->setDocumentMargin(1);
        edit->selectAll();
        connect(edit, &QPlainTextEdit::textChanged, this, [this] { placeEditor(); });
        editor = edit;
    } else {
        auto *line = new QLineEdit(this);
        line->setText(text);
        line->setMaxLength(int(drawingItemShortTextLimit));
        line->setFrame(false);
        line->selectAll();
        editor = line;
    }
    editor->setObjectName("sheetTextEditor");
    editor->setAccessibleName(name);
    editor->setAccessibleDescription(editing_.field.isEmpty()
                                         ? "Esc, Ctrl+Enter or a click outside finishes"
                                         : "Tab moves to the next field; Enter, Esc or a click "
                                           "outside finishes");
    editor->installEventFilter(this);
    editor_ = editor;
    placeEditor();
    editor->show();
    editor->setFocus(Qt::OtherFocusReason);
    update();
    announce();
    emit stepChanged();
}
void SheetCanvas::placeEditor() {
    if (!editor_)
        return;
    QFont font(QString::fromStdString(editing_.font.family));
    const auto pixels =
        std::max(6, int(std::round(editing_.font.sizePt * millimetresPerPoint * pixelsPerMm_)));
    font.setPixelSize(pixels);
    editor_->setFont(font);
    // The text edits at its size on paper, black on white (paper is white in both themes). The
    // style sheet carries the font, so the application's style sheet cannot override it.
    QString family = font.family();
    family.replace('\'', "\\'");
    editor_->setStyleSheet(QString("#sheetTextEditor {background:#ffffff;color:#000000;"
                                   "border:1px solid %1;selection-background-color:%1;"
                                   "selection-color:#ffffff;padding:0;font-family:'%2';"
                                   "font-size:%3px;}")
                               .arg(themeColors(false).selectionStroke.name(), family)
                               .arg(pixels));
    auto box = paperToWidget().mapRect(editing_.box);
    const QFontMetricsF metrics(font);
    if (auto *edit = qobject_cast<QPlainTextEdit *>(editor_)) {
        const auto text = edit->toPlainText();
        const auto lines = std::max<qsizetype>(1, text.count('\n') + 1);
        double width = box.width();
        if (!editing_.wrap) {
            double natural = 0;
            for (const auto &line : text.split('\n'))
                natural = std::max(natural, metrics.horizontalAdvance(line));
            width = std::max({width, natural + metrics.averageCharWidth() * 2 + 6, 60.});
        }
        auto height = std::max(box.height(), lines * metrics.lineSpacing() + 8);
        if (editing_.wrap) {
            const auto bounds = metrics.boundingRect(QRectF(0, 0, std::max(1., width - 10), 1e6),
                                                     Qt::TextWordWrap, text.isEmpty() ? "M" : text);
            // A spare line keeps the first line in view while the text grows.
            height = std::max(height, bounds.height() + metrics.lineSpacing() + 8);
        }
        box.setSize({width, height});
    } else
        box.setHeight(std::max(box.height(), metrics.height() + 4));
    editor_->setGeometry(box.toAlignedRect().adjusted(-2, -2, 2, 2));
    editor_->raise();
}
bool SheetCanvas::eventFilter(QObject *object, QEvent *event) {
    if (object != editor_ || !editor_)
        return QWidget::eventFilter(object, event);
    if (event->type() == QEvent::ShortcutOverride) {
        auto *key = static_cast<QKeyEvent *>(event);
        if (key->key() == Qt::Key_Escape || key->key() == Qt::Key_Tab ||
            key->key() == Qt::Key_Backtab) {
            event->accept();
            return true;
        }
    }
    if (event->type() == QEvent::KeyPress) {
        editorKey(static_cast<QKeyEvent *>(event));
        return event->isAccepted();
    }
    if (event->type() == QEvent::FocusOut) {
        // Focus leaving for another widget ends editing; a menu or a window switch does not.
        const auto reason = static_cast<QFocusEvent *>(event)->reason();
        if (reason != Qt::PopupFocusReason && reason != Qt::ActiveWindowFocusReason && !ending_)
            QMetaObject::invokeMethod(this, [this] { endTextEdit(); }, Qt::QueuedConnection);
    }
    return QWidget::eventFilter(object, event);
}
void SheetCanvas::editorKey(QKeyEvent *event) {
    const auto key = event->key();
    const bool enter = key == Qt::Key_Return || key == Qt::Key_Enter;
    const bool line = qobject_cast<QLineEdit *>(editor_) != nullptr;
    if (key == Qt::Key_Escape || (enter && (line || event->modifiers() & Qt::ControlModifier))) {
        event->accept();
        endTextEdit();
        return;
    }
    if (line && (key == Qt::Key_Tab || key == Qt::Key_Backtab)) {
        // Title-block fields: Tab and Shift+Tab move between them in the same entry.
        event->accept();
        const auto *item = liveItem(editing_.item);
        if (!item)
            return;
        editing_.fields[editing_.field] = static_cast<QLineEdit *>(editor_)->text();
        const auto &fields = titleFields();
        auto at = std::find_if(fields.begin(), fields.end(),
                               [&](const auto &e) { return e.first == editing_.field; });
        const auto index = size_t(at - fields.begin());
        const bool back = key == Qt::Key_Backtab || event->modifiers() & Qt::ShiftModifier;
        const auto next = fields[(index + (back ? fields.size() - 1 : 1)) % fields.size()];
        const auto &block = std::get<DrawingTitleBlockItem>(item->data);
        editing_.field = next.first;
        for (const auto &cell : drawingTitleBlockCells(block.frame))
            if (QString::fromStdString(cell.field) == next.first)
                editing_.box = cell.rectMm;
        auto *edit = static_cast<QLineEdit *>(editor_);
        edit->setText(editing_.fields.contains(next.first)
                          ? editing_.fields[next.first]
                          : QString::fromStdString(titleValue(block.fields, next.first)));
        edit->selectAll();
        edit->setAccessibleName(next.second);
        placeEditor();
        QAccessibleEvent changed(edit, QAccessible::NameChanged);
        QAccessible::updateAccessibility(&changed);
        emit stepChanged();
        return;
    }
    event->ignore();
}
void SheetCanvas::endTextEdit() {
    if (!editor_ || ending_)
        return;
    QScopedValueRollback guard(ending_, true);
    QString text;
    if (auto *edit = qobject_cast<QPlainTextEdit *>(editor_))
        text = edit->toPlainText();
    else if (auto *line = qobject_cast<QLineEdit *>(editor_))
        text = line->text();
    auto *editor = editor_;
    editor_ = nullptr;
    editor->removeEventFilter(this);
    const bool hadFocus = editor->hasFocus();
    editor->hide();
    editor->deleteLater();
    const auto editing = editing_;
    editing_ = {};
    if (hadFocus || !QApplication::focusWidget())
        setFocus(Qt::OtherFocusReason);
    update();
    const auto *sheet = record();
    const auto font = fontJson(editing.font);
    if (!editing.item) {
        // A new text or label is created with what was typed; nothing typed, nothing made.
        if (text.trimmed().isEmpty() || !sheet) {
            emit stepChanged();
            announce();
            return;
        }
        auto record = editing.pending;
        QJsonObject data;
        if (auto *t = std::get_if<DrawingTextItem>(&record.data)) {
            const auto wrap = editing.wrap ? std::optional<double>(t->frame.widthMm) : std::nullopt;
            const auto size = drawingTextSizeMm(text.toStdString(), t->font, wrap);
            QRectF frame(t->frame.xMm, t->frame.yMm,
                         wrap ? t->frame.widthMm : tidy(size.width() + textSlackMm, .1),
                         std::max(.1, tidy(size.height() + .05, .1)));
            frame.setWidth(std::min(frame.width(), sheet->paper.widthMm - frame.x()));
            frame.setHeight(std::min(frame.height(), sheet->paper.heightMm - frame.y()));
            data = {{"frame", frameJson(frame)}, {"text", text}, {"font", font}};
            commitItem({{"command", "drawing_item.create"},
                        {"sheet", QString::number(sheet_)},
                        {"kind", "text"},
                        {"data", data}},
                       "Add text", false);
        } else if (auto *l = std::get_if<DrawingLabelItem>(&record.data)) {
            QJsonObject target;
            if (l->target.kind == DrawingLabelTargetKind::View)
                target = {{"kind", "view"},
                          {"view", QString::number(l->target.view)},
                          {"anchor", sheetAnchorInput(l->target.anchor)}};
            else
                target = {{"kind", "paper"},
                          {"xMm", l->target.pointMm[0]},
                          {"yMm", l->target.pointMm[1]}};
            data = {{"text", text},
                    {"font", font},
                    {"textAt", QJsonObject{{"xMm", l->textAt[0]}, {"yMm", l->textAt[1]}}},
                    {"leader", true},
                    {"target", target}};
            commitItem({{"command", "drawing_item.create"},
                        {"sheet", QString::number(sheet_)},
                        {"kind", "label"},
                        {"data", data}},
                       "Add label", false);
        }
        announce();
        return;
    }
    const auto *item = liveItem(editing.item);
    if (!item) {
        emit stepChanged();
        return;
    }
    const auto key = itemKey("edit", item->id);
    if (const auto *b = std::get_if<DrawingTitleBlockItem>(&item->data)) {
        auto fields = editing.fields;
        fields[editing.field] = text;
        QJsonObject changed;
        for (const auto &[field, value] : fields)
            if (value.toStdString() != titleValue(b->fields, field))
                changed[field] = value;
        if (!changed.isEmpty())
            commit({QJsonObject{{"command", "drawing_item.update"},
                                {"item", QString::number(item->id)},
                                {"kind", "titleBlock"},
                                {"data", QJsonObject{{"fields", changed}}}}},
                   "Edit title block", key);
    } else {
        std::string old;
        if (const auto *t = std::get_if<DrawingTextItem>(&item->data))
            old = t->text;
        else if (const auto *l = std::get_if<DrawingLabelItem>(&item->data))
            old = l->text;
        if (text.isEmpty()) {
            emit message("Text cannot be empty; delete the item to remove it.");
        } else if (text.toStdString() != old) {
            QJsonObject data{{"text", text}};
            if (const auto *t = std::get_if<DrawingTextItem>(&item->data);
                t && editing.autoWidth && sheet) {
                // Text placed with a click keeps fitting its words.
                const auto size = drawingTextSizeMm(text.toStdString(), t->font);
                QRectF frame(t->frame.xMm, t->frame.yMm, tidy(size.width() + textSlackMm, .1),
                             std::max(.1, tidy(size.height() + .05, .1)));
                frame.setWidth(std::min(frame.width(), sheet->paper.widthMm - frame.x()));
                frame.setHeight(std::min(frame.height(), sheet->paper.heightMm - frame.y()));
                data["frame"] = frameJson(frame);
            }
            commit({QJsonObject{{"command", "drawing_item.update"},
                                {"item", QString::number(item->id)},
                                {"kind", sheetItemKindCode(item->kind())},
                                {"data", data}}},
                   item->kind() == DrawingItemKind::Text ? "Edit text" : "Edit label", key);
        }
    }
    announce();
    emit stepChanged();
}

// ---- Reattach (decision 2) --------------------------------------------------------------
bool SheetCanvas::beginReattach() {
    const auto *item = liveItem(selectedItem_);
    if (!item || !drawingItemView(*item)) {
        emit message("Select a broken dimension or label to reattach it.");
        return false;
    }
    DrawingReattachProposal proposal;
    try {
        proposal = drawingReattachCandidates(doc_, item->id);
    } catch (const std::exception &error) {
        emit message(QString::fromUtf8(error.what()));
        return false;
    }
    if (proposal.evaluation.state != DrawingItemState::Broken) {
        emit message("This item is attached to the model; nothing to reattach.");
        return false;
    }
    if (!proposal.unavailable.empty()) {
        QStringList reasons;
        for (const auto &reason : proposal.unavailable)
            reasons << brokenReasonText(reason);
        emit message("The view cannot be measured (" + reasons.join(", ") +
                     "), so its points cannot be reattached. Choose a scene for the view first.");
        return false;
    }
    const auto id = item->id, view = drawingItemView(*item);
    if (textEditing())
        endTextEdit();
    cancelOperation();
    reattach_.item = id;
    reattach_.view = view;
    reattach_.proposal = proposal;
    reattach_.chosen.assign(proposal.anchors.size(), std::nullopt);
    reattach_.active = proposal.anchors.size();
    for (size_t i = 0; i < proposal.anchors.size(); ++i) {
        const auto &candidate = proposal.anchors[i];
        if (candidate.state == AnchorState::Resolved)
            continue;
        if (reattach_.active == proposal.anchors.size())
            reattach_.active = i;
        if (candidate.anchor && candidate.paperMm)
            reattach_.chosen[i] = DrawingViewAnchorHit{
                *candidate.anchor, *candidate.paperMm, candidate.distanceMm.value_or(0),
                candidate.anchor->kind == AnchorKind::Vertex};
    }
    update();
    announce();
    emit stepChanged();
    return true;
}
void SheetCanvas::cancelReattach() {
    if (!reattach_.item)
        return;
    reattach_ = {};
    update();
    emit stepChanged();
}
std::vector<QPointF> SheetCanvas::reattachPoints() const {
    std::vector<QPointF> result;
    for (const auto &chosen : reattach_.chosen)
        if (chosen)
            result.push_back(qpoint(chosen->paperMm));
    return result;
}
bool SheetCanvas::confirmReattach() {
    const auto *item = liveItem(reattach_.item);
    if (!item) {
        cancelReattach();
        return false;
    }
    std::vector<AnnotationAnchor> stored;
    if (const auto *d = std::get_if<DrawingDimensionItem>(&item->data))
        stored = {d->anchors[0], d->anchors[1]};
    else if (const auto *l = std::get_if<DrawingLabelItem>(&item->data))
        stored = {l->target.anchor};
    QJsonArray anchors;
    for (size_t i = 0; i < reattach_.proposal.anchors.size() && i < stored.size(); ++i) {
        if (reattach_.proposal.anchors[i].state == AnchorState::Resolved) {
            anchors.append(sheetAnchorInput(stored[i]));
            continue;
        }
        if (!reattach_.chosen[i]) {
            emit message("No vertex or edge lies near the old point; click one in the view.");
            return false;
        }
        anchors.append(sheetAnchorInput(reattach_.chosen[i]->anchor));
    }
    const auto label = item->kind() == DrawingItemKind::Dimension ? QString("Reattach dimension")
                                                                  : QString("Reattach label");
    const auto result = commit({QJsonObject{{"command", "drawing_item.reattach"},
                                            {"item", QString::number(item->id)},
                                            {"anchors", anchors}}},
                               label, itemKey("reattach", item->id));
    if (result.isEmpty())
        return false;
    cancelReattach();
    emit message(label == "Reattach dimension" ? QString("The dimension measures the model again.")
                                               : QString("The label points at the model again."));
    return true;
}
std::optional<QPointF> SheetCanvas::anchorMarker() const {
    if (hoverAnchor_)
        return qpoint(hoverAnchor_->paperMm);
    return std::nullopt;
}
QString SheetCanvas::anchorKind() const {
    if (!hoverAnchor_)
        return {};
    return hoverAnchor_->vertex ? QString("Endpoint") : QString("On edge");
}

// ---- Keys --------------------------------------------------------------------------------
bool SheetCanvas::keyItems(QKeyEvent *event) {
    const auto key = event->key();
    if (event->modifiers() & (Qt::ControlModifier | Qt::AltModifier | Qt::MetaModifier))
        return false;
    const bool enter = key == Qt::Key_Return || key == Qt::Key_Enter;
    if (reattaching() && enter) {
        confirmReattach();
        return true;
    }
    if (enter && tool_ == Tool::Dimension && itemTool_.step == 2) {
        commitDimension(itemTool_.offsetMm);
        return true;
    }
    const auto *item = liveItem(selectedItem_);
    if (!item || operationActive() || editView_)
        return false;
    if (enter && tool_ == Tool::Select) {
        const auto kind = item->kind();
        if (kind == DrawingItemKind::Text || kind == DrawingItemKind::Label ||
            kind == DrawingItemKind::TitleBlock)
            beginTextEdit();
        return true;
    }
    const bool shift = event->modifiers() & Qt::ShiftModifier;
    const double step = shift ? 10 : 1;
    if ((tool_ == Tool::Select || tool_ == Tool::Move) &&
        (key == Qt::Key_Left || key == Qt::Key_Right || key == Qt::Key_Up || key == Qt::Key_Down)) {
        nudgeItem(QPointF(key == Qt::Key_Left    ? -step
                          : key == Qt::Key_Right ? step
                                                 : 0,
                          key == Qt::Key_Up     ? -step
                          : key == Qt::Key_Down ? step
                                                : 0));
        return true;
    }
    return false;
}
void SheetCanvas::mouseDoubleClickEvent(QMouseEvent *event) {
    // A double-click on a text, label or title-block field edits it in place.
    if (event->button() == Qt::LeftButton && tool_ == Tool::Select && !editView_) {
        const auto point = toPaper(event->position());
        if (const auto hit = itemAt(point)) {
            const auto *item = liveItem(hit);
            QString field;
            if (const auto *b = std::get_if<DrawingTitleBlockItem>(&item->data))
                for (const auto &cell : drawingTitleBlockCells(b->frame))
                    if (cell.stored && cell.rectMm.contains(point))
                        field = QString::fromStdString(cell.field);
            const auto kind = item->kind();
            if (kind == DrawingItemKind::Text || kind == DrawingItemKind::Label ||
                kind == DrawingItemKind::TitleBlock) {
                drag_ = Drag::None;
                clearPreview();
                setSelectedItem(hit);
                beginTextEdit(field);
                event->accept();
                return;
            }
        }
    }
    QWidget::mouseDoubleClickEvent(event);
}

// ---- Measurements ------------------------------------------------------------------------
bool SheetCanvas::itemMeasurements(const QString &label, QString text, QString &error) {
    try {
        const auto *sheet = record();
        if (!sheet)
            throw std::runtime_error("Add a page first");
        auto &t = itemTool_;
        // A text size in points: for new text, or the selected item's text.
        const auto point = QRegularExpression::escape(QLocale().decimalPoint());
        const QRegularExpression points(R"(^([0-9]+(?:)" + point + R"([0-9]+)?)\s*pt$)");
        if (const auto match = points.match(text); match.hasMatch()) {
            bool ok = false;
            const auto size = QLocale().toDouble(match.captured(1), &ok);
            if (!ok || size < drawingFontMinPt || size > drawingFontMaxPt)
                throw std::runtime_error("Text size must be from 4 to 72 pt");
            if (tool_ == Tool::Text || tool_ == Tool::Label) {
                textSizePt_ = size;
                emit message("New text is " + QLocale().toString(size) + " pt.");
                emit stepChanged();
                return true;
            }
            const auto *live = liveItem(selectedItem_);
            if (!live)
                throw std::runtime_error("Select a text, label, dimension or title block first");
            const bool again = itemKeyOf(revision_.key, "size", live->id);
            const auto base = again ? revision_.item : *live;
            auto after = base;
            std::visit(
                [&](auto &data) {
                    if constexpr (requires { data.font; })
                        data.font.sizePt = size;
                    else
                        throw std::runtime_error("Lines and rectangles have no text");
                },
                after.data);
            if (!commitItemUpdate(*live, after, "Change text size",
                                  again ? revision_.key : itemKey("size", live->id)))
                throw std::runtime_error("The text already has this size");
            revision_.item = base;
            return true;
        }
        const bool absolute = text.startsWith('[');
        const bool relative = text.startsWith('<');
        if (absolute || relative) {
            if (!text.endsWith(absolute ? ']' : '>'))
                throw std::runtime_error("Coordinate brackets do not match");
            text = text.mid(1, text.size() - 2);
        }
        const auto values = lengths(text);
        auto pair = [&](const char *what) {
            if (values.size() != 2)
                throw std::runtime_error(what);
            return QPointF(values[0], values[1]);
        };
        auto onPaper = [&](QPointF p) {
            if (p.x() < 0 || p.y() < 0 || p.x() > sheet->paper.widthMm ||
                p.y() > sheet->paper.heightMm)
                throw std::runtime_error(
                    "The point must be on the paper (" +
                    paperMillimetres(sheet->paper.widthMm).toStdString() + " × " +
                    paperMillimetres(sheet->paper.heightMm).toStdString() + " mm)");
            return p;
        };
        switch (tool_) {
        case Tool::Line: {
            if (t.step == 0) {
                if (absolute) {
                    t.start = t.pointer = onPaper(pair("Type the start as [x,y] in millimetres"));
                    t.step = 1;
                    update();
                    emit stepChanged();
                    return true;
                }
                if (revision_.key.startsWith("item:line") && !revision_.create.isEmpty()) {
                    // Re-entry revises the line just drawn.
                    const auto data = revision_.create["data"].toObject();
                    const QPointF from(data["from"].toArray()[0].toDouble(),
                                       data["from"].toArray()[1].toDouble());
                    const QPointF to(data["to"].toArray()[0].toDouble(),
                                     data["to"].toArray()[1].toDouble());
                    QPointF end;
                    if (relative || values.size() == 2)
                        end = from + pair("Type the change as dx,dy");
                    else {
                        const auto length = QLineF(from, to).length();
                        end = from + (to - from) / length * values.at(0);
                    }
                    if (!commitLine(from, onPaper(end), true))
                        throw std::runtime_error("The line could not be revised");
                    return true;
                }
                throw std::runtime_error("Click the line's start, or type [x,y] to start there");
            }
            QPointF end;
            if (absolute)
                end = pair("Type the end as [x,y] in millimetres");
            else if (relative || values.size() == 2)
                end = t.start + pair("Type the change as dx,dy");
            else {
                auto direction = t.pointer - t.start;
                const auto length = std::hypot(direction.x(), direction.y());
                direction = length > 1e-9 ? direction / length : QPointF(1, 0);
                end = t.start + direction * values.at(0);
            }
            if (!commitLine(t.start, onPaper(end)))
                throw std::runtime_error("The line could not be drawn");
            return true;
        }
        case Tool::Rectangle: {
            if (absolute) {
                if (t.step != 0)
                    throw std::runtime_error("Type the size as width,height");
                t.start = t.pointer = onPaper(pair("Type the corner as [x,y] in millimetres"));
                t.step = 1;
                update();
                emit stepChanged();
                return true;
            }
            const auto size = pair("Type the size as width,height in millimetres");
            if (size.x() <= 0 || size.y() <= 0)
                throw std::runtime_error("Width and height must be positive");
            QRectF frame;
            if (t.step == 1) {
                // From the first corner towards the pointer.
                QPointF at = t.start;
                if (t.pointer.x() < t.start.x())
                    at.rx() -= size.x();
                if (t.pointer.y() < t.start.y())
                    at.ry() -= size.y();
                frame = QRectF(at, QSizeF(size.x(), size.y()));
            } else if (revision_.key.startsWith("item:rectangle") && !revision_.create.isEmpty()) {
                const auto old = revision_.create["data"].toObject()["frame"].toObject();
                frame = QRectF(old["xMm"].toDouble(), old["yMm"].toDouble(), size.x(), size.y());
                if (!commitRectangle(frame, true))
                    throw std::runtime_error("The rectangle could not be revised");
                return true;
            } else {
                QPointF at(sheet->marginMm, sheet->marginMm);
                if (hoverMm_ &&
                    QRectF(0, 0, sheet->paper.widthMm, sheet->paper.heightMm).contains(*hoverMm_))
                    at = tidy(*hoverMm_);
                frame = QRectF(at, QSizeF(size.x(), size.y()));
            }
            if (!commitRectangle(frame, false))
                throw std::runtime_error("The rectangle could not be drawn");
            return true;
        }
        case Tool::Text: {
            if (!absolute)
                throw std::runtime_error("Type [x,y] to place text, or a size such as 12pt");
            const auto at = onPaper(pair("Type the position as [x,y] in millimetres"));
            DrawingTextItem item;
            item.font.sizePt = textSizePt_;
            item.frame = {std::min(at.x(), sheet->paper.widthMm - minimumTextWidthMm),
                          std::min(at.y(), sheet->paper.heightMm - 1), minimumTextWidthMm,
                          std::max(.1, std::min(1., sheet->paper.heightMm - at.y()))};
            editing_ = {};
            editing_.kind = DrawingItemKind::Text;
            editing_.font = item.font;
            editing_.autoWidth = true;
            editing_.pending = DrawingItemRecord{0, sheet_, 0, item};
            resetItemTool();
            openEditor(rectOf(item.frame), {}, true, "Text");
            return true;
        }
        case Tool::Label: {
            if (!absolute)
                throw std::runtime_error("Type [x,y] for the leader's point, then for the text");
            const auto at = onPaper(pair("Type the position as [x,y] in millimetres"));
            if (t.step == 0) {
                t.view = 0;
                t.anchors[0].reset();
                t.start = t.pointer = at;
                t.step = 1;
                update();
                emit stepChanged();
                return true;
            }
            t.pointer = at;
            QMouseEvent none(QEvent::MouseButtonPress, paperToWidget().map(at),
                             mapToGlobal(paperToWidget().map(at)), Qt::LeftButton, Qt::LeftButton,
                             Qt::NoModifier);
            pressItemTool(&none);
            return true;
        }
        case Tool::Dimension: {
            if (values.size() != 1 || absolute || relative)
                throw std::runtime_error("Type the offset in millimetres");
            if (t.step == 2) {
                if (!commitDimension(values[0]))
                    throw std::runtime_error("The dimension could not be placed");
                return true;
            }
            if (revision_.key.startsWith("item:dimension") && !revision_.create.isEmpty()) {
                if (!commitDimension(values[0], true))
                    throw std::runtime_error("The dimension could not be revised");
                return true;
            }
            throw std::runtime_error("Pick two points in a view first");
        }
        default:
            break;
        }
        // Select or Move with an item selected: move it, or set a dimension's offset.
        const auto *live = liveItem(selectedItem_);
        if (!live)
            throw std::runtime_error("Select an item first");
        if (live->kind() == DrawingItemKind::Dimension && !absolute && !relative &&
            values.size() == 1) {
            const bool again = itemKeyOf(revision_.key, "offset", live->id);
            const auto base = again ? revision_.item : *live;
            auto after = base;
            std::get<DrawingDimensionItem>(after.data).offsetMm = values[0];
            if (!commitItemUpdate(*live, after, "Move dimension",
                                  again ? revision_.key : itemKey("offset", live->id)))
                throw std::runtime_error("The dimension already has this offset");
            revision_.item = base;
            return true;
        }
        const bool again = itemKeyOf(revision_.key, "itemmove", live->id);
        const auto base = again ? revision_.item : *live;
        QPointF delta;
        if (absolute) {
            if (live->kind() == DrawingItemKind::Dimension)
                throw std::runtime_error("A dimension follows its model points; type its offset");
            delta =
                pair("Type the top-left corner as [x,y] in millimetres") - sheetItemOrigin(base);
        } else if (values.size() == 2)
            delta = pair("Type the move as dx,dy");
        else if (values.size() == 1 && !relative && label == "Move") {
            if (!again || revision_.direction.isNull())
                throw std::runtime_error("Drag the item first to type a distance, or type dx,dy");
            delta = revision_.direction * values[0];
        } else
            throw std::runtime_error("Type the move as dx,dy in millimetres");
        const auto after = movedSheetItem(doc_, base, delta, sheet->paper);
        if (const auto bounds = storedBounds(base)) {
            // Typed moves are exact: one that would leave the paper is refused, not clamped.
            const auto moved = storedBounds(after);
            if (moved && (moved->topLeft() - bounds->topLeft() - delta).manhattanLength() > 1e-6)
                throw std::runtime_error("The item must stay on the paper");
        }
        const auto direction = revision_.direction;
        if (!commitItemUpdate(*live, after, "Move " + sheetItemKindName(live->kind()).toLower(),
                              again ? revision_.key : itemKey("itemmove", live->id)))
            throw std::runtime_error("The item is already there");
        revision_.item = base;
        const auto length = std::hypot(delta.x(), delta.y());
        revision_.direction = again && values.size() == 1 ? direction
                              : length > 0                ? delta / length
                                                          : QPointF();
        return true;
    } catch (const std::exception &failure) {
        error = QString::fromUtf8(failure.what());
        return false;
    }
}

// ---- Instructions --------------------------------------------------------------------------
QString SheetCanvas::itemInstruction() const {
    if (textEditing())
        return editing_.field.isEmpty()
                   ? "Type the text · Enter starts a new line · Esc, Ctrl+Enter or a click "
                     "outside finishes"
                   : "Type the field · Tab moves to the next field · Enter, Esc or a click "
                     "outside finishes";
    if (reattaching())
        return "Enter reattaches to the proposed point · Click a vertex or edge in the view to "
               "choose another · Esc cancels";
    const auto step = itemTool_.step;
    switch (tool_) {
    case Tool::Line:
        return step ? "Click the end, or type a length, <dx,dy> or [x,y] · Shift keeps 45° steps · "
                      "Alt ignores snapping · Esc cancels"
                    : "Click or drag the line · Shift keeps 45° steps · Alt ignores snapping · "
                      "Type [x,y] to start at a point";
        break;
    case Tool::Rectangle:
        return step ? "Click the opposite corner, or type width,height · Shift draws a square · "
                      "Esc cancels"
                    : "Click or drag the rectangle · Shift draws a square · Type width,height · "
                      "Alt ignores snapping";
    case Tool::Text:
        return "Click to place text, or drag its wrap width · Type [x,y] to place it, or a size "
               "such as 12pt";
    case Tool::Label:
        return step ? "Click where the text goes · Esc cancels"
                    : "Click the point the leader points to: a model point in a view, or the "
                      "paper · Type [x,y]";
    case Tool::Dimension:
        return step == 2   ? "Move to place the dimension line and click, or type the offset in "
                             "mm · Enter places · Esc cancels"
               : step == 1 ? "Click the second point in the same view · Esc cancels"
                           : "Click a model vertex or edge in a view drawn to scale";
    default:
        break;
    }
    const auto *item = liveItem(selectedItem_);
    if (!item)
        return {};
    const auto kind = item->kind();
    QString text = "Drag to move · Arrows nudge 1 mm · Shift+Arrows 10 mm";
    if (kind == DrawingItemKind::Text || kind == DrawingItemKind::Label ||
        kind == DrawingItemKind::TitleBlock)
        text += " · Enter edits the text";
    if (drawingItemView(*item))
        text += " · Reattach repairs a broken reference";
    return text + " · Delete erases";
}

// ---- Painting ------------------------------------------------------------------------------
void SheetCanvas::paintItems(QPainter &painter) {
    const auto paper = themeColors(false);
    const auto stroke = paper.selectionStroke;
    const auto transform = paperToWidget();
    const auto *sheet = record();
    if (!sheet)
        return;
    painter.save();
    painter.setRenderHint(QPainter::Antialiasing);
    // Paints primitives on paper in the selection colour (previews).
    auto ghost = [&](std::vector<DrawingSheetPrimitive> primitives) {
        PreparedDrawingSheet shown;
        shown.sheet = *sheet;
        for (auto &primitive : primitives) {
            if (primitive.fill && primitive.text.isEmpty() && primitive.color == Qt::white)
                continue; // No knock-out under a preview.
            primitive.color = stroke;
            shown.primitives.push_back(std::move(primitive));
        }
        DrawingSheetPaintOptions options;
        options.paperToDevice = transform;
        options.marginFrame = false;
        painter.save();
        paintDrawingSheet(painter, shown, options);
        painter.restore();
    };
    auto pill = [&](QPointF at, const QString &text) {
        const auto font = uiFont(this, 11);
        const QFontMetricsF metrics(font);
        QRectF box(0, 0, metrics.horizontalAdvance(text) + 12, metrics.height() + 4);
        box.moveCenter(at);
        painter.setPen(Qt::NoPen);
        painter.setBrush(stroke);
        painter.drawRoundedRect(box, 4, 4);
        painter.setFont(font);
        painter.setPen(Qt::white);
        painter.drawText(box, Qt::AlignCenter, text);
    };
    auto hint = [&](QPointF at, const QString &text) {
        if (text.isEmpty())
            return;
        const auto font = uiFont(this, 10);
        painter.setFont(font);
        painter.setPen(stroke.darker(125));
        painter.drawText(at + QPointF(12, 18), text);
    };
    auto marker = [&](QPointF paperMm, bool vertex, bool strong) {
        const auto at = transform.map(paperMm);
        painter.setPen(QPen(stroke, strong ? 2 : 1.5));
        painter.setBrush(strong ? QBrush(stroke) : QBrush(Qt::white));
        if (vertex)
            painter.drawRect(QRectF(at - QPointF(4, 4), QSizeF(8, 8)));
        else {
            QPolygonF diamond{at + QPointF(0, -5), at + QPointF(5, 0), at + QPointF(0, 5),
                              at + QPointF(-5, 0)};
            painter.drawPolygon(diamond);
        }
    };
    // The selected item: a blue-violet outline offset from its extent.
    if (selectedItem_ && liveItem(selectedItem_) && !textEditing()) {
        painter.setPen(QPen(stroke, 1.5));
        painter.setBrush(Qt::NoBrush);
        painter.drawRect(itemRect(selectedItem_).adjusted(-3, -3, 3, 3));
    }
    auto &t = itemTool_;
    QPen dashed(stroke, 1.5, Qt::DashLine);
    if (t.step > 0) {
        switch (tool_) {
        case Tool::Line: {
            painter.setPen(dashed);
            painter.drawLine(transform.map(QLineF(t.start, t.pointer)));
            const auto length = QLineF(t.start, t.pointer).length();
            pill(transform.map((t.start + t.pointer) / 2) + QPointF(0, -14),
                 paperMillimetres(tidy(length)) + " mm");
            hint(transform.map(t.pointer), snapHint_);
            break;
        }
        case Tool::Rectangle:
        case Tool::Text: {
            if (tool_ == Tool::Text && !dragMoved_)
                break;
            const auto frame = transform.mapRect(QRectF(t.start, t.pointer).normalized());
            painter.setPen(dashed);
            painter.setBrush(Qt::NoBrush);
            painter.drawRect(frame);
            const auto size = QRectF(t.start, t.pointer).normalized().size();
            pill(frame.center(), tool_ == Tool::Text
                                     ? "Wrap " + paperMillimetres(tidy(size.width())) + " mm"
                                     : paperMillimetres(tidy(size.width())) + " × " +
                                           paperMillimetres(tidy(size.height())) + " mm");
            hint(transform.map(t.pointer), snapHint_);
            break;
        }
        case Tool::Label:
            painter.setPen(dashed);
            painter.drawLine(transform.map(QLineF(t.start, t.pointer)));
            marker(t.start, t.anchors[0] && t.anchors[0]->vertex, true);
            break;
        case Tool::Dimension:
            if (t.step == 1) {
                painter.setPen(dashed);
                painter.drawLine(transform.map(QLineF(t.start, t.pointer)));
            } else if (t.step == 2 && t.anchors[0] && t.anchors[1]) {
                DrawingDimensionItem dimension;
                dimension.view = t.view;
                dimension.anchors = {t.anchors[0]->anchor, t.anchors[1]->anchor};
                dimension.offsetMm = t.offsetMm;
                const DrawingItemRecord preview{0, sheet_, 0, dimension};
                try {
                    ghost(drawingItemPrimitives(*sheet,
                                                {preview, evaluateDrawingItem(doc_, preview)}));
                } catch (const std::exception &) {
                }
                pill(transform.map(t.pointer) + QPointF(0, -18),
                     "Offset " + paperMillimetres(tidy(t.offsetMm)) + " mm");
            }
            for (const auto &anchor : t.anchors)
                if (anchor)
                    marker(qpoint(anchor->paperMm), anchor->vertex, true);
            break;
        default:
            break;
        }
    }
    if (hoverAnchor_ && (tool_ == Tool::Dimension || tool_ == Tool::Label) && t.step < 2 &&
        !(tool_ == Tool::Label && t.step == 1)) {
        marker(qpoint(hoverAnchor_->paperMm), hoverAnchor_->vertex, false);
        hint(transform.map(qpoint(hoverAnchor_->paperMm)), anchorKind());
    }
    if (previewItem_ && (dragMoved_ || anchored_))
        hint(itemRect(previewItem_->id).bottomLeft() + QPointF(-12, 0), snapHint_);
    // Reattach: the old point (the red cross) joined to the proposed or chosen point.
    if (reattaching())
        for (size_t i = 0; i < reattach_.proposal.anchors.size(); ++i) {
            const auto &candidate = reattach_.proposal.anchors[i];
            if (candidate.state == AnchorState::Resolved)
                continue;
            const auto from = transform.map(qpoint(candidate.lastMm));
            if (const auto &chosen = reattach_.chosen[i]) {
                const auto to = transform.map(qpoint(chosen->paperMm));
                painter.setPen(dashed);
                painter.drawLine(from, to);
                marker(qpoint(chosen->paperMm), chosen->vertex, i == reattach_.active);
                hint(to, i == reattach_.active ? "Proposed · Enter reattaches" : "Proposed");
            } else {
                hint(from, "No point nearby · click one in the view");
            }
        }
    painter.restore();
}
} // namespace sketchy
