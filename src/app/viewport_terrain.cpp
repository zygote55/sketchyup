// R095.e1 (ADR 0168 "Decisions > Interaction design", Tools on terrain): terrain commands from
// the desktop, the refusing tools' hover over a terrain tile, and the viewport context menu
// request. The core refusal rule (Document::applyEdit, R095.a) is unchanged; this is only the
// pointer feedback before a click.
#include "app/terrain_actions.hpp"
#include "app/unit_display.hpp"
#include "app/viewport.hpp"
#include <QContextMenuEvent>
#include <QPainter>
namespace sketchy {
QJsonObject Viewport::commitTerrainCommand(const QJsonObject &command) {
    cancel();
    // Terrain commands name top-level bodies or model edges; they never run in a component
    // scope's definition frame.
    auto result = commitCommands(QJsonArray{command}, false);
    refresh();
    emit changed();
    return result;
}
QString Viewport::terrainRefusingToolName(Tool tool) {
    // R095.a's refusing tools (push/pull, offset, draw edge and curve, guides into a tile, move
    // of vertices, edges and faces, solid booleans, reverse and orient faces) by their names on
    // the tool rail. Allowed: Select, Paint, the camera tools and Intersect, which may use
    // terrain as a reference (intersect *into* a tile refuses at the click).
    switch (tool) {
    case Tool::Extrude:
        return "Push/pull";
    case Tool::Offset:
        return "Offset";
    case Tool::Sweep:
        return "Follow Me";
    case Tool::Boolean:
        return "Solid tools";
    case Tool::Orientation:
        return "Face orientation";
    case Tool::Line:
        return "Line";
    case Tool::Rectangle:
        return "Rectangle";
    case Tool::RotatedRectangle:
        return "Rotated rectangle";
    case Tool::Circle:
        return "Circle";
    case Tool::Polygon:
        return "Regular polygon";
    case Tool::Freehand:
        return "Freehand";
    case Tool::CenterArc:
        return "Center arc";
    case Tool::TwoPointArc:
        return "Two-point arc";
    case Tool::ThreePointArc:
        return "Three-point arc";
    case Tool::Pie:
        return "Pie";
    case Tool::Tape:
        return "Tape measure";
    case Tool::Protractor:
        return "Protractor";
    case Tool::Move:
        return "Move";
    case Tool::Rotate:
        return "Rotate";
    case Tool::Scale:
        return "Scale";
    default:
        return {};
    }
}
void Viewport::updateTerrainHover(QPointF point) {
    Id terrain = 0;
    const auto name = terrainRefusingToolName(tool_);
    if (!name.isEmpty() && !session_.active() && !dragging_ &&
        doc_.materializedTotals().terrainTriangles) {
        const auto [body, face] = pick(point);
        const auto found = body ? doc_.bodies().find(body) : doc_.bodies().end();
        if (found != doc_.bodies().end() && found->second->terrainTile) {
            // Face and solid tools always act on the face under the pointer. Drawing, guide and
            // transform tools act on a tile only inside the open terrain: from outside, a
            // click on a terrain draws in the active context or moves the whole terrain,
            // which is allowed.
            const bool faceTool = tool_ == Tool::Extrude || tool_ == Tool::Offset ||
                                  tool_ == Tool::Sweep || tool_ == Tool::Boolean ||
                                  tool_ == Tool::Orientation;
            if (faceTool || selection_.inContext(doc_, body))
                terrain = found->second->parent;
        }
    }
    if (terrain == terrainRefused_)
        return;
    terrainRefused_ = terrain;
    setCursor(terrain              ? Qt::ForbiddenCursor
              : tool_ == Tool::Select ? Qt::ArrowCursor
                                      : Qt::CrossCursor);
    emit terrainToolRefused(terrain, terrain ? terrainToolRefusal(name) : QString());
}
void Viewport::clearTerrainHover() {
    if (!terrainRefused_)
        return;
    terrainRefused_ = 0;
    setCursor(tool_ == Tool::Select ? Qt::ArrowCursor : Qt::CrossCursor);
    emit terrainToolRefused(0, {});
}
void Viewport::contextMenuEvent(QContextMenuEvent *event) {
    event->accept();
    // A right press starts a pan; the menu opens on a release without a drag (mouseRelease).
    if (event->reason() == QContextMenuEvent::Mouse)
        return;
    const auto local = mapFromGlobal(QCursor::pos());
    emit contextMenuRequested(rect().contains(local) ? local : rect().center());
}

// ---- R095.e2: terrain display and the site origin marker -----------------------------------
// (ADR 0168 "Decisions > Interaction design", Appearance and Site.)
const Viewport::TerrainView *Viewport::terrainViewOfTile(Id tile) const {
    const auto body = doc_.bodies().find(tile);
    if (body == doc_.bodies().end() || !body->second->terrainTile)
        return nullptr;
    const auto view = terrainViews_.find(body->second->parent);
    return view == terrainViews_.end() ? nullptr : view->second.get();
}
bool Viewport::terrainEdgeDrawn(Id tile, Id edge) const {
    const auto *view = terrainViewOfTile(tile);
    if (!view || !view->surface)
        return true;
    const auto surface = view->surface->tile(tile);
    return surface && surface->outerEdge(edge);
}
std::shared_ptr<const TerrainContours> Viewport::terrainContoursShown(Id terrain) const {
    const auto view = terrainViews_.find(terrain);
    return view == terrainViews_.end() || view->second->hidden ? nullptr : view->second->contours;
}
std::shared_ptr<const TerrainContours> Viewport::terrainContours(Id terrain) {
    if (!isTerrain(doc_, terrain) || terrainTiles(doc_, terrain).empty())
        return nullptr;
    return contourCache_.get(doc_, terrain);
}
void Viewport::syncTerrainViews() {
    stats_.contourSegments = 0;
    std::erase_if(terrainViews_, [&](const auto &item) { return !isTerrain(doc_, item.first); });
    contourCache_.prune(doc_);
    surfaceCache_.prune(doc_);
    if (terrainViews_.empty() && !doc_.materializedTotals().terrainTriangles &&
        std::none_of(doc_.bodies().begin(), doc_.bodies().end(),
                     [](const auto &item) { return item.second->kind == BodyKind::Terrain; }))
        return;
    std::map<Id, std::vector<BodyPtr>> tiles;
    for (const auto &[id, body] : doc_.bodies())
        if (body->terrainTile)
            tiles[body->parent].push_back(body);
    for (const auto &[id, body] : doc_.bodies()) {
        if (body->kind != BodyKind::Terrain || !body->terrain)
            continue;
        auto &view = terrainViews_[id];
        if (!view)
            view = std::make_unique<TerrainView>();
        const auto &list = tiles[id];
        if (!view->surface || view->tiles != list) {
            // R096.a: per-tile surfaces; after an edit only the rebuilt tiles and their seam
            // ring get new ones.
            view->surface = list.empty() ? std::make_shared<const TerrainTileSurfaces>()
                                         : surfaceCache_.get(doc_, id);
            view->tiles = list;
            ++stats_.terrainSurfaces;
        }
        view->record = body;
        view->hidden = selection_.hidden(doc_, {id, SelectionKind::Body, 0}) ||
                       (opacity_.contains(id) && opacity_.at(id) == 0);
        std::shared_ptr<const TerrainContours> contours;
        if (body->terrain->display.contours && !list.empty())
            contours = contourCache_.get(doc_, id);
        const auto world = doc_.worldTransform(id);
        if (contours != view->contours || world != view->world || colors_.dark != view->dark ||
            (contours && view->linesGpu.origin != renderOrigin_) ||
            !view->linesGpu.buffer.isCreated()) {
            // World-space lines: ordinary and index levels in two batches, so index lines can
            // be drawn heavier (screen offsets work on drivers limited to 1 px GL lines).
            std::vector<Vertex> lines, index;
            const std::array<float, 3> color =
                colors_.dark ? std::array<float, 3>{.80f, .68f, .50f}
                             : std::array<float, 3>{.42f, .31f, .20f};
            if (contours)
                for (const auto &segment : contours->segments) {
                    auto &target = segment.index ? index : lines;
                    for (const auto point : {segment.a, segment.b}) {
                        const auto p = world.point(point);
                        target.push_back(Vertex{p.x, p.y, p.z, color[0], color[1], color[2]});
                    }
                }
            upload(view->linesGpu, lines);
            upload(view->indexGpu, index);
            view->contours = contours;
            view->world = world;
            view->dark = colors_.dark;
        }
        if (contours && !view->hidden)
            stats_.contourSegments += contours->segments.size();
    }
}
void Viewport::drawTerrainContours() {
    if (terrainViews_.empty())
        return;
    // Contours lie on the surface; surfaces are drawn with a polygon offset, so the lines pass
    // the depth test where the surface is visible and are hidden behind nearer ground.
    gl_->glDepthMask(GL_FALSE);
    for (auto &[id, view] : terrainViews_) {
        if (view->hidden || !view->contours)
            continue;
        draw(view->linesGpu, GL_LINES);
        // Index lines: the line and four one-pixel offsets.
        for (const auto offset :
             {QPointF{}, QPointF(1, 0), QPointF(-1, 0), QPointF(0, 1), QPointF(0, -1)}) {
            shader_->setUniformValue("pixelOffset", QVector2D(float(2 * offset.x() / width()),
                                                              float(2 * offset.y() / height())));
            draw(view->indexGpu, GL_LINES);
        }
        shader_->setUniformValue("pixelOffset", QVector2D{});
    }
    gl_->glDepthMask(GL_TRUE);
}
void Viewport::setSiteOriginVisible(bool visible) {
    if (siteOriginVisible_ == visible)
        return;
    siteOriginVisible_ = visible;
    update();
}
void Viewport::setSiteSettingsOpen(bool open) {
    if (siteSettingsOpen_ == open)
        return;
    siteSettingsOpen_ = open;
    update();
}
bool Viewport::siteOriginMarkerShown() const {
    return doc_.site() &&
           (siteOriginVisible_ || siteSettingsOpen_ || selectedTerrain(doc_, selection_));
}
QString Viewport::siteOriginMarkerLabel() const {
    if (!siteOriginMarkerShown())
        return {};
    const auto &site = *doc_.site();
    const auto units = doc_.displayUnits();
    const auto precision = doc_.displayPrecision();
    return QString("Site origin · E %1 · N %2 · Z %3")
        .arg(displayLength(site.easting, units, precision),
             displayLength(site.northing, units, precision),
             displayLength(site.elevation, units, precision));
}
void Viewport::paintSiteOrigin(QPainter &p) {
    if (!siteOriginMarkerShown())
        return;
    // The site origin is model (0, 0, 0): model = site - origin.
    const auto clip = matrix() * QVector4D(float(-renderOrigin_.x), float(-renderOrigin_.y),
                                           float(-renderOrigin_.z), 1);
    if (clip.w() <= 1e-9)
        return; // Behind the camera.
    const auto at = project(Vec3{});
    if (!QRectF(rect()).adjusted(-40, -40, 40, 40).contains(at))
        return;
    p.save();
    p.setRenderHint(QPainter::Antialiasing);
    // A halo in the canvas colour keeps the crosshair legible over any surface.
    for (const auto &[pen, width] : {std::pair{colors_.canvas, 4.0}, std::pair{colors_.accent, 2.0}}) {
        p.setPen(QPen(pen, width));
        p.setBrush(Qt::NoBrush);
        p.drawLine(at + QPointF(-16, 0), at + QPointF(-5, 0));
        p.drawLine(at + QPointF(5, 0), at + QPointF(16, 0));
        p.drawLine(at + QPointF(0, -16), at + QPointF(0, -5));
        p.drawLine(at + QPointF(0, 5), at + QPointF(0, 16));
        p.drawEllipse(at, 5, 5);
    }
    const auto label = siteOriginMarkerLabel();
    const auto metrics = p.fontMetrics();
    const auto extent = metrics.boundingRect(label).adjusted(-6, -3, 6, 3);
    const auto labelWidth = std::min(extent.width(), std::max(1, width() - 24));
    const QRectF box(std::clamp(at.x() + 20, 12.0, double(std::max(12, width() - labelWidth - 12))),
                     std::clamp(at.y() + 20, 40.0, double(std::max(40, height() - 50))),
                     labelWidth, extent.height());
    p.fillRect(box, colors_.surface);
    p.setPen(colors_.ink);
    p.drawText(box, Qt::AlignCenter, metrics.elidedText(label, Qt::ElideRight, labelWidth - 12));
    p.restore();
}
} // namespace sketchy
