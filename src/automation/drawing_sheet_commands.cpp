#include "automation/drawing_sheet_commands.hpp"
#include "automation/inspection.hpp"
#include "io/drawing_sheets_io.hpp"
#include "io/drawing_view.hpp"
#include <QJsonArray>
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
// Fields that change what the view draws, so its fingerprint.
bool drawsDifferently(const DrawingViewRecord &a, const DrawingViewRecord &b) {
    return a.source != b.source || a.frame != b.frame || a.offsetMm != b.offsetMm ||
           a.scaleDenominator != b.scaleDenominator || a.content != b.content ||
           a.hiddenLines != b.hiddenLines;
}
// ADR 0163 re-stamp rule (R090.d): a user change to what a *current* view draws
// re-stamps it with the new fingerprint, because the user made that change. A view
// that was stale, broken, failed or never accepted keeps its stamp, so the change
// never silently accepts unreviewed model edits. If the changed view cannot be
// evaluated (broken or failed) the old stamp is kept and the state says why.
void replaceView(Document &doc, Id view, DrawingViewRecord record, std::uint64_t revision) {
    const auto before = requireView(doc, view);
    record.id = view;
    record.accepted = before->accepted;
    const bool restamp = record.accepted && drawsDifferently(*before, record) &&
                         evaluateDrawingView(doc, view).state == DrawingViewState::Current;
    updateDrawingView(doc, view, std::move(record));
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
} // namespace
bool isDrawingSheetCommand(const QString &name) {
    return name == "drawing_sheet.create" || name == "drawing_sheet.rename" ||
           name == "drawing_sheet.resize" || name == "drawing_sheet.reorder" ||
           name == "drawing_sheet.delete" || name == "drawing_view.create" ||
           name == "drawing_view.update" || name == "drawing_view.move" ||
           name == "drawing_view.delete" || name == "drawing_view.accept";
}
void executeDrawingSheetCommand(Document &doc, const QJsonObject &command,
                                std::uint64_t revision) {
    const auto operation = command["command"].toString();
    if (operation == "drawing_sheet.create")
        createDrawingSheet(doc, name(command["name"]), paper(command["paper"]));
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
} // namespace sketchy
