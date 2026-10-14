#include "io/drawing_items_io.hpp"
#include "io/annotations_io.hpp"
#include <QJsonObject>
#include <QStringList>
#include <cmath>
#include <stdexcept>
namespace sketchy {
namespace {
void require(bool condition, const char *message) {
    if (!condition)
        throw std::runtime_error(message);
}
QJsonObject object(const QJsonValue &value, const QStringList &fields) {
    require(value.isObject(), "Drawing item record requires an object");
    const auto result = value.toObject();
    require(result.size() == fields.size(), "Drawing item record has missing or unknown fields");
    for (const auto &field : fields)
        require(result.contains(field), "Missing drawing item field");
    return result;
}
double number(const QJsonValue &value) {
    require(value.isDouble() && std::isfinite(value.toDouble()),
            "Drawing item records require finite numeric values");
    return value.toDouble();
}
Id id(const QJsonValue &value) {
    bool ok{};
    const auto text = value.toString();
    const auto result = text.toULongLong(&ok);
    require(value.isString() && ok && result && QString::number(result) == text,
            "Drawing item identity requires a canonical nonzero decimal string");
    return result;
}
std::string text(const QJsonValue &value) {
    require(value.isString(), "Drawing item text requires a string");
    return value.toString().toStdString();
}
QString string(const std::string &value) { return QString::fromStdString(value); }

QJsonArray encodePoint(const DrawingPointMm &point) { return {point[0], point[1]}; }
DrawingPointMm decodePoint(const QJsonValue &value) {
    require(value.isArray() && value.toArray().size() == 2,
            "Drawing item point requires two numbers");
    const auto point = value.toArray();
    return {number(point[0]), number(point[1])};
}
QJsonObject encodeFrame(const DrawingItemFrame &frame) {
    return {{"xMm", frame.xMm},
            {"yMm", frame.yMm},
            {"widthMm", frame.widthMm},
            {"heightMm", frame.heightMm}};
}
DrawingItemFrame decodeFrame(const QJsonValue &value) {
    const auto frame = object(value, {"xMm", "yMm", "widthMm", "heightMm"});
    return {number(frame["xMm"]), number(frame["yMm"]), number(frame["widthMm"]),
            number(frame["heightMm"])};
}
QJsonObject encodeFont(const DrawingFont &font) {
    return {{"family", string(font.family)}, {"sizePt", font.sizePt}};
}
DrawingFont decodeFont(const QJsonValue &value) {
    const auto font = object(value, {"family", "sizePt"});
    return {text(font["family"]), number(font["sizePt"])};
}
// Enumerations are stored as their documented lowerCamel codes.
template <class Enum, size_t N>
QString code(Enum value, const std::array<std::pair<Enum, const char *>, N> &codes) {
    for (const auto &[candidate, name] : codes)
        if (candidate == value)
            return name;
    throw std::runtime_error("Unknown drawing item enumeration value");
}
template <class Enum, size_t N>
Enum enumValue(const QJsonValue &json, const std::array<std::pair<Enum, const char *>, N> &codes,
           const char *message) {
    for (const auto &[candidate, name] : codes)
        if (json.isString() && json.toString() == QLatin1String(name))
            return candidate;
    throw std::runtime_error(message);
}
constexpr std::array<std::pair<DrawingTextAlign, const char *>, 3> alignCodes{
    {{DrawingTextAlign::Left, "left"},
     {DrawingTextAlign::Center, "center"},
     {DrawingTextAlign::Right, "right"}}};
constexpr std::array<std::pair<DrawingStrokeDash, const char *>, 2> dashCodes{
    {{DrawingStrokeDash::Solid, "solid"}, {DrawingStrokeDash::Dashed, "dashed"}}};
constexpr std::array<std::pair<DrawingDimensionOrientation, const char *>, 3> orientationCodes{
    {{DrawingDimensionOrientation::Aligned, "aligned"},
     {DrawingDimensionOrientation::Horizontal, "horizontal"},
     {DrawingDimensionOrientation::Vertical, "vertical"}}};
constexpr std::array<std::pair<DrawingItemKind, const char *>, 6> kindCodes{
    {{DrawingItemKind::Text, "text"},
     {DrawingItemKind::Label, "label"},
     {DrawingItemKind::Dimension, "dimension"},
     {DrawingItemKind::Line, "line"},
     {DrawingItemKind::Rectangle, "rectangle"},
     {DrawingItemKind::TitleBlock, "titleBlock"}}};

struct DataEncoder {
    QJsonObject operator()(const DrawingTextItem &item) const {
        return {{"frame", encodeFrame(item.frame)},
                {"text", string(item.text)},
                {"font", encodeFont(item.font)},
                {"align", code(item.align, alignCodes)}};
    }
    QJsonObject operator()(const DrawingLabelItem &item) const {
        const auto &target = item.target;
        QJsonObject encodedTarget;
        switch (target.kind) {
        case DrawingLabelTargetKind::Paper:
            encodedTarget = {
                {"kind", "paper"}, {"xMm", target.pointMm[0]}, {"yMm", target.pointMm[1]}};
            break;
        case DrawingLabelTargetKind::View:
            encodedTarget = {{"kind", "view"},
                             {"view", QString::number(target.view)},
                             {"anchor", encodeAnnotationAnchor(target.anchor)},
                             {"lastMm", encodePoint(target.lastMm)}};
            break;
        default:
            throw std::runtime_error("Unknown drawing label target kind");
        }
        return {{"text", string(item.text)},
                {"font", encodeFont(item.font)},
                {"textAt", QJsonObject{{"xMm", item.textAt[0]}, {"yMm", item.textAt[1]}}},
                {"leader", item.leader},
                {"target", encodedTarget}};
    }
    QJsonObject operator()(const DrawingDimensionItem &item) const {
        return {{"view", QString::number(item.view)},
                {"anchors", QJsonArray{encodeAnnotationAnchor(item.anchors[0]),
                                       encodeAnnotationAnchor(item.anchors[1])}},
                {"orientation", code(item.orientation, orientationCodes)},
                {"offsetMm", item.offsetMm},
                {"prefix", string(item.prefix)},
                {"font", encodeFont(item.font)},
                {"lastMm", QJsonArray{encodePoint(item.lastMm[0]), encodePoint(item.lastMm[1])}}};
    }
    QJsonObject operator()(const DrawingLineItem &item) const {
        return {{"from", encodePoint(item.from)},
                {"to", encodePoint(item.to)},
                {"strokeMm", item.strokeMm},
                {"dash", code(item.dash, dashCodes)}};
    }
    QJsonObject operator()(const DrawingRectangleItem &item) const {
        return {{"frame", encodeFrame(item.frame)},
                {"strokeMm", item.strokeMm},
                {"dash", code(item.dash, dashCodes)}};
    }
    QJsonObject operator()(const DrawingTitleBlockItem &item) const {
        return {{"frame", encodeFrame(item.frame)},
                {"font", encodeFont(item.font)},
                {"fields", QJsonObject{{"project", string(item.fields.project)},
                                       {"drawingTitle", string(item.fields.drawingTitle)},
                                       {"date", string(item.fields.date)},
                                       {"revision", string(item.fields.revision)}}}};
    }
};

DrawingLabelTarget decodeTarget(const QJsonValue &value) {
    require(value.isObject(), "Drawing label target requires an object");
    DrawingLabelTarget target;
    const QJsonValue kind = value.toObject().value("kind");
    if (kind == "paper") {
        const auto o = object(value, {"kind", "xMm", "yMm"});
        target.kind = DrawingLabelTargetKind::Paper;
        target.pointMm = {number(o["xMm"]), number(o["yMm"])};
    } else if (kind == "view") {
        const auto o = object(value, {"kind", "view", "anchor", "lastMm"});
        target.kind = DrawingLabelTargetKind::View;
        target.view = id(o["view"]);
        target.anchor = decodeAnnotationAnchor(o["anchor"]);
        target.lastMm = decodePoint(o["lastMm"]);
    } else
        throw std::runtime_error("Unknown drawing label target kind");
    return target;
}
DrawingItemData decodeData(DrawingItemKind kind, const QJsonValue &value) {
    switch (kind) {
    case DrawingItemKind::Text: {
        const auto o = object(value, {"frame", "text", "font", "align"});
        return DrawingTextItem{decodeFrame(o["frame"]), text(o["text"]), decodeFont(o["font"]),
                               enumValue(o["align"], alignCodes,
                                                "Unknown drawing text alignment")};
    }
    case DrawingItemKind::Label: {
        const auto o = object(value, {"text", "font", "textAt", "leader", "target"});
        const auto at = object(o["textAt"], {"xMm", "yMm"});
        require(o["leader"].isBool(), "Drawing label leader flag requires a boolean");
        return DrawingLabelItem{text(o["text"]), decodeFont(o["font"]),
                                {number(at["xMm"]), number(at["yMm"])}, o["leader"].toBool(),
                                decodeTarget(o["target"])};
    }
    case DrawingItemKind::Dimension: {
        const auto o = object(value, {"view", "anchors", "orientation", "offsetMm", "prefix",
                                      "font", "lastMm"});
        require(o["anchors"].isArray() && o["anchors"].toArray().size() == 2,
                "Drawing dimension requires two anchors");
        require(o["lastMm"].isArray() && o["lastMm"].toArray().size() == 2,
                "Drawing dimension requires two last positions");
        const auto anchors = o["anchors"].toArray(), last = o["lastMm"].toArray();
        return DrawingDimensionItem{
            id(o["view"]),
            {decodeAnnotationAnchor(anchors[0]), decodeAnnotationAnchor(anchors[1])},
            enumValue(o["orientation"], orientationCodes,
                             "Unknown drawing dimension orientation"),
            number(o["offsetMm"]),
            text(o["prefix"]),
            decodeFont(o["font"]),
            {decodePoint(last[0]), decodePoint(last[1])}};
    }
    case DrawingItemKind::Line: {
        const auto o = object(value, {"from", "to", "strokeMm", "dash"});
        return DrawingLineItem{decodePoint(o["from"]), decodePoint(o["to"]), number(o["strokeMm"]),
                               enumValue(o["dash"], dashCodes,
                                                "Unknown drawing item dash style")};
    }
    case DrawingItemKind::Rectangle: {
        const auto o = object(value, {"frame", "strokeMm", "dash"});
        return DrawingRectangleItem{decodeFrame(o["frame"]), number(o["strokeMm"]),
                                    enumValue(o["dash"], dashCodes,
                                                     "Unknown drawing item dash style")};
    }
    case DrawingItemKind::TitleBlock: {
        const auto o = object(value, {"frame", "font", "fields"});
        const auto fields = object(o["fields"], {"project", "drawingTitle", "date", "revision"});
        return DrawingTitleBlockItem{decodeFrame(o["frame"]), decodeFont(o["font"]),
                                     {text(fields["project"]), text(fields["drawingTitle"]),
                                      text(fields["date"]), text(fields["revision"])}};
    }
    }
    throw std::runtime_error("Unknown drawing item kind");
}
} // namespace
QJsonArray encodeDrawingItems(const DrawingItemRecords &items) {
    QJsonArray result;
    for (const auto &[key, item] : items)
        result.append(QJsonObject{{"id", QString::number(key)},
                                  {"sheet", QString::number(item->sheet)},
                                  {"z", qint64(item->z)},
                                  {"kind", code(item->kind(), kindCodes)},
                                  {"data", std::visit(DataEncoder{}, item->data)}});
    return result;
}
DrawingItemRecords decodeDrawingItems(const QJsonValue &value) {
    require(value.isArray() && value.toArray().size() <= qsizetype(drawingItemCountLimit),
            "Drawing item table exceeds its shape or count limit");
    DrawingItemRecords items;
    for (const auto &entry : value.toArray()) {
        const auto record = object(entry, {"id", "sheet", "z", "kind", "data"});
        const auto z = number(record["z"]);
        require(z >= 0 && z < double(drawingItemsPerSheetLimit) && z == std::floor(z),
                "Invalid drawing item order position");
        const auto kind = enumValue(record["kind"], kindCodes, "Unknown drawing item kind");
        auto item = std::make_shared<DrawingItemRecord>(
            DrawingItemRecord{id(record["id"]), id(record["sheet"]), static_cast<std::uint32_t>(z),
                              decodeData(kind, record["data"])});
        // Canonical tables are written in ascending ID order.
        require(items.empty() || item->id > items.rbegin()->first,
                "Drawing items must be stored once each in ID order");
        items.emplace(item->id, std::move(item));
    }
    return items;
}
} // namespace sketchy
