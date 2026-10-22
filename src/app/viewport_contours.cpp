// R096.f2 (UX_DESIGN §15.3, §15.7; ADR 0169 §4.2, §5): the Edit contours tool.
//
// - What is shown: the terrain's current source contours solid at 1.5 px, breaklines dash-dot and
//   spot points as small crosses, over display contours drawn faded. Hovering a source highlights
//   and names it ("Contour 105.00 m · source 17").
// - A click selects a source: selection-coloured, with square handles on its vertices and round
//   handles at its segment midpoints. Dragging a square handle moves the vertex; dragging a round
//   one inserts a vertex there. Tab cycles overlapping sources under the pointer, or, with a source
//   selected, its handles (§15.7).
// - A drag previews the re-triangulated cavity on the worker (latest wins) with the changed region
//   outlined; release commits "Edit contour". A drag the command refuses (crossing another source,
//   leaving the outline, entering a pad) draws the moved run red and dashed with an × at the first
//   conflict and a failure label; the preview is withdrawn and releasing changes nothing.
// - A typed number sets the selected source's elevation; Delete removes the focused vertex and
//   Shift+Delete the source; the strip's Role switches contour and breakline.
// - Add contour, Add breakline and Add spot draw on the surface as the Line tool does (Enter
//   finishes, the first point closes a contour); Promote turns the display contour under the
//   pointer into a source at its level.
#include "app/terrain_actions.hpp"
#include "app/unit_display.hpp"
#include "app/viewport.hpp"
#include "automation/measurements.hpp"
#include "core/terrain_sources.hpp"
#include <QKeyEvent>
#include <QLineF>
#include <QLocale>
#include <QMouseEvent>
#include <QPainter>
#include <algorithm>
#include <cmath>
#include <limits>
namespace sketchy {
namespace {
constexpr double pickRadius = 6;     // logical pixels to a source line or point
constexpr double handleRadius = 7;   // to a handle
constexpr std::size_t crossLimit = 2000; // spot points in view drawn as crosses, at most
bool finite(const Vec3 &p) { return std::isfinite(p.x) && std::isfinite(p.y) && std::isfinite(p.z); }
double segmentDistance(QPointF p, QPointF a, QPointF b) {
    const QPointF d = b - a;
    const double length2 = d.x() * d.x() + d.y() * d.y();
    double t = length2 > 0 ? QPointF::dotProduct(p - a, d) / length2 : 0;
    t = std::clamp(t, 0.0, 1.0);
    return QLineF(p, a + d * t).length();
}
// "[x, y]" or "[x, y, z]" in model coordinates.
std::optional<std::array<double, 2>> typedPosition(const QString &text, const QString &unit) {
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
QJsonArray planPoint(const Vec3 &p) { return QJsonArray{p.x, p.y}; }
QJsonArray modelPoint(const Vec3 &p) { return QJsonArray{p.x, p.y, p.z}; }
} // namespace

// ---- The sources shown -------------------------------------------------------------------------
const Viewport::ContourSources &Viewport::currentContourSources() const {
    const auto terrain = contourTool_.terrain;
    if (!isTerrain(doc_, terrain)) {
        contourSources_ = {};
        return contourSources_;
    }
    const auto &body = doc_.bodies().at(terrain);
    if (contourSources_.body == body && contourSources_.state)
        return contourSources_;
    contourSources_ = {};
    contourSources_.body = body;
    try {
        contourSources_.state = terrainSourceState(*body->terrain);
    } catch (const std::exception &) {
        return contourSources_;
    }
    const auto world = doc_.worldTransform(terrain);
    const auto &state = *contourSources_.state;
    for (const auto *source : state.sources()) {
        if (source->role == TerrainSourceRole::Boundary || source->role == TerrainSourceRole::Hole ||
            source->points.empty())
            continue;
        ContourSource shown;
        shown.id = source->id;
        shown.role = source->role;
        shown.points.reserve(source->points.size());
        for (const auto &p : source->points)
            shown.points.push_back(world.point(p));
        shown.closed = source->role != TerrainSourceRole::Points && source->points.size() >= 4 &&
                       source->points.front() == source->points.back();
        shown.pad = state.isPadSource(source->id);
        shown.drape = state.isDrapeSource(source->id);
        shown.owner = shown.pad ? state.padSourceOwner(source->id)
                                : state.drapeSourceOwner(source->id);
        contourSources_.sources.push_back(std::move(shown));
    }
    return contourSources_;
}
const Viewport::ContourSource *Viewport::contourSource(std::uint32_t id) const {
    if (!id)
        return nullptr;
    for (const auto &source : currentContourSources().sources)
        if (source.id == id)
            return &source;
    return nullptr;
}
QString Viewport::contourSourceLabel(const ContourSource &source) const {
    const auto units = doc_.displayUnits();
    if (source.pad)
        return QString("Pad outline (change #%1) · source %2").arg(source.owner).arg(source.id);
    if (source.drape)
        return QString("Draped line (change #%1) · source %2").arg(source.owner).arg(source.id);
    switch (source.role) {
    case TerrainSourceRole::Contour:
        return QString("Contour %1 · source %2")
            .arg(displayLength(source.points.front().z, units, 2))
            .arg(source.id);
    case TerrainSourceRole::Breakline:
        return QString("Breakline · source %1").arg(source.id);
    case TerrainSourceRole::Points:
        return QString("Spot points (%1) · source %2")
            .arg(QLocale().toString(qulonglong(source.points.size())))
            .arg(source.id);
    default:
        return QString("Source %1").arg(source.id);
    }
}
// The sources under the pointer, nearest first.
std::vector<std::uint32_t> Viewport::contourSourcesAt(QPointF point) const {
    const auto project = screenProjector();
    std::vector<std::pair<double, std::uint32_t>> found;
    for (const auto &source : currentContourSources().sources) {
        double best = std::numeric_limits<double>::infinity();
        if (source.role == TerrainSourceRole::Points) {
            for (const auto &p : source.points)
                best = std::min(best, QLineF(project(p), point).length());
        } else {
            QPointF previous = project(source.points.front());
            for (std::size_t i = 1; i < source.points.size(); ++i) {
                const QPointF next = project(source.points[i]);
                best = std::min(best, segmentDistance(point, previous, next));
                previous = next;
            }
        }
        if (best <= pickRadius)
            found.emplace_back(best, source.id);
    }
    std::sort(found.begin(), found.end());
    std::vector<std::uint32_t> ids;
    for (const auto &[distance, id] : found)
        ids.push_back(id);
    return ids;
}
// The selected source's handles in order: vertex i at 2 i, the midpoint after it at 2 i + 1 (NaN
// where there is none: an open line's end, every spot point). A closed ring's repeated start is
// not a vertex of its own.
std::vector<Vec3> Viewport::contourHandles(const ContourSource &source) const {
    auto points = contourTool_.dragging && !contourTool_.dragPoints.empty() &&
                          contourTool_.selected == source.id
                      ? contourTool_.dragPoints
                      : source.points;
    if (source.closed && contourTool_.dragPoints.empty() && points.size() > 1)
        points.pop_back();
    const bool lines = source.role != TerrainSourceRole::Points;
    const Vec3 none{std::numeric_limits<double>::quiet_NaN(), 0, 0};
    std::vector<Vec3> handles;
    for (std::size_t i = 0; i < points.size(); ++i) {
        handles.push_back(points[i]);
        const bool last = i + 1 == points.size();
        if (!lines || (last && !source.closed))
            handles.push_back(none);
        else {
            const auto &b = points[last ? 0 : i + 1];
            handles.push_back((points[i] + b) * 0.5);
        }
    }
    return handles;
}
int Viewport::contourHandleAt(QPointF point) const {
    const auto *source = contourSource(contourTool_.selected);
    if (!source)
        return -1;
    const auto handles = contourHandles(*source);
    int best = -1;
    double bestDistance = handleRadius;
    // Vertices before midpoints: a vertex wins a tie.
    for (int pass = 0; pass < 2; ++pass)
        for (std::size_t i = std::size_t(pass); i < handles.size(); i += 2) {
            if (!finite(handles[i]))
                continue;
            const double d = QLineF(project(handles[i]), point).length();
            if (d < bestDistance - (pass ? 0.5 : 0)) {
                bestDistance = d;
                best = int(i);
            }
        }
    return best;
}
// The point under the pointer on the horizontal plane at `z` (model), where a dragged vertex keeps
// its elevation.
std::optional<Vec3> Viewport::contourPlanePoint(QPointF point, double z) const {
    const auto [origin, direction] = ray(point);
    if (std::abs(direction.z) < 1e-9)
        return std::nullopt;
    const double t = (z - origin.z) / direction.z;
    if (!(t > 0))
        return std::nullopt;
    auto p = origin + direction * t;
    p.z = z;
    return p;
}

// ---- Commands ----------------------------------------------------------------------------------
// terrain.edit_source with the source's new points (model): a contour's as [x, y] (it keeps its
// one elevation), a breakline's and spot points' as [x, y, z]; a closed ring repeats its start.
QJsonObject Viewport::contourPointsCommand(const ContourSource &source,
                                           const std::vector<Vec3> &input) const {
    auto points = input;
    if (source.closed && !points.empty() && points.front() != points.back())
        points.push_back(points.front());
    QJsonArray list;
    for (const auto &p : points)
        list.append(source.role == TerrainSourceRole::Contour ? planPoint(p) : modelPoint(p));
    return {{"command", "terrain.edit_source"},
            {"terrain", QString::number(contourTool_.terrain)},
            {"source", double(source.id)},
            {"points", list}};
}
QJsonObject Viewport::contourAddCommand() const {
    const auto &tool = contourTool_;
    QJsonObject command{{"command", "terrain.add_source"},
                        {"terrain", QString::number(tool.terrain)}};
    if (tool.mode == ContourMode::Promote) {
        command["level"] = tool.promoteLevel.value_or(0);
        if (tool.pointer)
            command["at"] = planPoint(*tool.pointer);
        return command;
    }
    QJsonArray list;
    if (tool.mode == ContourMode::AddContour) {
        command["role"] = "contour";
        command["elevation"] = tool.addElevation.value_or(
            tool.adding.empty() ? 0.0 : tool.adding.front().z);
        for (const auto &p : tool.adding)
            list.append(planPoint(p));
    } else if (tool.mode == ContourMode::AddBreakline) {
        command["role"] = "breakline";
        for (const auto &p : tool.adding)
            list.append(modelPoint(p));
    } else {
        command["role"] = "points";
        for (auto p : tool.adding) {
            if (tool.addElevation)
                p.z = *tool.addElevation;
            list.append(modelPoint(p));
        }
    }
    command["points"] = list;
    return command;
}

// ---- Tool state --------------------------------------------------------------------------------
void Viewport::beginContourTool() {
    brush_ = {};
    terrainCandidate_.reset();
    terrainCandidateShown_ = false;
    terrainStatusShown_.clear();
    const auto mode = contourTool_.mode;
    contourTool_ = {};
    contourTool_.mode = mode;
    contourSources_ = {};
    withdrawTerrainEditPreview();
    terrainPreviewDirty_ = true;
    // The selected terrain, else the first terrain in the open context, shows its sources.
    contourTool_.terrain = selectedTerrain(doc_, selection_);
    if (!contourTool_.terrain)
        for (const auto &[id, body] : doc_.bodies())
            if (body->kind == BodyKind::Terrain && selection_.inActiveHierarchy(doc_, id)) {
                contourTool_.terrain = id;
                break;
            }
    if (!doc_.materializedTotals().terrainTriangles)
        contourTool_.refusal = "No terrain in this model. Import one with File → Import terrain…";
    emitTerrainStatus();
    emit terrainToolChanged();
    update();
}
void Viewport::cancelContourOperation() {
    auto &tool = contourTool_;
    const bool was = tool.pressed || tool.dragging || !tool.adding.empty() ||
                     terrainEditShown_ || !terrainEditRequest_.command.isEmpty();
    if (tool.pressed || tool.dragging || !tool.adding.empty())
        terrainSession_.cancel(); // the operation ends
    tool.pressed = tool.dragging = false;
    tool.pressedHandle = tool.dragVertex = -1;
    tool.dragPoints.clear();
    tool.adding.clear();
    tool.addElevation.reset();
    tool.promoteLevel.reset();
    withdrawTerrainEditPreview();
    if (was && tool_ == Tool::Contours) {
        emit measurementPreview({});
        emitTerrainStatus();
        emit terrainToolChanged();
    }
}
void Viewport::setContourMode(ContourMode mode) {
    if (tool_ != Tool::Contours) {
        contourTool_.mode = mode;
        return;
    }
    cancelContourOperation();
    contourTool_.mode = contourTool_.mode == mode && mode != ContourMode::Edit ? ContourMode::Edit
                                                                               : mode;
    if (contourTool_.mode != ContourMode::Edit)
        contourTool_.focus = -1;
    terrainStatusShown_.clear();
    emitTerrainStatus();
    emit terrainToolChanged();
    update();
}
void Viewport::selectTerrainSource(Id terrain, std::uint32_t source) {
    if (terrain != contourTool_.terrain) {
        cancelContourOperation();
        contourTool_.terrain = terrain;
        contourSources_ = {};
    }
    contourTool_.selected = contourSource(source) ? source : 0;
    contourTool_.focus = -1;
    terrainStatusShown_.clear();
    emitTerrainStatus();
    emit terrainToolChanged();
    update();
}
Viewport::ContourToolView Viewport::contourTool() const {
    ContourToolView view;
    const auto &tool = contourTool_;
    view.mode = tool.mode;
    view.terrain = tool.terrain;
    view.hovered = tool.hovered;
    view.selected = tool.selected;
    view.sources = currentContourSources().sources.size();
    view.focus = tool.focus;
    view.dragging = tool.dragging;
    view.adding = tool.adding;
    view.promoteLevel = tool.promoteLevel;
    if (const auto *hovered = contourSource(tool.hovered))
        view.hoverLabel = contourSourceLabel(*hovered);
    if (const auto *source = contourSource(tool.selected)) {
        view.role = source->role;
        view.editable = !source->pad && !source->drape;
        const auto units = doc_.displayUnits();
        view.selectedName = source->pad                                ? QString("Pad outline")
                            : source->drape                            ? QString("Draped line")
                            : source->role == TerrainSourceRole::Contour
                                ? "Contour " + displayLength(source->points.front().z, units, 2)
                            : source->role == TerrainSourceRole::Breakline ? QString("Breakline")
                                                                           : QString("Spot points");
        view.selectedDetail =
            source->pad || source->drape
                ? QString("change #%1 · edit it in Terrain edits").arg(source->owner)
                : QString("source %1").arg(source->id);
        if (source->role == TerrainSourceRole::Contour)
            view.elevation = source->points.front().z;
        view.handles = 0;
        for (const auto &h : contourHandles(*source))
            view.handles += finite(h);
    }
    if (const auto shown = terrainEditPreviewShown()) {
        view.previewShown = bool(shown->candidate);
        view.refused = shown->refused();
        if (view.refused) {
            const auto failure = terrainEditFailure(*shown);
            view.failure = failure.label;
            view.failureAt = failure.at;
        }
    }
    return view;
}
QString Viewport::contourMeasurementLabel() const {
    const auto &tool = contourTool_;
    switch (tool.mode) {
    case ContourMode::AddContour:
    case ContourMode::AddSpot:
        return "Elevation";
    case ContourMode::AddBreakline:
        return "Point";
    case ContourMode::Promote:
        return "Position";
    case ContourMode::Edit:
        break;
    }
    const auto *source = contourSource(tool.selected);
    if (!source)
        return "Position";
    // A number sets the elevation; a typed position moves the focused handle.
    return source->role == TerrainSourceRole::Points ? QString("Position") : QString("Elevation");
}
void Viewport::emitContourStatus() {
    const auto &tool = contourTool_;
    if (terrainStatusHeld_)
        return; // a refusal or result stays until the pointer moves or a key acts
    QString text;
    const auto shown = terrainEditPreviewShown();
    const bool refused = shown && shown->refused() && terrainEditPreviewCurrent();
    terrainFailureStatus_.clear();
    if (!tool.refusal.isEmpty())
        text = tool.refusal;
    else if (tool.dragging && refused)
        text = terrainEditFailure(*shown).status + " Release to cancel, or move back.";
    else if (tool.dragging)
        text = "Edit contours · Release to apply · Esc cancels";
    else if (tool.mode == ContourMode::AddContour)
        text = tool.adding.empty()
                   ? "Add contour · Click points on the terrain, or type [x, y] · Type an elevation "
                     "first, or the first point's is used · Esc cancels"
                   : "Add contour · Click the next point · Enter finishes · Click the first point to "
                     "close it · Backspace removes the last · Esc cancels";
    else if (tool.mode == ContourMode::AddBreakline)
        text = tool.adding.empty()
                   ? "Add breakline · Click points on the terrain, or type [x, y] · Esc cancels"
                   : "Add breakline · Click the next point · Enter finishes · Backspace removes the "
                     "last · Esc cancels";
    else if (tool.mode == ContourMode::AddSpot)
        text = tool.adding.empty()
                   ? "Add spot · Click the terrain, or type [x, y] · Esc cancels"
                   : "Add spot · Type its elevation, or press Enter for the surface's · Esc cancels";
    else if (tool.mode == ContourMode::Promote)
        text = tool.promoteLevel
                   ? QString("Promote · Click to make the display contour at %1 a source contour · "
                             "Esc cancels")
                         .arg(displayLength(*tool.promoteLevel, doc_.displayUnits(), 2))
                   : QString("Promote · Point at a display contour on the terrain");
    else if (const auto *source = contourSource(tool.selected)) {
        const auto view = contourTool();
        text = view.selectedName + " · " + view.selectedDetail;
        if (view.editable)
            text += source->role == TerrainSourceRole::Points
                        ? " · Drag a cross to move a point · Delete removes the focused point · "
                          "Shift+Delete removes the source · Tab moves between points"
                        : " · Drag a square handle to move a vertex, a round one to insert one · "
                          "Type an elevation · Delete removes the focused vertex · Shift+Delete "
                          "removes the source · Tab moves between handles";
    } else if (const auto *hovered = contourSource(tool.hovered))
        text = contourSourceLabel(*hovered) + " · Click to select it" +
               (tool.candidates.size() > 1 ? QString(" · Tab chooses another under the pointer")
                                           : QString());
    else
        text = "Edit contours · Click a source contour, breakline or spot point to select it · Add "
               "and Promote are in the strip · Esc leaves";
    if (!terrainEditRequest_.error.isEmpty() && (tool.dragging || !tool.adding.empty() ||
                                                 tool.mode == ContourMode::Promote))
        text = "Nothing would change: " + terrainEditRequest_.error;
    terrainFailureStatus_ = tool.dragging && refused ? text : QString();
    if (text == terrainStatusShown_)
        return;
    terrainStatusShown_ = text;
    emit message(text);
}

// ---- Pointer -----------------------------------------------------------------------------------
void Viewport::contourHover(QPointF point) {
    auto &tool = contourTool_;
    tool.screen = point;
    terrainStatusHeld_ = false;
    const auto hit = terrainHit(point);
    tool.refusal.clear();
    if (!doc_.materializedTotals().terrainTriangles)
        tool.refusal = "No terrain in this model. Import one with File → Import terrain…";
    if (hit && hit->first != tool.terrain && !tool.dragging && tool.adding.empty()) {
        // Another terrain under the pointer: its sources are shown instead.
        tool.terrain = hit->first;
        tool.selected = tool.hovered = 0;
        tool.focus = -1;
        contourSources_ = {};
    }
    tool.pointer = hit && hit->first == tool.terrain ? std::optional(hit->second) : std::nullopt;
    if (tool.dragging) {
        const auto *source = contourSource(tool.selected);
        if (source && tool.dragVertex >= 0 && std::size_t(tool.dragVertex) < tool.dragPoints.size()) {
            auto &vertex = tool.dragPoints[std::size_t(tool.dragVertex)];
            if (const auto at = contourPlanePoint(point, vertex.z)) {
                // The vertex keeps its own elevation (a contour's is its level).
                vertex.x = at->x;
                vertex.y = at->y;
                requestTerrainEditPreview(contourPointsCommand(*source, tool.dragPoints));
            }
        }
        emitTerrainStatus();
        updateAfterTerrainEdit();
        return;
    }
    if (tool.mode == ContourMode::Edit) {
        tool.candidates = contourSourcesAt(point);
        if (std::find(tool.candidates.begin(), tool.candidates.end(), tool.hovered) ==
            tool.candidates.end())
            tool.hovered = tool.candidates.empty() ? 0 : tool.candidates.front();
    } else if (tool.mode == ContourMode::Promote) {
        // The display contour level nearest the surface under the pointer.
        if (tool.pointer && isTerrain(doc_, tool.terrain)) {
            const auto &record = *doc_.bodies().at(tool.terrain)->terrain;
            const double interval = record.display.contourInterval;
            const double offset = terrainElevationOffset(doc_, tool.terrain);
            const double worldZ = doc_.worldTransform(tool.terrain).point({0, 0, 0}).z;
            // Site elevation e = z(model) - worldZ + offset; levels are whole multiples of the
            // interval; back to model Z.
            const double e = tool.pointer->z - worldZ + offset;
            const double level = std::round(e / interval) * interval - offset + worldZ;
            // Every move asks again (latest wins): the line nearest the pointer is the one
            // shown, and the one a click commits.
            tool.promoteLevel = level;
            tool.promoteAt = point;
            requestTerrainEditPreview(contourAddCommand());
        }
    }
    // R095.e1's hover pattern: off a terrain (and off its sources) the cursor is not-allowed and
    // the status bar names what to point at.
    if (tool.refusal.isEmpty() && !hit && !tool.hovered && tool.adding.empty())
        tool.refusal = "Point at a terrain to edit its contours";
    setCursor(tool.refusal.isEmpty() ? Qt::CrossCursor : Qt::ForbiddenCursor);
    emitTerrainStatus();
    updateAfterTerrainEdit();
}
bool Viewport::contourPress(QMouseEvent *event) {
    auto &tool = contourTool_;
    terrainStatusHeld_ = false;
    const auto point = event->position();
    if (!tool.refusal.isEmpty() && tool.refusal.startsWith("No terrain")) {
        emit message(tool.refusal);
        return true;
    }
    if (tool.mode != ContourMode::Edit) {
        const auto hit = terrainHit(point);
        if (tool.mode == ContourMode::Promote) {
            if (!hit) {
                emit message("Point at a display contour on the terrain");
                return true;
            }
            contourHover(point);
            if (const auto shown = terrainEditPreviewShown();
                shown && shown->refused() && terrainEditPreviewCurrent()) {
                emit message("Nothing changed: " + terrainEditFailure(*shown).status);
                return true;
            }
            const auto command = contourAddCommand();
            if (commitTerrainEdit(command, "Promoted a display contour to a source contour"))
                if (const auto &record = *doc_.bodies().at(tool.terrain)->terrain;
                    !record.edits.empty())
                    if (const auto *data = dynamic_cast<const TerrainSourceEditData *>(
                            record.edits.back()->data.get())) {
                        tool.mode = ContourMode::Edit;
                        selectTerrainSource(tool.terrain, data->source);
                    }
            return true;
        }
        if (!hit || (hit->first != tool.terrain && !tool.adding.empty())) {
            emit message("Point at the terrain to add points");
            return true;
        }
        if (tool.adding.empty()) {
            terrainSession_.begin();
            if (hit->first != tool.terrain) {
                tool.terrain = hit->first;
                tool.selected = tool.hovered = 0;
                tool.focus = -1;
                contourSources_ = {};
            }
        }
        auto p = hit->second;
        if (tool.mode == ContourMode::AddSpot) {
            tool.adding = {p};
            emit measurementPreview(displayLength(tool.addElevation.value_or(p.z),
                                                  doc_.displayUnits(), doc_.displayPrecision()));
            requestTerrainEditPreview(contourAddCommand());
        } else if (tool.mode == ContourMode::AddContour && tool.adding.size() >= 3 &&
                   QLineF(project(tool.adding.front()), point).length() <= 8) {
            finishContourAdd(true);
            return true;
        } else {
            tool.adding.push_back(p);
            if (tool.adding.size() >= 2)
                requestTerrainEditPreview(contourAddCommand());
        }
        emitTerrainStatus();
        emit terrainToolChanged();
        update();
        return true;
    }
    // Edit: a handle of the selected source starts a drag; a source selects; elsewhere clears.
    const int handle = contourHandleAt(point);
    if (handle >= 0) {
        tool.pressed = true;
        tool.pressedHandle = handle;
        tool.pressPosition = point;
        terrainSession_.begin();
        return true;
    }
    const auto under = contourSourcesAt(point);
    if (!under.empty()) {
        const auto chosen = std::find(under.begin(), under.end(), tool.hovered) != under.end()
                                ? tool.hovered
                                : under.front();
        tool.selected = chosen;
        tool.focus = -1;
    } else
        tool.selected = 0;
    terrainStatusShown_.clear();
    emitTerrainStatus();
    emit terrainToolChanged();
    update();
    return true;
}
bool Viewport::contourMove(QMouseEvent *event) {
    auto &tool = contourTool_;
    if (tool.pressed && !tool.dragging && event->buttons().testFlag(Qt::LeftButton) &&
        (event->position() - tool.pressPosition).manhattanLength() >= 4) {
        const auto *source = contourSource(tool.selected);
        if (source) {
            tool.dragging = true;
            tool.dragPoints = source->points;
            if (source->closed && tool.dragPoints.size() > 1)
                tool.dragPoints.pop_back();
            const int index = tool.pressedHandle / 2;
            if (tool.pressedHandle % 2) {
                // A round handle inserts a vertex after vertex `index`, at the midpoint.
                const auto handles = contourHandles(*source);
                tool.dragPoints.insert(tool.dragPoints.begin() + index + 1,
                                       handles[std::size_t(tool.pressedHandle)]);
                tool.dragVertex = index + 1;
            } else
                tool.dragVertex = index;
            tool.focus = 2 * tool.dragVertex;
            emit terrainToolChanged();
        }
    }
    contourHover(event->position());
    return true;
}
bool Viewport::contourRelease(QMouseEvent *event) {
    auto &tool = contourTool_;
    if (!tool.pressed)
        return true;
    tool.pressed = false;
    if (!tool.dragging) {
        // A click on a handle focuses it (Delete removes a focused vertex; a typed position moves
        // it).
        tool.focus = tool.pressedHandle;
        tool.pressedHandle = -1;
        terrainSession_.cancel(); // no operation is in progress
        terrainStatusShown_.clear();
        emitTerrainStatus();
        emit terrainToolChanged();
        update();
        return true;
    }
    contourHover(event->position());
    const auto *source = contourSource(tool.selected);
    const auto command = source ? contourPointsCommand(*source, tool.dragPoints) : QJsonObject{};
    const auto shown = terrainEditPreviewShown();
    const bool refused = shown && shown->refused() && shown->command == command;
    const auto vertex = tool.dragVertex;
    tool.dragging = false;
    tool.dragPoints.clear();
    tool.dragVertex = tool.pressedHandle = -1;
    if (!source || refused) {
        // A refused drag changes nothing (UX §15.3): the preview is withdrawn.
        const auto status = refused ? terrainEditFailure(*shown).status : QString();
        withdrawTerrainEditPreview();
        terrainSession_.cancel();
        holdTerrainStatus(refused ? "Nothing changed: " + status : QString("Nothing changed"));
        emit terrainToolChanged();
        update();
        return true;
    }
    const auto selected = tool.selected;
    if (commitTerrainEdit(command, "Edited the contour")) {
        tool.selected = contourSource(selected) ? selected : 0;
        tool.focus = tool.selected ? 2 * vertex : -1;
    } else
        withdrawTerrainEditPreview();
    emitTerrainStatus();
    emit terrainToolChanged();
    update();
    return true;
}
void Viewport::finishContourAdd(bool close) {
    auto &tool = contourTool_;
    if (close && tool.adding.size() >= 3)
        tool.adding.push_back(tool.adding.front());
    const auto minimum = tool.mode == ContourMode::AddSpot ? 1u : 2u;
    if (tool.adding.size() < minimum) {
        emit message(tool.mode == ContourMode::AddSpot ? "Click the terrain to place a spot first"
                                                       : "Click at least two points first");
        return;
    }
    const auto command = contourAddCommand();
    const auto shown = terrainEditPreviewShown();
    if (shown && shown->refused() && shown->command == command) {
        emit message("Nothing changed: " + terrainEditFailure(*shown).status);
        if (close)
            tool.adding.pop_back();
        return;
    }
    const auto terrain = tool.terrain;
    const auto mode = tool.mode;
    const QString done = mode == ContourMode::AddContour     ? "Added a contour"
                         : mode == ContourMode::AddBreakline ? "Added a breakline"
                                                             : "Added a spot point";
    if (!commitTerrainEdit(command, done)) {
        if (close)
            tool.adding.pop_back();
        return;
    }
    tool.adding.clear();
    tool.addElevation.reset();
    // The new source is selected, so a typed elevation adjusts it (spot points stay in Add spot).
    if (const auto &record = *doc_.bodies().at(terrain)->terrain; !record.edits.empty())
        if (const auto *data =
                dynamic_cast<const TerrainSourceEditData *>(record.edits.back()->data.get())) {
            if (mode != ContourMode::AddSpot)
                tool.mode = ContourMode::Edit;
            selectTerrainSource(terrain, data->source);
        }
    emitTerrainStatus();
    emit terrainToolChanged();
}
bool Viewport::contourKey(QKeyEvent *event) {
    auto &tool = contourTool_;
    terrainStatusHeld_ = false;
    const auto modifiers = event->modifiers();
    const bool plain = !(modifiers & (Qt::ControlModifier | Qt::AltModifier | Qt::MetaModifier));
    const auto *source = contourSource(tool.selected);
    if (event->key() == Qt::Key_Tab || event->key() == Qt::Key_Backtab) {
        const bool back = event->key() == Qt::Key_Backtab || modifiers.testFlag(Qt::ShiftModifier);
        if (source && tool.mode == ContourMode::Edit && !tool.dragging) {
            // §15.7: Tab moves between the selected source's handles.
            const auto handles = contourHandles(*source);
            const int count = int(handles.size());
            int next = tool.focus;
            for (int step = 0; step < count; ++step) {
                next = back ? (next <= 0 ? count - 1 : next - 1) : (next + 1) % count;
                if (finite(handles[std::size_t(next)]))
                    break;
            }
            tool.focus = next;
            const bool midpoint = next % 2;
            emit message(QString("%1 %2 of %3 · Type [x, y] or @E, N to %4 · Delete removes a "
                                 "vertex")
                             .arg(midpoint ? "Midpoint" : "Vertex")
                             .arg(next / 2 + 1)
                             .arg((count + 1) / 2)
                             .arg(midpoint ? "insert a vertex there" : "move it"));
            terrainStatusShown_.clear();
            emit terrainToolChanged();
            update();
            event->accept();
            return true;
        }
        if (tool.candidates.size() > 1) {
            // §4.2: Tab cycles the overlapping sources under the pointer.
            auto at = std::find(tool.candidates.begin(), tool.candidates.end(), tool.hovered);
            const auto index = at == tool.candidates.end() ? 0 : at - tool.candidates.begin();
            const auto size = std::ptrdiff_t(tool.candidates.size());
            tool.hovered = tool.candidates[std::size_t((index + (back ? size - 1 : 1)) % size)];
            terrainStatusShown_.clear();
            emitTerrainStatus();
            update();
            event->accept();
            return true;
        }
        return false;
    }
    if (event->key() == Qt::Key_Escape) {
        if (tool.pressed || tool.dragging || !tool.adding.empty() ||
            (tool.mode == ContourMode::Promote && terrainEditShown_)) {
            cancelContourOperation();
            emit message("Operation canceled");
            event->accept();
            return true;
        }
        if (tool.mode != ContourMode::Edit) {
            setContourMode(ContourMode::Edit);
            event->accept();
            return true;
        }
        if (tool.selected) {
            tool.selected = 0;
            tool.focus = -1;
            terrainStatusShown_.clear();
            emitTerrainStatus();
            emit terrainToolChanged();
            update();
            event->accept();
            return true;
        }
        return false; // Leaves the tool.
    }
    if (event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter) {
        if (tool.mode == ContourMode::AddContour || tool.mode == ContourMode::AddBreakline ||
            tool.mode == ContourMode::AddSpot)
            finishContourAdd(false);
        else if (tool.mode == ContourMode::Promote && tool.promoteLevel) {
            const auto command = contourAddCommand();
            if (commitTerrainEdit(command, "Promoted a display contour to a source contour"))
                if (const auto &record = *doc_.bodies().at(tool.terrain)->terrain;
                    !record.edits.empty())
                    if (const auto *data = dynamic_cast<const TerrainSourceEditData *>(
                            record.edits.back()->data.get())) {
                        tool.mode = ContourMode::Edit;
                        selectTerrainSource(tool.terrain, data->source);
                    }
        }
        event->accept();
        return true;
    }
    if (event->key() == Qt::Key_Backspace && !tool.adding.empty()) {
        tool.adding.pop_back();
        if (tool.adding.size() >= 2)
            requestTerrainEditPreview(contourAddCommand());
        else
            withdrawTerrainEditPreview();
        emitTerrainStatus();
        emit terrainToolChanged();
        update();
        event->accept();
        return true;
    }
    if (event->key() == Qt::Key_Delete && (tool.pressed || tool.dragging || !source ||
                                           tool.mode != ContourMode::Edit)) {
        // Taken from the window's Delete (the model selection is the terrain): nothing to remove.
        if (!tool.pressed && !tool.dragging && tool.mode == ContourMode::Edit)
            holdTerrainStatus("Select a source contour, breakline or spot point first");
        event->accept();
        return true;
    }
    if (event->key() == Qt::Key_Delete && source && tool.mode == ContourMode::Edit) {
        if (modifiers.testFlag(Qt::ShiftModifier))
            removeSelectedSource();
        else if (tool.focus >= 0 && tool.focus % 2 == 0) {
            auto points = source->points;
            if (source->closed && points.size() > 1)
                points.pop_back();
            const auto index = std::size_t(tool.focus / 2);
            if (index < points.size()) {
                points.erase(points.begin() + std::ptrdiff_t(index));
                const auto selected = tool.selected;
                if (commitTerrainEdit(contourPointsCommand(*source, points), "Removed the vertex")) {
                    tool.selected = contourSource(selected) ? selected : 0;
                    tool.focus = -1;
                }
            }
        } else
            holdTerrainStatus("Focus a vertex first (click a square handle or press Tab); "
                              "Shift+Delete removes the source");
        emitTerrainStatus();
        emit terrainToolChanged();
        update();
        event->accept();
        return true;
    }
    // '@' opens the Measurements box for a site position, '[' for a model one.
    if (plain && (event->text() == "@" || event->text() == "[")) {
        emit measurementsRequested(event->text());
        event->accept();
        return true;
    }
    return false;
}

// ---- Strip actions -----------------------------------------------------------------------------
void Viewport::setSelectedSourceRole(TerrainSourceRole role) {
    const auto *source = contourSource(contourTool_.selected);
    if (!source || source->role == role || source->role == TerrainSourceRole::Points)
        return;
    const auto selected = contourTool_.selected;
    commitTerrainEdit({{"command", "terrain.edit_source"},
                       {"terrain", QString::number(contourTool_.terrain)},
                       {"source", double(selected)},
                       {"role", role == TerrainSourceRole::Contour ? "contour" : "breakline"}},
                      role == TerrainSourceRole::Contour ? "The breakline is a contour now"
                                                         : "The contour is a breakline now");
    contourTool_.selected = contourSource(selected) ? selected : 0;
    emitTerrainStatus();
    emit terrainToolChanged();
    update();
}
void Viewport::setSelectedSourceElevation(double elevation) {
    const auto *source = contourSource(contourTool_.selected);
    if (!source)
        throw std::runtime_error("Select a source contour or breakline first");
    if (source->role == TerrainSourceRole::Points)
        throw std::runtime_error("Spot points keep their own elevations; move or remove them");
    const auto selected = contourTool_.selected;
    QString error;
    const bool done = commitTerrainEdit(
        {{"command", "terrain.edit_source"},
         {"terrain", QString::number(contourTool_.terrain)},
         {"source", double(selected)},
         {"elevation", elevation}},
        "Set the elevation to " +
            displayLength(elevation, doc_.displayUnits(), doc_.displayPrecision()),
        &error);
    contourTool_.selected = contourSource(selected) ? selected : 0;
    emit terrainToolChanged();
    update();
    if (!done)
        throw std::runtime_error("Nothing changed: " + error.toStdString());
}
void Viewport::removeSelectedSource() {
    if (!contourSource(contourTool_.selected)) {
        emit message("Select a source first");
        return;
    }
    if (commitTerrainEdit({{"command", "terrain.remove_source"},
                           {"terrain", QString::number(contourTool_.terrain)},
                           {"source", double(contourTool_.selected)}},
                          "Removed the source")) {
        contourTool_.selected = 0;
        contourTool_.focus = -1;
    }
    emitTerrainStatus();
    emit terrainToolChanged();
    update();
}

// ---- Measurements box (UX §15.3, §15.7) --------------------------------------------------------
bool Viewport::contourMeasurements(const QString &text) {
    auto &tool = contourTool_;
    if (tool.pressed || tool.dragging)
        throw std::runtime_error("Release the drag first");
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
        position = typedPosition(text, unit);
    if (position) {
        const auto [x, y] = *position;
        const auto *source = contourSource(tool.selected);
        if (tool.mode == ContourMode::Edit && source && tool.focus >= 0) {
            // A typed position moves the focused handle (a midpoint inserts a vertex there).
            auto points = source->points;
            if (source->closed && points.size() > 1)
                points.pop_back();
            const auto index = std::size_t(tool.focus / 2);
            if (index >= points.size())
                throw std::runtime_error("Choose a handle with Tab first");
            Vec3 moved = tool.focus % 2 ? contourHandles(*source)[std::size_t(tool.focus)]
                                        : points[index];
            moved.x = x;
            moved.y = y;
            std::size_t vertex = index;
            if (tool.focus % 2) {
                points.insert(points.begin() + std::ptrdiff_t(index + 1), moved);
                vertex = index + 1;
            } else
                points[index] = moved;
            const auto selected = tool.selected;
            const auto revision = doc_.revision();
            if (commitTerrainEdit(contourPointsCommand(*source, points), "Moved the vertex")) {
                tool.selected = contourSource(selected) ? selected : 0;
                tool.focus = tool.selected ? int(2 * vertex) : -1;
            }
            emit terrainToolChanged();
            return doc_.revision() != revision;
        }
        // The terrain under the typed position.
        Id terrain = tool.terrain;
        std::optional<double> z = terrain ? terrainElevation(terrain, x, y) : std::nullopt;
        if (!z)
            for (const auto &[id, body] : doc_.bodies())
                if (body->kind == BodyKind::Terrain && selection_.inActiveHierarchy(doc_, id))
                    if ((z = terrainElevation(id, x, y))) {
                        terrain = id;
                        break;
                    }
        if (!z)
            throw std::runtime_error("That position is not on a terrain");
        const Vec3 point{x, y, *z};
        if (tool.mode == ContourMode::Edit) {
            // No handle focused: select the source passing nearest the position (within 2 m).
            if (terrain != tool.terrain)
                selectTerrainSource(terrain, 0);
            std::uint32_t best = 0;
            double bestDistance = 2;
            for (const auto &candidate : currentContourSources().sources) {
                for (std::size_t i = 0; i < candidate.points.size(); ++i) {
                    const auto &a = candidate.points[i];
                    const auto &b = candidate.points[std::min(i + 1, candidate.points.size() - 1)];
                    const double dx = b.x - a.x, dy = b.y - a.y, l2 = dx * dx + dy * dy;
                    const double t = l2 > 0 ? std::clamp(((x - a.x) * dx + (y - a.y) * dy) / l2, 0.0, 1.0)
                                            : 0.0;
                    const double d = std::hypot(a.x + t * dx - x, a.y + t * dy - y);
                    if (d < bestDistance) {
                        bestDistance = d;
                        best = candidate.id;
                    }
                }
            }
            if (!best)
                throw std::runtime_error("No source contour, breakline or spot point within 2 m of "
                                         "that position");
            selectTerrainSource(terrain, best);
            return true;
        }
        if (tool.mode == ContourMode::Promote) {
            if (terrain != tool.terrain) {
                tool.terrain = terrain;
                tool.selected = tool.hovered = 0;
                tool.focus = -1;
                contourSources_ = {};
            }
            tool.pointer = point;
            const auto &record = *doc_.bodies().at(terrain)->terrain;
            const double interval = record.display.contourInterval;
            const double offset = terrainElevationOffset(doc_, terrain);
            const double worldZ = doc_.worldTransform(terrain).point({0, 0, 0}).z;
            tool.promoteLevel = std::round((point.z - worldZ + offset) / interval) * interval -
                                offset + worldZ;
            const auto revision = doc_.revision();
            const auto command = contourAddCommand();
            if (commitTerrainEdit(command, "Promoted a display contour to a source contour"))
                if (const auto &edits = doc_.bodies().at(terrain)->terrain->edits; !edits.empty())
                    if (const auto *data =
                            dynamic_cast<const TerrainSourceEditData *>(edits.back()->data.get())) {
                        tool.mode = ContourMode::Edit;
                        selectTerrainSource(terrain, data->source);
                    }
            return doc_.revision() != revision;
        }
        if (tool.adding.empty()) {
            terrainSession_.begin();
            if (terrain != tool.terrain) {
                tool.terrain = terrain;
                tool.selected = tool.hovered = 0;
                tool.focus = -1;
                contourSources_ = {};
            }
        } else if (terrain != tool.terrain)
            throw std::runtime_error("The new source's points must lie on one terrain");
        if (tool.mode == ContourMode::AddSpot)
            tool.adding = {point};
        else if (tool.mode == ContourMode::AddContour && tool.adding.size() >= 3 &&
                 std::hypot(point.x - tool.adding.front().x, point.y - tool.adding.front().y) <= 1e-9) {
            finishContourAdd(true);
            return true;
        } else
            tool.adding.push_back(point);
        if (tool.adding.size() >= (tool.mode == ContourMode::AddSpot ? 1u : 2u))
            requestTerrainEditPreview(contourAddCommand());
        emitTerrainStatus();
        emit terrainToolChanged();
        update();
        return true;
    }
    // A number: an elevation (model Z).
    const double value = parseLength(text, unit, locale);
    if (tool.mode == ContourMode::AddContour || tool.mode == ContourMode::AddSpot) {
        tool.addElevation = value;
        if (tool.mode == ContourMode::AddSpot && !tool.adding.empty()) {
            const auto revision = doc_.revision();
            finishContourAdd(false);
            return doc_.revision() != revision;
        }
        if (tool.adding.size() >= 2)
            requestTerrainEditPreview(contourAddCommand());
        emit measurementPreview(displayLength(value, doc_.displayUnits(), doc_.displayPrecision()));
        emit message(QString("The new %1 takes %2")
                         .arg(tool.mode == ContourMode::AddSpot ? "spot point" : "contour")
                         .arg(displayLength(value, doc_.displayUnits(), doc_.displayPrecision())));
        emit terrainToolChanged();
        return true;
    }
    if (tool.mode != ContourMode::Edit || !contourSource(tool.selected))
        throw std::runtime_error("Select a source contour or breakline, then type its elevation");
    const auto revision = doc_.revision();
    setSelectedSourceElevation(value);
    return doc_.revision() != revision;
}

// ---- Drawing (UX §15.3) ------------------------------------------------------------------------
void Viewport::paintContourTool(QPainter &p) {
    const auto &tool = contourTool_;
    const auto project = screenProjector();
    p.save();
    p.setRenderHint(QPainter::Antialiasing);
    const QColor ink = colors_.ink, halo(255, 255, 255, 220);
    const QColor selection = colors_.selectionStroke.isValid() ? colors_.selectionStroke
                                                               : QColor("#5b5bd6");
    const QColor red = colors_.failure.isValid() ? colors_.failure : QColor("#a8322b");
    const QRectF visible = QRectF(rect()).adjusted(-20, -20, 20, 20);
    auto polyline = [&](const std::vector<Vec3> &points) {
        QPolygonF polygon;
        polygon.reserve(qsizetype(points.size()));
        for (const auto &point : points)
            polygon << project(point);
        return polygon;
    };
    auto cross = [&](QPointF at, double arm) {
        p.drawLine(at + QPointF(-arm, -arm), at + QPointF(arm, arm));
        p.drawLine(at + QPointF(arm, -arm), at + QPointF(-arm, arm));
    };
    const auto shown = terrainEditPreviewShown();
    // A refusal is drawn while it is the plan of the newest request, as the status bar says it.
    const bool refused = shown && shown->refused() && terrainEditPreviewCurrent();
    // The changed region, hatched as for Sculpt (a black and white band, not colour alone).
    if (shown && shown->candidate && !refused) {
        for (const auto &loop : shown->outline) {
            std::vector<Vec3> model;
            for (const auto &q : loop)
                model.push_back(shown->world.point(q));
            auto screen = polyline(model);
            if (!screen.isEmpty())
                screen << screen.front();
            p.setBrush(Qt::NoBrush);
            p.setPen(QPen(ink, 6, Qt::SolidLine, Qt::FlatCap, Qt::RoundJoin));
            p.drawPolyline(screen);
            p.setPen(QPen(Qt::white, 3.5, Qt::SolidLine, Qt::FlatCap, Qt::RoundJoin));
            p.drawPolyline(screen);
            QPen hatch(ink, 3.5, Qt::CustomDashLine, Qt::FlatCap, Qt::RoundJoin);
            hatch.setDashPattern({0.6, 0.6});
            p.setPen(hatch);
            p.drawPolyline(screen);
        }
    }
    // The sources: contours solid at 1.5 px, breaklines dash-dot, spot points as crosses.
    for (const auto &source : currentContourSources().sources) {
        if (source.id == tool.selected)
            continue;
        const bool hovered = source.id == tool.hovered;
        const QColor colour = hovered ? selection : ink;
        // An imported point file (over 20,000 spot points) is drawn only while hovered: as the
        // base of a points terrain it would cover the view.
        if (source.role == TerrainSourceRole::Points && source.points.size() > 20000 && !hovered)
            continue;
        if (source.role == TerrainSourceRole::Points) {
            // Crosses while few are in view; a dense point set draws as dots (a cross each would
            // cost the frame seconds under software rendering).
            QPolygonF shown;
            for (const auto &point : source.points)
                if (const auto at = project(point); visible.contains(at))
                    shown << at;
            if (shown.size() <= qsizetype(crossLimit)) {
                p.setPen(QPen(colour, hovered ? 1.8 : 1.2));
                for (const auto &at : shown)
                    cross(at, hovered ? 4 : 3);
            } else {
                p.save();
                p.setRenderHint(QPainter::Antialiasing, false);
                p.setPen(QPen(colour, hovered ? 2.5 : 1.5));
                p.drawPoints(shown);
                p.restore();
            }
            continue;
        }
        // Only the runs of segments in view are drawn (a large terrain's rings run far outside a
        // close view, and every point drawn costs the frame).
        std::vector<QPolygonF> runs;
        {
            QPolygonF run;
            QPointF previous = project(source.points.front());
            for (std::size_t i = 1; i < source.points.size(); ++i) {
                const QPointF next = project(source.points[i]);
                const bool shown =
                    QRectF(previous, next).normalized().adjusted(-1, -1, 1, 1).intersects(visible);
                if (shown) {
                    if (run.isEmpty())
                        run << previous;
                    run << next;
                } else if (!run.isEmpty()) {
                    runs.push_back(std::move(run));
                    run = {};
                }
                previous = next;
            }
            if (!run.isEmpty())
                runs.push_back(std::move(run));
        }
        if (hovered) {
            p.setPen(QPen(halo, 5));
            for (const auto &run : runs)
                p.drawPolyline(run);
        }
        QPen pen(colour, hovered ? 2.4 : 1.5,
                 source.role == TerrainSourceRole::Breakline ? Qt::DashDotLine : Qt::SolidLine,
                 Qt::RoundCap, Qt::RoundJoin);
        p.setPen(pen);
        for (const auto &run : runs)
            p.drawPolyline(run);
    }
    // The selected source with its handles: squares on vertices, rounds at midpoints.
    if (const auto *source = contourSource(tool.selected)) {
        const auto handles = contourHandles(*source);
        if (source->role == TerrainSourceRole::Points) {
            p.setPen(QPen(selection, 2));
            for (const auto &point : source->points)
                if (const auto at = project(point); visible.contains(at))
                    cross(at, 4.5);
        } else {
            std::vector<Vec3> line = tool.dragging ? tool.dragPoints : source->points;
            if (tool.dragging && source->closed && !line.empty())
                line.push_back(line.front());
            if (tool.dragging) {
                // The line as it was, dashed, under the dragged one.
                p.setPen(QPen(selection, 1, Qt::DashLine));
                p.drawPolyline(polyline(source->points));
            }
            const auto screen = polyline(line);
            p.setPen(QPen(halo, 5, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
            p.drawPolyline(screen);
            p.setPen(QPen(selection, 2.4, source->role == TerrainSourceRole::Breakline
                                              ? Qt::DashDotLine
                                              : Qt::SolidLine,
                          Qt::RoundCap, Qt::RoundJoin));
            p.drawPolyline(screen);
        }
        // A refused drag: the moved run red and dashed (UX §15.3).
        if (tool.dragging && refused && tool.dragVertex >= 0 &&
            std::size_t(tool.dragVertex) < tool.dragPoints.size()) {
            const auto n = tool.dragPoints.size();
            const auto i = std::size_t(tool.dragVertex);
            std::vector<Vec3> run;
            if (i > 0 || source->closed)
                run.push_back(tool.dragPoints[(i + n - 1) % n]);
            run.push_back(tool.dragPoints[i]);
            if (i + 1 < n || source->closed)
                run.push_back(tool.dragPoints[(i + 1) % n]);
            QPen dashed(red, 2.2, Qt::CustomDashLine, Qt::FlatCap, Qt::RoundJoin);
            dashed.setDashPattern({3, 1.5});
            p.setPen(dashed);
            p.drawPolyline(polyline(run));
        }
        for (std::size_t h = 0; h < handles.size(); ++h) {
            if (!finite(handles[h]))
                continue;
            const auto at = project(handles[h]);
            if (!visible.contains(at))
                continue;
            const bool focused = int(h) == tool.focus;
            const bool moving = tool.dragging && int(h) == 2 * tool.dragVertex;
            if (h % 2 == 0) {
                const double size = moving ? 9 : focused ? 9 : 7;
                p.setPen(QPen(moving && refused ? Qt::white : selection, 1.5));
                p.setBrush(moving && refused ? red : focused ? selection : QColor(Qt::white));
                p.drawRect(QRectF(at.x() - size / 2, at.y() - size / 2, size, size));
            } else {
                p.setPen(QPen(selection, 1.2));
                p.setBrush(focused ? selection : QColor(Qt::white));
                p.drawEllipse(at, focused ? 3.6 : 2.6, focused ? 3.6 : 2.6);
            }
        }
    }
    // A new source: its points so far and a dashed rubber band to the pointer.
    if (!tool.adding.empty()) {
        auto line = tool.adding;
        if (tool.mode == ContourMode::AddContour && tool.addElevation)
            for (auto &q : line)
                q.z = *tool.addElevation;
        const auto screen = polyline(line);
        if (tool.mode == ContourMode::AddSpot) {
            p.setPen(QPen(selection, 2));
            cross(screen.front(), 5);
        } else {
            p.setPen(QPen(halo, 4));
            p.drawPolyline(screen);
            p.setPen(QPen(selection, 2, tool.mode == ContourMode::AddBreakline ? Qt::DashDotLine
                                                                               : Qt::SolidLine));
            p.drawPolyline(screen);
            if (tool.pointer) {
                p.setPen(QPen(selection, 1.5, Qt::DashLine));
                p.drawLine(screen.back(), project(*tool.pointer));
            }
            p.setBrush(Qt::white);
            p.setPen(QPen(selection, 1.5));
            for (const auto &corner : screen)
                p.drawEllipse(corner, 3.5, 3.5);
        }
    }
    // Promote: the display contour under the pointer, as it would be added.
    if (tool.mode == ContourMode::Promote && shown && !refused && !shown->points.empty()) {
        std::vector<Vec3> line;
        for (const auto &q : shown->points)
            line.push_back(shown->world.point(q));
        const auto screen = polyline(line);
        p.setPen(QPen(halo, 5));
        p.drawPolyline(screen);
        p.setPen(QPen(selection, 2.4));
        p.drawPolyline(screen);
    }
    p.restore();
    paintTerrainEditPreview(p); // the × at the first conflict
    // The label: the failure at the pointer, else the hovered source's name.
    QStringList lines;
    std::optional<QPointF> anchor;
    bool failure = false;
    if (refused && (tool.dragging || !tool.adding.empty() || tool.mode == ContourMode::Promote)) {
        const auto refusal = terrainEditFailure(*shown);
        lines << refusal.label
              << (tool.dragging ? QString("Release to cancel, or move back")
                                : QString("Change it, or press Esc"));
        if (refusal.at)
            anchor = project(*refusal.at) + QPointF(16, 14);
        failure = true;
    } else if (!tool.dragging && tool.mode == ContourMode::Edit && tool.hovered &&
               tool.hovered != tool.selected) {
        if (const auto *source = contourSource(tool.hovered))
            lines << contourSourceLabel(*source);
        anchor = tool.screen + QPointF(18, 12);
    } else if (tool.mode == ContourMode::Promote && tool.promoteLevel && tool.pointer) {
        lines << "Promote contour " + displayLength(*tool.promoteLevel, doc_.displayUnits(), 2);
        anchor = project(*tool.pointer) + QPointF(18, 12);
    }
    if (lines.isEmpty() || !anchor)
        return;
    p.save();
    auto font = p.font();
    font.setBold(failure);
    p.setFont(font);
    const QFontMetrics metrics(font);
    int textWidth = 0;
    for (const auto &line : lines)
        textWidth = std::max(textWidth, metrics.horizontalAdvance(line));
    const int boxWidth = std::min(textWidth + 20, std::max(1, width() - 24));
    const int boxHeight = int(lines.size()) * metrics.lineSpacing() + 10;
    const QRectF box(std::clamp(anchor->x(), 12.0, double(std::max(12, width() - boxWidth - 12))),
                     std::clamp(anchor->y(), 12.0, double(std::max(12, height() - boxHeight - 12))),
                     boxWidth, boxHeight);
    p.setRenderHint(QPainter::Antialiasing);
    p.setPen(QPen(failure ? red : colors_.border, failure ? 1.5 : 1));
    p.setBrush(failure ? (colors_.failureSurface.isValid() ? colors_.failureSurface : colors_.surface)
                       : colors_.surface);
    p.drawRoundedRect(box, 4, 4);
    p.setPen(failure ? red : ink);
    for (int i = 0; i < int(lines.size()); ++i)
        p.drawText(QRectF(box.left() + 10, box.top() + 5 + i * metrics.lineSpacing(),
                          box.width() - 20, metrics.lineSpacing()),
                   Qt::AlignLeft | Qt::AlignVCenter,
                   metrics.elidedText(lines[i], Qt::ElideRight, int(box.width()) - 20));
    p.restore();
}
} // namespace sketchy
