#include "app/viewport.hpp"
#include "automation/commands.hpp"
#include <QApplication>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QOpenGLFramebufferObject>
#include <QPainter>
#include <QVector2D>
#include <algorithm>
namespace sketchy {
namespace {
constexpr std::array<float, 3> selectionColor{.36f, .31f, .83f}, hoverColor{.55f, .50f, .94f};
}
bool Viewport::visible(SelectedEntity entity) const {
    return selection_.exists(doc_, entity) &&
           (selection_.showingHidden() || !selection_.hidden(doc_, entity)) &&
           (entity.kind != SelectionKind::Guide || guidesVisible_) &&
           !(cachedDocument_ == doc_.identity() && opacity_.contains(entity.body) &&
             opacity_.at(entity.body) == 0);
}
bool Viewport::selectable(SelectedEntity entity) const {
    return visible(entity) && selection_.selectable(doc_, entity);
}
QString Viewport::selectionSummary() const { return QString::fromStdString(selection_.summary()); }
void Viewport::syncSelection() {
    if (doc_.isCurrentSnapshot(selectionStamp_))
        return;
    const auto oldContext = selection_.context();
    const bool newSession = !doc_.owns(selectionStamp_);
    selection_.sync(doc_);
    if (newSession || oldContext != selection_.context())
        ++presentationRevision_;
    selectionStamp_ = doc_.saveStamp();
    updatePrimarySelection();
    pickDirty_ = overlayDirty_ = true;
    hover_.reset();
    selectionPressed_ = selectingBox_ = false;
}
void Viewport::updatePrimarySelection() {
    selected_ = selectedFace_ = 0;
    size_t faces = 0;
    for (auto e : selection_.entities()) {
        if (selected_ && selected_ != e.body) {
            selected_ = selectedFace_ = 0;
            return;
        }
        selected_ = e.body;
        if (e.kind == SelectionKind::Face) {
            selectedFace_ = e.entity;
            ++faces;
        }
    }
    if (faces != 1)
        selectedFace_ = 0;
}
void Viewport::selectionChanged(bool policy) {
    if (policy) {
        cancel();
        ++presentationRevision_;
        pickDirty_ = true;
    }
    if (hover_ && !selectable(*hover_))
        hover_.reset();
    updatePrimarySelection();
    overlayDirty_ = true;
    refresh();
    setAccessibleDescription(selectionSummary());
    emit selected(selected_, selectedFace_);
    emit message(selectionSummary());
}
void Viewport::selectEntities(const SelectionSet &entities, SelectionMode mode) {
    selection_.sync(doc_);
    SelectionSet eligible;
    for (auto entity : entities)
        if (selectable(entity))
            eligible.insert(entity);
    selection_.apply(doc_, eligible, mode);
    selectionChanged();
}
void Viewport::enterContext(Id context) {
    selection_.enter(doc_, context);
    selectionChanged(true);
}
void Viewport::leaveContext() {
    const auto context = selection_.context();
    enterContext(context && doc_.bodies().contains(context) ? doc_.bodies().at(context)->parent
                                                            : 0);
}
void Viewport::showHiddenGeometry(bool show) {
    selection_.showHidden(doc_, show);
    selectionChanged(true);
}
void Viewport::hideSelection() {
    selection_.hide(doc_, selection_.entities());
    selectionChanged(true);
}
void Viewport::revealHiddenGeometry() {
    selection_.reveal(doc_);
    selectionChanged(true);
}
void Viewport::lockSelection() {
    std::set<Id> bodies;
    for (auto e : selection_.entities())
        bodies.insert(e.body);
    for (auto body : bodies)
        selection_.lock(doc_, body, true);
    selectionChanged(true);
}
void Viewport::unlockContexts() {
    selection_.unlockAll(doc_);
    selectionChanged(true);
}
void Viewport::deleteSelection() {
    if (selection_.entities().empty())
        return;
    cancel();
    selection_.sync(doc_);
    QJsonArray entities;
    for (auto entity : selection_.entities()) {
        if (!selectable(entity))
            continue;
        const char *kind = entity.kind == SelectionKind::Body   ? "context"
                           : entity.kind == SelectionKind::Face ? "face"
                           : entity.kind == SelectionKind::Edge ? "edge"
                                                                : "guide";
        entities.append(QJsonObject{{"body", QString::number(entity.body)},
                                    {"kind", kind},
                                    {"entity", QString::number(entity.entity)}});
    }
    if (entities.empty())
        return;
    executeBatch(doc_, {{"apiVersion", 1},
                        {"documentId", QString::fromStdString(doc_.identity())},
                        {"expectedRevision", QString::number(doc_.revision())},
                        {"commands", QJsonArray{QJsonObject{{"command", "geometry.erase_selection"},
                                                            {"entities", entities}}}}});
    selection_.clear();
    selectionChanged();
    emit changed();
}
void Viewport::selectAll() {
    selection_.sync(doc_);
    const auto ordered = selection_.ordered(doc_);
    selectEntities(SelectionSet(ordered.begin(), ordered.end()));
}
SelectionMode Viewport::selectionMode(Qt::KeyboardModifiers modifiers) const {
    return modifiers.testFlag(Qt::ShiftModifier)     ? SelectionMode::Toggle
           : modifiers.testFlag(Qt::ControlModifier) ? SelectionMode::Add
                                                     : SelectionMode::Replace;
}
void Viewport::rebuildPickGeometry() {
    pickEntities_.clear();
    std::map<SelectedEntity, unsigned> ids;
    std::vector<Vertex> faces, edges;
    auto color = [&](SelectedEntity entity) {
        unsigned id = 0;
        if (selectable(entity)) {
            if (!ids.contains(entity)) {
                if (pickEntities_.size() >= 0xffffff)
                    throw std::runtime_error("Selection ID buffer limit exceeded");
                pickEntities_.push_back(entity);
                ids[entity] = unsigned(pickEntities_.size());
            }
            id = ids.at(entity);
        }
        return std::array<float, 3>{float((id >> 16) & 255) / 255, float((id >> 8) & 255) / 255,
                                    float(id & 255) / 255};
    };
    auto vertex = [](Vec3 point, std::array<float, 3> color) {
        return Vertex{
            float(point.x), float(point.y), float(point.z), color[0], color[1], color[2], 1};
    };
    for (const auto &[id, cache] : bodyCaches_) {
        if (!cache->alpha)
            continue;
        for (const auto &triangle : cache->worldTriangles) {
            const SelectedEntity entity{id, SelectionKind::Face, triangle.face};
            if (!visible(entity))
                continue;
            const auto c = color(entity);
            for (auto point : {triangle.a, triangle.b, triangle.c})
                faces.push_back(vertex(point, c));
        }
        for (const auto &edge : cache->worldEdges) {
            const SelectedEntity entity{id, SelectionKind::Edge, edge.id};
            if (!visible(entity))
                continue;
            const auto c = color(entity);
            edges.push_back(vertex(edge.a, c));
            edges.push_back(vertex(edge.b, c));
        }
    }
    upload(pickFacesGpu_, faces);
    upload(pickEdgesGpu_, edges);
    pickDirty_ = false;
}
Viewport::PickPixels Viewport::selectionPixels(QRectF region) {
    PickPixels result;
    if (!ready_ || !context() || benchmarkTriangles_)
        return result;
    const auto ratio = devicePixelRatioF();
    const QSize pixels(qRound(width() * ratio), qRound(height() * ratio));
    region = region.normalized().intersected(QRectF(rect()));
    if (region.isEmpty())
        return result;
    result.deviceRect =
        QRect(QPoint(int(std::floor(region.left() * ratio)), int(std::floor(region.top() * ratio))),
              QPoint(int(std::ceil(region.right() * ratio)) - 1,
                     int(std::ceil(region.bottom() * ratio)) - 1))
            .intersected(QRect(QPoint{}, pixels));
    if (result.deviceRect.isEmpty())
        return result;
    makeCurrent();
    const bool dither = glIsEnabled(GL_DITHER), srgb = glIsEnabled(GL_FRAMEBUFFER_SRGB);
    struct Restore {
        std::function<void()> call;
        ~Restore() { call(); }
    } restore{[&] {
        glBindFramebuffer(GL_FRAMEBUFFER, defaultFramebufferObject());
        glViewport(0, 0, pixels.width(), pixels.height());
        if (dither)
            glEnable(GL_DITHER);
        if (srgb)
            glEnable(GL_FRAMEBUFFER_SRGB);
        if (shader_)
            shader_->release();
        doneCurrent();
    }};
    syncSelection();
    if (cacheDirty_ || cachedRevision_ != doc_.revision() || cachedDocument_ != doc_.identity())
        rebuild();
    if (pickDirty_)
        rebuildPickGeometry();
    QOpenGLFramebufferObjectFormat format;
    format.setAttachment(QOpenGLFramebufferObject::Depth);
    format.setInternalTextureFormat(GL_RGBA8);
    format.setSamples(0);
    QOpenGLFramebufferObject buffer(result.deviceRect.size(), format);
    if (!buffer.isValid() || !buffer.bind()) {
        emit message("Could not allocate selection framebuffer");
        return result;
    }
    glViewport(0, 0, result.deviceRect.width(), result.deviceRect.height());
    glDisable(GL_BLEND);
    glDisable(GL_DITHER);
    glDisable(GL_FRAMEBUFFER_SRGB);
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LEQUAL);
    glDepthMask(GL_TRUE);
    glClearColor(0, 0, 0, 1);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    QMatrix4x4 crop;
    crop.scale(float(pixels.width()) / result.deviceRect.width(),
               float(pixels.height()) / result.deviceRect.height(), 1);
    crop.translate(
        1 - 2 * (result.deviceRect.left() + result.deviceRect.width() * .5) / pixels.width(),
        2 * (result.deviceRect.top() + result.deviceRect.height() * .5) / pixels.height() - 1, 0);
    shader_->bind();
    shader_->setUniformValue("mvp", crop * matrix());
    shader_->setUniformValue("instanced", 0);
    shader_->setUniformValue("stipple", 0);
    shader_->setUniformValue("pixelOffset", QVector2D{});
    shader_->setUniformValue("clipEnabled", clipPlane_ ? 1 : 0);
    if (clipPlane_) {
        const auto &p = *clipPlane_;
        shader_->setUniformValue("clipPlane", QVector4D(p[0], p[1], p[2], p[3]));
    }
    glEnable(GL_POLYGON_OFFSET_FILL);
    glPolygonOffset(1, 1);
    draw(pickFacesGpu_, GL_TRIANGLES);
    glDisable(GL_POLYGON_OFFSET_FILL);
    glLineWidth(1);
    draw(pickEdgesGpu_, GL_LINES);
    result.image = buffer.toImage().convertToFormat(QImage::Format_RGB32);
    return result;
}
std::optional<SelectedEntity> Viewport::pickEntity(QRgb color) const {
    const auto id = unsigned(qRed(color) << 16 | qGreen(color) << 8 | qBlue(color));
    return id && id <= pickEntities_.size() ? std::optional<SelectedEntity>{pickEntities_[id - 1]}
                                            : std::nullopt;
}
std::optional<SelectedEntity> Viewport::selectionAt(QPointF point) {
    const auto pixels = selectionPixels(QRectF(point - QPointF(7, 7), QSizeF(14, 14)));
    if (pixels.image.isNull())
        return {};
    std::optional<SelectedEntity> edge, face, guide;
    double edgeDistance = 6, guideDistance = 6;
    const auto ratio = devicePixelRatioF();
    for (int y = 0; y < pixels.image.height(); ++y)
        for (int x = 0; x < pixels.image.width(); ++x) {
            const auto entity = pickEntity(pixels.image.pixel(x, y));
            if (!entity || entity->kind != SelectionKind::Edge)
                continue;
            const QPointF at((pixels.deviceRect.left() + x + .5) / ratio,
                             (pixels.deviceRect.top() + y + .5) / ratio);
            const auto distance = std::hypot(at.x() - point.x(), at.y() - point.y());
            if (distance < edgeDistance ||
                (distance == edgeDistance && (!edge || *entity < *edge))) {
                edge = entity;
                edgeDistance = distance;
            }
        }
    if (edge)
        return edge;
    const QPoint center(int(std::floor(point.x() * ratio)) - pixels.deviceRect.left(),
                        int(std::floor(point.y() * ratio)) - pixels.deviceRect.top());
    if (pixels.image.rect().contains(center))
        face = pickEntity(pixels.image.pixel(center));
    const auto camera = inferenceCamera();
    if (guidesVisible_)
        for (const auto &[id, body] : doc_.bodies()) {
            const auto world = doc_.worldTransform(id);
            for (const auto &[gid, record] : body->guides) {
                const SelectedEntity entity{id, SelectionKind::Guide, gid};
                if (!selectable(entity))
                    continue;
                const auto origin = world.point(record.origin);
                std::optional<ScreenPoint> screen;
                if (record.kind == GuideKind::Point) {
                    if (!clipped(origin))
                        screen = camera.project(origin);
                } else if (const auto projected = projectDirection(
                               {DirectionKind::Parallel, origin, world.vector(record.direction)},
                               camera, point.x(), point.y());
                           projected && !clipped(projected->point))
                    screen = camera.project(projected->point);
                if (!screen)
                    continue;
                const auto distance = std::hypot(screen->x - point.x(), screen->y - point.y());
                if (distance < guideDistance) {
                    guide = entity;
                    guideDistance = distance;
                }
            }
        }
    return guide ? guide : face;
}
SelectionSet Viewport::windowSelection(QRectF bounds, bool crossing) {
    bounds = bounds.normalized();
    const auto pixels = selectionPixels(bounds);
    SelectionSet candidates;
    for (int y = 0; y < pixels.image.height(); ++y) {
        const auto *row = reinterpret_cast<const QRgb *>(pixels.image.constScanLine(y));
        for (int x = 0; x < pixels.image.width(); ++x)
            if (const auto entity = pickEntity(row[x]))
                candidates.insert(*entity);
    }
    auto inside = [&](Vec3 point) {
        const auto screen = inferenceCamera().project(point);
        return screen && !clipped(point) && bounds.contains(QPointF(screen->x, screen->y));
    };
    if (!crossing)
        std::erase_if(candidates, [&](auto e) {
            const auto &body = *doc_.bodies().at(e.body);
            const auto world = doc_.worldTransform(e.body);
            if (e.kind == SelectionKind::Edge) {
                const auto &edge = body.topology.edges.at(e.entity);
                return !inside(world.point(body.surface.vertices.at(edge.a))) ||
                       !inside(world.point(body.surface.vertices.at(edge.b)));
            }
            for (const auto &loop : body.surface.faces.at(e.entity).loops)
                for (auto vertex : loop)
                    if (!inside(world.point(body.surface.vertices.at(vertex))))
                        return true;
            return false;
        });
    if (guidesVisible_)
        for (const auto &[id, body] : doc_.bodies()) {
            const auto world = doc_.worldTransform(id);
            for (const auto &[gid, record] : body->guides) {
                const SelectedEntity entity{id, SelectionKind::Guide, gid};
                if (!selectable(entity))
                    continue;
                const auto origin = world.point(record.origin);
                if (record.kind == GuideKind::Point) {
                    if (inside(origin))
                        candidates.insert(entity);
                } else if (crossing) {
                    if (const auto line =
                            guideSegment(guideLine(origin, world.vector(record.direction)))) {
                        const QLineF segment((*line)[0], (*line)[1]);
                        QPointF intersection;
                        bool intersects =
                            bounds.contains(segment.p1()) || bounds.contains(segment.p2());
                        for (const auto &side : {QLineF(bounds.topLeft(), bounds.topRight()),
                                                 QLineF(bounds.topRight(), bounds.bottomRight()),
                                                 QLineF(bounds.bottomRight(), bounds.bottomLeft()),
                                                 QLineF(bounds.bottomLeft(), bounds.topLeft())})
                            intersects |= segment.intersects(side, &intersection) ==
                                          QLineF::BoundedIntersection;
                        if (intersects)
                            candidates.insert(entity);
                    }
                }
            }
        }
    return candidates;
}
void Viewport::rebuildSelectionOverlay() {
    auto build = [&](const SelectionSet &entities, std::array<float, 3> color, GpuBatch &faceBatch,
                     GpuBatch &edgeBatch) {
        std::vector<Vertex> faces, edges;
        auto vertex = [&](Vec3 p) {
            return Vertex{float(p.x), float(p.y), float(p.z), color[0], color[1], color[2], 1};
        };
        auto line = [&](Vec3 a, Vec3 b) {
            edges.push_back(vertex(a));
            edges.push_back(vertex(b));
        };
        std::set<Id> faceBodies;
        for (auto e : entities)
            if (e.kind == SelectionKind::Face && selectable(e))
                faceBodies.insert(e.body);
        for (auto id : faceBodies)
            for (const auto &t : bodyCaches_.at(id)->worldTriangles)
                if (entities.contains({id, SelectionKind::Face, t.face}))
                    for (auto p : {t.a, t.b, t.c})
                        faces.push_back(vertex(p));
        std::map<Id, Bounds> contextBounds;
        for (auto entity : entities)
            if (entity.kind == SelectionKind::Body && selectable(entity))
                contextBounds[entity.body] = {};
        if (!contextBounds.empty())
            for (const auto &[id, child] : doc_.bodies()) {
                Id root = id;
                while (root && !contextBounds.contains(root))
                    root = doc_.bodies().at(root)->parent;
                if (!root)
                    continue;
                const auto b = bodyBounds(id);
                if (!b.valid)
                    continue;
                auto &bounds = contextBounds.at(root);
                if (!bounds.valid)
                    bounds = b;
                else {
                    bounds.minimum = {std::min(bounds.minimum.x, b.minimum.x),
                                      std::min(bounds.minimum.y, b.minimum.y),
                                      std::min(bounds.minimum.z, b.minimum.z)};
                    bounds.maximum = {std::max(bounds.maximum.x, b.maximum.x),
                                      std::max(bounds.maximum.y, b.maximum.y),
                                      std::max(bounds.maximum.z, b.maximum.z)};
                }
            }
        for (auto e : entities) {
            if (!selectable(e) || !bodyCaches_.contains(e.body))
                continue;
            const auto &body = *doc_.bodies().at(e.body);
            const auto world = doc_.worldTransform(e.body);
            if (e.kind == SelectionKind::Face) {
                for (const auto &loop : body.surface.faces.at(e.entity).loops)
                    for (size_t i = 0; i < loop.size(); ++i)
                        line(world.point(body.surface.vertices.at(loop[i])),
                             world.point(body.surface.vertices.at(loop[(i + 1) % loop.size()])));
            } else if (e.kind == SelectionKind::Edge) {
                const auto &edge = body.topology.edges.at(e.entity);
                line(world.point(body.surface.vertices.at(edge.a)),
                     world.point(body.surface.vertices.at(edge.b)));
            } else if (e.kind == SelectionKind::Body) {
                const auto bounds = contextBounds.at(e.body);
                if (!bounds.valid)
                    continue;
                std::array<Vec3, 8> corners;
                for (int i = 0; i < 8; ++i)
                    corners[i] = {i & 1 ? bounds.maximum.x : bounds.minimum.x,
                                  i & 2 ? bounds.maximum.y : bounds.minimum.y,
                                  i & 4 ? bounds.maximum.z : bounds.minimum.z};
                for (int i = 0; i < 8; ++i)
                    for (int bit : {1, 2, 4})
                        if (!(i & bit))
                            line(corners[i], corners[i | bit]);
            }
        }
        upload(faceBatch, faces);
        upload(edgeBatch, edges);
    };
    build(selection_.entities(), selectionColor, selectedFacesGpu_, selectedEdgesGpu_);
    build(hover_ ? SelectionSet{*hover_} : SelectionSet{}, hoverColor, hoverFacesGpu_,
          hoverEdgesGpu_);
    overlayDirty_ = false;
}
void Viewport::drawSelectionOverlay() {
    if (overlayDirty_)
        rebuildSelectionOverlay();
    shader_->setUniformValue("pixelRatio", float(devicePixelRatioF()));
    shader_->setUniformValue("stipple", 1);
    glEnable(GL_POLYGON_OFFSET_FILL);
    glPolygonOffset(-1, -1);
    glDepthMask(GL_FALSE);
    draw(selectedFacesGpu_, GL_TRIANGLES);
    draw(hoverFacesGpu_, GL_TRIANGLES);
    glDisable(GL_POLYGON_OFFSET_FILL);
    shader_->setUniformValue("stipple", 0);
    // Screen-space offsets work even when the driver supports only 1px lines.
    for (auto offset : {QPointF{}, QPointF(1, 0), QPointF(-1, 0), QPointF(0, 1), QPointF(0, -1)}) {
        shader_->setUniformValue("pixelOffset",
                                 QVector2D(2 * offset.x() / width(), 2 * offset.y() / height()));
        draw(selectedEdgesGpu_, GL_LINES);
    }
    shader_->setUniformValue("pixelOffset", QVector2D{});
    draw(hoverEdgesGpu_, GL_LINES);
    glDepthMask(GL_TRUE);
}
void Viewport::paintSelection(QPainter &p) {
    p.save();
    p.setBrush(Qt::NoBrush);
    auto guides = [&](const SelectionSet &entities, QColor color) {
        p.setPen(QPen(color, 2, Qt::DashLine));
        for (auto e : entities)
            if (e.kind == SelectionKind::Guide && selectable(e)) {
                const auto &guide = doc_.bodies().at(e.body)->guides.at(e.entity);
                const auto world = doc_.worldTransform(e.body);
                paintGuide(
                    p, guide.kind == GuideKind::Point
                           ? guidePoint(world.point(guide.origin))
                           : guideLine(world.point(guide.origin), world.vector(guide.direction)));
            }
    };
    guides(selection_.entities(),
           QColor::fromRgbF(selectionColor[0], selectionColor[1], selectionColor[2]));
    if (hover_)
        guides({*hover_}, QColor::fromRgbF(hoverColor[0], hoverColor[1], hoverColor[2]));
    if (selectingBox_) {
        const bool crossing = selectionEnd_.x() < selectionStart_.x();
        p.setPen(QPen(QColor("#6354d4"), 1, crossing ? Qt::DashLine : Qt::SolidLine));
        p.setBrush(QColor(99, 84, 212, 24));
        p.drawRect(QRectF(selectionStart_, selectionEnd_).normalized());
    }
    if (selection_.context()) {
        p.setPen(colors_.ink);
        p.drawText(20, 48,
                   QString("Editing context #%1 · Esc closes one level").arg(selection_.context()));
    }
    if (tool_ == Tool::Select) {
        p.setPen(colors_.muted);
        p.drawText(QRect(20, height() - 66, width() - 40, 38), Qt::TextWordWrap,
                   "Ctrl: add · Shift: toggle · Drag →: inside / ←: crossing · Tab: traverse · "
                   "Double-click: boundaries · Triple-click: connected");
    }
    p.restore();
}
void Viewport::selectionRelease(QPointF point) {
    selectionPressed_ = false;
    if (!doc_.isCurrentSnapshot(selectionGestureStamp_)) {
        selectingBox_ = false;
        update();
        return;
    }
    if (selectingBox_) {
        selectingBox_ = false;
        const auto candidates =
            windowSelection(QRectF(selectionStart_, point), point.x() < selectionStart_.x());
        selection_.apply(doc_, boxBase_, SelectionMode::Replace);
        selectEntities(candidates, selectionMode_);
        clickCount_ = 0;
    } else {
        const auto entity = selectionAt(point);
        const bool third = entity && lastClickEntity_ == entity && clickCount_ == 2 &&
                           clickTimer_.isValid() &&
                           clickTimer_.elapsed() <= QApplication::doubleClickInterval() &&
                           (point - lastClickPosition_).manhattanLength() < 6;
        selectEntities(entity
                           ? (third ? selection_.connected(doc_, *entity) : SelectionSet{*entity})
                           : SelectionSet{},
                       selectionMode_);
        clickCount_ = third ? 3 : 1;
        lastClickEntity_ = entity;
        lastClickPosition_ = point;
        clickTimer_.restart();
    }
    update();
}
void Viewport::mouseDoubleClickEvent(QMouseEvent *event) {
    if (tool_ == Tool::Extrude && event->button() == Qt::LeftButton) {
        try {
            const auto [body, face] = pick(event->position());
            if (!body || !selectable({body, SelectionKind::Face, face}))
                throw std::runtime_error("Choose an editable face to repeat push/pull");
            setSelection(body, face);
            repeatPushPull();
        } catch (const std::exception &error) {
            emit message(error.what());
        }
        event->accept();
        return;
    }
    if (tool_ != Tool::Select || event->button() != Qt::LeftButton) {
        QOpenGLWidget::mouseDoubleClickEvent(event);
        return;
    }
    selectionPressed_ = selectingBox_ = false;
    const auto entity = selectionAt(event->position());
    if (entity) {
        if (entity->kind == SelectionKind::Body)
            enterContext(entity->body);
        else
            selectEntities(selection_.boundary(doc_, *entity), selectionMode(event->modifiers()));
    }
    lastClickEntity_ = entity;
    lastClickPosition_ = event->position();
    clickCount_ = 2;
    clickTimer_.restart();
    event->accept();
}
bool Viewport::selectionKey(QKeyEvent *event) {
    if (tool_ != Tool::Select)
        return false;
    if (event->key() == Qt::Key_Escape) {
        if (!selection_.entities().empty())
            selectEntities({});
        else if (selection_.context())
            leaveContext();
        else {
            selectingBox_ = selectionPressed_ = false;
            update();
        }
        return true;
    }
    if (event->key() == Qt::Key_A && event->modifiers() == Qt::ControlModifier) {
        selectAll();
        return true;
    }
    if (event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter) {
        if (selection_.entities().size() == 1) {
            const auto entity = *selection_.entities().begin();
            if (entity.kind == SelectionKind::Body)
                enterContext(entity.body);
            else
                selectEntities(selection_.boundary(doc_, entity));
        }
        return true;
    }
    if (event->key() != Qt::Key_Tab && event->key() != Qt::Key_Backtab)
        return false;
    const auto visible = windowSelection(QRectF(rect()), true);
    if (visible.empty())
        return true;
    auto current = selection_.entities().empty() ? visible.end()
                                                 : visible.find(*selection_.entities().begin());
    const bool previous =
        event->key() == Qt::Key_Backtab || event->modifiers().testFlag(Qt::ShiftModifier);
    if (previous) {
        if (current == visible.end() || current == visible.begin())
            current = visible.end();
        --current;
    } else {
        if (current != visible.end())
            ++current;
        if (current == visible.end())
            current = visible.begin();
    }
    selectEntities({*current});
    return true;
}
} // namespace sketchy
