// R096.f2 (UX_DESIGN §15.4, §15.7; ADR 0169 §4.3, §5): the Stamp pad tool.
//
// - Footprint: a face, group, component or closed edge loop selected when the tool starts, or the
//   one under the pointer when clicked. A group or component uses its lowest horizontal face, which
//   is highlighted before the click. The strip names the footprint.
// - Elevation: the pad starts at the footprint's height less the Pad offset (remembered); moving
//   the pointer up or down, or typing, sets it. Enter or a click stamps it ("Stamp pad").
// - The preview, planned on the worker, draws the candidate surface in place with cut tinted brown
//   and hatched and fill teal and dotted (the strip's legend names both), the pad flat and light,
//   the slope rays as thin lines and the daylight line dashed and labelled. A refusal ("Ray at E …,
//   N … leaves the terrain", "Overlaps Pad") is marked in red with an ×, and Enter is refused until
//   it changes.
// - After a stamp, a typed elevation or slope ("1:3", "26.6°") revises the same entry (UX §4.3)
//   through Document::amendLast's terrain policy.
#include "app/terrain_actions.hpp"
#include "app/unit_display.hpp"
#include "app/viewport.hpp"
#include "automation/measurements.hpp"
#include <QKeyEvent>
#include <QLineF>
#include <QLocale>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QSettings>
#include <algorithm>
#include <cmath>
namespace sketchy {
namespace {
std::optional<std::array<double, 2>> typedPadPosition(const QString &text, const QString &unit) {
    if (!text.startsWith('[') || !text.endsWith(']'))
        return std::nullopt;
    const auto locale = QLocale();
    const auto inner = text.mid(1, text.size() - 2);
    const auto separator = locale.decimalPoint() == "," ? ';' : ',';
    const auto pieces = inner.split(inner.contains(';') ? ';' : separator, Qt::KeepEmptyParts);
    if (pieces.size() != 2 && pieces.size() != 3)
        throw std::runtime_error("A position is [x, y] in model coordinates, or @E, N in site "
                                 "coordinates");
    return std::array<double, 2>{parseLength(pieces[0], unit, locale),
                                 parseLength(pieces[1], unit, locale)};
}
bool insidePlan(const std::vector<Vec3> &polygon, double x, double y) {
    bool inside = false;
    for (std::size_t i = 0, j = polygon.size() - 1; i < polygon.size(); j = i++) {
        const auto &a = polygon[i], &b = polygon[j];
        if ((a.y > y) != (b.y > y) && x < (b.x - a.x) * (y - a.y) / (b.y - a.y) + a.x)
            inside = !inside;
    }
    return inside;
}
} // namespace

double Viewport::padElevation() const {
    return padTool_.footprintZ - terrainOptions_.padOffset + padTool_.delta;
}
QJsonObject Viewport::padCommand(double elevation) const {
    QJsonObject command{{"command", "terrain.stamp_pad"},
                        {"terrain", QString::number(padTool_.terrain)},
                        {"footprint", padTool_.footprint},
                        {"elevation", elevation}};
    if (terrainOptions_.padMode == TerrainStampMode::Width) {
        command["mode"] = "width";
        command["width"] = terrainOptions_.padWidth;
    } else {
        command["mode"] = "daylight";
        command["cutSlope"] = terrainOptions_.cutSlope;
        command["fillSlope"] = terrainOptions_.fillSlope;
        command["maxDistance"] = terrainOptions_.maxDistance;
    }
    return command;
}
// The footprint an entity gives: a face of its own, or a group's or component's lowest horizontal
// face; with its name for the strip and its outline (model) for the highlight.
bool Viewport::padFootprintOf(const SelectedEntity &entity, PadFootprint &out) const {
    const auto found = doc_.bodies().find(entity.body);
    if (found == doc_.bodies().end() || found->second->terrainTile ||
        found->second->kind == BodyKind::Terrain)
        return false;
    QJsonObject footprint{{"body", QString::number(entity.body)}};
    if (entity.kind == SelectionKind::Face)
        footprint["face"] = QString::number(entity.entity);
    else if (entity.kind != SelectionKind::Body)
        return false;
    try {
        const auto shape = terrainPadFootprint(doc_, footprint);
        out.footprint = footprint;
        out.outline = shape.outline;
        out.z = shape.z.value_or(0);
        const auto &name = found->second->name;
        out.name = entity.kind == SelectionKind::Face
                       ? (found->second->parent && doc_.bodies().contains(found->second->parent)
                              ? QString::fromStdString(doc_.bodies().at(found->second->parent)->name)
                              : QString("Face"))
                       : QString::fromStdString(name.empty() ? std::string("Group") : name);
        if (out.name.isEmpty())
            out.name = "Face";
        return true;
    } catch (const std::exception &error) {
        out.error = QString::fromUtf8(error.what());
        return false;
    }
}
// The terrain under a footprint: the first in the open context holding its centre (or a corner).
Id Viewport::padTerrainUnder(const std::vector<Vec3> &outline) const {
    if (outline.empty())
        return 0;
    Vec3 centre{};
    for (const auto &p : outline)
        centre = centre + p;
    centre = centre * (1.0 / double(outline.size()));
    std::vector<Vec3> probes{centre};
    probes.insert(probes.end(), outline.begin(), outline.end());
    for (const auto &[id, body] : doc_.bodies())
        if (body->kind == BodyKind::Terrain && selection_.inActiveHierarchy(doc_, id))
            for (const auto &p : probes)
                if (terrainElevation(id, p.x, p.y))
                    return id;
    return 0;
}
void Viewport::choosePadFootprint(PadFootprint footprint) { // by value: it may be the hover
    auto &tool = padTool_;
    tool.footprint = footprint.footprint;
    tool.name = footprint.name;
    tool.outline = footprint.outline;
    tool.footprintZ = footprint.z;
    tool.delta = 0;
    tool.anchor.reset();
    tool.hover = {};
    tool.terrain = padTerrainUnder(footprint.outline);
    if (!tool.terrain) {
        tool.refusal = "The footprint is not over a terrain";
        tool.footprint = {};
        emitTerrainStatus();
        emit terrainToolChanged();
        update();
        return;
    }
    tool.refusal.clear();
    if (terrainSession_.canRevise())
        terrainCommitted_ = {}; // a new pad: the last one can no longer be revised
    terrainSession_.begin();
    requestTerrainEditPreview(padCommand(padElevation()));
    emit measurementPreview(displayLength(padElevation(), doc_.displayUnits(), 2));
    terrainStatusShown_.clear();
    emitTerrainStatus();
    emit terrainToolChanged();
    update();
}
void Viewport::beginPadTool() {
    brush_ = {};
    terrainCandidate_.reset();
    terrainCandidateShown_ = false;
    terrainStatusShown_.clear();
    padTool_ = {};
    withdrawTerrainEditPreview();
    if (!doc_.materializedTotals().terrainTriangles)
        padTool_.refusal = "No terrain in this model. Import one with File → Import terrain…";
    else {
        // The footprint from the selection: one face, group or component, or a closed edge loop.
        const auto &entities = selection_.entities();
        PadFootprint footprint;
        bool chosen = false;
        if (entities.size() == 1 && (entities.begin()->kind == SelectionKind::Face ||
                                     entities.begin()->kind == SelectionKind::Body))
            chosen = padFootprintOf(*entities.begin(), footprint);
        else if (!entities.empty() &&
                 std::all_of(entities.begin(), entities.end(), [](const SelectedEntity &e) {
                     return e.kind == SelectionKind::Edge;
                 })) {
            const QJsonObject loop{{"edges", selectedTerrainEdges(selection_)}};
            try {
                const auto shape = terrainPadFootprint(doc_, loop);
                footprint.footprint = loop;
                footprint.outline = shape.outline;
                footprint.z = shape.z.value_or(0);
                footprint.name = "Edge loop";
                chosen = true;
            } catch (const std::exception &error) {
                footprint.error = QString::fromUtf8(error.what());
            }
        }
        if (chosen)
            choosePadFootprint(footprint);
        else if (!footprint.error.isEmpty() && !entities.empty())
            emit message("The selection is not a footprint: " + footprint.error);
    }
    emitTerrainStatus();
    emit terrainToolChanged();
    update();
}
void Viewport::cancelPadOperation() {
    auto &tool = padTool_;
    const bool was = !tool.footprint.isEmpty();
    padTint_ = {};
    tool.footprint = {};
    tool.outline.clear();
    tool.name.clear();
    tool.delta = 0;
    tool.anchor.reset();
    if (was && tool_ == Tool::Pad) {
        terrainSession_.cancel();
        terrainCommitted_ = {};
        emit measurementPreview({});
        emit terrainToolChanged(); // the strip forgets the footprint
    }
}
Viewport::PadToolView Viewport::padTool() const {
    PadToolView view;
    const auto &tool = padTool_;
    view.terrain = tool.terrain;
    view.footprint = !tool.footprint.isEmpty();
    view.name = view.footprint ? tool.name : tool.hover.name;
    view.outline = view.footprint ? tool.outline : tool.hover.outline;
    view.hovering = !view.footprint && !tool.hover.outline.empty();
    view.footprintZ = tool.footprintZ;
    view.elevation = padElevation();
    view.refusal = tool.refusal;
    if (const auto shown = terrainEditPreviewShown(); shown && shown->kind == terrainStampKind) {
        view.previewShown = bool(shown->candidate);
        view.refused = shown->refused();
        view.cut = shown->cut;
        view.fill = shown->fill;
        view.rays = shown->rays;
        if (view.refused) {
            const auto failure = terrainEditFailure(*shown);
            view.failure = failure.label;
            view.failureAt = failure.at;
        }
    }
    return view;
}
QString Viewport::padMeasurementLabel() const {
    if (!padTool_.footprint.isEmpty() || !terrainAmendable().isEmpty())
        return "Elevation";
    return "Position";
}
void Viewport::emitPadStatus() {
    const auto &tool = padTool_;
    if (terrainStatusHeld_)
        return;
    terrainFailureStatus_.clear();
    QString text;
    const auto units = doc_.displayUnits();
    const auto shown = terrainEditPreviewShown();
    if (!tool.refusal.isEmpty())
        text = tool.refusal;
    else if (!tool.footprint.isEmpty()) {
        if (shown && shown->refused() && terrainEditPreviewCurrent()) {
            text = terrainEditFailure(*shown).status + " Change the elevation, slopes or footprint.";
            terrainFailureStatus_ = text;
        }
        else if (!terrainEditRequest_.error.isEmpty())
            text = "Nothing would change: " + terrainEditRequest_.error;
        else
            text = "Stamp pad · " + tool.name +
                   " · Move up or down or type the pad elevation · Enter stamps · Esc cancels";
    } else if (!tool.hover.name.isEmpty())
        text = "Stamp pad · " + tool.hover.name + " · Click to use its footprint";
    else if (!tool.hover.error.isEmpty())
        text = "Stamp pad · " + tool.hover.error;
    else
        text = "Stamp pad · Point at a horizontal face, group or component and click, or select "
               "one first · Type [x, y] or @E, N · Esc leaves";
    if (tool.footprint.isEmpty() && !terrainAmendable().isEmpty() && tool.refusal.isEmpty())
        text = "Stamped · Type a new elevation or slope (1:3, 26.6°) to revise it · " + text;
    (void)units;
    if (text == terrainStatusShown_)
        return;
    terrainStatusShown_ = text;
    emit message(text);
}
void Viewport::padHover(QPointF point) {
    auto &tool = padTool_;
    tool.screen = point;
    terrainStatusHeld_ = false;
    if (!tool.footprint.isEmpty()) {
        // Up or down sets the elevation: metres at the footprint's depth, in centimetres.
        if (!tool.anchor)
            tool.anchor = point;
        Vec3 centre{};
        for (const auto &p : tool.outline)
            centre = centre + p;
        centre = centre * (1.0 / double(std::max<std::size_t>(1, tool.outline.size())));
        centre.z = padElevation();
        const auto base = project(centre);
        const double up = (project(centre + Vec3{0, 0, 1}) - base).manhattanLength();
        const double across = (project(centre + Vec3{1, 0, 0}) - base).manhattanLength();
        const double perMetre = std::max({up, across * 0.5, 1e-6});
        const double delta = std::round((tool.anchor->y() - point.y()) / perMetre * 100) / 100;
        if (delta != tool.delta) {
            tool.delta = std::clamp(delta, -1000.0, 1000.0);
            requestTerrainEditPreview(padCommand(padElevation()));
            emit measurementPreview(displayLength(padElevation(), doc_.displayUnits(), 2));
        }
        emitTerrainStatus();
        updateAfterTerrainEdit();
        return;
    }
    // Choosing: the face, group or component under the pointer, highlighted.
    const auto hovered = tool.hover.name;
    tool.hover = {};
    tool.refusal.clear();
    if (!doc_.materializedTotals().terrainTriangles)
        tool.refusal = "No terrain in this model. Import one with File → Import terrain…";
    if (const auto entity = selectionAt(point)) {
        PadFootprint footprint;
        if (padFootprintOf(*entity, footprint))
            tool.hover = footprint;
        else {
            tool.hover.error = footprint.error.isEmpty()
                                   ? QString("Point at a horizontal face, group or component")
                                   : footprint.error;
        }
    }
    setCursor(tool.hover.outline.empty() ? Qt::ForbiddenCursor : Qt::CrossCursor);
    emitTerrainStatus();
    if (tool.hover.name != hovered)
        emit terrainToolChanged(); // the strip names the hovered footprint
    update();
}
bool Viewport::padPress(QMouseEvent *event) {
    auto &tool = padTool_;
    if (!tool.footprint.isEmpty()) {
        commitPad();
        return true;
    }
    padHover(event->position());
    if (tool.hover.outline.empty()) {
        emit message(tool.hover.error.isEmpty() ? "Point at a horizontal face, group or component"
                                                : tool.hover.error);
        return true;
    }
    choosePadFootprint(tool.hover);
    tool.anchor = event->position();
    return true;
}
void Viewport::commitPad(std::optional<double> elevation) {
    auto &tool = padTool_;
    if (tool.footprint.isEmpty())
        return;
    const auto command = padCommand(elevation.value_or(padElevation()));
    if (!elevation)
        if (const auto shown = terrainEditPreviewShown();
            shown && shown->refused() && shown->command == command) {
            holdTerrainStatus("The pad cannot be stamped here: " + terrainEditFailure(*shown).status);
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
        // The footprint stays chosen: the session keeps guarding it against document changes.
        terrainSession_.begin();
        holdTerrainStatus(QString("Nothing changed: %1").arg(QString::fromUtf8(error.what())));
        return;
    }
    const auto terrain = tool.terrain;
    const auto name = tool.name;
    tool.footprint = {};
    tool.outline.clear();
    tool.delta = 0;
    tool.anchor.reset();
    withdrawTerrainEditPreview();
    terrainCommitted_ = command;
    rememberTerrainReport(terrain, result);
    prefetchTerrainDigest(terrain);
    QElapsedTimer timing;
    timing.start();
    refresh();
    emit changed();
    stats_.terrainRefreshMs = double(timing.nsecsElapsed()) / 1e6;
    holdTerrainStatus(
        QString("Stamped the pad · %1 at %2 · Type a new elevation or slope (1:3, 26.6°) to revise it")
            .arg(name, displayLength(command["elevation"].toDouble(), doc_.displayUnits(), 2)));
    emit terrainToolChanged();
}
// UX §4.3: after a stamp, a typed elevation or slope revises the same History entry.
void Viewport::amendPad(const QJsonObject &command, const QString &done) {
    QJsonObject result;
    try {
        result = terrainSession_.commit(command, BatchResponse::Changes);
    } catch (const std::exception &error) {
        holdTerrainStatus(QString("Nothing changed: %1").arg(QString::fromUtf8(error.what())));
        return;
    }
    terrainCommitted_ = command;
    rememberTerrainReport(Id(command["terrain"].toString().toULongLong()), result);
    prefetchTerrainDigest(Id(command["terrain"].toString().toULongLong()));
    refresh();
    emit changed();
    holdTerrainStatus(done + " · Type another value to revise it again");
    emit terrainToolChanged();
}
bool Viewport::padKey(QKeyEvent *event) {
    auto &tool = padTool_;
    terrainStatusHeld_ = false;
    const bool plain =
        !(event->modifiers() & (Qt::ControlModifier | Qt::AltModifier | Qt::MetaModifier));
    if (event->key() == Qt::Key_Escape) {
        if (!tool.footprint.isEmpty()) {
            cancelTerrainOperation();
            emit message("Operation canceled");
            event->accept();
            return true;
        }
        return false;
    }
    if (event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter) {
        if (!tool.footprint.isEmpty())
            commitPad();
        else
            holdTerrainStatus("Choose a footprint first: point at a face, group or component and "
                              "click, or type its position as [x, y] or @E, N");
        event->accept();
        return true;
    }
    if (plain && (event->text() == "@" || event->text() == "[")) {
        emit measurementsRequested(event->text());
        event->accept();
        return true;
    }
    return false;
}
bool Viewport::padMeasurements(const QString &text) {
    auto &tool = padTool_;
    const auto unit = inputUnit(doc_.displayUnits());
    const auto locale = QLocale();
    std::optional<std::array<double, 2>> position;
    if (text.startsWith('@')) {
        if (!doc_.site())
            throw std::runtime_error("A leading @ types a site position (E, N), but this document "
                                     "has no site origin. Import a terrain, or set one in File → "
                                     "Site settings….");
        const auto values = parseSitePosition(text.mid(1), unit, locale);
        position = std::array<double, 2>{values[0] - doc_.site()->easting,
                                         values[1] - doc_.site()->northing};
    } else
        position = typedPadPosition(text, unit);
    if (position) {
        // §15.7: a typed position picks the footprint there: a group or component in the open
        // context, or a face of loose geometry, whose footprint holds the position in plan.
        PadFootprint best;
        for (const auto &[id, body] : doc_.bodies()) {
            if (body->terrainTile || body->kind == BodyKind::Terrain ||
                body->kind == BodyKind::ReferenceImage || body->parent != selection_.context())
                continue;
            std::vector<SelectedEntity> candidates;
            if (body->kind == BodyKind::Group)
                candidates.push_back({id, SelectionKind::Body, 0});
            else
                for (const auto &[face, record] : body->surface.faces) {
                    // Only horizontal faces of one loop can be footprints (checked cheaply first).
                    if (record.loops.size() != 1)
                        continue;
                    double low = 1e300, high = -1e300;
                    for (const auto vertex : record.loops.front())
                        if (const auto p = body->surface.vertices.find(vertex);
                            p != body->surface.vertices.end()) {
                            low = std::min(low, p->second.z);
                            high = std::max(high, p->second.z);
                        }
                    if (high - low <= 1e-6)
                        candidates.push_back({id, SelectionKind::Face, face});
                }
            std::erase_if(candidates, [&](const SelectedEntity &entity) {
                return selection_.hidden(doc_, entity) || selection_.locked(doc_, entity.body);
            });
            for (const auto &entity : candidates) {
                PadFootprint footprint;
                if (padFootprintOf(entity, footprint) &&
                    insidePlan(footprint.outline, (*position)[0], (*position)[1]) &&
                    (best.outline.empty() || footprint.z > best.z))
                    best = footprint;
            }
        }
        if (best.outline.empty())
            throw std::runtime_error("No horizontal face, group or component footprint holds that "
                                     "position");
        choosePadFootprint(best);
        return true;
    }
    // A slope ("1:3", "26.6°") or an elevation (a length).
    if (looksLikeTerrainSlope(text)) {
        const double slope = parseTerrainSlope(text);
        if (tool.footprint.isEmpty() && !terrainAmendable().isEmpty()) {
            auto command = terrainAmendable();
            if (command["mode"] != "daylight")
                throw std::runtime_error("A fixed-width pad has no slopes; type an elevation");
            command["cutSlope"] = command["fillSlope"] = slope;
            amendPad(command, "Revised the pad · slopes " + terrainSlopeText(slope));
            return true;
        }
        if (terrainOptions_.padMode == TerrainStampMode::Width)
            throw std::runtime_error("Fixed width has no slopes; type an elevation, or choose "
                                     "Daylight in the strip");
        auto options = terrainOptions_;
        options.cutSlope = options.fillSlope = slope;
        setTerrainOptions(options);
        return true;
    }
    const double value = parseLength(text, unit, locale);
    if (!tool.footprint.isEmpty()) {
        const auto revision = doc_.revision();
        commitPad(value);
        return doc_.revision() != revision;
    }
    if (!terrainAmendable().isEmpty()) {
        auto command = terrainAmendable();
        command["elevation"] = value;
        amendPad(command, "Revised the pad · " + displayLength(value, doc_.displayUnits(), 2));
        return true;
    }
    throw std::runtime_error("Choose a footprint first: point at a face, group or component and "
                             "click, or type its position as [x, y] or @E, N");
}

// ---- Drawing (UX §15.4) ------------------------------------------------------------------------
void Viewport::paintPadTool(QPainter &p) {
    const auto &tool = padTool_;
    const auto project = screenProjector();
    p.save();
    p.setRenderHint(QPainter::Antialiasing);
    const QColor ink = colors_.ink;
    const QColor selection = colors_.selectionStroke.isValid() ? colors_.selectionStroke
                                                               : QColor("#5b5bd6");
    auto polygon = [&](const std::vector<Vec3> &points, bool closed) {
        QPolygonF screen;
        for (const auto &q : points)
            screen << project(q);
        if (closed && !screen.isEmpty())
            screen << screen.front();
        return screen;
    };
    // The hovered footprint (a group's lowest horizontal face), highlighted before the click.
    if (tool.footprint.isEmpty() && !tool.hover.outline.empty()) {
        const auto screen = polygon(tool.hover.outline, true);
        QColor fill = selection;
        fill.setAlphaF(0.18f);
        p.setBrush(fill);
        p.setPen(QPen(Qt::white, 5));
        p.drawPolygon(screen);
        p.setPen(QPen(selection, 2.4));
        p.drawPolygon(screen);
    }
    const auto shown = terrainEditPreviewShown();
    const bool stamp = shown && shown->kind == terrainStampKind && !tool.footprint.isEmpty();
    if (stamp && shown->candidate && shown->pad) {
        const auto &pad = *shown->pad;
        const auto world = shown->world;
        const double h = pad.elevation;
        // Cut brown with a diagonal hatch, fill teal with dots, the pad flat and light: colour is
        // never the only signal (the strip's legend names them).
        const QColor cutColour(214, 165, 120, 150), fillColour(140, 191, 189, 150);
        const QColor cutHatch(120, 72, 32, 200), fillDots(30, 96, 94, 210);
        // The triangles inside the daylight line, classified once per preview: at the pad's
        // elevation the pad, above it cut, below it fill (the cavity's other triangles, outside
        // the daylight line, are ground the stamp re-triangulated, not graded).
        if (padTint_.preview.lock() != shown) {
            padTint_ = {};
            padTint_.preview = shown;
            const auto ring = terrainStampRing(pad);
            auto inside = [&](double x, double y) {
                bool in = false;
                for (std::size_t i = 0, j = ring.size() - 1; i < ring.size(); j = i++)
                    if ((ring[i].y > y) != (ring[j].y > y) &&
                        x < (ring[j].x - ring[i].x) * (y - ring[i].y) / (ring[j].y - ring[i].y) +
                                ring[i].x)
                        in = !in;
                return in;
            };
            for (const auto &t : shown->added) {
                const auto c = (t[0] + t[1] + t[2]) * (1.0 / 3);
                const std::array<Vec3, 3> model{world.point(t[0]), world.point(t[1]),
                                                world.point(t[2])};
                if (t[0].z == h && t[1].z == h && t[2].z == h)
                    padTint_.flat.push_back(model);
                else if (ring.size() < 3 || !inside(c.x, c.y))
                    continue;
                else
                    (c.z > h ? padTint_.cut : padTint_.fill).push_back(model);
            }
        }
        QPainterPath cut, fill, flat;
        cut.setFillRule(Qt::WindingFill);
        fill.setFillRule(Qt::WindingFill);
        flat.setFillRule(Qt::WindingFill);
        // Each label beside its area, apart: cut at its lowest triangle on screen, fill at its
        // leftmost.
        std::optional<QPointF> cutAt, fillAt;
        const std::size_t cuts = padTint_.cut.size(), fills = padTint_.fill.size();
        auto add = [&](QPainterPath &path, const std::array<Vec3, 3> &t) {
            QPolygonF screen;
            for (const auto &q : t)
                screen << project(q);
            path.addPolygon(screen);
            path.closeSubpath();
            return project((t[0] + t[1] + t[2]) * (1.0 / 3));
        };
        for (const auto &t : padTint_.flat)
            add(flat, t);
        for (const auto &t : padTint_.cut)
            if (const auto at = add(cut, t); !cutAt || at.y() > cutAt->y())
                cutAt = at;
        for (const auto &t : padTint_.fill)
            if (const auto at = add(fill, t); !fillAt || at.x() < fillAt->x())
                fillAt = at;
        p.setPen(Qt::NoPen);
        p.setBrush(QColor(255, 255, 255, 110));
        p.drawPath(flat);
        for (const auto &[path, colour, pattern, mark] :
             {std::tuple{&cut, cutColour, Qt::BDiagPattern, cutHatch},
              std::tuple{&fill, fillColour, Qt::Dense6Pattern, fillDots}}) {
            p.setBrush(colour);
            p.drawPath(*path);
            p.setBrush(QBrush(mark, pattern));
            p.drawPath(*path);
        }
        // The rays: thin lines from the outline to the daylight line.
        QColor rayColour = ink;
        rayColour.setAlphaF(0.45f);
        p.setPen(QPen(rayColour, 0.7));
        for (const auto &ray : pad.rays) {
            if (ray.start >= pad.outline.size())
                continue;
            const auto &o = pad.outline[ray.start];
            p.drawLine(project(world.point({o[0], o[1], h})), project(world.point(ray.daylight)));
        }
        // The pad outline and the daylight line, dashed and labelled.
        std::vector<Vec3> outline;
        for (const auto &o : pad.outline)
            outline.push_back(world.point({o[0], o[1], h}));
        p.setBrush(Qt::NoBrush);
        p.setPen(QPen(ink, 1.6));
        p.drawPolyline(polygon(outline, true));
        std::vector<Vec3> daylight;
        for (const auto &q : terrainStampRing(pad))
            daylight.push_back(world.point(q));
        for (const auto &loop : shown->outline) {
            std::vector<Vec3> model;
            for (const auto &q : loop)
                model.push_back(world.point(q));
            const auto screen = polygon(model, true);
            p.setPen(QPen(ink, 6, Qt::SolidLine, Qt::FlatCap, Qt::RoundJoin));
            p.drawPolyline(screen);
            p.setPen(QPen(Qt::white, 3.5, Qt::SolidLine, Qt::FlatCap, Qt::RoundJoin));
            p.drawPolyline(screen);
            QPen hatch(ink, 3.5, Qt::CustomDashLine, Qt::FlatCap, Qt::RoundJoin);
            hatch.setDashPattern({0.6, 0.6});
            p.setPen(hatch);
            p.drawPolyline(screen);
        }
        QPen dashed(ink, 1.4, Qt::CustomDashLine);
        dashed.setDashPattern({5, 3});
        p.setPen(dashed);
        p.drawPolyline(polygon(daylight, true));
        // Labels: the pad, the daylight line, cut and fill with their slopes.
        std::vector<std::pair<QPointF, QStringList>> labels;
        if (!daylight.empty())
            labels.push_back({project(daylight[daylight.size() * 5 / 8]) + QPointF(12, 6),
                              {"Daylight"}});
        const bool width = pad.mode == TerrainStampMode::Width;
        if (cuts && cutAt)
            labels.push_back({*cutAt + QPointF(10, 4),
                              {width ? QString("Cut") : "Cut " + terrainSlopeText(pad.cutSlope)}});
        if (fills && fillAt)
            labels.push_back({*fillAt + QPointF(-80, -10),
                              {width ? QString("Fill") : "Fill " + terrainSlopeText(pad.fillSlope)}});
        for (const auto &[at, lines] : labels) {
            const QFontMetrics metrics(p.font());
            int textWidth = 0;
            for (const auto &line : lines)
                textWidth = std::max(textWidth, metrics.horizontalAdvance(line));
            const QRectF box(at, QSizeF(textWidth + 16, metrics.lineSpacing() * lines.size() + 8));
            p.setPen(QPen(colors_.border, 1));
            p.setBrush(colors_.surface);
            p.drawRoundedRect(box, 3, 3);
            p.setPen(ink);
            for (int i = 0; i < int(lines.size()); ++i)
                p.drawText(QRectF(box.left() + 8, box.top() + 4 + i * metrics.lineSpacing(),
                                  textWidth + 2, metrics.lineSpacing()),
                           Qt::AlignLeft | Qt::AlignVCenter, lines[i]);
        }
    } else if (!tool.footprint.isEmpty()) {
        // The footprint while its first plan runs, or when refused.
        const auto screen = polygon(tool.outline, true);
        p.setBrush(Qt::NoBrush);
        p.setPen(QPen(Qt::white, 5));
        p.drawPolyline(screen);
        p.setPen(QPen(selection, 2.4));
        p.drawPolyline(screen);
    }
    // A refusal: the footprint's nearest corner to the conflict joined to it red and dashed.
    if (stamp && shown->refused() && !tool.outline.empty()) {
        const auto failure = terrainEditFailure(*shown);
        if (failure.at) {
            const QColor red = colors_.failure.isValid() ? colors_.failure : QColor("#a8322b");
            const auto at = project(*failure.at);
            QPointF nearest = project(tool.outline.front());
            for (const auto &q : tool.outline)
                if (QLineF(project(q), at).length() < QLineF(nearest, at).length())
                    nearest = project(q);
            QPen dashed(red, 2.2, Qt::CustomDashLine, Qt::FlatCap);
            dashed.setDashPattern({3, 1.5});
            p.setPen(dashed);
            p.drawLine(nearest, at);
        }
    }
    p.restore();
    paintTerrainEditPreview(p);
    // The pad's label ("Pad 106.00 m / Building A − 0.15 m offset"), or the failure.
    if (tool.footprint.isEmpty())
        return;
    QStringList lines;
    bool failure = false;
    std::optional<QPointF> anchor;
    if (stamp && shown->refused() && terrainEditPreviewCurrent()) {
        const auto refusal = terrainEditFailure(*shown);
        lines << refusal.label << "Change the elevation, slopes or footprint";
        if (refusal.at)
            anchor = project(*refusal.at) + QPointF(16, 14);
        failure = true;
    } else {
        lines << "Pad " + displayLength(padElevation(), doc_.displayUnits(), 2);
        const double offset = terrainOptions_.padOffset - padTool_.delta;
        lines << (std::abs(offset) < 5e-7
                      ? tool.name
                      : tool.name + (offset > 0 ? " − " : " + ") +
                            displayLength(std::abs(offset), doc_.displayUnits(), 2) + " offset");
        Vec3 top{};
        double best = 1e300;
        for (const auto &q : tool.outline) {
            const auto s = project(q);
            if (s.y() < best) {
                best = s.y();
                top = q;
            }
        }
        anchor = project(top) + QPointF(-120, -70);
    }
    if (!anchor)
        return;
    p.save();
    auto font = p.font();
    const QFontMetrics metrics(font);
    auto bold = font;
    bold.setBold(true);
    const QFontMetrics boldMetrics(bold);
    int textWidth = 0;
    for (int i = 0; i < int(lines.size()); ++i)
        textWidth = std::max(textWidth, (i == 0 ? boldMetrics : metrics).horizontalAdvance(lines[i]));
    const int boxWidth = std::min(textWidth + 22, std::max(1, width() - 24));
    const int boxHeight = int(lines.size()) * metrics.lineSpacing() + 10;
    const QRectF box(std::clamp(anchor->x(), 12.0, double(std::max(12, width() - boxWidth - 12))),
                     std::clamp(anchor->y(), 12.0, double(std::max(12, height() - boxHeight - 12))),
                     boxWidth, boxHeight);
    const QColor red = colors_.failure.isValid() ? colors_.failure : QColor("#a8322b");
    p.setRenderHint(QPainter::Antialiasing);
    p.setPen(QPen(failure ? red : colors_.border, failure ? 1.5 : 1));
    p.setBrush(failure ? (colors_.failureSurface.isValid() ? colors_.failureSurface : colors_.surface)
                       : colors_.surface);
    p.drawRoundedRect(box, 4, 4);
    for (int i = 0; i < int(lines.size()); ++i) {
        p.setFont(i == 0 ? bold : font);
        p.setPen(failure ? red : colors_.ink);
        p.drawText(QRectF(box.left() + 10, box.top() + 5 + i * metrics.lineSpacing(),
                          box.width() - 20, metrics.lineSpacing()),
                   Qt::AlignLeft | Qt::AlignVCenter,
                   metrics.elidedText(lines[i], Qt::ElideRight, int(box.width()) - 20));
    }
    p.restore();
}
} // namespace sketchy
