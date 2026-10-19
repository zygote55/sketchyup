// R095.e1 (ADR 0168 "Decisions > Interaction design", Tools on terrain): terrain commands from
// the desktop, the refusing tools' hover over a terrain tile, and the viewport context menu
// request. The core refusal rule (Document::applyEdit, R095.a) is unchanged; this is only the
// pointer feedback before a click.
#include "app/terrain_actions.hpp"
#include "app/viewport.hpp"
#include <QContextMenuEvent>
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
} // namespace sketchy
