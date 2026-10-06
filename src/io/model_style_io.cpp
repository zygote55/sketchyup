#include "io/model_style_io.hpp"
#include <QJsonArray>
#include <QStringList>
namespace sketchy {
QJsonObject encodeModelStyle(const ModelStyle &style) {
    style.validate();
    auto rgb = [](const auto &color) { return QJsonArray{color[0], color[1], color[2]}; };
    return {{"mode", QString::fromLatin1(styleModeCode(style.mode).data())},
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
}
ModelStyle decodeModelStyle(const QJsonValue &value) {
    if (!value.isObject())
        throw std::runtime_error("Model style requires an object");
    const auto object = value.toObject();
    const QStringList fields{"mode",         "background",  "ground",        "front",
                             "back",         "edge",        "groundVisible", "groundHeight",
                             "gridVisible",  "axesVisible", "edgesVisible",  "profiles",
                             "profileWidth", "xrayOpacity"};
    if (object.size() != fields.size())
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
    style.validate();
    return style;
}
} // namespace sketchy
