#include "automation/drawing_sheet_commands.hpp"
#include "automation/inspection.hpp"
#include "io/annotations_io.hpp"
#include "io/drawing_items.hpp"
#include "io/drawing_items_io.hpp"
#include "io/drawing_sheets_io.hpp"
#include "io/drawing_view.hpp"
#include <QJsonArray>
#include <QStringList>
#include <cmath>
namespace sketchy {
namespace {
Id id(const QJsonValue &value) {
    bool ok{};
    const auto text = value.toString();
    const auto result = text.toULongLong(&ok);
    if (!value.isString() || !ok || !result || QString::number(result) != text)
        throw std::runtime_error("Drawing sheet command requires a canonical nonzero decimal identity");
    return result;
}
std::string name(const QJsonValue &value) {
    if (!value.isString())
        throw std::runtime_error("Drawing sheet or view name requires text");
    return value.toString().toStdString();
}
std::string sheetNumber(const QJsonValue &value) {
    if (!value.isString())
        throw std::runtime_error("Drawing sheet number requires text");
    return value.toString().toStdString();
}
double number(const QJsonValue &value) {
    if (!value.isDouble() || !std::isfinite(value.toDouble()))
        throw std::runtime_error("Drawing sheet command requires finite numbers");
    return value.toDouble();
}
QJsonObject exactObject(const QJsonValue &value, std::initializer_list<const char *> fields) {
    if (!value.isObject() || value.toObject().size() != qsizetype(fields.size()))
        throw std::runtime_error("Drawing sheet command object has missing or unknown fields");
    const auto result = value.toObject();
    for (const auto *field : fields)
        if (!result.contains(field))
            throw std::runtime_error("Drawing sheet command object has missing fields");
    return result;
}
DrawingPaper paper(const QJsonValue &value) {
    const auto object = exactObject(value, {"widthMm", "heightMm"});
    DrawingPaper result{number(object["widthMm"]), number(object["heightMm"])};
    result.validate();
    return result;
}
DrawingFrame frame(const QJsonValue &value) {
    const auto object = exactObject(value, {"xMm", "yMm", "widthMm", "heightMm"});
    return {number(object["xMm"]), number(object["yMm"]), number(object["widthMm"]),
            number(object["heightMm"])};
}
DrawingViewSource source(const QJsonValue &value) {
    const auto object = exactObject(value, {"kind", "scene"});
    if (object["kind"] != "scene")
        throw std::runtime_error("Drawing view source kind must be scene");
    return {DrawingViewSourceKind::Scene, id(object["scene"])};
}
std::array<double, 2> offset(const QJsonValue &value) {
    if (!value.isArray() || value.toArray().size() != 2)
        throw std::runtime_error("Drawing view offset requires two numbers");
    const auto values = value.toArray();
    return {number(values[0]), number(values[1])};
}
DrawingViewContent content(const QJsonValue &value) {
    if (value == "technicalLines")
        return DrawingViewContent::TechnicalLines;
    if (value == "rasterAppearance")
        return DrawingViewContent::RasterAppearance;
    throw std::runtime_error("Drawing view content must be technicalLines or rasterAppearance");
}
bool flag(const QJsonValue &value) {
    if (!value.isBool())
        throw std::runtime_error("Drawing view hidden-line setting must be boolean");
    return value.toBool();
}
DrawingViewPtr requireView(const Document &doc, Id view) {
    const auto found = doc.drawingViews().find(view);
    if (found == doc.drawingViews().end())
        throw std::runtime_error("Drawing view does not exist");
    return found->second;
}
// Fields that change what the view draws, so its fingerprint. The fingerprint is
// frame-local, so a pure frame translation (same size, offset, scale, content and
// hidden lines) draws the same and needs no evaluation (ADR 0164 §2).
bool drawsDifferently(const DrawingViewRecord &a, const DrawingViewRecord &b) {
    return a.source != b.source || a.frame.widthMm != b.frame.widthMm ||
           a.frame.heightMm != b.frame.heightMm || a.offsetMm != b.offsetMm ||
           a.scaleDenominator != b.scaleDenominator || a.content != b.content ||
           a.hiddenLines != b.hiddenLines;
}
// ADR 0163 re-stamp rule (R090.d): a user change to what a *current* view draws
// re-stamps it with the new fingerprint, because the user made that change. A view
// that was stale, broken, failed or never accepted keeps its stamp, so the change
// never silently accepts unreviewed model edits. If the changed view cannot be
// evaluated (broken or failed) the old stamp is kept and the state says why.
void remapBoundItems(Document &doc, Id view, const DrawingViewProjection &from);
void replaceView(Document &doc, Id view, DrawingViewRecord record, std::uint64_t revision) {
    const auto before = requireView(doc, view);
    record.id = view;
    record.accepted = before->accepted;
    const bool restamp = record.accepted && drawsDifferently(*before, record) &&
                         evaluateDrawingView(doc, view).state == DrawingViewState::Current;
    // Any change to the world <-> sheet mapping carries the bound items' lastMm
    // caches with it, in the same edit (ADR 0164, R091.c).
    const bool remaps = before->source != record.source || before->frame != record.frame ||
                        before->offsetMm != record.offsetMm ||
                        before->scaleDenominator != record.scaleDenominator;
    const auto mapping = remaps ? drawingViewMapping(doc, view) : DrawingViewMapping{};
    updateDrawingView(doc, view, std::move(record));
    if (mapping.projection)
        remapBoundItems(doc, view, *mapping.projection);
    if (!restamp)
        return;
    const auto after = evaluateDrawingView(doc, view);
    if (after.state == DrawingViewState::Broken || after.state == DrawingViewState::Failed ||
        after.fingerprint == before->accepted->fingerprint)
        return;
    acceptDrawingViews(doc, {{view, DrawingViewAcceptance{revision, after.fingerprint}}});
}
std::string reasons(const DrawingViewStatus &status) {
    std::string text;
    for (const auto &reason : status.reasons)
        text += (text.empty() ? "" : ", ") + reason;
    return text;
}
// Stamps every listed view that is stale. A broken or failed view is refused when
// it was named explicitly, and skipped by Accept all (it stays listed by status).
void accept(Document &doc, const std::vector<Id> &views, bool named, std::uint64_t revision) {
    std::map<Id, DrawingViewAcceptance> stamps;
    for (const auto view : views) {
        const auto status = evaluateDrawingView(doc, view);
        if (status.state == DrawingViewState::Broken || status.state == DrawingViewState::Failed) {
            if (named)
                throw InspectionError(
                    "DRAWING_VIEW_NOT_ACCEPTABLE",
                    "Drawing view " + std::to_string(view) + " is " +
                        drawingViewStateName(status.state) + " (" + reasons(status) +
                        ") and cannot be accepted until its source is repaired");
            continue;
        }
        if (status.state == DrawingViewState::Stale)
            stamps[view] = {revision, status.fingerprint};
    }
    acceptDrawingViews(doc, stamps);
}
QJsonObject object(QJsonObject properties) {
    QJsonArray required;
    for (const auto &key : properties.keys())
        required.append(key);
    return {{"type", "object"},
            {"properties", properties},
            {"required", required},
            {"additionalProperties", false}};
}
QJsonObject range(double minimum, double maximum) {
    return {{"type", "number"}, {"minimum", minimum}, {"maximum", maximum}};
}
QJsonObject viewRecord(const Document &doc, Id view) {
    auto record =
        encodeDrawingViews({{view, doc.drawingViews().at(view)}}).first().toObject();
    record["sourceMissing"] =
        !drawingViewSourceExists(doc.scenes(), doc.drawingViews().at(view)->source);
    return record;
}
QJsonArray vector(Vec3 value) { return {value.x, value.y, value.z}; }
QJsonObject statusJson(const Document &doc, const DrawingViewStatus &status) {
    const auto &record = *doc.drawingViews().at(status.view);
    QJsonArray reasons;
    for (const auto &reason : status.reasons)
        reasons.append(QString::fromStdString(reason));
    return {{"view", QString::number(status.view)},
            {"sheet", QString::number(record.sheet)},
            {"name", QString::fromStdString(record.name)},
            {"state", drawingViewStateName(status.state)},
            {"reasons", reasons},
            {"notToScale", status.notToScale},
            {"warnings", qint64(status.warnings)},
            {"fingerprint", status.fingerprint.empty()
                                ? QJsonValue(QJsonValue::Null)
                                : QJsonValue(QString::fromStdString(status.fingerprint))},
            {"detail", status.detail.empty() ? QJsonValue(QJsonValue::Null)
                                             : QJsonValue(QString::fromStdString(status.detail))}};
}
// ---- Page items (ADR 0164 §3, R091.c) ----
const char *kindCode(DrawingItemKind kind) {
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
    throw std::runtime_error("Unknown drawing item kind");
}
DrawingItemData defaultItem(const QJsonValue &kind) {
    if (kind == "text")
        return DrawingTextItem{};
    if (kind == "label")
        return DrawingLabelItem{};
    if (kind == "dimension")
        return DrawingDimensionItem{};
    if (kind == "line")
        return DrawingLineItem{};
    if (kind == "rectangle")
        return DrawingRectangleItem{};
    if (kind == "titleBlock")
        return DrawingTitleBlockItem{};
    throw std::runtime_error(
        "Drawing item kind must be text, label, dimension, line, rectangle or titleBlock");
}
DrawingItemPtr requireItem(const Document &doc, Id item) {
    const auto found = doc.drawingItems().find(item);
    if (found == doc.drawingItems().end())
        throw std::runtime_error("Drawing item does not exist");
    return found->second;
}
QJsonObject encodeItem(Id key, const DrawingItemRecord &record) {
    auto copy = std::make_shared<DrawingItemRecord>(record);
    copy->id = key;
    return encodeDrawingItems({{key, copy}}).first().toObject();
}
// Command anchors name live model topology; R091 binds vertices and edges only.
AnnotationAnchor viewAnchor(const Document &doc, const QJsonValue &value) {
    if (!value.isObject())
        throw std::runtime_error("Drawing item anchor requires an object");
    const auto o = value.toObject();
    if (o["kind"] == "vertex") {
        exactObject(o, {"kind", "body", "vertex"});
        return vertexAnchor(doc, id(o["body"]), id(o["vertex"]));
    }
    if (o["kind"] == "edge") {
        exactObject(o, {"kind", "body", "edge", "fraction"});
        return edgeAnchor(doc, id(o["body"]), id(o["edge"]), number(o["fraction"]));
    }
    throw std::runtime_error("Drawing item anchors must be vertex or edge anchors");
}
QJsonObject anchorInput(const AnnotationAnchor &anchor) {
    if (anchor.kind == AnchorKind::Vertex)
        return {{"kind", "vertex"},
                {"body", QString::number(anchor.body)},
                {"vertex", QString::number(anchor.entity)}};
    return {{"kind", "edge"},
            {"body", QString::number(anchor.body)},
            {"edge", QString::number(anchor.entity)},
            {"fraction", anchor.parameter}};
}
// A dimension or a view-target label can be placed only on a view that is drawn
// to scale and evaluates (ADR 0164: DRAWING_ITEM_NOT_MEASURABLE). Costs one capture.
void requireMeasurable(const Document &doc, Id view) {
    if (!doc.drawingViews().contains(view))
        throw std::runtime_error("Drawing item must reference an existing drawing view");
    const auto status = evaluateDrawingView(doc, view);
    if (status.state == DrawingViewState::Broken || status.state == DrawingViewState::Failed)
        throw InspectionError("DRAWING_ITEM_NOT_MEASURABLE",
                              "Drawing view " + std::to_string(view) + " is " +
                                  drawingViewStateName(status.state) + " (" + reasons(status) +
                                  ") and cannot be measured");
    if (status.notToScale)
        throw InspectionError("DRAWING_ITEM_NOT_MEASURABLE",
                              "Drawing view " + std::to_string(view) +
                                  " is not to scale (perspective) and cannot be measured");
}
// The view-bound anchors of an item, or none.
std::vector<AnnotationAnchor *> boundAnchors(DrawingItemData &data, Id &view,
                                             std::vector<DrawingPointMm *> &last) {
    std::vector<AnnotationAnchor *> result;
    if (auto *dimension = std::get_if<DrawingDimensionItem>(&data)) {
        view = dimension->view;
        for (size_t i = 0; i < 2; ++i) {
            result.push_back(&dimension->anchors[i]);
            last.push_back(&dimension->lastMm[i]);
        }
    } else if (auto *label = std::get_if<DrawingLabelItem>(&data);
               label && label->target.kind == DrawingLabelTargetKind::View) {
        view = label->target.view;
        result.push_back(&label->target.anchor);
        last.push_back(&label->target.lastMm);
    }
    return result;
}
// Carries each bound item's lastMm from the view's old mapping to its new one:
// sheetToWorld through the old projection, at the depth of the anchor's last
// resolved world point, then worldToSheet through the new one. When either mapping
// is unavailable (broken or not to scale) lastMm is left as it is.
void remapBoundItems(Document &doc, Id view, const DrawingViewProjection &from) {
    const auto to = drawingViewMapping(doc, view);
    if (!to.projection)
        return;
    std::vector<std::pair<Id, DrawingItemData>> changed;
    for (const auto &[id, item] : doc.drawingItems()) {
        if (drawingItemView(*item) != view)
            continue;
        auto data = item->data;
        Id bound{};
        std::vector<DrawingPointMm *> last;
        const auto anchors = boundAnchors(data, bound, last);
        for (size_t i = 0; i < anchors.size(); ++i) {
            const auto depth = from.worldToSheet(anchors[i]->fallback).z;
            const auto world = from.sheetToWorld({(*last[i])[0], (*last[i])[1], depth});
            const auto sheet = to.projection->worldToSheet(world);
            *last[i] = {sheet.x, sheet.y};
        }
        if (data != item->data)
            changed.emplace_back(id, std::move(data));
    }
    for (auto &[id, data] : changed)
        updateDrawingItem(doc, id, std::move(data));
}
// Writes the derived paper positions (lastMm) and world fallbacks of the bound
// anchors when every anchor resolves and the view projects; otherwise the item is
// broken and keeps its last positions for the broken marker and Reattach.
void refreshDerived(const Document &doc, DrawingItemData &data) {
    Id view{};
    std::vector<DrawingPointMm *> last;
    const auto anchors = boundAnchors(data, view, last);
    if (anchors.empty() || !doc.drawingViews().contains(view))
        return;
    const auto mapping = drawingViewMapping(doc, view);
    if (!mapping.projection)
        return;
    std::vector<Vec3> world;
    for (const auto *anchor : anchors) {
        const auto resolved = resolveAnnotationAnchor(doc, *anchor);
        if (resolved.state != AnchorState::Resolved)
            return;
        world.push_back(resolved.point);
    }
    for (size_t i = 0; i < anchors.size(); ++i) {
        anchors[i]->fallback = world[i];
        const auto sheet = mapping.projection->worldToSheet(world[i]);
        *last[i] = {sheet.x, sheet.y};
    }
}
void requireFields(const QJsonObject &data, const QStringList &allowed,
                   const QStringList &required) {
    for (auto it = data.begin(); it != data.end(); ++it)
        if (!allowed.contains(it.key()))
            throw std::runtime_error("Drawing item data has an unknown or unchangeable field: " +
                                     it.key().toStdString());
    for (const auto &field : required)
        if (!data.contains(field))
            throw std::runtime_error("Drawing item data is missing " + field.toStdString());
}
// Builds item data from the command's data object over a base (the kind's defaults
// on create, the stored data on update). Field shapes reuse the strict record codec.
// Returns true when the command binds the item to view anchors anew.
bool applyItemData(const Document &doc, DrawingItemData &data, const QJsonValue &value,
                   bool creating) {
    if (!value.isObject() || value.toObject().isEmpty())
        throw std::runtime_error("Drawing item data requires an object with at least one field");
    const auto input = value.toObject();
    const auto kind = DrawingItemRecord{0, 0, 0, data}.kind();
    QJsonObject encoded =
        encodeItem(1, DrawingItemRecord{1, 1, 0, data})["data"].toObject();
    bool binds = false;
    switch (kind) {
    case DrawingItemKind::Text:
        requireFields(input, {"frame", "text", "font", "align"},
                      creating ? QStringList{"frame", "text"} : QStringList{});
        break;
    case DrawingItemKind::Label:
        requireFields(input, {"text", "font", "textAt", "leader", "target"},
                      creating ? QStringList{"text", "textAt", "target"} : QStringList{});
        if (input.contains("target")) {
            const auto target = input["target"].toObject();
            if (target["kind"] == "view") {
                exactObject(target, {"kind", "view", "anchor"});
                encoded["target"] = QJsonObject{
                    {"kind", "view"},
                    {"view", target["view"]},
                    {"anchor", encodeAnnotationAnchor(viewAnchor(doc, target["anchor"]))},
                    {"lastMm", QJsonArray{0, 0}}};
                binds = true;
            } else
                encoded["target"] = input["target"];
        }
        break;
    case DrawingItemKind::Dimension:
        requireFields(input,
                      creating ? QStringList{"view", "anchors", "orientation", "offsetMm",
                                             "prefix", "font"}
                               : QStringList{"orientation", "offsetMm", "prefix", "font"},
                      creating ? QStringList{"view", "anchors"} : QStringList{});
        if (creating) {
            const auto anchors = input["anchors"];
            if (!anchors.isArray() || anchors.toArray().size() != 2)
                throw std::runtime_error("Drawing dimension requires two anchors");
            encoded["view"] = input["view"];
            encoded["anchors"] = QJsonArray{
                encodeAnnotationAnchor(viewAnchor(doc, anchors.toArray()[0])),
                encodeAnnotationAnchor(viewAnchor(doc, anchors.toArray()[1]))};
            binds = true;
        }
        break;
    case DrawingItemKind::Line:
        requireFields(input, {"from", "to", "strokeMm", "dash"},
                      creating ? QStringList{"from", "to"} : QStringList{});
        break;
    case DrawingItemKind::Rectangle:
        requireFields(input, {"frame", "strokeMm", "dash"},
                      creating ? QStringList{"frame"} : QStringList{});
        break;
    case DrawingItemKind::TitleBlock:
        requireFields(input, {"frame", "font", "fields"},
                      creating ? QStringList{"frame"} : QStringList{});
        if (input.contains("fields")) {
            // Fields merge one by one; the derived sheet number, page and scale are
            // never stored.
            const auto value = input["fields"];
            const auto fields = value.toObject();
            auto merged = encoded["fields"].toObject();
            if (!value.isObject() || fields.isEmpty())
                throw std::runtime_error("Title block fields require an object");
            for (auto it = fields.begin(); it != fields.end(); ++it) {
                if (!merged.contains(it.key()))
                    throw std::runtime_error("Unknown title block field: " +
                                             it.key().toStdString());
                merged[it.key()] = it.value();
            }
            encoded["fields"] = merged;
        }
        break;
    }
    for (auto it = input.begin(); it != input.end(); ++it)
        if (it.key() != "anchors" && it.key() != "target" && it.key() != "fields" &&
            !(creating && it.key() == "view"))
            encoded[it.key()] = it.value();
    const QJsonObject record{{"id", "1"}, {"sheet", "1"}, {"z", 0},
                             {"kind", kindCode(kind)}, {"data", encoded}};
    data = decodeDrawingItems(QJsonArray{record}).at(1)->data;
    return binds;
}
Id itemView(const DrawingItemData &data) {
    return drawingItemView(DrawingItemRecord{0, 0, 0, data});
}
QJsonValue pointJson(const std::optional<DrawingPointMm> &point) {
    return point ? QJsonValue(QJsonArray{(*point)[0], (*point)[1]}) : QJsonValue(QJsonValue::Null);
}
QJsonValue pairJson(const std::optional<std::array<DrawingPointMm, 2>> &points) {
    return points ? QJsonValue(QJsonArray{pointJson((*points)[0]), pointJson((*points)[1])})
                  : QJsonValue(QJsonValue::Null);
}
QJsonValue numberJson(const std::optional<double> &value) {
    return value ? QJsonValue(*value) : QJsonValue(QJsonValue::Null);
}
const char *anchorStateName(AnchorState state) {
    return state == AnchorState::Resolved  ? "resolved"
           : state == AnchorState::Missing ? "missing"
                                           : "ambiguous";
}
QJsonArray reasonsJson(const std::vector<std::string> &reasons) {
    QJsonArray result;
    for (const auto &reason : reasons)
        result.append(QString::fromStdString(reason));
    return result;
}
QJsonObject evaluationJson(const Document &doc, const DrawingItemEvaluation &evaluation) {
    QJsonArray anchors;
    for (const auto &anchor : evaluation.anchors)
        anchors.append(QJsonObject{
            {"state", anchorStateName(anchor.state)},
            {"world", vector(anchor.world)},
            {"paperMm", pointJson(anchor.paper)}});
    QJsonValue titleBlock = QJsonValue::Null;
    if (evaluation.titleBlock)
        titleBlock = QJsonObject{
            {"sheetNumber", QString::fromStdString(evaluation.titleBlock->sheetNumber)},
            {"page", QString::fromStdString(evaluation.titleBlock->page)},
            {"scale", QString::fromStdString(evaluation.titleBlock->scale)}};
    return {{"state", drawingItemStateName(evaluation.state)},
            {"reasons", reasonsJson(evaluation.reasons)},
            {"anchors", anchors},
            {"measuredMm", pairJson(evaluation.measuredMm)},
            {"lineMm", pairJson(evaluation.lineMm)},
            {"textMm", pointJson(evaluation.textMm)},
            {"tipMm", pointJson(evaluation.tipMm)},
            {"paperLengthMm", numberJson(evaluation.paperLengthMm)},
            {"valueMetres", numberJson(evaluation.valueMetres)},
            {"displayUnits", QString::fromUtf8(unitCode(doc.displayUnits()).data())},
            {"text", QString::fromStdString(evaluation.text)},
            {"titleBlock", titleBlock}};
}
void createItem(Document &doc, const QJsonObject &command) {
    const auto sheet = id(command["sheet"]);
    if (!doc.drawingSheets().contains(sheet))
        throw std::runtime_error("Drawing sheet does not exist");
    auto data = defaultItem(command["kind"]);
    if (applyItemData(doc, data, command["data"], true))
        requireMeasurable(doc, itemView(data));
    refreshDerived(doc, data);
    createDrawingItem(doc, sheet, std::move(data));
}
void updateItem(Document &doc, const QJsonObject &command) {
    const auto item = id(command["item"]);
    const auto before = requireItem(doc, item);
    if (command["kind"] != kindCode(before->kind()))
        throw std::runtime_error("Drawing item update must name the item's kind; the kind "
                                 "cannot change");
    auto data = before->data;
    if (applyItemData(doc, data, command["data"], false))
        requireMeasurable(doc, itemView(data));
    refreshDerived(doc, data);
    updateDrawingItem(doc, item, std::move(data));
}
// Rebinds a dimension's or view-target label's anchors to explicit new model
// topology (ADR 0164 Reattach). The proposal is a query; the user confirms.
void reattachItem(Document &doc, const QJsonObject &command) {
    const auto item = id(command["item"]);
    auto data = requireItem(doc, item)->data;
    Id view{};
    std::vector<DrawingPointMm *> last;
    const auto anchors = boundAnchors(data, view, last);
    if (anchors.empty())
        throw std::runtime_error("Only dimensions and view-target labels can be reattached");
    const auto value = command["anchors"];
    if (!value.isArray() || value.toArray().size() != qsizetype(anchors.size()))
        throw std::runtime_error(anchors.size() == 2
                                     ? "Reattaching a dimension requires two anchors"
                                     : "Reattaching a label requires one anchor");
    for (size_t i = 0; i < anchors.size(); ++i)
        *anchors[i] = viewAnchor(doc, value.toArray()[qsizetype(i)]);
    requireMeasurable(doc, view);
    refreshDerived(doc, data);
    updateDrawingItem(doc, item, std::move(data));
}
} // namespace
bool isDrawingSheetCommand(const QString &name) {
    return name == "drawing_sheet.create" || name == "drawing_sheet.rename" ||
           name == "drawing_sheet.resize" || name == "drawing_sheet.reorder" ||
           name == "drawing_sheet.delete" || name == "drawing_view.create" ||
           name == "drawing_view.update" || name == "drawing_view.move" ||
           name == "drawing_view.delete" || name == "drawing_view.accept" ||
           name == "drawing_sheet.update" || isDrawingItemCommand(name);
}
bool isDrawingItemCommand(const QString &name) {
    return name == "drawing_item.create" || name == "drawing_item.update" ||
           name == "drawing_item.move" || name == "drawing_item.reorder" ||
           name == "drawing_item.delete" || name == "drawing_item.reattach";
}
void executeDrawingSheetCommand(Document &doc, const QJsonObject &command,
                                std::uint64_t revision) {
    const auto operation = command["command"].toString();
    if (operation == "drawing_sheet.create") {
        const auto page = paper(command["paper"]);
        createDrawingSheet(doc, name(command["name"]), page,
                           command.contains("number") ? sheetNumber(command["number"])
                                                      : std::string{},
                           command.contains("marginMm")
                               ? std::optional<double>(number(command["marginMm"]))
                               : std::nullopt);
    } else if (operation == "drawing_sheet.update") {
        const auto sheet = id(command["sheet"]);
        const auto found = doc.drawingSheets().find(sheet);
        if (found == doc.drawingSheets().end())
            throw std::runtime_error("Drawing sheet does not exist");
        if (!command.contains("number") && !command.contains("marginMm") &&
            !command.contains("dimensionFormat"))
            throw std::runtime_error(
                "Drawing sheet update requires number, marginMm or dimensionFormat");
        std::optional<DrawingDimensionFormat> format;
        if (command.contains("dimensionFormat")) {
            // Fields merge one by one over the sheet's format (R091.g).
            const auto value = command["dimensionFormat"];
            if (!value.isObject() || value.toObject().isEmpty())
                throw std::runtime_error("Drawing dimension format requires an object");
            auto merged = encodeDrawingDimensionFormat(found->second->dimensionFormat);
            const auto fields = value.toObject();
            for (auto it = fields.begin(); it != fields.end(); ++it) {
                if (!merged.contains(it.key()))
                    throw std::runtime_error("Unknown drawing dimension format field: " +
                                             it.key().toStdString());
                merged[it.key()] = it.value();
            }
            format = decodeDrawingDimensionFormat(merged);
        }
        updateDrawingSheetPage(doc, sheet,
                               command.contains("number") ? sheetNumber(command["number"])
                                                          : found->second->number,
                               command.contains("marginMm") ? number(command["marginMm"])
                                                            : found->second->marginMm,
                               format);
    } else if (operation == "drawing_item.create")
        createItem(doc, command);
    else if (operation == "drawing_item.update")
        updateItem(doc, command);
    else if (operation == "drawing_item.reattach")
        reattachItem(doc, command);
    else if (operation == "drawing_item.move") {
        const auto item = id(command["item"]);
        const auto sheet = id(command["sheet"]);
        if (!doc.drawingSheets().contains(sheet))
            throw std::runtime_error("Drawing sheet does not exist");
        if (drawingItemView(*requireItem(doc, item)))
            throw std::runtime_error("An item bound to a drawing view moves with its view "
                                     "(drawing_view.move)");
        moveDrawingItem(doc, item, sheet);
    } else if (operation == "drawing_item.reorder") {
        const auto sheet = id(command["sheet"]);
        if (!doc.drawingSheets().contains(sheet))
            throw std::runtime_error("Drawing sheet does not exist");
        const auto value = command["order"];
        if (!value.isArray() || value.toArray().size() > qsizetype(drawingItemsPerSheetLimit))
            throw std::runtime_error("Drawing item order requires a bounded identity array");
        std::vector<Id> order;
        for (const auto &entry : value.toArray())
            order.push_back(id(entry));
        reorderDrawingItems(doc, sheet, order);
    } else if (operation == "drawing_item.delete") {
        const auto item = id(command["item"]);
        requireItem(doc, item);
        eraseDrawingItem(doc, item);
    }
    else if (operation == "drawing_sheet.rename")
        renameDrawingSheet(doc, id(command["sheet"]), name(command["name"]));
    else if (operation == "drawing_sheet.resize")
        resizeDrawingSheet(doc, id(command["sheet"]), paper(command["paper"]));
    else if (operation == "drawing_sheet.delete") {
        const auto sheet = id(command["sheet"]);
        if (!doc.drawingSheets().contains(sheet))
            throw std::runtime_error("Drawing sheet does not exist");
        eraseDrawingSheet(doc, sheet);
    } else if (operation == "drawing_sheet.reorder") {
        const auto value = command["order"];
        if (!value.isArray() || value.toArray().size() > qsizetype(drawingSheetCountLimit))
            throw std::runtime_error("Drawing sheet order requires a bounded identity array");
        std::vector<Id> order;
        for (const auto &entry : value.toArray())
            order.push_back(id(entry));
        reorderDrawingSheets(doc, order);
    } else if (operation == "drawing_view.create") {
        DrawingViewRecord view;
        view.name = name(command["name"]);
        view.sheet = id(command["sheet"]);
        view.source = source(command["source"]);
        view.frame = frame(command["frame"]);
        view.scaleDenominator = number(command["scaleDenominator"]);
        if (command.contains("offsetMm"))
            view.offsetMm = offset(command["offsetMm"]);
        if (command.contains("content"))
            view.content = content(command["content"]);
        if (command.contains("hiddenLines"))
            view.hiddenLines = flag(command["hiddenLines"]);
        // A new view starts unaccepted (stale, notAccepted) until drawing_view.accept.
        createDrawingView(doc, std::move(view));
    } else if (operation == "drawing_view.update") {
        const auto view = id(command["view"]);
        auto record = *requireView(doc, view);
        bool any = false;
        if (command.contains("name"))
            record.name = name(command["name"]), any = true;
        if (command.contains("source"))
            record.source = source(command["source"]), any = true;
        if (command.contains("frame"))
            record.frame = frame(command["frame"]), any = true;
        if (command.contains("offsetMm"))
            record.offsetMm = offset(command["offsetMm"]), any = true;
        if (command.contains("scaleDenominator"))
            record.scaleDenominator = number(command["scaleDenominator"]), any = true;
        if (command.contains("content"))
            record.content = content(command["content"]), any = true;
        if (command.contains("hiddenLines"))
            record.hiddenLines = flag(command["hiddenLines"]), any = true;
        if (!any)
            throw std::runtime_error("Drawing view update requires at least one property");
        replaceView(doc, view, std::move(record), revision);
    } else if (operation == "drawing_view.move") {
        const auto view = id(command["view"]);
        const auto sheet = id(command["sheet"]);
        if (!doc.drawingSheets().contains(sheet))
            throw std::runtime_error("Drawing sheet does not exist");
        auto record = *requireView(doc, view);
        record.sheet = sheet;
        if (command.contains("frame"))
            record.frame = frame(command["frame"]);
        replaceView(doc, view, std::move(record), revision);
    } else if (operation == "drawing_view.delete")
        eraseDrawingView(doc, id(command["view"]));
    else if (operation == "drawing_view.accept") {
        const int scopes = int(command.contains("view")) + int(command.contains("sheet")) +
                           int(command.contains("all"));
        if (scopes != 1 || (command.contains("all") && command["all"] != true))
            throw std::runtime_error(
                "Drawing view accept requires exactly one of view, sheet or all: true");
        if (command.contains("view")) {
            const auto view = id(command["view"]);
            requireView(doc, view);
            accept(doc, {view}, true, revision);
        } else if (command.contains("sheet")) {
            const auto sheet = id(command["sheet"]);
            if (!doc.drawingSheets().contains(sheet))
                throw std::runtime_error("Drawing sheet does not exist");
            accept(doc, drawingViewsOnSheet(doc, sheet), false, revision);
        } else
            accept(doc, orderedDrawingViews(doc), false, revision);
    } else
        throw std::runtime_error("Unknown drawing sheet command");
}
QJsonObject drawingPaperSchema() {
    return object({{"widthMm", range(10, 2000)}, {"heightMm", range(10, 2000)}});
}
QJsonObject drawingFrameSchema() {
    auto result = object({{"xMm", range(0, 1990)},
                          {"yMm", range(0, 1990)},
                          {"widthMm", range(10, 2000)},
                          {"heightMm", range(10, 2000)}});
    result["description"] =
        "Viewport and crop rectangle in paper millimetres from the top-left corner, x right "
        "and y down; it must lie inside the sheet paper";
    return result;
}
QJsonObject drawingViewSourceSchema() {
    return object({{"kind", QJsonObject{{"const", "scene"}}},
                   {"scene", QJsonObject{{"type", "string"},
                                         {"pattern", "^[1-9][0-9]*$"},
                                         {"maxLength", 20}}}});
}
QJsonObject drawingViewOffsetSchema() {
    return {{"type", "array"},
            {"items", range(-drawingViewOffsetLimitMm, drawingViewOffsetLimitMm)},
            {"minItems", 2},
            {"maxItems", 2},
            {"description",
             "Paper-millimetre pan of the scene camera target from the frame centre, x right "
             "and y down"}};
}
QJsonObject drawingViewScaleSchema() {
    auto result = range(.001, 1e9);
    result["description"] =
        "Declared scale denominator (1:N); never derived from camera zoom or geometry";
    return result;
}
QJsonObject drawingViewContentSchema() {
    return {{"type", "string"}, {"enum", QJsonArray{"technicalLines", "rasterAppearance"}}};
}
std::vector<Id> orderedDrawingViews(const Document &doc, Id sheet) {
    std::vector<Id> result;
    for (const auto page : orderedDrawingSheets(doc))
        if (!sheet || page == sheet)
            for (const auto view : drawingViewsOnSheet(doc, page))
                result.push_back(view);
    return result;
}
QJsonObject drawingSheetSummary(const Document &doc, Id sheet) {
    const auto found = doc.drawingSheets().find(sheet);
    if (found == doc.drawingSheets().end())
        throw InspectionError("NOT_FOUND", "Drawing sheet does not exist");
    auto result = encodeDrawingSheets({{sheet, found->second}}).first().toObject();
    result["views"] = int(drawingViewsOnSheet(doc, sheet).size());
    result["items"] = int(drawingItemsOnSheet(doc, sheet).size());
    return result;
}
QJsonObject drawingSheetDescription(const Document &doc, Id sheet) {
    auto result = drawingSheetSummary(doc, sheet);
    QJsonArray views;
    for (const auto view : drawingViewsOnSheet(doc, sheet))
        views.append(viewRecord(doc, view));
    result["views"] = views;
    result["status"] = "Derived view states are read through drawing_views.status";
    return result;
}
QJsonObject drawingViewStatusDescription(const Document &doc, Id view) {
    if (!doc.drawingViews().contains(view))
        throw InspectionError("NOT_FOUND", "Drawing view does not exist");
    return statusJson(doc, evaluateDrawingView(doc, view));
}
QJsonObject drawingViewDescription(const Document &doc, Id view) {
    if (!doc.drawingViews().contains(view))
        throw InspectionError("NOT_FOUND", "Drawing view does not exist");
    auto result = viewRecord(doc, view);
    const auto evaluation = evaluateDrawingViewDrawing(doc, view);
    result["status"] = statusJson(doc, evaluation.status);
    if (evaluation.projection) {
        const auto &page = evaluation.projection->page;
        const auto &frame = evaluation.projection->frame;
        result["projection"] = QJsonObject{
            {"frame", QJsonObject{{"xMm", frame.xMm},
                                  {"yMm", frame.yMm},
                                  {"widthMm", frame.widthMm},
                                  {"heightMm", frame.heightMm}}},
            {"scaleDenominator", page.scaleDenominator},
            {"paperMillimetresPerMetre", 1000 / page.scaleDenominator},
            {"origin", vector(page.origin)},
            {"right", vector(page.right)},
            {"up", vector(page.up)},
            {"towardEye", vector(page.towardEye)},
            {"includeHidden", page.includeHidden},
            {"mapping", "origin is drawn at the frame centre: sheet x = frame centre x + "
                        "dot(right, p - origin) * paperMillimetresPerMetre and sheet y = frame "
                        "centre y - dot(up, p - origin) * paperMillimetresPerMetre"}};
    } else
        result["projection"] = QJsonValue::Null;
    if (evaluation.drawing) {
        qint64 hidden = 0;
        for (const auto &line : evaluation.drawing->geometry.lines)
            hidden += line.hidden;
        result["drawing"] = QJsonObject{
            {"lines", qint64(evaluation.drawing->geometry.lines.size())},
            {"hiddenLines", hidden},
            {"annotations", qint64(evaluation.drawing->annotations.size())}};
    } else
        result["drawing"] = QJsonValue::Null;
    return result;
}
std::vector<Id> orderedDrawingItems(const Document &doc, Id sheet) {
    std::vector<Id> result;
    for (const auto page : orderedDrawingSheets(doc))
        if (!sheet || page == sheet)
            for (const auto item : drawingItemsOnSheet(doc, page))
                result.push_back(item);
    return result;
}
QJsonObject drawingItemSummary(const Document &doc, Id item) {
    const auto found = doc.drawingItems().find(item);
    if (found == doc.drawingItems().end())
        throw InspectionError("NOT_FOUND", "Drawing item does not exist");
    auto result = encodeItem(item, *found->second);
    const auto evaluation = evaluateDrawingItem(doc, item);
    result["state"] = drawingItemStateName(evaluation.state);
    result["reasons"] = reasonsJson(evaluation.reasons);
    return result;
}
QJsonObject drawingItemDescription(const Document &doc, Id item) {
    const auto found = doc.drawingItems().find(item);
    if (found == doc.drawingItems().end())
        throw InspectionError("NOT_FOUND", "Drawing item does not exist");
    auto result = encodeItem(item, *found->second);
    result["evaluation"] = evaluationJson(doc, evaluateDrawingItem(doc, item));
    result["revision"] = QString::number(doc.revision());
    return result;
}
QJsonObject drawingItemReattachDescription(const Document &doc, Id item) {
    const auto found = doc.drawingItems().find(item);
    if (found == doc.drawingItems().end())
        throw InspectionError("NOT_FOUND", "Drawing item does not exist");
    if (!drawingItemView(*found->second))
        throw InspectionError("INVALID_REQUEST",
                              "Only dimensions and view-target labels can be reattached");
    const auto proposal = drawingReattachCandidates(doc, item);
    QJsonArray anchors;
    for (const auto &candidate : proposal.anchors)
        anchors.append(QJsonObject{
            {"index", qint64(candidate.index)},
            {"state", anchorStateName(candidate.state)},
            {"lastMm", QJsonArray{candidate.lastMm[0], candidate.lastMm[1]}},
            {"candidate", candidate.anchor ? QJsonValue(anchorInput(*candidate.anchor))
                                           : QJsonValue(QJsonValue::Null)},
            {"candidatePaperMm", pointJson(candidate.paperMm)},
            {"distanceMm", numberJson(candidate.distanceMm)}});
    return {{"item", QString::number(item)},
            {"view", QString::number(drawingItemView(*found->second))},
            {"revision", QString::number(doc.revision())},
            {"state", drawingItemStateName(proposal.evaluation.state)},
            {"reasons", reasonsJson(proposal.evaluation.reasons)},
            {"anchors", anchors},
            {"unavailable", reasonsJson(proposal.unavailable)},
            {"confirm", "Proposals are never applied automatically; commit the confirmed anchors "
                        "with drawing_item.reattach"}};
}
QJsonObject drawingSheetNumberSchema() {
    return {{"type", "string"},
            {"maxLength", 32},
            {"description",
             "Sheet number such as A-101; at most 32 bytes, unique among sheets when nonempty"}};
}
QJsonObject drawingSheetMarginSchema() {
    auto result = range(0, drawingSheetMarginLimitMm);
    result["description"] =
        "Page-margin frame in millimetres, at most a quarter of the shorter paper side; "
        "defaults to 10";
    return result;
}
QJsonObject drawingItemAnchorSchema() {
    const QJsonObject id{{"type", "string"}, {"pattern", "^[1-9][0-9]*$"}, {"maxLength", 20}};
    return {{"oneOf",
             QJsonArray{object({{"kind", QJsonObject{{"const", "vertex"}}},
                                {"body", id},
                                {"vertex", id}}),
                        object({{"kind", QJsonObject{{"const", "edge"}}},
                                {"body", id},
                                {"edge", id},
                                {"fraction", range(0, 1)}})}},
            {"description", "A model vertex, or a point on a model edge at a fraction from its "
                            "canonical a endpoint to b"}};
}
namespace {
QJsonObject optionalObject(QJsonObject properties, QJsonArray required) {
    return {{"type", "object"},
            {"properties", properties},
            {"required", required},
            {"additionalProperties", false}};
}
QJsonObject paperPoint() {
    return {{"type", "array"}, {"items", range(0, 2000)}, {"minItems", 2}, {"maxItems", 2}};
}
QJsonObject itemFrame() {
    return object({{"xMm", range(0, 2000)},
                   {"yMm", range(0, 2000)},
                   {"widthMm", range(0, 2000)},
                   {"heightMm", range(0, 2000)}});
}
QJsonObject font() {
    return object({{"family", QJsonObject{{"type", "string"}, {"minLength", 1}, {"maxLength", 128}}},
                   {"sizePt", range(drawingFontMinPt, drawingFontMaxPt)}});
}
QJsonObject text(int minimum, int maximum) {
    return {{"type", "string"}, {"minLength", minimum}, {"maxLength", maximum}};
}
QJsonObject choice(QJsonArray values) { return {{"type", "string"}, {"enum", values}}; }
QJsonObject stroke() { return range(drawingStrokeMinMm, drawingStrokeMaxMm); }
} // namespace
QJsonArray drawingItemDataSchemas(bool creating) {
    const QJsonObject id{{"type", "string"}, {"pattern", "^[1-9][0-9]*$"}, {"maxLength", 20}};
    auto required = [&](QJsonArray fields) { return creating ? fields : QJsonArray{}; };
    const auto labelTarget = QJsonObject{
        {"oneOf", QJsonArray{object({{"kind", QJsonObject{{"const", "paper"}}},
                                     {"xMm", range(0, 2000)},
                                     {"yMm", range(0, 2000)}}),
                             object({{"kind", QJsonObject{{"const", "view"}}},
                                     {"view", id},
                                     {"anchor", drawingItemAnchorSchema()}})}}};
    QJsonObject dimension{{"orientation", choice({"aligned", "horizontal", "vertical"})},
                          {"offsetMm", range(-drawingViewOffsetLimitMm, drawingViewOffsetLimitMm)},
                          {"prefix", text(0, 256)},
                          {"font", font()}};
    if (creating) {
        dimension["view"] = id;
        dimension["anchors"] = QJsonObject{{"type", "array"},
                                           {"items", drawingItemAnchorSchema()},
                                           {"minItems", 2},
                                           {"maxItems", 2}};
    }
    const QJsonObject fields{{"project", text(0, 256)},
                             {"drawingTitle", text(0, 256)},
                             {"date", text(0, 256)},
                             {"revision", text(0, 256)}};
    auto entry = [&](const char *kind, QJsonObject properties, QJsonArray needed) {
        auto data = optionalObject(properties, required(needed));
        data["minProperties"] = 1;
        return QJsonObject{{"properties", QJsonObject{{"kind", QJsonObject{{"const", kind}}},
                                                      {"data", data}}}};
    };
    return {
        entry("text",
              {{"frame", itemFrame()},
               {"text", text(1, 4096)},
               {"font", font()},
               {"align", choice({"left", "center", "right"})}},
              {"frame", "text"}),
        entry("label",
              {{"text", text(1, 4096)},
               {"font", font()},
               {"textAt", object({{"xMm", range(0, 2000)}, {"yMm", range(0, 2000)}})},
               {"leader", QJsonObject{{"type", "boolean"}}},
               {"target", labelTarget}},
              {"text", "textAt", "target"}),
        entry("dimension", dimension, {"view", "anchors"}),
        entry("line",
              {{"from", paperPoint()},
               {"to", paperPoint()},
               {"strokeMm", stroke()},
               {"dash", choice({"solid", "dashed"})}},
              {"from", "to"}),
        entry("rectangle",
              {{"frame", itemFrame()}, {"strokeMm", stroke()}, {"dash", choice({"solid", "dashed"})}},
              {"frame"}),
        entry("titleBlock",
              {{"frame", itemFrame()},
               {"font", font()},
               {"fields", optionalObject(fields, {})}},
              {"frame"})};
}
QJsonObject drawingItemKindSchema() {
    return choice({"text", "label", "dimension", "line", "rectangle", "titleBlock"});
}
} // namespace sketchy
