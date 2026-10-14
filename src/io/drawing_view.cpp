#include "io/drawing_view.hpp"
#include "core/material_records.hpp"
#include "core/model.hpp"
#include "core/reference_image.hpp"
#include "core/units.hpp"
#include "io/model_style_io.hpp"
#include <QCryptographicHash>
#include <QJsonDocument>
#include <algorithm>
#include <array>
#include <cmath>
#include <set>
#include <stdexcept>
#include <tuple>
namespace sketchy {
namespace {
void require(bool ok, const char *message) {
    if (!ok)
        throw std::runtime_error(message);
}
// Fixed-point integers keep the serialization identical across builds and
// sanitizer configurations; no floating-point text formatting is involved.
std::int64_t thousandths(double value) {
    require(std::isfinite(value) && std::abs(value) <= 1e15,
            "Drawing fingerprint value exceeds numeric bounds");
    return std::llround(value * 1000);
}
std::int64_t millionths(double value) {
    require(std::isfinite(value) && std::abs(value) <= 1e12,
            "Drawing fingerprint value exceeds numeric bounds");
    return std::llround(value * 1000000);
}
std::string integer(std::int64_t value) { return std::to_string(value); }
std::string sha256(const std::string &bytes) {
    return QCryptographicHash::hash(QByteArray::fromStdString(bytes), QCryptographicHash::Sha256)
        .toHex()
        .toStdString();
}
std::string assetSha(const Document &doc, Id asset) {
    if (!asset)
        return "none";
    const auto it = doc.assets().find(asset);
    if (it == doc.assets().end() || !it->second->payload)
        return "missing";
    const auto &bytes = it->second->payload->bytes();
    return QCryptographicHash::hash(
               QByteArray(reinterpret_cast<const char *>(bytes.data()), qsizetype(bytes.size())),
               QCryptographicHash::Sha256)
        .toHex()
        .toStdString();
}
const char *anchorStateName(AnchorState state) {
    switch (state) {
    case AnchorState::Resolved:
        return "resolved";
    case AnchorState::Missing:
        return "missing";
    case AnchorState::Ambiguous:
        return "ambiguous";
    }
    return "?";
}
bool reachesFrame(const MeasuredAnnotation &annotation, const MeasuredPage &page) {
    auto low = annotation.textPoint, high = annotation.textPoint;
    for (auto p : annotation.anchors) {
        low = {std::min(low.x, p.x), std::min(low.y, p.y), 0};
        high = {std::max(high.x, p.x), std::max(high.y, p.y), 0};
    }
    return high.x >= 0 && high.y >= 0 && low.x <= page.widthMm && low.y <= page.heightMm;
}
} // namespace
Vec3 DrawingViewProjection::worldToSheet(Vec3 world) const {
    const auto p = page.project(world);
    return {p.x + frame.xMm, p.y + frame.yMm, p.z};
}
Vec3 DrawingViewProjection::sheetToWorld(Vec3 sheet) const {
    require(std::isfinite(sheet.x) && std::isfinite(sheet.y) && std::isfinite(sheet.z),
            "Sheet point must be finite");
    const double metresPerMm = page.scaleDenominator / 1000;
    const auto x = (sheet.x - frame.xMm - page.widthMm / 2) * metresPerMm,
               y = (sheet.y - frame.yMm - page.heightMm / 2) * metresPerMm;
    const auto world = page.origin + page.right * x - page.up * y + page.towardEye * sheet.z;
    checkPoint(world);
    return world;
}
DrawingViewProjection drawingViewProjection(const DrawingViewRecord &view,
                                            const RenderCamera &camera) {
    require(camera.orthographic,
            "Perspective drawing views are not to scale and have no measured mapping");
    for (auto v : view.offsetMm)
        require(std::isfinite(v) && std::abs(v) <= drawingViewOffsetLimitMm,
                "Drawing view offset out of bounds");
    const auto transform = renderCameraTransform(camera);
    DrawingViewProjection result;
    result.frame = view.frame;
    auto &page = result.page;
    page.widthMm = view.frame.widthMm;
    page.heightMm = view.frame.heightMm;
    page.marginMm = 0;
    page.scaleDenominator = view.scaleDenominator;
    page.includeHidden = view.hiddenLines;
    page.right = transform.vector({1, 0, 0});
    page.up = transform.vector({0, 1, 0});
    page.towardEye = transform.vector({0, 0, 1});
    const double metresPerMm = view.scaleDenominator / 1000;
    // Paper y runs down, so a positive y offset moves the target down the page.
    page.origin = camera.target - page.right * (view.offsetMm[0] * metresPerMm) +
                  page.up * (view.offsetMm[1] * metresPerMm);
    page.validate();
    return result;
}
std::string drawingFingerprint(const MeasuredDrawing &drawing,
                               const std::string &appearanceDigest) {
    const auto &page = drawing.page;
    std::string text = "sketchyup-drawing-fingerprint-v1\n";
    text += "page " + integer(thousandths(page.widthMm)) + ' ' +
            integer(thousandths(page.heightMm)) + ' ' + integer(thousandths(page.marginMm)) + ' ' +
            integer(thousandths(page.scaleDenominator)) + ' ' +
            (page.includeHidden ? "hidden" : "visible") + '\n';
    text += "units " + std::string(unitCode(drawing.units)) + ' ' +
            std::to_string(drawing.precision) + '\n';
    using Line = std::tuple<std::int64_t, std::int64_t, std::int64_t, std::int64_t, bool>;
    std::vector<Line> lines;
    lines.reserve(drawing.geometry.lines.size());
    for (const auto &line : drawing.geometry.lines) {
        auto a = std::pair{thousandths(line.a.x), thousandths(line.a.y)};
        auto b = std::pair{thousandths(line.b.x), thousandths(line.b.y)};
        if (b < a)
            std::swap(a, b);
        if (a == b)
            continue; // Shorter than the quantum: draws nothing distinguishable.
        lines.emplace_back(a.first, a.second, b.first, b.second, line.hidden);
    }
    std::sort(lines.begin(), lines.end());
    text += "lines " + std::to_string(lines.size()) + '\n';
    for (const auto &[ax, ay, bx, by, hidden] : lines)
        text += integer(ax) + ' ' + integer(ay) + ' ' + integer(bx) + ' ' + integer(by) +
                (hidden ? " h\n" : " v\n");
    std::vector<std::string> annotations;
    for (const auto &annotation : drawing.annotations) {
        if (!reachesFrame(annotation, page))
            continue;
        const auto &record = annotation.record;
        const auto &measured = annotation.measurement;
        std::string item = record.kind == AnnotationKind::Distance ? "distance" : "label";
        item += ' ' + std::string(anchorStateName(measured.state));
        item += ' ' + (measured.distance ? integer(millionths(*measured.distance)) : "-");
        item += ' ' + std::string(record.leader ? "leader" : "plain");
        item += ' ' + integer(thousandths(record.textSize));
        for (auto c : record.color)
            item += ' ' + integer(millionths(c));
        item += " text " + integer(thousandths(annotation.textPoint.x)) + ' ' +
                integer(thousandths(annotation.textPoint.y));
        for (size_t i = 0; i < annotation.anchors.size(); ++i)
            item += " anchor " + integer(thousandths(annotation.anchors[i].x)) + ' ' +
                    integer(thousandths(annotation.anchors[i].y)) + ' ' +
                    anchorStateName(measured.anchors.at(i).state);
        // Length-prefixed so arbitrary label bytes cannot imitate the structure.
        item += " label " + std::to_string(record.text.size()) + ':' + record.text;
        annotations.push_back(std::move(item));
    }
    std::sort(annotations.begin(), annotations.end());
    text += "annotations " + std::to_string(annotations.size()) + '\n';
    for (const auto &item : annotations)
        text += item + '\n';
    if (!appearanceDigest.empty())
        text += "appearance " + appearanceDigest + '\n';
    return sha256(text);
}
std::string drawingAppearanceDigest(const RenderSnapshot &snapshot, const MeasuredPage &page) {
    const auto &doc = snapshot.document();
    std::string text = "sketchyup-drawing-appearance-v1\nstyle ";
    text +=
        QJsonDocument(encodeModelStyle(doc.style())).toJson(QJsonDocument::Compact).toStdString();
    text += '\n';
    std::set<std::string> surfaces, images;
    for (const auto &[id, body] : doc.bodies()) {
        if (!snapshot.visible(id) || body->kind == BodyKind::Group)
            continue;
        if (body->referenceImage) {
            const auto &image = *body->referenceImage;
            std::string item =
                assetSha(doc, image.asset) + ' ' + integer(millionths(image.opacity));
            for (auto corner : referenceImageCorners(image, doc.worldTransform(id))) {
                const auto p = page.project(corner);
                item += ' ' + integer(thousandths(p.x)) + ' ' + integer(thousandths(p.y));
            }
            images.insert(std::move(item));
            continue;
        }
        for (const auto &[face, record] : body->surface.faces) {
            (void)record;
            if (!snapshot.visible(id, face))
                continue;
            for (bool back : {false, true}) {
                const auto appearance = surfaceAppearance(doc.materials(), *body, face, back);
                std::string item;
                for (auto c : appearance.color)
                    item += integer(millionths(c)) + ' ';
                item += integer(millionths(appearance.opacity)) + ' ';
                item += appearance.material
                            ? assetSha(doc, doc.materials().at(appearance.material)->asset)
                            : std::string("none");
                surfaces.insert(std::move(item));
            }
        }
    }
    for (const auto &item : surfaces)
        text += "surface " + item + '\n';
    for (const auto &item : images)
        text += "image " + item + '\n';
    return sha256(text);
}
const char *drawingViewStateName(DrawingViewState state) {
    switch (state) {
    case DrawingViewState::Current:
        return "current";
    case DrawingViewState::Stale:
        return "stale";
    case DrawingViewState::NotToScale:
        return "notToScale";
    case DrawingViewState::Broken:
        return "broken";
    case DrawingViewState::Failed:
        return "failed";
    }
    return "?";
}
DrawingViewEvaluation evaluateDrawingViewDrawing(const Document &doc, Id id) {
    const auto it = doc.drawingViews().find(id);
    require(it != doc.drawingViews().end(), "Drawing view does not exist");
    const auto &view = *it->second;
    DrawingViewEvaluation result;
    auto &status = result.status;
    status.view = id;
    status.revision = doc.revision();
    const auto scene = view.source.scene;
    try {
        RenderOptions options;
        if (const auto saved = doc.scenes().find(scene);
            saved != doc.scenes().end() && saved->second->snapshot.camera) {
            auto camera = renderSceneCamera(*saved->second->snapshot.camera);
            if (camera.orthographic) {
                result.projection = drawingViewProjection(view, camera);
                // Pan the camera so captureMeasuredDrawing centres the frame on the
                // projection origin; the view direction and scale are unchanged.
                const auto pan = result.projection->page.origin - camera.target;
                camera.target = camera.target + pan;
                camera.position = camera.position + pan;
            }
            options.camera = camera;
        }
        auto capture = RenderSnapshot::captureDrawingView(doc, scene, std::move(options));
        status.warnings = capture.missingReferences;
        if (!capture.broken.empty()) {
            status.state = DrawingViewState::Broken;
            status.reasons = std::move(capture.broken);
            result.projection.reset();
            return result;
        }
        if (capture.perspective) {
            status.state = DrawingViewState::NotToScale;
            status.reasons = {"perspectiveCamera"};
            return result;
        }
        auto drawing = captureMeasuredDrawing(*capture.snapshot, result.projection->page);
        status.fingerprint = drawingFingerprint(
            drawing, view.content == DrawingViewContent::RasterAppearance
                         ? drawingAppearanceDigest(*capture.snapshot, drawing.page)
                         : std::string{});
        result.drawing = std::move(drawing);
    } catch (const ValidationOracleMismatch &) {
        throw;
    } catch (const std::exception &error) {
        status.state = DrawingViewState::Failed;
        status.reasons = {"captureFailed"};
        status.detail = error.what();
        status.fingerprint.clear();
        result.projection.reset();
        result.drawing.reset();
        return result;
    }
    if (!view.accepted) {
        status.state = DrawingViewState::Stale;
        status.reasons = {"notAccepted"};
    } else if (view.accepted->fingerprint != status.fingerprint) {
        status.state = DrawingViewState::Stale;
        status.reasons = {"changedSinceAccepted"};
    } else {
        status.state = DrawingViewState::Current;
    }
    return result;
}
DrawingViewStatus evaluateDrawingView(const Document &doc, Id view) {
    return evaluateDrawingViewDrawing(doc, view).status;
}
const DrawingViewStatus *DrawingViewStatusCache::find(const std::string &identity, Id view,
                                                      std::uint64_t revision) const {
    const auto it = entries_.find({identity, view});
    return it != entries_.end() && it->second.revision == revision ? &it->second : nullptr;
}
void DrawingViewStatusCache::store(const std::string &identity, DrawingViewStatus status) {
    auto &entry = entries_[{identity, status.view}];
    // Never let a late result from an older revision replace a newer one.
    if (entry.view && entry.revision > status.revision)
        return;
    entry = std::move(status);
}
DrawingViewStatus DrawingViewStatusCache::status(const Document &doc, Id view) {
    if (const auto *cached = find(doc.identity(), view, doc.revision()))
        return *cached;
    ++evaluations_;
    auto result = evaluateDrawingView(doc, view);
    store(doc.identity(), result);
    return result;
}
void DrawingViewStatusCache::prune(const Document &doc) {
    for (auto it = entries_.begin(); it != entries_.end();)
        if (it->first.first == doc.identity() && !doc.drawingViews().contains(it->first.second))
            it = entries_.erase(it);
        else
            ++it;
}
} // namespace sketchy
