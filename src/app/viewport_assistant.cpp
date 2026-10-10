#include "app/unit_display.hpp"
#include "app/viewport.hpp"
#include "core/entity_measure.hpp"
#include <QPainter>
namespace sketchy {
void Viewport::setAssistantPreview(std::shared_ptr<const Document::PreparedEdit> edit) {
    if (edit && !doc_.canApply(*edit))
        throw std::runtime_error("Assistant preview no longer matches this model");
    assistantPreview_ = std::move(edit);
    assistantFocus_ = 0;
    assistantPreviewLabel_.clear();
    setAccessibleDescription({});
    assistantPreviewDirty_ = true;
    assistantPreviewPresentation_ = presentationRevision_;
    assistantTriangles_.clear();
    assistantLines_.clear();
    try {
        rebuildAssistantPreview();
    } catch (...) {
        assistantPreview_.reset();
        assistantTriangles_.clear();
        assistantLines_.clear();
        update();
        throw;
    }
    update();
}
bool Viewport::hasAssistantPreview() const {
    return assistantPreview_ && doc_.canApply(*assistantPreview_);
}
void Viewport::setAssistantPreviewFocus(Id body) {
    if (body &&
        (!hasAssistantPreview() ||
         (!doc_.bodies().contains(body) && !assistantPreview_->snapshot().bodies().contains(body))))
        throw std::runtime_error("Preview entity unavailable");
    assistantFocus_ = body;
    assistantPreviewLabel_.clear();
    setAccessibleDescription({});
    update();
    if (!body)
        return;
    const auto &after = assistantPreview_->snapshot();
    auto bounds = [&](const Document &source) -> std::optional<EntityBounds> {
        if (!source.bodies().contains(assistantFocus_))
            return {};
        return measureEntity(source, {assistantFocus_, SelectionKind::Body, 0}).world.bounds;
    };
    const auto beforeBounds = bounds(doc_), afterBounds = bounds(after);
    const auto shown = afterBounds ? afterBounds : beforeBounds;
    if (!shown)
        return;
    auto dimensions = [&](const std::optional<EntityBounds> &value) {
        if (!value)
            return QString("absent");
        const auto d = value->dimensions();
        return displayLength(d.x, doc_.displayUnits(), doc_.displayPrecision()) + " × " +
               displayLength(d.y, doc_.displayUnits(), doc_.displayPrecision()) + " × " +
               displayLength(d.z, doc_.displayUnits(), doc_.displayPrecision());
    };
    const auto text = QString("Preview #%1 · world bounds\n%2 → %3")
                          .arg(assistantFocus_)
                          .arg(dimensions(beforeBounds), dimensions(afterBounds));
    assistantPreviewLabel_ = text;
    setAccessibleDescription(text);
    assistantPreviewLabelPoint_ = (shown->low + shown->high) * .5;
}
void Viewport::rebuildAssistantPreview() {
    assistantTriangles_.clear();
    assistantLines_.clear();
    assistantPreviewDirty_ = true;
    assistantPreviewPresentation_ = presentationRevision_;
    if (!hasAssistantPreview()) {
        assistantPreview_.reset();
        assistantFocus_ = 0;
        setAccessibleDescription({});
        return;
    }
    const auto &after = assistantPreview_->snapshot();
    constexpr size_t maximumVertices = 600000;
    auto append = [&](std::vector<Vertex> &vertices, Vec3 point, std::array<float, 3> color) {
        if (assistantTriangles_.size() + assistantLines_.size() >= maximumVertices)
            throw std::runtime_error("Assistant preview exceeds the display geometry limit");
        vertices.push_back(
            {point.x, point.y, point.z, color[0], color[1], color[2]});
    };
    auto geometry = [&](const Document &source, Id id, std::array<float, 3> color, bool faces,
                        const Body *other = nullptr, bool edges = true) {
        const auto &body = *source.bodies().at(id);
        const auto world = source.worldTransform(id);
        if (opacity_.contains(id) && opacity_.at(id) == 0)
            return;
        // Preflight before tessellation to avoid an unbounded display allocation.
        size_t corners{};
        for (const auto &[face, record] : body.surface.faces)
            for (const auto &loop : record.loops) {
                corners += loop.size();
                if (corners > maximumVertices / 6)
                    throw std::runtime_error(
                        "Assistant preview face complexity exceeds display limit");
            }
        if (faces)
            for (const auto &triangle : body.surface.triangles()) {
                if (other && other->surface.faces.contains(triangle.face))
                    continue;
                if (!selection_.showingHidden() &&
                    selection_.hidden(source, {id, SelectionKind::Face, triangle.face}))
                    continue;
                for (auto point : {triangle.a, triangle.b, triangle.c})
                    append(assistantTriangles_, world.point(point), color);
            }
        if (!edges)
            return;
        for (const auto &[edge, record] : body.topology.edges) {
            if (!selection_.showingHidden() &&
                selection_.hidden(source, {id, SelectionKind::Edge, edge}))
                continue;
            append(assistantLines_, world.point(body.surface.vertices.at(record.a)), color);
            append(assistantLines_, world.point(body.surface.vertices.at(record.b)), color);
        }
    };
    for (const auto &[id, before] : doc_.bodies()) {
        if (!after.bodies().contains(id))
            geometry(doc_, id, {.85f, .18f, .20f}, true);
        else if (*before != *after.bodies().at(id) ||
                 doc_.worldTransform(id) != after.worldTransform(id)) {
            geometry(doc_, id, {.85f, .18f, .20f}, true, after.bodies().at(id).get(), false);
            geometry(after, id, {.12f, .64f, .28f}, true, before.get(), false);
            geometry(after, id, {.85f, .48f, .04f}, false);
        }
    }
    for (const auto &[id, body] : after.bodies())
        if (!doc_.bodies().contains(id))
            geometry(after, id, {.12f, .64f, .28f}, true);
}
void Viewport::drawAssistantPreview() {
    if (assistantPreview_ && !hasAssistantPreview()) {
        assistantPreview_.reset();
        assistantTriangles_.clear();
        assistantLines_.clear();
        assistantFocus_ = 0;
        setAccessibleDescription({});
        assistantPreviewDirty_ = true;
    }
    if (assistantPreview_ && assistantPreviewPresentation_ != presentationRevision_) {
        try {
            rebuildAssistantPreview();
        } catch (const std::exception &error) {
            setAssistantPreview({});
            emit message(QString::fromUtf8(error.what()));
        }
    }
    if (assistantPreviewDirty_) {
        upload(assistantTrianglesGpu_, assistantTriangles_);
        upload(assistantLinesGpu_, assistantLines_);
        assistantPreviewDirty_ = false;
    }
    if (!assistantPreview_)
        return;
    shader_->setUniformValue("pixelRatio", float(devicePixelRatioF()));
    shader_->setUniformValue("stipple", 2);
    gl_->glEnable(GL_POLYGON_OFFSET_FILL);
    gl_->glPolygonOffset(-1, -1);
    draw(assistantTrianglesGpu_, GL_TRIANGLES);
    gl_->glDisable(GL_POLYGON_OFFSET_FILL);
    shader_->setUniformValue("stipple", 0);
    draw(assistantLinesGpu_, GL_LINES);
}
void Viewport::paintAssistantPreview(QPainter &painter) {
    if (!hasAssistantPreview() || assistantPreviewLabel_.isEmpty())
        return;
    const auto &text = assistantPreviewLabel_;
    const auto point = project(assistantPreviewLabelPoint_);
    QRectF box =
        painter.boundingRect(QRectF(0, 0, std::min(360, width() - 24), 100), Qt::TextWordWrap, text)
            .adjusted(-6, -6, 6, 6);
    box.moveTopLeft({std::clamp(point.x(), 8.0, std::max(8.0, width() - box.width() - 8)),
                     std::clamp(point.y(), 60.0, std::max(60.0, height() - box.height() - 8))});
    painter.fillRect(box, colors_.surface);
    painter.setPen(colors_.ink);
    painter.drawText(box.adjusted(6, 6, -6, -6), Qt::TextWordWrap, text);
}
} // namespace sketchy
