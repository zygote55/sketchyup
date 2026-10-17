#include "io/drawing_sheets_io.hpp"
#include "core/model.hpp"
#include "io/native_format.hpp"
#include <QJsonObject>
#include <QStringList>
#include <cmath>
namespace sketchy {
namespace {
QJsonObject object(const QJsonValue &value, const QStringList &fields) {
    if (!value.isObject())
        throw std::runtime_error("Drawing sheet record requires an object");
    const auto result = value.toObject();
    if (result.size() != fields.size())
        throw std::runtime_error("Drawing sheet record has missing or unknown fields");
    for (const auto &field : fields)
        if (!result.contains(field))
            throw std::runtime_error("Missing drawing sheet field");
    return result;
}
double number(const QJsonValue &value) {
    if (!value.isDouble() || !std::isfinite(value.toDouble()))
        throw std::runtime_error("Drawing sheet records require finite numeric values");
    return value.toDouble();
}
std::uint64_t decimal(const QJsonValue &value, bool allowZero) {
    bool ok{};
    const auto text = value.toString();
    const auto result = text.toULongLong(&ok);
    if (!value.isString() || !ok || (!allowZero && !result) || QString::number(result) != text)
        throw std::runtime_error(
            "Drawing sheet identity requires a canonical nonzero decimal string");
    return result;
}
Id id(const QJsonValue &value) { return decimal(value, false); }
std::string name(const QJsonValue &value) {
    if (!value.isString())
        throw std::runtime_error("Drawing sheet name requires text");
    return value.toString().toStdString();
}
QJsonArray array(const QJsonValue &value, qsizetype maximum) {
    if (!value.isArray() || value.toArray().size() > maximum)
        throw std::runtime_error("Drawing sheet table exceeds its shape or count limit");
    return value.toArray();
}
QString contentCode(DrawingViewContent content) {
    switch (content) {
    case DrawingViewContent::TechnicalLines:
        return "technicalLines";
    case DrawingViewContent::RasterAppearance:
        return "rasterAppearance";
    }
    throw std::runtime_error("Unknown drawing view content mode");
}
DrawingViewContent contentValue(const QJsonValue &value) {
    if (value == "technicalLines")
        return DrawingViewContent::TechnicalLines;
    if (value == "rasterAppearance")
        return DrawingViewContent::RasterAppearance;
    throw std::runtime_error("Unknown drawing view content mode");
}
QJsonObject encodeSource(const DrawingViewSource &source) {
    switch (source.kind) {
    case DrawingViewSourceKind::Scene:
        return {{"kind", "scene"}, {"scene", QString::number(source.scene)}};
    }
    throw std::runtime_error("Unknown drawing view source kind");
}
DrawingViewSource decodeSource(const QJsonValue &value) {
    if (!value.isObject() || value.toObject()["kind"] != "scene")
        throw std::runtime_error("Unknown drawing view source kind");
    const auto source = object(value, {"kind", "scene"});
    return {DrawingViewSourceKind::Scene, id(source["scene"])};
}
} // namespace
QJsonObject encodeDrawingDimensionFormat(const DrawingDimensionFormat &format) {
    const char *unit = format.unit == DrawingDimensionUnit::Centimetres  ? "cm"
                       : format.unit == DrawingDimensionUnit::Metres     ? "m"
                       : format.unit == DrawingDimensionUnit::FeetInches ? "ftIn"
                                                                         : "mm";
    return {{"unit", unit}, {"decimals", format.decimals}, {"showUnit", format.showUnit}};
}
DrawingDimensionFormat decodeDrawingDimensionFormat(const QJsonValue &value) {
    const auto o = object(value, {"unit", "decimals", "showUnit"});
    DrawingDimensionFormat format;
    const auto unit = o["unit"].toString();
    if (!o["unit"].isString() || (unit != "mm" && unit != "cm" && unit != "m" && unit != "ftIn"))
        throw std::runtime_error("Unknown drawing dimension unit");
    format.unit = unit == "cm"     ? DrawingDimensionUnit::Centimetres
                  : unit == "m"    ? DrawingDimensionUnit::Metres
                  : unit == "ftIn" ? DrawingDimensionUnit::FeetInches
                                   : DrawingDimensionUnit::Millimetres;
    const auto decimals = number(o["decimals"]);
    if (decimals != std::floor(decimals) || decimals < 0 || decimals > 4)
        throw std::runtime_error("Drawing dimension decimals must be an integer from 0 to 4");
    format.decimals = int(decimals);
    if (!o["showUnit"].isBool())
        throw std::runtime_error("Drawing dimension showUnit requires a boolean");
    format.showUnit = o["showUnit"].toBool();
    return format;
}
QJsonArray encodeDrawingSheets(const DrawingSheetRecords &sheets) {
    QJsonArray result;
    for (const auto &[key, sheet] : sheets)
        result.append(QJsonObject{
            {"id", QString::number(key)},
            {"name", QString::fromStdString(sheet->name)},
            {"position", int(sheet->position)},
            {"number", QString::fromStdString(sheet->number)},
            {"marginMm", sheet->marginMm},
            {"dimensionFormat", encodeDrawingDimensionFormat(sheet->dimensionFormat)},
            {"paper",
             QJsonObject{{"widthMm", sheet->paper.widthMm}, {"heightMm", sheet->paper.heightMm}}}});
    return result;
}
QJsonArray encodeDrawingViews(const DrawingViewRecords &views) {
    QJsonArray result;
    for (const auto &[key, view] : views) {
        const auto &frame = view->frame;
        result.append(QJsonObject{
            {"id", QString::number(key)},
            {"name", QString::fromStdString(view->name)},
            {"sheet", QString::number(view->sheet)},
            {"source", encodeSource(view->source)},
            {"frame", QJsonObject{{"xMm", frame.xMm},
                                  {"yMm", frame.yMm},
                                  {"widthMm", frame.widthMm},
                                  {"heightMm", frame.heightMm}}},
            {"offsetMm", QJsonArray{view->offsetMm[0], view->offsetMm[1]}},
            {"scaleDenominator", view->scaleDenominator},
            {"content", contentCode(view->content)},
            {"hiddenLines", view->hiddenLines},
            {"accepted", view->accepted
                             ? QJsonValue(QJsonObject{
                                   {"revision", QString::number(view->accepted->revision)},
                                   {"fingerprint",
                                    QString::fromStdString(view->accepted->fingerprint)}})
                             : QJsonValue(QJsonValue::Null)}});
    }
    return result;
}
std::pair<DrawingSheetRecords, DrawingViewRecords>
decodeDrawingSheets(const QJsonValue &sheetsValue, Id nextSheet, const QJsonValue &viewsValue,
                    Id nextView) {
    DrawingSheetRecords sheets;
    for (const auto &entry : array(sheetsValue, drawingSheetCountLimit)) {
        const auto record =
            object(entry, {"id", "name", "position", "paper", "number", "marginMm",
                           "dimensionFormat"});
        const auto paper = object(record["paper"], {"widthMm", "heightMm"});
        const auto position = number(record["position"]);
        if (position < 0 || position >= double(drawingSheetCountLimit) ||
            position != std::floor(position))
            throw std::runtime_error("Invalid drawing sheet order position");
        const DrawingPaper size{number(paper["widthMm"]), number(paper["heightMm"])};
        if (!record["number"].isString())
            throw std::runtime_error("Drawing sheet number requires text");
        auto sheet = std::make_shared<DrawingSheetRecord>(
            DrawingSheetRecord{id(record["id"]), name(record["name"]),
                               static_cast<std::uint32_t>(position), size,
                               record["number"].toString().toStdString(),
                               number(record["marginMm"])});
        sheet->dimensionFormat = decodeDrawingDimensionFormat(record["dimensionFormat"]);
        // Canonical tables are written in ascending ID order.
        if (!sheets.empty() && sheet->id <= sheets.rbegin()->first)
            throw std::runtime_error("Drawing sheets must be stored once each in ID order");
        sheets.emplace(sheet->id, std::move(sheet));
    }
    DrawingViewRecords views;
    for (const auto &entry : array(viewsValue, drawingViewCountLimit)) {
        const auto record = object(entry, {"id", "name", "sheet", "source", "frame", "offsetMm",
                                           "scaleDenominator", "content", "hiddenLines",
                                           "accepted"});
        const auto frame = object(record["frame"], {"xMm", "yMm", "widthMm", "heightMm"});
        const auto offset = record["offsetMm"].toArray();
        if (!record["offsetMm"].isArray() || offset.size() != 2)
            throw std::runtime_error("Drawing view offset requires two numbers");
        if (!record["hiddenLines"].isBool())
            throw std::runtime_error("Drawing view hidden-line flag requires a boolean");
        std::optional<DrawingViewAcceptance> accepted;
        if (!record["accepted"].isNull()) {
            const auto stamp = object(record["accepted"], {"revision", "fingerprint"});
            if (!stamp["fingerprint"].isString())
                throw std::runtime_error(
                    "Drawing view acceptance requires a lowercase SHA-256 fingerprint");
            accepted = DrawingViewAcceptance{decimal(stamp["revision"], true),
                                             stamp["fingerprint"].toString().toStdString()};
        }
        auto view = std::make_shared<DrawingViewRecord>(DrawingViewRecord{
            id(record["id"]),
            name(record["name"]),
            id(record["sheet"]),
            decodeSource(record["source"]),
            DrawingFrame{number(frame["xMm"]), number(frame["yMm"]), number(frame["widthMm"]),
                         number(frame["heightMm"])},
            {number(offset[0]), number(offset[1])},
            number(record["scaleDenominator"]),
            contentValue(record["content"]),
            record["hiddenLines"].toBool(),
            std::move(accepted)});
        if (!views.empty() && view->id <= views.rbegin()->first)
            throw std::runtime_error("Drawing views must be stored once each in ID order");
        views.emplace(view->id, std::move(view));
    }
    validateDrawingSheetRecords(sheets, nextSheet, views, nextView);
    return {std::move(sheets), std::move(views)};
}
int nativeDocumentVersionFor(const Document &document) {
    for (const auto &[id, definition] : document.definitions())
        if (definition->parameters)
            return nativeDocumentVersion;
    for (const auto &[root, instance] : document.instances())
        if (!instance->overrides.empty())
            return nativeDocumentVersion;
    return document.drawingSheets().empty() && document.drawingViews().empty() &&
                   document.drawingItems().empty()
               ? nativeSheetFreeDocumentVersion
               : nativeParameterFreeDocumentVersion;
}
} // namespace sketchy
