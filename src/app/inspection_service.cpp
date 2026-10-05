#include "app/inspection_service.hpp"
#include "app/viewport.hpp"
#include "app/window.hpp"
#include "automation/inspection_validation.hpp"
#include <QBuffer>
#include <QCryptographicHash>
#include <QJsonDocument>
#include <QThread>
#include <numbers>
namespace sketchy {
namespace {
constexpr int capturePngBytes = 3 * 1024 * 1024;
constexpr int captureResponseBytes = 4 * 1024 * 1024 + 64 * 1024;
constexpr double sourcePixelLimit = 16 * 1024 * 1024;
[[noreturn]] void fail(const char *code, const char *message) {
    throw InspectionError(code, message);
}
QJsonObject viewSchema(QString name) {
    QJsonObject properties{
        {"apiVersion", QJsonObject{{"const", 1}}},
        {"documentId", QJsonObject{{"type", "string"}, {"maxLength", 128}}},
        {"expectedRevision",
         QJsonObject{{"type", "string"}, {"maxLength", 20}, {"pattern", "^(0|[1-9][0-9]*)$"}}},
        {"query", QJsonObject{{"const", name}}}};
    if (name == "view.capture")
        for (const auto *key : {"maxWidth", "maxHeight"})
            properties[key] = QJsonObject{{"type", "integer"}, {"minimum", 64}, {"maximum", 1024}};
    return {{"name", name},
            {"paged", false},
            {"sideEffects", "none"},
            {"requiresRevision", true},
            {"parameters", QJsonObject{{"$schema", "https://json-schema.org/draft/2020-12/schema"},
                                       {"type", "object"},
                                       {"properties", properties},
                                       {"required", QJsonArray{"apiVersion", "documentId",
                                                               "expectedRevision", "query"}},
                                       {"additionalProperties", false}}}};
}
QJsonArray matrix(const std::array<double, 16> &values) {
    QJsonArray result;
    for (auto value : values)
        result.append(value);
    return result;
}
QJsonObject describeView(const Viewport &view) {
    const auto camera = view.inferenceCamera();
    const auto &selection = view.selectionState();
    return {{"projection", view.orthographic() ? "orthographic" : "perspective"},
            {"fieldOfView", view.fieldOfView() * std::numbers::pi / 180.0},
            {"angleUnits", "rad"},
            {"clipFromWorld", matrix(camera.clipFromWorld)},
            {"worldFromClip", matrix(camera.worldFromClip)},
            {"matrixLayout", "column-major 4x4"},
            {"clipConvention", "OpenGL: xyz in [-1,1]; image origin top-left; image Y inverted"},
            {"logicalWidth", view.width()},
            {"logicalHeight", view.height()},
            {"devicePixelRatio", view.devicePixelRatioF()},
            {"selectedCount", int(selection.entities().size())},
            {"activeContext", selection.context() ? QJsonValue(inspectionReference(
                                                        view.document(), selection.context()))
                                                  : QJsonValue()},
            {"showHidden", selection.showingHidden()},
            {"guidesVisible", view.guidesVisible()},
            {"busy", view.inspectionBusy()},
            {"renderOverrides", view.inspectionRenderOverrides()},
            {"rendererReady", view.rendererReady()},
            {"visible", view.isVisible()},
            {"renderedOverlaysIncluded", true}};
}
QByteArray viewStamp(const Viewport &view) {
    QCryptographicHash hash(QCryptographicHash::Sha256);
    hash.addData(QJsonDocument(describeView(view)).toJson(QJsonDocument::Compact));
    const auto &selection = view.selectionState();
    auto add = [&](const SelectionSet &entities) {
        for (auto e : entities)
            hash.addData(QByteArray::number(e.body) + ":" + QByteArray::number(int(e.kind)) + ":" +
                         QByteArray::number(e.entity) + ";");
        hash.addData("|");
    };
    add(selection.entities());
    add(selection.hiddenEntities());
    for (auto body : selection.lockedBodies())
        hash.addData(QByteArray::number(body) + ";");
    return hash.result();
}
} // namespace
QByteArray desktopInspectionStamp(const Viewport &viewport) { return viewStamp(viewport); }
QJsonObject desktopInspectionCapabilities() {
    auto result = inspectionSessionCapabilities();
    auto queries = result["queries"].toArray();
    queries.append(viewSchema("view.describe"));
    queries.append(viewSchema("view.capture"));
    result["queries"] = queries;
    result["transport"] = "In-process native window dispatch on the GUI thread";
    result["viewCapture"] = QJsonObject{{"mimeType", "image/png"},
                                        {"encoding", "base64"},
                                        {"defaultMaximumDimension", 768},
                                        {"maximumDimension", 1024},
                                        {"sourcePixelLimit", sourcePixelLimit},
                                        {"sourceMaximumDimension", 8192},
                                        {"pngByteLimit", capturePngBytes},
                                        {"responseByteLimit", captureResponseBytes},
                                        {"snapshots", false},
                                        {"pendingGestures", false},
                                        {"renderOverrides", false},
                                        {"writesFiles", false}};
    return result;
}
QJsonObject Window::inspect(const QJsonObject &request) { return inspection_->execute(request); }
QJsonObject DesktopInspection::execute(const QJsonObject &request) {
    if (QThread::currentThread() != viewport_.thread())
        fail("WRONG_THREAD", "Desktop inspection must run on the GUI thread");
    const auto &doc = viewport_.document();
    const auto query = request["query"].toString();
    if (query != "view.describe" && query != "view.capture")
        return session_.execute(doc, request, &viewport_.selectionState());
    if (QJsonDocument(request).toJson(QJsonDocument::Compact).size() > inspectionRequestBytes)
        fail("LIMIT_EXCEEDED", "Inspection request exceeds 16 KiB");
    if (request["apiVersion"] != 1)
        fail("UNSUPPORTED_VERSION", "Desktop inspection requires apiVersion 1");
    inspection_detail::validateParameters(request, viewSchema(query)["parameters"].toObject());
    // The common query boundary owns document/session/revision validation.
    auto envelope = request;
    envelope.remove("maxWidth");
    envelope.remove("maxHeight");
    envelope["query"] = "document.describe";
    auto result = inspectDocument(doc, envelope, &viewport_.selectionState());
    result["query"] = query;
    result["data"] = describeView(viewport_);
    if (query == "view.describe")
        return result;
    if (!viewport_.rendererReady() || !viewport_.isVisible() || viewport_.width() <= 0 ||
        viewport_.height() <= 0)
        fail("VIEW_UNAVAILABLE", "A visible initialized viewport is required for capture");
    if (viewport_.inspectionBusy())
        fail("VIEW_BUSY", "Finish or cancel the current gesture before capturing");
    if (viewport_.inspectionRenderOverrides())
        fail("UNSUPPORTED_VIEW",
             "Capture of benchmark, clipping or opacity overrides is unavailable");
    const auto sourceWidth = std::ceil(viewport_.width() * viewport_.devicePixelRatioF());
    const auto sourceHeight = std::ceil(viewport_.height() * viewport_.devicePixelRatioF());
    if (sourceWidth > 8192 || sourceHeight > 8192 || sourceWidth * sourceHeight > sourcePixelLimit)
        fail("CAPTURE_LIMIT", "Source framebuffer exceeds the capture pixel budget");
    const auto modelStamp = doc.saveStamp();
    const auto revision = doc.revision();
    const auto before = viewStamp(viewport_);
    auto image = viewport_.grabFramebuffer();
    if (image.isNull() || !viewport_.rendererReady())
        fail("VIEW_UNAVAILABLE", "Viewport readback failed");
    if (!doc.isCurrentSnapshot(modelStamp) || doc.revision() != revision ||
        before != viewStamp(viewport_))
        fail("VIEW_CHANGED", "Model or viewport changed during capture; retry current state");
    if (image.width() > 8192 || image.height() > 8192 ||
        double(image.width()) * image.height() > sourcePixelLimit)
        fail("CAPTURE_LIMIT", "Framebuffer dimensions exceed the capture pixel budget");
    auto data = result["data"].toObject();
    data["sourceWidth"] = image.width();
    data["sourceHeight"] = image.height();
    const QSize bound(request["maxWidth"].toInt(768), request["maxHeight"].toInt(768));
    if (image.width() > bound.width() || image.height() > bound.height())
        image = image.scaled(bound, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    image = image.convertToFormat(QImage::Format_RGB888);
    image.setDevicePixelRatio(1);
    QByteArray png;
    QBuffer buffer(&png);
    if (!buffer.open(QIODevice::WriteOnly) || !image.save(&buffer, "PNG"))
        fail("VIEW_UNAVAILABLE", "PNG encoding failed");
    if (png.size() > capturePngBytes)
        fail("CAPTURE_LIMIT", "PNG exceeds capture byte budget; request a smaller image");
    data["capturedAt"] = QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs);
    data["width"] = image.width();
    data["height"] = image.height();
    data["mimeType"] = "image/png";
    data["encoding"] = "base64";
    data["sha256"] =
        QString::fromLatin1(QCryptographicHash::hash(png, QCryptographicHash::Sha256).toHex());
    data["image"] = QString::fromLatin1(png.toBase64());
    result["data"] = data;
    if (QJsonDocument(result).toJson(QJsonDocument::Compact).size() > captureResponseBytes)
        fail("CAPTURE_LIMIT", "Capture response exceeds byte budget");
    return result;
}
} // namespace sketchy
