#include "io/measured_request.hpp"
namespace sketchy {
namespace {
void require(bool ok, const char *message) {
    if (!ok)
        throw std::runtime_error(message);
}
void fields(const QJsonObject &object, const QStringList &allowed) {
    for (auto it = object.begin(); it != object.end(); ++it)
        require(allowed.contains(it.key()), "Unknown measured export setting");
}
double number(const QJsonObject &object, const QString &key, double fallback) {
    if (!object.contains(key))
        return fallback;
    require(object[key].isDouble() && std::isfinite(object[key].toDouble()),
            "Measured page settings require finite numbers");
    return object[key].toDouble();
}
} // namespace
MeasuredRequest parseMeasuredRequest(const QJsonObject &request) {
    fields(request, {"apiVersion", "mode", "format", "page", "camera"});
    require(request["apiVersion"] == 1, "Measured export requires apiVersion 1");
    require(request["mode"] == "technical-lines",
            "CLI measured export requires explicit technical-lines mode; use the native view for "
            "raster appearance");
    require(request["format"] == "svg" || request["format"] == "pdf",
            "Choose svg or pdf measured format");
    require(request["page"].isObject() && request["camera"].isObject(),
            "Measured export requires page and orthographic camera objects");
    MeasuredRequest result;
    result.format = request["format"] == "svg" ? MeasuredFormat::Svg : MeasuredFormat::Pdf;
    const auto page = request["page"].toObject();
    fields(page, {"widthMm", "heightMm", "marginMm", "scaleDenominator", "includeHidden"});
    require(page.contains("scaleDenominator"), "Choose an explicit print scale denominator");
    result.page.widthMm = number(page, "widthMm", result.page.widthMm);
    result.page.heightMm = number(page, "heightMm", result.page.heightMm);
    result.page.marginMm = number(page, "marginMm", result.page.marginMm);
    result.page.scaleDenominator = number(page, "scaleDenominator", result.page.scaleDenominator);
    if (page.contains("includeHidden")) {
        require(page["includeHidden"].isBool(), "includeHidden must be boolean");
        result.page.includeHidden = page["includeHidden"].toBool();
    }
    result.page.validate();
    result.render = parseRenderOptions({{"apiVersion", 1}, {"camera", request["camera"]}});
    require(result.render.camera && result.render.camera->orthographic,
            "Measured output requires orthographic projection");
    return result;
}
} // namespace sketchy
