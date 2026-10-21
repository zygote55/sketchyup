// R096.f2 (UX_DESIGN §15.6): the viewport side of the Terrain edits section: the selected change's
// region outlined in the selection colour with its label, framing it, and the changes a replay
// skipped.
#include "app/terrain_actions.hpp"
#include "app/viewport.hpp"
#include "core/terrain_drape.hpp"
#include "core/terrain_sculpt.hpp"
#include "core/terrain_source_edit.hpp"
#include "core/terrain_stamp.hpp"
#include <QPainter>
#include <QPainterPath>
#include <QPainterPathStroker>
#include <algorithm>
namespace sketchy {
void Viewport::setTerrainEditHighlight(Id terrain, std::uint32_t edit, const QStringList &label) {
    if (editHighlight_.terrain == terrain && editHighlight_.edit == edit &&
        editHighlight_.label == label)
        return;
    editHighlight_ = {};
    editHighlight_.terrain = terrain;
    editHighlight_.edit = edit;
    editHighlight_.label = label;
    buildTerrainEditHighlight();
    update();
}
std::size_t Viewport::terrainEditHighlightPoints() const {
    std::size_t points = 0;
    for (const auto &line : editHighlight_.lines)
        points += line.size();
    return points;
}
// The change's region in model coordinates: a pad's outline and daylight line, a drape's runs, a
// source edit's line, a sculpt's swept brush or region, else its plan box; lines from plan points
// lie on the surface.
void Viewport::buildTerrainEditHighlight() {
    auto &h = editHighlight_;
    h.lines.clear();
    h.document = doc_.identity();
    h.revision = doc_.revision();
    if (!h.edit || !isTerrain(doc_, h.terrain))
        return;
    const auto &record = *doc_.bodies().at(h.terrain)->terrain;
    const auto found = std::find_if(record.edits.begin(), record.edits.end(),
                                    [&](const auto &entry) { return entry->id == h.edit; });
    if (found == record.edits.end())
        return;
    const auto &entry = **found;
    const auto world = doc_.worldTransform(h.terrain);
    std::vector<std::vector<Vec3>> local;     // with their own Z (terrain frame)
    std::vector<std::vector<QPointF>> planar; // to lie on the surface (terrain frame)
    auto closed = [](std::vector<QPointF> points) {
        if (!points.empty())
            points.push_back(points.front());
        return points;
    };
    if (const auto *pad = dynamic_cast<const TerrainStampData *>(entry.data.get())) {
        if (pad->resolved()) {
            std::vector<Vec3> outline;
            for (const auto &p : pad->outline)
                outline.push_back({p[0], p[1], pad->elevation});
            outline.push_back(outline.front());
            local.push_back(outline);
            auto ring = terrainStampRing(*pad);
            if (!ring.empty()) {
                ring.push_back(ring.front());
                local.push_back(ring);
            }
        } else {
            std::vector<Vec3> footprint;
            for (const auto &p : pad->footprint)
                footprint.push_back({p[0], p[1], pad->elevation});
            footprint.push_back(footprint.front());
            local.push_back(footprint);
        }
    } else if (const auto *drape = dynamic_cast<const TerrainDrapeData *>(entry.data.get())) {
        if (drape->resolved())
            local = drape->runs;
        else
            for (const auto &line : drape->lines) {
                std::vector<QPointF> points;
                for (const auto &p : line.points)
                    points.push_back({p[0], p[1]});
                planar.push_back(line.closed ? closed(points) : points);
            }
    } else if (const auto *source = dynamic_cast<const TerrainSourceEditData *>(entry.data.get())) {
        if (!source->points.empty() && source->role != TerrainSourceRole::Points)
            local.push_back(source->points);
        else if (!source->points.empty())
            for (const auto &p : source->points) {
                // A spot point: a small square around it.
                local.push_back({p + Vec3{-0.5, -0.5, 0}, p + Vec3{0.5, -0.5, 0}, p + Vec3{0.5, 0.5, 0},
                                 p + Vec3{-0.5, 0.5, 0}, p + Vec3{-0.5, -0.5, 0}});
                if (local.size() > 256)
                    break;
            }
        else {
            const auto &b = source->before;
            planar.push_back(closed({{b[0], b[1]}, {b[2], b[1]}, {b[2], b[3]}, {b[0], b[3]}}));
        }
    } else if (const auto *sculpt = dynamic_cast<const TerrainSculptData *>(entry.data.get())) {
        QPainterPath plan;
        if (sculpt->brush()) {
            QPainterPath line(QPointF(sculpt->path.front()[0], sculpt->path.front()[1]));
            for (const auto &p : sculpt->path)
                line.lineTo(p[0], p[1]);
            if (sculpt->path.size() == 1)
                plan.addEllipse(QPointF(sculpt->path[0][0], sculpt->path[0][1]), sculpt->radius,
                                sculpt->radius);
            else {
                QPainterPathStroker stroker;
                stroker.setWidth(2 * sculpt->radius);
                stroker.setCapStyle(Qt::RoundCap);
                stroker.setJoinStyle(Qt::RoundJoin);
                stroker.setCurveThreshold(sculpt->radius * 0.02);
                plan = stroker.createStroke(line).simplified();
            }
        } else {
            QPolygonF region;
            for (const auto &p : sculpt->region)
                region << QPointF(p[0], p[1]);
            plan.addPolygon(region);
            plan.closeSubpath();
        }
        for (const auto &polygon : plan.toSubpathPolygons())
            planar.emplace_back(polygon.begin(), polygon.end());
    } else if (const auto *kind = findTerrainEditKind(entry.kind); kind && kind->bounds) {
        const auto b = kind->bounds(entry);
        planar.push_back(closed({{b[0], b[1]}, {b[2], b[1]}, {b[2], b[3]}, {b[0], b[3]}}));
    }
    for (const auto &line : local) {
        std::vector<Vec3> model;
        for (const auto &p : line)
            model.push_back(world.point(p));
        h.lines.push_back(std::move(model));
    }
    // Plan lines lie on the surface (one elevation query a batch).
    for (const auto &line : planar) {
        std::vector<std::array<double, 2>> plan;
        for (const auto &p : line) {
            const auto m = world.point({p.x(), p.y(), 0});
            plan.push_back({m.x, m.y});
        }
        std::vector<Vec3> model;
        try {
            for (std::size_t first = 0; first < plan.size(); first += terrainPointBatch) {
                const auto last = std::min(plan.size(), first + terrainPointBatch);
                const auto heights = terrainElevationAt(
                    doc_, h.terrain,
                    {plan.begin() + std::ptrdiff_t(first), plan.begin() + std::ptrdiff_t(last)});
                for (std::size_t i = 0; i < heights.size(); ++i)
                    if (heights[i].found)
                        model.push_back({plan[first + i][0], plan[first + i][1], heights[i].elevation});
            }
        } catch (const std::exception &) {
        }
        if (model.size() >= 2)
            h.lines.push_back(std::move(model));
    }
}
void Viewport::frameTerrainEdit(Id terrain, std::uint32_t edit) {
    if (editHighlight_.terrain != terrain || editHighlight_.edit != edit)
        setTerrainEditHighlight(terrain, edit, editHighlight_.label);
    else if (editHighlight_.document != doc_.identity() || editHighlight_.revision != doc_.revision())
        buildTerrainEditHighlight();
    Vec3 low{1e300, 1e300, 1e300}, high{-1e300, -1e300, -1e300};
    for (const auto &line : editHighlight_.lines)
        for (const auto &p : line) {
            low = {std::min(low.x, p.x), std::min(low.y, p.y), std::min(low.z, p.z)};
            high = {std::max(high.x, p.x), std::max(high.y, p.y), std::max(high.z, p.z)};
        }
    if (low.x > high.x)
        return;
    const double margin = std::max({high.x - low.x, high.y - low.y, 2.0}) * 0.25;
    frameBounds(low - Vec3{margin, margin, 0.5}, high + Vec3{margin, margin, 0.5});
    emit message(QString("Framed change %1").arg(edit));
}
void Viewport::paintTerrainEditHighlight(QPainter &p) {
    auto &h = editHighlight_;
    // Only while its terrain is the selection (the section shows it), never in an export.
    if (!h.edit || !isTerrain(doc_, h.terrain) || !rasterSize_.isEmpty() ||
        selectedTerrain(doc_, selection_) != h.terrain)
        return;
    if (h.document != doc_.identity() || h.revision != doc_.revision())
        buildTerrainEditHighlight();
    if (h.lines.empty())
        return;
    const auto project = screenProjector();
    const QColor selection = colors_.selectionStroke.isValid() ? colors_.selectionStroke
                                                               : QColor("#5b5bd6");
    p.save();
    p.setRenderHint(QPainter::Antialiasing);
    p.setBrush(Qt::NoBrush);
    std::optional<QPointF> anchor;
    for (const auto &line : h.lines) {
        QPolygonF screen;
        for (const auto &q : line)
            screen << project(q);
        if (!anchor && !screen.isEmpty())
            anchor = screen.front();
        for (const auto &q : screen)
            if (q.x() < anchor->x())
                anchor = q;
        p.setPen(QPen(QColor(255, 255, 255, 220), 5, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        p.drawPolyline(screen);
        p.setPen(QPen(selection, 2.4, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        p.drawPolyline(screen);
    }
    if (anchor && !h.label.isEmpty()) {
        const QFontMetrics metrics(p.font());
        int textWidth = 0;
        for (const auto &line : h.label)
            textWidth = std::max(textWidth, metrics.horizontalAdvance(line));
        const int boxWidth = std::min(textWidth + 28, std::max(1, width() - 24));
        const int boxHeight = int(h.label.size()) * metrics.lineSpacing() + 10;
        const QRectF box(std::clamp(anchor->x() - boxWidth - 12, 12.0,
                                    double(std::max(12, width() - boxWidth - 12))),
                         std::clamp(anchor->y() - boxHeight / 2.0, 12.0,
                                    double(std::max(12, height() - boxHeight - 12))),
                         boxWidth, boxHeight);
        p.setPen(QPen(selection, 1.5));
        p.setBrush(colors_.surface);
        p.drawRoundedRect(box, 4, 4);
        p.setPen(colors_.ink);
        for (int i = 0; i < int(h.label.size()); ++i)
            p.drawText(QRectF(box.left() + 10, box.top() + 5 + i * metrics.lineSpacing(),
                              box.width() - 20, metrics.lineSpacing()),
                       Qt::AlignLeft | Qt::AlignVCenter,
                       metrics.elidedText(h.label[i], Qt::ElideRight, int(box.width()) - 20));
    }
    p.restore();
}
// Skipped changes stay marked while the list the replay produced is a prefix of the record's (a
// later change keeps them; Undo of the replay or a removal clears them).
void Viewport::setTerrainSkips(Id terrain, std::map<std::uint32_t, QString> skips) {
    std::vector<TerrainEditPtr> edits;
    if (isTerrain(doc_, terrain))
        edits = doc_.bodies().at(terrain)->terrain->edits;
    terrainSkips_[terrain] = {std::move(edits), std::move(skips), doc_.saveStamp()};
    ++terrainReportGeneration_;
}
std::map<std::uint32_t, QString> Viewport::terrainSkips(Id terrain) const {
    const auto found = terrainSkips_.find(terrain);
    if (found == terrainSkips_.end() || !isTerrain(doc_, terrain) || !found->second.document ||
        !doc_.owns(*found->second.document))
        return {};
    const auto &current = doc_.bodies().at(terrain)->terrain->edits;
    const auto &replayed = found->second.edits;
    if (replayed.size() > current.size() ||
        !std::equal(replayed.begin(), replayed.end(), current.begin()))
        return {};
    return found->second.skips;
}
} // namespace sketchy
