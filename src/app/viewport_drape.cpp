// R096.f2 (UX_DESIGN §15.5, §15.7; ADR 0169 §4.4, §5): the Drape tool.
//
// - Input: the edges, curves and faces selected when the tool starts (in any context, above or
//   below the terrain; a selected group or component contributes its faces). Terrain → Drape, the
//   strip's Drape, or Drape onto terrain in the context menu start it.
// - Pointing at a terrain previews the drape on the worker: the lines lying on the surface, a
//   face's region in its material with Paint regions on, and the parts outside the terrain red and
//   dashed, counted in the strip ("1 edge clipped"). A click on the terrain or Enter drapes.
// - Imprint (the default) leaves the terrain's shape and adds the lines as breaklines; afterwards
//   the terrain is selected. Overlay makes ordinary geometry in a new group in the active context,
//   lifted by Lift; afterwards the group is selected. Either way the tool returns to Select.
#include "app/terrain_actions.hpp"
#include "app/unit_display.hpp"
#include "app/viewport.hpp"
#include "automation/measurements.hpp"
#include "core/material_records.hpp"
#include <QKeyEvent>
#include <QLocale>
#include <QMouseEvent>
#include <QPainter>
#include <algorithm>
#include <cmath>
namespace sketchy {
namespace {
QString countText(std::size_t faces, std::size_t edges) {
    QStringList parts;
    if (faces)
        parts << QString("%1 %2").arg(faces).arg(faces == 1 ? "face" : "faces");
    if (edges)
        parts << QString("%1 %2").arg(edges).arg(edges == 1 ? "edge" : "edges");
    return parts.isEmpty() ? QString("nothing") : parts.join(", ");
}
} // namespace

void Viewport::beginDrapeTool() {
    brush_ = {};
    terrainCandidate_.reset();
    terrainCandidateShown_ = false;
    terrainStatusShown_.clear();
    drapeTool_ = {};
    withdrawTerrainEditPreview();
    auto &tool = drapeTool_;
    // The selection: edges (curves select their edges), faces, and the faces of selected groups
    // and components; never a terrain's own.
    tool.edges = selectedTerrainEdges(selection_);
    std::set<std::pair<Id, Id>> faces;
    for (const auto &entity : selection_.entities()) {
        const auto found = doc_.bodies().find(entity.body);
        if (found == doc_.bodies().end() || found->second->terrainTile ||
            found->second->kind == BodyKind::Terrain)
            continue;
        if (entity.kind == SelectionKind::Face)
            faces.insert({entity.body, entity.entity});
        else if (entity.kind == SelectionKind::Body)
            for (const auto &[id, body] : doc_.bodies()) {
                if (body->kind != BodyKind::Geometry || body->terrainTile)
                    continue;
                bool inside = false;
                for (Id at = id; at && !inside;) {
                    inside = at == entity.body;
                    const auto parent = doc_.bodies().find(at);
                    at = parent == doc_.bodies().end() ? 0 : parent->second->parent;
                }
                if (inside)
                    for (const auto &[face, record] : body->surface.faces)
                        faces.insert({id, face});
            }
    }
    for (const auto &[body, face] : faces)
        tool.faces.append(QJsonObject{{"body", QString::number(body)}, {"face", QString::number(face)}});
    if (!doc_.materializedTotals().terrainTriangles)
        tool.refusal = "No terrain in this model. Import one with File → Import terrain…";
    else if (tool.edges.isEmpty() && tool.faces.isEmpty())
        tool.refusal = "Select edges, curves or faces first, then choose Drape (or right-click → "
                       "Drape onto terrain)";
    else {
        // With one terrain in the open context, it is the target until the pointer names another.
        for (const auto &[id, body] : doc_.bodies())
            if (body->kind == BodyKind::Terrain && selection_.inActiveHierarchy(doc_, id)) {
                if (tool.terrain) {
                    tool.terrain = 0;
                    break;
                }
                tool.terrain = id;
            }
        if (tool.terrain)
            requestTerrainEditPreview(drapeCommand());
    }
    emitTerrainStatus();
    emit terrainToolChanged();
    update();
}
QJsonObject Viewport::drapeCommand() const {
    const auto &tool = drapeTool_;
    QJsonObject command{{"command", "terrain.drape"},
                        {"terrain", QString::number(tool.terrain)},
                        {"mode", terrainOptions_.drapeOverlay ? "overlay" : "imprint"}};
    if (!tool.edges.isEmpty())
        command["edges"] = tool.edges;
    if (!tool.faces.isEmpty())
        command["faces"] = tool.faces;
    if (terrainOptions_.drapeOverlay) {
        command["lift"] = terrainOptions_.lift;
        command["context"] = QString::number(selection_.context());
    } else
        command["paintRegions"] = terrainOptions_.paintRegions;
    return command;
}
Viewport::DrapeToolView Viewport::drapeTool() const {
    DrapeToolView view;
    const auto &tool = drapeTool_;
    view.terrain = tool.terrain;
    view.edges = std::size_t(tool.edges.size());
    view.faces = std::size_t(tool.faces.size());
    view.refusal = tool.refusal;
    QString summary = countText(view.faces, view.edges);
    if (const auto shown = terrainEditPreviewShown(); shown && shown->kind == terrainDrapeKind) {
        view.previewShown = !shown->refused();
        view.refused = shown->refused();
        view.clipped = shown->clipped;
        view.runs = shown->runs.size();
        if (shown->clipped)
            summary += QString(" · %1 %2 clipped").arg(shown->clipped).arg(shown->clipped == 1 ? "edge" : "edges");
        if (view.refused) {
            const auto failure = terrainEditFailure(*shown);
            view.failure = failure.label;
            view.failureAt = failure.at;
        }
    }
    view.summary = summary;
    return view;
}
void Viewport::emitDrapeStatus() {
    const auto &tool = drapeTool_;
    if (terrainStatusHeld_)
        return;
    terrainFailureStatus_.clear();
    QString text;
    const auto view = drapeTool();
    if (!tool.refusal.isEmpty())
        text = tool.refusal;
    else if (view.refused) {
        text = terrainEditFailure(*terrainEditPreviewShown()).status + " Nothing can be draped here.";
        terrainFailureStatus_ = text;
    }
    else if (!terrainEditRequest_.error.isEmpty())
        text = "Nothing would change: " + terrainEditRequest_.error;
    else if (!tool.terrain)
        text = "Drape · Point at the terrain to drape " + countText(view.faces, view.edges) +
               " · Esc leaves";
    else {
        text = "Drape · Click the terrain or press Enter to drape the selection";
        if (view.clipped)
            text += QString(" · %1 %2 outside the terrain %3 clipped")
                        .arg(view.clipped)
                        .arg(view.clipped == 1 ? "edge" : "edges")
                        .arg(view.clipped == 1 ? "is" : "are");
        text += " · Esc leaves";
    }
    if (text == terrainStatusShown_)
        return;
    terrainStatusShown_ = text;
    emit message(text);
}
void Viewport::drapeHover(QPointF point) {
    auto &tool = drapeTool_;
    tool.screen = point;
    terrainStatusHeld_ = false;
    const auto hit = terrainHit(point);
    if (hit && hit->first != tool.terrain && tool.refusal.isEmpty()) {
        tool.terrain = hit->first;
        requestTerrainEditPreview(drapeCommand());
    }
    setCursor(hit && tool.refusal.isEmpty() ? Qt::CrossCursor : Qt::ForbiddenCursor);
    emitTerrainStatus();
    updateAfterTerrainEdit();
}
bool Viewport::drapePress(QMouseEvent *event) {
    auto &tool = drapeTool_;
    if (!tool.refusal.isEmpty()) {
        emit message(tool.refusal);
        return true;
    }
    const auto hit = terrainHit(event->position());
    if (!hit) {
        emit message("Point at the terrain to drape the selection on");
        return true;
    }
    if (hit->first != tool.terrain) {
        tool.terrain = hit->first;
        requestTerrainEditPreview(drapeCommand());
    }
    commitDrape();
    return true;
}
void Viewport::commitDrape() {
    auto &tool = drapeTool_;
    if (!tool.refusal.isEmpty() || !tool.terrain) {
        emit message(tool.refusal.isEmpty() ? QString("Point at the terrain to drape the selection on")
                                            : tool.refusal);
        return;
    }
    const auto command = drapeCommand();
    if (const auto shown = terrainEditPreviewShown();
        shown && shown->refused() && shown->command == command) {
        holdTerrainStatus("Nothing changed: " + terrainEditFailure(*shown).status);
        return;
    }
    QJsonObject result;
    try {
        terrainSession_.begin();
        QElapsedTimer timing;
        timing.start();
        result = terrainSession_.commit(command, BatchResponse::Changes);
        stats_.terrainCommandMs = double(timing.nsecsElapsed()) / 1e6;
    } catch (const std::exception &error) {
        terrainSession_.cancel();
        holdTerrainStatus(QString("Nothing changed: %1").arg(QString::fromUtf8(error.what())));
        return;
    }
    terrainCommitted_ = {};
    const auto terrain = tool.terrain;
    const bool overlay = command["mode"] == "overlay";
    withdrawTerrainEditPreview();
    rememberTerrainReport(terrain, result);
    if (!overlay)
        prefetchTerrainDigest(terrain);
    const auto operation = result["terrainOperations"].toArray().first().toObject();
    const auto summary = countText(std::size_t(tool.faces.size()), std::size_t(tool.edges.size()));
    // What is selected afterwards (UX §15.5): the terrain after an imprint, the new group after
    // an overlay. The tool then returns to Select, its input used.
    QElapsedTimer timing;
    timing.start();
    refresh();
    setTool(Tool::Select);
    if (overlay) {
        const Id group = operation["group"].toString().toULongLong();
        setSelection(doc_.bodies().contains(group) ? group : 0);
    } else
        setSelection(terrain);
    emit changed();
    stats_.terrainRefreshMs = double(timing.nsecsElapsed()) / 1e6;
    emit message(overlay ? "Draped " + summary + " as an overlay group"
                         : "Draped " + summary + " onto the terrain");
}
bool Viewport::drapeKey(QKeyEvent *event) {
    if (event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter) {
        commitDrape();
        event->accept();
        return true;
    }
    const bool plain =
        !(event->modifiers() & (Qt::ControlModifier | Qt::AltModifier | Qt::MetaModifier));
    if (plain && (event->text() == "@" || event->text() == "[")) {
        emit measurementsRequested(event->text());
        event->accept();
        return true;
    }
    return false; // Escape leaves the tool (nothing is in progress)
}
bool Viewport::drapeMeasurements(const QString &text) {
    // §15.7: a typed position chooses the terrain there; Enter (or a click) then drapes.
    const auto unit = inputUnit(doc_.displayUnits());
    const auto locale = QLocale();
    std::array<double, 2> position{};
    if (text.startsWith('@')) {
        if (!doc_.site())
            throw std::runtime_error("A leading @ types a site position (E, N), but this document "
                                     "has no site origin. Import a terrain, or set one in File → "
                                     "Site settings….");
        const auto values = parseSitePosition(text.mid(1), unit, locale);
        position = {values[0] - doc_.site()->easting, values[1] - doc_.site()->northing};
    } else if (text.startsWith('[') && text.endsWith(']')) {
        const auto inner = text.mid(1, text.size() - 2);
        const auto separator = locale.decimalPoint() == "," ? ';' : ',';
        const auto pieces = inner.split(inner.contains(';') ? ';' : separator);
        if (pieces.size() != 2 && pieces.size() != 3)
            throw std::runtime_error("A position is [x, y] in model coordinates");
        position = {parseLength(pieces[0], unit, locale), parseLength(pieces[1], unit, locale)};
    } else
        throw std::runtime_error("Type the terrain's position as [x, y] or @E, N, then press Enter "
                                 "to drape");
    for (const auto &[id, body] : doc_.bodies())
        if (body->kind == BodyKind::Terrain && selection_.inActiveHierarchy(doc_, id) &&
            terrainElevation(id, position[0], position[1])) {
            drapeTool_.terrain = id;
            requestTerrainEditPreview(drapeCommand());
            emitTerrainStatus();
            emit terrainToolChanged();
            return true;
        }
    throw std::runtime_error("That position is not on a terrain");
}

// ---- Drawing (UX §15.5) ------------------------------------------------------------------------
void Viewport::paintDrapeTool(QPainter &p) {
    const auto &tool = drapeTool_;
    const auto shown = terrainEditPreviewShown();
    if (!shown || shown->kind != terrainDrapeKind) {
        paintTerrainEditPreview(p);
        return;
    }
    const auto project = screenProjector();
    const auto world = shown->world;
    const QColor ink = colors_.ink;
    const QColor selection = colors_.selectionStroke.isValid() ? colors_.selectionStroke
                                                               : QColor("#5b5bd6");
    const QColor red = colors_.failure.isValid() ? colors_.failure : QColor("#a8322b");
    auto line = [&](const std::vector<Vec3> &points) {
        QPolygonF screen;
        for (const auto &q : points)
            screen << project(world.point(q));
        return screen;
    };
    p.save();
    p.setRenderHint(QPainter::Antialiasing);
    // Region paint: each painted face's region in its material, lying on the surface.
    if (!shown->refused() && !shown->overlay && shown->drape) {
        if (drapeTool_.paintFor != shown.get()) {
            drapeTool_.paintFor = shown.get();
            drapeTool_.paint.clear();
            for (const auto &region : shown->drape->paint) {
                const auto found = doc_.materials().find(region.material);
                QColor colour = found == doc_.materials().end()
                                    ? QColor(180, 180, 180)
                                    : QColor::fromRgbF(found->second->color[0], found->second->color[1],
                                                       found->second->color[2]);
                colour.setAlphaF(0.7f);
                for (const auto &loop : region.loops) {
                    std::vector<std::array<double, 2>> plan;
                    for (const auto &q : loop) {
                        const auto m = world.point({q[0], q[1], 0});
                        plan.push_back({m.x, m.y});
                    }
                    std::vector<Vec3> draped;
                    try {
                        for (std::size_t first = 0; first < plan.size(); first += terrainPointBatch) {
                            const auto last = std::min(plan.size(), first + terrainPointBatch);
                            const auto part = terrainElevationAt(
                                doc_, shown->terrain,
                                {plan.begin() + std::ptrdiff_t(first), plan.begin() + std::ptrdiff_t(last)});
                            for (std::size_t i = 0; i < part.size(); ++i)
                                draped.push_back({plan[first + i][0], plan[first + i][1],
                                                  part[i].found ? part[i].elevation : 0});
                        }
                    } catch (const std::exception &) {
                        draped.clear();
                    }
                    drapeTool_.paint.push_back({colour, std::move(draped)});
                }
            }
        }
        p.setPen(Qt::NoPen);
        for (const auto &[colour, loop] : drapeTool_.paint) {
            QPolygonF screen;
            for (const auto &q : loop)
                screen << project(q);
            p.setBrush(colour);
            p.drawPolygon(screen, Qt::OddEvenFill);
        }
    }
    // Overlay: the lifted faces and edges.
    if (shown->overlay && !shown->refused()) {
        QColor fill = selection;
        fill.setAlphaF(0.25f);
        p.setBrush(fill);
        p.setPen(Qt::NoPen);
        for (const auto &face : shown->overlayGeometry.faces)
            for (const auto &loop : face.loops)
                p.drawPolygon(line(loop));
    }
    // The changed region (the split triangles), hatched; then the lines on the surface.
    if (shown->candidate)
        for (const auto &loop : shown->outline) {
            auto screen = line(loop);
            if (!screen.isEmpty())
                screen << screen.front();
            p.setBrush(Qt::NoBrush);
            p.setPen(QPen(ink, 4, Qt::SolidLine, Qt::FlatCap, Qt::RoundJoin));
            p.drawPolyline(screen);
            QPen hatch(Qt::white, 2.5, Qt::CustomDashLine, Qt::FlatCap, Qt::RoundJoin);
            hatch.setDashPattern({0.8, 0.8});
            p.setPen(hatch);
            p.drawPolyline(screen);
        }
    p.setBrush(Qt::NoBrush);
    for (const auto &run : shown->runs) {
        const auto screen = line(run);
        p.setPen(QPen(QColor(255, 255, 255, 220), 4.5, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        p.drawPolyline(screen);
        p.setPen(QPen(ink, 1.8, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        p.drawPolyline(screen);
    }
    // Parts outside the terrain, red and dashed, with a label.
    QPen dashed(red, 2, Qt::CustomDashLine, Qt::FlatCap, Qt::RoundJoin);
    dashed.setDashPattern({3, 2});
    p.setPen(dashed);
    for (const auto &part : shown->clippedParts)
        p.drawPolyline(line(part));
    p.restore();
    paintTerrainEditPreview(p);
    // Labels: the clipped parts, and the drape at the pointer ("Imprint · 1 face, 5 edges").
    std::vector<std::tuple<QPointF, QStringList, bool>> labels;
    if (!shown->clippedParts.empty()) {
        const auto &part = shown->clippedParts.front();
        labels.push_back({project(world.point(part[part.size() / 2])) + QPointF(12, -10),
                          {"Clipped: outside the terrain"}, true});
    }
    if (!shown->refused() && tool.terrain && !shown->runs.empty()) {
        QStringList lines{(shown->overlay ? "Overlay · " : "Imprint · ") +
                          countText(std::size_t(tool.faces.size()), std::size_t(tool.edges.size()))};
        if (!shown->overlay && shown->drape && !shown->drape->paint.empty()) {
            const auto found = doc_.materials().find(shown->drape->paint.front().material);
            if (found != doc_.materials().end())
                lines << "Paint: " + QString::fromStdString(found->second->name);
        }
        // At the pointer, or (typed, without one) beside the first line on the surface.
        const auto &run = shown->runs.front();
        labels.push_back({tool.screen ? *tool.screen + QPointF(16, 10)
                                      : project(world.point(run[run.size() / 2])) + QPointF(24, 18),
                          lines, false});
    } else if (shown->refused()) {
        const auto failure = terrainEditFailure(*shown);
        if (failure.at)
            labels.push_back({project(*failure.at) + QPointF(16, 14),
                              {failure.label, "Nothing can be draped here"}, true});
    }
    p.save();
    p.setRenderHint(QPainter::Antialiasing);
    const QFontMetrics metrics(p.font());
    for (const auto &[at, lines, failure] : labels) {
        int textWidth = 0;
        for (const auto &text : lines)
            textWidth = std::max(textWidth, metrics.horizontalAdvance(text));
        const int boxWidth = std::min(textWidth + 20, std::max(1, width() - 24));
        const int boxHeight = int(lines.size()) * metrics.lineSpacing() + 10;
        const QRectF box(std::clamp(at.x(), 12.0, double(std::max(12, width() - boxWidth - 12))),
                         std::clamp(at.y(), 12.0, double(std::max(12, height() - boxHeight - 12))),
                         boxWidth, boxHeight);
        p.setPen(QPen(failure ? red : colors_.border, failure ? 1.5 : 1));
        p.setBrush(failure ? (colors_.failureSurface.isValid() ? colors_.failureSurface
                                                               : colors_.surface)
                           : colors_.surface);
        p.drawRoundedRect(box, 4, 4);
        p.setPen(failure ? red : ink);
        for (int i = 0; i < int(lines.size()); ++i)
            p.drawText(QRectF(box.left() + 10, box.top() + 5 + i * metrics.lineSpacing(),
                              box.width() - 20, metrics.lineSpacing()),
                       Qt::AlignLeft | Qt::AlignVCenter,
                       metrics.elidedText(lines[i], Qt::ElideRight, int(box.width()) - 20));
    }
    p.restore();
}
} // namespace sketchy
