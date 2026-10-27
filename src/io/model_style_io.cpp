#include "io/model_style_io.hpp"
#include <QJsonArray>
#include <QStringList>
namespace sketchy {
namespace {

// A canonical decimal asset ID. Zero is allowed: it is the texture field's "none".
Id canonicalId(const QJsonValue &value) {
    bool ok = false;
    const auto text = value.toString();
    const auto id = text.toULongLong(&ok);
    if (!value.isString() || !ok || QString::number(id) != text)
        throw std::runtime_error("Edge stroke texture requires a canonical decimal asset ID");
    return id;
}

double finite(const QJsonValue &value, const char *what) {
    if (!value.isDouble() || !std::isfinite(value.toDouble()))
        throw std::runtime_error(std::string(what) + " requires a finite number");
    return value.toDouble();
}

// The shortest text of at most `decimals` places: 2.5, 60, 0.25.
std::string trimmed(double value, int decimals) {
    auto text = QString::number(value, 'f', decimals);
    if (text.contains('.')) {
        while (text.endsWith('0'))
            text.chop(1);
        if (text.endsWith('.'))
            text.chop(1);
    }
    return text.toStdString();
}

// The dash names the Styles tab lists (UX_DESIGN §18.3).
std::string dashName(EdgeDash dash) {
    switch (dash) {
    case EdgeDash::Solid:
        return "Solid";
    case EdgeDash::Dashed:
        return "Dashed";
    case EdgeDash::Dotted:
        return "Dotted";
    case EdgeDash::DashDot:
        return "Dash-dot";
    }
    throw std::runtime_error("Unknown edge dash pattern");
}

// Returns the style with its stroke changed, after the same validation the command applies.
ModelStyle withStroke(ModelStyle style, EdgeStroke stroke) {
    style.edgeStroke = stroke;
    style.validate();
    return style;
}

EdgeStroke requiredStroke(const ModelStyle &style) {
    if (!style.edgeStroke)
        throw std::runtime_error("Turn on the edge stroke before setting its fields");
    return *style.edgeStroke;
}

} // namespace

QJsonObject encodeEdgeStroke(const EdgeStroke &stroke) {
    stroke.validate();
    return {{"width", stroke.width},
            {"opacity", stroke.opacity},
            {"dash", QString::fromLatin1(edgeDashCode(stroke.dash).data())},
            {"texture", QString::number(stroke.texture)}};
}

EdgeStroke decodeEdgeStroke(const QJsonValue &value) {
    if (!value.isObject())
        throw std::runtime_error("Edge stroke requires an object");
    const auto object = value.toObject();
    const QStringList fields{"width", "opacity", "dash", "texture"};
    if (object.size() != fields.size())
        throw std::runtime_error("Edge stroke requires exactly its published fields");
    for (const auto &field : fields)
        if (!object.contains(field))
            throw std::runtime_error("Missing edge stroke field");
    if (!object["dash"].isString())
        throw std::runtime_error("Edge stroke requires a named dash pattern");
    EdgeStroke stroke;
    stroke.width = finite(object["width"], "Edge width");
    stroke.opacity = finite(object["opacity"], "Edge opacity");
    stroke.dash = parseEdgeDash(object["dash"].toString().toStdString());
    stroke.texture = canonicalId(object["texture"]);
    stroke.validate();
    return stroke;
}

QJsonObject encodeModelStyle(const ModelStyle &style) {
    style.validate();
    auto rgb = [](const auto &color) { return QJsonArray{color[0], color[1], color[2]}; };
    QJsonObject result{{"mode", QString::fromLatin1(styleModeCode(style.mode).data())},
                       {"background", rgb(style.background)},
                       {"ground", rgb(style.ground)},
                       {"front", rgb(style.front)},
                       {"back", rgb(style.back)},
                       {"edge", rgb(style.edge)},
                       {"groundVisible", style.groundVisible},
                       {"groundHeight", style.groundHeight},
                       {"gridVisible", style.gridVisible},
                       {"axesVisible", style.axesVisible},
                       {"edgesVisible", style.edgesVisible},
                       {"profiles", style.profiles},
                       {"profileWidth", style.profileWidth},
                       {"xrayOpacity", style.xrayOpacity}};
    // Schema 31 (edge-styles-v1): the sub-record is written only when it is set.
    if (style.edgeStroke)
        result["edgeStroke"] = encodeEdgeStroke(*style.edgeStroke);
    return result;
}

ModelStyle decodeModelStyle(const QJsonValue &value, bool edgeStrokes) {
    if (!value.isObject())
        throw std::runtime_error("Model style requires an object");
    const auto object = value.toObject();
    const QStringList fields{"mode",         "background",  "ground",        "front",
                             "back",         "edge",        "groundVisible", "groundHeight",
                             "gridVisible",  "axesVisible", "edgesVisible",  "profiles",
                             "profileWidth", "xrayOpacity"};
    const bool withStroke = object.contains("edgeStroke");
    if (withStroke && !edgeStrokes)
        throw std::runtime_error("Model style edge stroke requires schema 31");
    if (object.size() != fields.size() + (withStroke ? 1 : 0))
        throw std::runtime_error("Model style requires exactly its published fields");
    for (const auto &field : fields)
        if (!object.contains(field))
            throw std::runtime_error("Missing model style field");
    auto number = [](const QJsonValue &v) {
        if (!v.isDouble() || !std::isfinite(v.toDouble()))
            throw std::runtime_error("Model style requires finite numeric values");
        return v.toDouble();
    };
    auto color = [&](const char *name) {
        const auto value = object[name];
        if (!value.isArray() || value.toArray().size() != 3)
            throw std::runtime_error("Model style requires three RGB channels");
        const auto array = value.toArray();
        std::array<float, 3> result;
        for (int i = 0; i < 3; ++i) {
            const auto channel = number(array[i]);
            if (channel < 0 || channel > 1)
                throw std::runtime_error("Style RGB channels must be from zero to one");
            result[size_t(i)] = float(channel);
        }
        return result;
    };
    auto flag = [&](const char *name) {
        if (!object[name].isBool())
            throw std::runtime_error("Model style visibility requires booleans");
        return object[name].toBool();
    };
    if (!object["mode"].isString())
        throw std::runtime_error("Model style requires a named display mode");
    ModelStyle style;
    style.mode = parseStyleMode(object["mode"].toString().toStdString());
    style.background = color("background");
    style.ground = color("ground");
    style.front = color("front");
    style.back = color("back");
    style.edge = color("edge");
    style.groundVisible = flag("groundVisible");
    style.gridVisible = flag("gridVisible");
    style.axesVisible = flag("axesVisible");
    style.edgesVisible = flag("edgesVisible");
    style.profiles = flag("profiles");
    style.groundHeight = number(object["groundHeight"]);
    style.profileWidth = number(object["profileWidth"]);
    style.xrayOpacity = number(object["xrayOpacity"]);
    if (withStroke)
        style.edgeStroke = decodeEdgeStroke(object["edgeStroke"]);
    style.validate();
    return style;
}

StyleEdit turnOnEdgeStroke(const ModelStyle &style) {
    if (style.edgeStroke)
        throw std::runtime_error("The edge stroke is already on");
    EdgeStroke stroke;
    stroke.width = style.profileWidth;
    return {withStroke(style, stroke), "Turn on edge stroke"};
}

StyleEdit turnOffEdgeStroke(const ModelStyle &style) {
    if (!style.edgeStroke)
        throw std::runtime_error("The edge stroke is already off");
    auto next = style;
    next.edgeStroke.reset();
    return {next, "Turn off edge stroke"};
}

StyleEdit setEdgeStrokeWidth(const ModelStyle &style, double width) {
    auto stroke = requiredStroke(style);
    stroke.width = width;
    return {withStroke(style, stroke), "Set edge width " + trimmed(width, 2) + " px"};
}

StyleEdit setEdgeStrokeOpacity(const ModelStyle &style, double opacity) {
    auto stroke = requiredStroke(style);
    stroke.opacity = opacity;
    return {withStroke(style, stroke), "Set edge opacity " + trimmed(opacity * 100, 2) + " %"};
}

StyleEdit setEdgeStrokeDash(const ModelStyle &style, EdgeDash dash) {
    auto stroke = requiredStroke(style);
    stroke.dash = dash;
    return {withStroke(style, stroke), "Set edge dash " + dashName(dash)};
}

StyleEdit setEdgeStrokeTexture(const ModelStyle &style, Id texture, const std::string &name) {
    auto stroke = requiredStroke(style);
    stroke.texture = texture;
    return {withStroke(style, stroke),
            texture ? "Set edge texture “" + name + "”" : std::string("Clear edge texture")};
}

} // namespace sketchy
