// R096.f1 (UX_DESIGN §15.1–15.2, §15.7; ADR 0169 §5, §8): the Sculpt and Smooth tools and the
// terrain preview path.
//
// - The brush follows the pointer on a terrain's surface. Every move plans the change on the
//   cached edit mesh (previewTerrainSculpt, on the UI thread as ADR 0169 §5 allows for sculpt):
//   the ring, the dots on the vertices that will move and the label come from that plan.
// - Press and drag paints a swept stroke at the strip's Amount; the candidate tiles are drawn in
//   place of the tiles they replace (their vertex buffers substituted, normals recomputed for
//   them and their seam ring, contours recomputed for them), with the changed region outlined by
//   a hatched band. Release commits terrain.sculpt as one labelled edit.
// - A click without a drag anchors a one-spot brush (Smoove): moving up or down sets the amount,
//   and a click or a typed value commits it.
// - Region scope uses a selected face or closed edge loop, or a polygon clicked on the surface.
// - Escape withdraws the candidate: the original buffers are drawn again and nothing else exists
//   to revert (the document is never touched by a preview).
// - After a commit, a typed value revises it (UX §4.3) through Document::amendLast's terrain policy.
#include "app/unit_display.hpp"
#include "app/viewport.hpp"
#include "automation/commands.hpp"
#include "automation/measurements.hpp"
#include "core/material_records.hpp"
#include "core/terrain_display.hpp"
#include "core/terrain_sculpt.hpp"
#include <QElapsedTimer>
#include <QKeyEvent>
#include <QLineF>
#include <QLocale>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPainterPathStroker>
#include <QThreadPool>
#include <algorithm>
#include <cmath>
#include <numbers>
namespace sketchy {
namespace {
constexpr int ringSegments = 72;
constexpr std::size_t dotLimit = 20000; // dots drawn at most (a wide brush on a dense terrain)
// The entry data terrain.sculpt builds from `command` (its conversion, step for step), so the
// preview plans exactly what the command will (ADR 0169, R096.f notes: avoid planning twice).
std::shared_ptr<const TerrainSculptData> sculptData(const QJsonObject &command,
                                                    const Transform &toLocal) {
    auto data = std::make_shared<TerrainSculptData>();
    data->falloff = terrainFalloffOf(command["falloff"].toString().toStdString())
                        .value_or(TerrainFalloff::Smooth);
    auto plan = [&](const QJsonArray &points) {
        std::vector<std::array<double, 2>> result;
        for (const auto &entry : points) {
            const auto xy = entry.toArray();
            const auto local = toLocal.point({xy[0].toDouble(), xy[1].toDouble(), 0});
            result.push_back({local.x + 0.0, local.y + 0.0});
        }
        return result;
    };
    if (command.contains("path")) {
        data->path = plan(command["path"].toArray());
        data->radius = command["radius"].toDouble();
    } else {
        std::vector<std::array<double, 2>> corners;
        for (const auto &p : plan(command["region"].toArray()))
            if (corners.empty() || corners.back() != p)
                corners.push_back(p);
        while (corners.size() > 1 && corners.back() == corners.front())
            corners.pop_back();
        data->region = std::move(corners);
        data->width = command["falloffWidth"].toDouble();
    }
    if (command["mode"] == "raise")
        data->amount = command["amount"].toDouble();
    else {
        data->strength = command["strength"].toDouble();
        data->iterations = command["iterations"].toInt();
    }
    return data;
}
double planDistance(Vec3 a, Vec3 b) { return std::hypot(a.x - b.x, a.y - b.y); }
// "[x, y]" or "[x, y, z]" in model coordinates (the z is ignored: the brush lies on the surface).
std::optional<std::array<double, 2>> typedPoint(const QString &text, const QString &unit) {
    if (!text.startsWith('[') || !text.endsWith(']'))
        return std::nullopt;
    const auto locale = QLocale();
    const auto inner = text.mid(1, text.size() - 2);
    const auto separator = locale.decimalPoint() == "," ? ';' : ',';
    const auto pieces = inner.split(inner.contains(';') ? ';' : separator, Qt::KeepEmptyParts);
    if (pieces.size() != 2 && pieces.size() != 3)
        throw std::runtime_error("A brush position is [x, y] in model coordinates");
    return std::array<double, 2>{parseLength(pieces[0], unit, locale),
                                 parseLength(pieces[1], unit, locale)};
}
} // namespace

bool Viewport::terrainSmoothing() const {
    // A one-spot brush keeps the mode it was anchored with: its amount is set by moving up or
    // down, which only a raise has (UX §15.2), and Shift while anchored changes nothing.
    if (brush_.anchored)
        return tool_ == Tool::Smooth;
    return tool_ == Tool::Smooth ||
           (tool_ == Tool::Sculpt && brush_.modifiers.testFlag(Qt::ShiftModifier));
}
double Viewport::terrainAmount() const {
    if (brush_.anchored)
        return brush_.spotAmount;
    return brush_.modifiers.testFlag(Qt::ControlModifier) ? -terrainOptions_.amount
                                                          : terrainOptions_.amount;
}
void Viewport::setTerrainOptions(const TerrainToolOptions &input) {
    auto options = input;
    options.radius = std::clamp(options.radius, terrainSculptRadiusMin, terrainSculptRadiusMax);
    options.amount = std::clamp(options.amount, -terrainSculptAmountMax, terrainSculptAmountMax);
    options.strength = std::clamp(options.strength, terrainToolStrengthMin, 1.0);
    options.passes = std::clamp(options.passes, 1, terrainSmoothIterationsMax);
    options.falloffWidth = std::clamp(options.falloffWidth, 0.0, terrainSculptWidthMax);
    if (options == terrainOptions_)
        return;
    const bool scopeChanged = options.scope != terrainOptions_.scope;
    terrainOptions_ = options;
    if (scopeChanged && terrainToolActive()) {
        cancelTerrainOperation();
        beginTerrainTool();
    } else if (terrainToolActive() && brush_.terrain)
        updateTerrainCandidate(terrainCandidateShown_);
    emitTerrainStatus();
    emit terrainToolChanged();
    update();
}
void Viewport::stepTerrainRadius(bool larger) {
    auto options = terrainOptions_;
    options.radius = larger ? options.radius * terrainToolRadiusStep
                            : options.radius / terrainToolRadiusStep;
    setTerrainOptions(options);
    emit message(QString("Brush radius %1")
                     .arg(displayLength(terrainOptions_.radius, doc_.displayUnits(),
                                        doc_.displayPrecision())));
}
QString Viewport::terrainMeasurementLabel() const {
    if (!terrainToolActive())
        return {};
    if (tool_ == Tool::Contours)
        return contourMeasurementLabel();
    // The value step follows the mode: a Sculpt stroke with Shift held, or committed that way, is
    // a smooth and takes a strength.
    if (!terrainAmendable().isEmpty() && !brush_.stroking && !brush_.anchored && !brush_.regionClosed)
        return terrainAmendable()["mode"] == "smooth" ? "Strength" : "Amount";
    if (brush_.stroking || brush_.anchored || brush_.regionClosed)
        return terrainSmoothing() ? "Strength" : "Amount";
    if (terrainOptions_.scope == TerrainToolOptions::Scope::Region)
        return brush_.region.empty() ? "Falloff width" : "Point";
    return "Radius";
}
QJsonObject Viewport::terrainAmendable() const {
    return terrainSession_.canRevise() ? terrainCommitted_ : QJsonObject{};
}
std::shared_ptr<const TerrainSculptPreview> Viewport::terrainPreviewShown() const {
    return terrainCandidateShown_ ? terrainCandidate_ : nullptr;
}
std::set<Id> Viewport::terrainPreviewHiddenTiles() const {
    return terrainPreviewGpu_.active ? terrainPreviewGpu_.hidden : std::set<Id>{};
}
Viewport::TerrainBrushView Viewport::terrainBrush() const {
    TerrainBrushView view;
    view.terrain = brush_.terrain;
    view.point = brush_.point;
    view.stroking = brush_.stroking;
    view.anchored = brush_.anchored;
    view.smoothing = terrainSmoothing();
    view.amount = terrainAmount();
    view.region = brush_.region;
    view.regionClosed = brush_.regionClosed;
    view.refusal = brush_.refusal.isEmpty() ? brush_.error : brush_.refusal;
    if (terrainCandidate_) {
        view.vertices = terrainCandidate_->moving.size();
        view.tiles = terrainCandidate_->tilesTouched();
        view.empty = terrainCandidate_->moving.empty();
    }
    view.previewShown = bool(terrainPreviewShown());
    view.ringPoints = terrainOverlay_.ring.size();
    for (const auto &outline : terrainOverlay_.outline)
        view.outlinePoints += outline.size();
    if (brush_.point || brush_.regionClosed) {
        const auto units = doc_.displayUnits();
        const int precision = doc_.displayPrecision() == fullDisplayPrecision
                                  ? (units == DisplayUnit::Millimeters ? 0 : 2)
                                  : doc_.displayPrecision();
        auto length = [&](double v) { return displayLength(v, units, precision); };
        QString first;
        if (view.smoothing)
            first = QString("Smooth %1 × %2")
                        .arg(QLocale().toString(terrainOptions_.strength, 'f', 2))
                        .arg(terrainOptions_.passes);
        else
            first = (view.amount >= 0 ? "+" : "") + length(view.amount);
        first += terrainOptions_.scope == TerrainToolOptions::Scope::Region
                     ? " · falloff " + length(terrainOptions_.falloffWidth)
                     : " · radius " + length(terrainOptions_.radius);
        const QString second =
            view.empty && terrainCandidate_ && !terrainCandidate_->plan.influenced
                ? QString("No vertices under the brush · ] enlarges it")
            : view.empty && terrainCandidate_
                ? QString::fromStdString(terrainCandidate_->plan.refusal)
                : QString("%1 vertices · %2 %3")
                      .arg(QLocale().toString(qulonglong(view.vertices)))
                      .arg(view.tiles)
                      .arg(view.tiles == 1 ? "tile" : "tiles");
        view.label = first + "\n" + second;
    }
    return view;
}

// ---- Pointer -------------------------------------------------------------------------------
std::optional<std::pair<Id, Vec3>> Viewport::terrainHit(QPointF point) const {
    const auto hit = nearestFace(point);
    if (!hit.body || !std::isfinite(hit.distance))
        return std::nullopt;
    const auto found = doc_.bodies().find(hit.body);
    if (found == doc_.bodies().end() || !found->second->terrainTile ||
        !selection_.inActiveHierarchy(doc_, hit.body))
        return std::nullopt; // a terrain outside the open editing context is not edited
    const auto [origin, direction] = ray(point);
    return std::pair{found->second->parent, origin + direction * hit.distance};
}
std::optional<double> Viewport::terrainElevation(Id terrain, double x, double y) const {
    // Painting asks too: a terrain an Undo removed has no surface, and nothing here may throw.
    if (!isTerrain(doc_, terrain))
        return std::nullopt;
    try {
        const auto found = terrainElevationAt(doc_, terrain, {{x, y}});
        if (found.empty() || !found.front().found)
            return std::nullopt;
        return found.front().elevation;
    } catch (const std::exception &) {
        return std::nullopt;
    }
}
void Viewport::emitTerrainStatus() {
    if (!terrainToolActive())
        return;
    if (tool_ == Tool::Contours)
        return emitContourStatus();
    const bool region = terrainOptions_.scope == TerrainToolOptions::Scope::Region;
    const auto name = tool_ == Tool::Smooth ? QString("Smooth") : QString("Sculpt");
    QString text;
    if (!brush_.refusal.isEmpty())
        text = brush_.refusal;
    else if (!brush_.error.isEmpty() && !brush_.stroking && !brush_.anchored)
        text = brush_.error; // a locked, tilted or scaled terrain, as a refusal
    else if (brush_.stroking)
        text = name + " · Release to apply the stroke · Ctrl: lower · Shift: smooth · Esc cancels";
    else if (brush_.anchored)
        text = tool_ == Tool::Smooth
                   ? "Smooth · Click or type a strength to apply · Esc cancels"
                   : "Sculpt · Move up or down to set the amount · Click or type it to apply · "
                     "Esc cancels";
    else if (region && brush_.regionClosed)
        text = name + " · Region · Click the terrain or press Enter to apply, or type the " +
               (tool_ == Tool::Smooth ? "strength" : "amount") + " · Esc cancels";
    else if (region)
        text = name + " · Region · Click points on the terrain and close on the first one, or "
                      "select a face or closed loop first · Type [x, y] or @E, N · Esc cancels";
    else if (tool_ == Tool::Smooth)
        text = "Smooth · Drag to smooth along a stroke · Click smooths one spot · [ ]: radius · "
               "Esc cancels";
    else
        text = "Sculpt · Drag to raise along a stroke · Ctrl: lower · Shift: smooth · [ ]: radius "
               "· Click for one spot, then move up or down · Esc cancels";
    // After a commit, a typed value revises it (UX §4.3); the status bar says so until the next
    // operation starts.
    if (brush_.refusal.isEmpty() && !brush_.stroking && !brush_.anchored && brush_.region.empty() &&
        !terrainAmendable().isEmpty())
        text = QString("%1 · Type a new %2 to revise it · ")
                   .arg(terrainAmendable()["mode"] == "smooth" ? "Smoothed" : "Sculpted")
                   .arg(terrainAmendable()["mode"] == "smooth" ? "strength" : "amount") +
               text;
    if (text == terrainStatusShown_)
        return;
    terrainStatusShown_ = text;
    emit message(text);
}
void Viewport::beginTerrainTool() {
    if (tool_ == Tool::Contours)
        return beginContourTool();
    brush_ = {};
    terrainCandidate_.reset();
    terrainCandidateShown_ = false;
    terrainPreviewDirty_ = true;
    terrainStatusShown_.clear();
    if (!doc_.materializedTotals().terrainTriangles)
        brush_.refusal = "No terrain in this model. Import one with File → Import terrain…";
    // Region scope: a selected face or closed edge loop is the region (UX §15.2).
    if (terrainOptions_.scope == TerrainToolOptions::Scope::Region && brush_.refusal.isEmpty()) {
        std::vector<Vec3> loop;
        const auto &entities = selection_.entities();
        if (entities.size() == 1 && entities.begin()->kind == SelectionKind::Face) {
            const auto entity = *entities.begin();
            const auto found = doc_.bodies().find(entity.body);
            if (found != doc_.bodies().end() && !found->second->terrainTile &&
                found->second->surface.faces.contains(entity.entity)) {
                const auto &body = *found->second;
                const auto world = doc_.worldTransform(entity.body);
                for (const auto vertex : body.surface.faces.at(entity.entity).loops.front())
                    loop.push_back(world.point(body.surface.vertices.at(vertex)));
            }
        } else if (!entities.empty() &&
                   std::all_of(entities.begin(), entities.end(), [](const SelectedEntity &e) {
                       return e.kind == SelectionKind::Edge;
                   })) {
            // Chain the edges by shared positions into one closed loop.
            std::vector<std::array<Vec3, 2>> edges;
            for (const auto &entity : entities) {
                const auto found = doc_.bodies().find(entity.body);
                if (found == doc_.bodies().end() ||
                    !found->second->topology.edges.contains(entity.entity))
                    continue;
                const auto &body = *found->second;
                const auto world = doc_.worldTransform(entity.body);
                const auto &edge = body.topology.edges.at(entity.entity);
                if (!body.surface.vertices.contains(edge.a) || !body.surface.vertices.contains(edge.b))
                    continue;
                edges.push_back({world.point(body.surface.vertices.at(edge.a)),
                                 world.point(body.surface.vertices.at(edge.b))});
            }
            if (edges.size() >= 3) {
                std::vector<bool> used(edges.size());
                used[0] = true;
                loop = {edges[0][0], edges[0][1]};
                for (std::size_t step = 1; step < edges.size() && !loop.empty(); ++step) {
                    bool linked = false;
                    for (std::size_t i = 0; i < edges.size() && !linked; ++i)
                        for (int end = 0; end < 2 && !linked; ++end)
                            if (!used[i] &&
                                length(edges[i][std::size_t(end)] - loop.back()) <= 1e-9) {
                                loop.push_back(edges[i][std::size_t(1 - end)]);
                                used[i] = linked = true;
                            }
                    if (!linked)
                        loop.clear();
                }
                if (loop.size() < 4 || length(loop.back() - loop.front()) > 1e-9)
                    loop.clear();
                else
                    loop.pop_back();
            }
        }
        if (loop.size() >= 3) {
            // The terrain under the region's first corner.
            Id terrain = 0;
            for (const auto &[id, body] : doc_.bodies())
                if (body->kind == BodyKind::Terrain && !terrain &&
                    selection_.inActiveHierarchy(doc_, id)) {
                    for (const auto &p : loop)
                        if (terrainElevation(id, p.x, p.y)) {
                            terrain = id;
                            break;
                        }
                }
            if (terrain) {
                brush_.terrain = terrain;
                for (auto p : loop) {
                    p.z = terrainElevation(terrain, p.x, p.y).value_or(p.z);
                    brush_.region.push_back(p);
                }
                brush_.regionClosed = brush_.regionSelected = true;
                terrainSession_.begin();
                updateTerrainCandidate(true);
            }
        }
    }
    emitTerrainStatus();
    emit terrainToolChanged();
    update();
}
void Viewport::cancelTerrainOperation(bool keepAmend) {
    // R096.f2: the worker tools' operation (a drag, a new source) and preview.
    cancelContourOperation();
    if (!sculptTool(tool_)) {
        if (!keepAmend) {
            terrainSession_.cancel();
            terrainCommitted_ = {};
        }
        return;
    }
    const bool was = brush_.pressed || brush_.stroking || brush_.anchored || !brush_.region.empty() ||
                     terrainCandidateShown_;
    brush_.path.clear();
    brush_.pressed = brush_.stroking = brush_.anchored = false;
    brush_.spotAmount = 0;
    brush_.region.clear();
    brush_.regionClosed = brush_.regionSelected = false;
    brush_.error.clear();
    terrainCandidate_.reset();
    terrainCandidateShown_ = false;
    terrainCandidateCommand_ = {};
    terrainPreviewDirty_ = true;
    terrainOverlay_ = {};
    forgetRememberedTerrainSculptPlan();
    if (!keepAmend) {
        terrainSession_.cancel();
        terrainCommitted_ = {};
    }
    if (was) {
        if (!keepAmend)
            emit measurementPreview({}); // the live value goes with the operation
        emitTerrainStatus();
        emit terrainToolChanged();
    }
    update();
}
void Viewport::terrainHover(QPointF point) {
    brush_.pointer = point;
    if (brush_.anchored) {
        // The one-spot amount: the pointer's height above the anchor, in metres at its depth.
        const auto anchor = brush_.path.front();
        const auto base = project(anchor);
        const double up = (project(anchor + Vec3{0, 0, 1}) - base).manhattanLength();
        const double across = (project(anchor + Vec3{1, 0, 0}) - base).manhattanLength();
        const double perMetre = std::max({up, across * 0.5, 1e-6});
        brush_.spotAmount = std::clamp((brush_.anchorPosition.y() - point.y()) / perMetre,
                                       -terrainSculptAmountMax, terrainSculptAmountMax);
        updateTerrainCandidate(true);
        return;
    }
    const auto hit = terrainHit(point);
    QString refusal;
    if (!doc_.materializedTotals().terrainTriangles)
        refusal = "No terrain in this model. Import one with File → Import terrain…";
    else if (!hit && !brush_.stroking)
        refusal = tool_ == Tool::Smooth ? "Point at a terrain to smooth it"
                                        : "Point at a terrain to sculpt it";
    if (hit && brush_.stroking && hit->first != brush_.terrain)
        return; // A stroke stays on its terrain.
    if (hit) {
        brush_.terrain = hit->first;
        brush_.point = hit->second;
    } else if (!brush_.stroking) {
        brush_.point.reset();
        if (!brush_.regionClosed && brush_.region.empty())
            brush_.terrain = 0;
    }
    brush_.refusal = refusal;
    setCursor(refusal.isEmpty() ? Qt::CrossCursor : Qt::ForbiddenCursor);
    if (terrainOptions_.scope == TerrainToolOptions::Scope::Brush) {
        if (brush_.stroking && hit) {
            // A swept stroke: a new sample once the pointer has moved a quarter radius.
            const double spacing = std::max(terrainOptions_.radius * 0.25, 1e-3);
            if (planDistance(hit->second, brush_.path.back()) >= spacing) {
                if (brush_.path.size() >= terrainSculptPathPoints) {
                    // Keep the ends and every other point between them.
                    std::vector<Vec3> thinned;
                    for (std::size_t i = 0; i < brush_.path.size(); i += 2)
                        thinned.push_back(brush_.path[i]);
                    if (thinned.back() != brush_.path.back())
                        thinned.push_back(brush_.path.back());
                    brush_.path = std::move(thinned);
                }
                brush_.path.push_back(hit->second);
                updateTerrainCandidate(true);
                return emitTerrainStatus(), update(); // the overlay was rebuilt with it
            }
        } else if (!brush_.pressed) {
            if (hit)
                updateTerrainCandidate(false); // the plan only: dots, label, empty ring
            else {
                terrainCandidate_.reset();
                terrainCandidateShown_ = false;
                terrainPreviewDirty_ = true;
            }
        }
    }
    if (brush_.stroking || terrainOptions_.scope == TerrainToolOptions::Scope::Region)
        buildTerrainOverlay(); // the ring follows the pointer between stroke samples
    // A terrain the tool cannot edit (locked, tilted, scaled) refuses like no terrain at all.
    setCursor(refusal.isEmpty() && brush_.error.isEmpty() ? Qt::CrossCursor : Qt::ForbiddenCursor);
    emitTerrainStatus();
    update();
}
bool Viewport::terrainPress(QMouseEvent *event) {
    if (tool_ == Tool::Contours)
        return contourPress(event);
    const auto point = event->position();
    if (brush_.anchored) {
        commitTerrain(); // the previewed spot, whatever the modifiers on this click
        return true;
    }
    brush_.modifiers = event->modifiers();
    const auto hit = terrainHit(point);
    if (terrainOptions_.scope == TerrainToolOptions::Scope::Region) {
        if (brush_.regionClosed) {
            if (hit && hit->first == brush_.terrain)
                commitTerrain();
            return true;
        }
        if (!hit) {
            emit message(tool_ == Tool::Smooth ? "Point at a terrain to smooth it"
                                               : "Point at a terrain to sculpt it");
            return true;
        }
        if (brush_.region.empty()) {
            if (terrainSession_.canRevise())
                terrainCommitted_ = {};
            terrainSession_.begin();
            brush_.terrain = hit->first;
        } else if (hit->first != brush_.terrain)
            return true;
        // Closing on the first point (within 8 logical pixels) ends the polygon.
        if (brush_.region.size() >= 3 &&
            QLineF(project(brush_.region.front()), point).length() <= 8) {
            brush_.regionClosed = true;
            updateTerrainCandidate(true);
        } else
            brush_.region.push_back(hit->second);
        emitTerrainStatus();
        emit terrainToolChanged();
        update();
        return true;
    }
    if (!hit) {
        emit message(brush_.refusal.isEmpty() ? (tool_ == Tool::Smooth
                                                     ? QString("Point at a terrain to smooth it")
                                                     : QString("Point at a terrain to sculpt it"))
                                              : brush_.refusal);
        return true;
    }
    // A new operation: the last commit can no longer be revised.
    terrainCommitted_ = {};
    terrainSession_.begin();
    brush_.terrain = hit->first;
    brush_.point = hit->second;
    brush_.path = {hit->second};
    brush_.pressed = true;
    brush_.stroking = false;
    brush_.pressPosition = point;
    updateTerrainCandidate(false);
    emit terrainToolChanged();
    return true;
}
bool Viewport::terrainMove(QMouseEvent *event) {
    if (tool_ == Tool::Contours)
        return contourMove(event);
    brush_.modifiers = event->modifiers();
    if (brush_.pressed && !brush_.stroking && event->buttons().testFlag(Qt::LeftButton) &&
        (event->position() - brush_.pressPosition).manhattanLength() >= 4) {
        brush_.stroking = true;
        updateTerrainCandidate(true);
        emit terrainToolChanged();
    }
    terrainHover(event->position());
    return true;
}
bool Viewport::terrainRelease(QMouseEvent *event) {
    if (tool_ == Tool::Contours)
        return contourRelease(event);
    brush_.modifiers = event->modifiers();
    if (!brush_.pressed)
        return true;
    brush_.pressed = false;
    if (brush_.stroking) {
        // The stroke previewed last is the one committed: the release adds no sample.
        commitTerrain();
        return true;
    }
    if (terrainSmoothing()) {
        commitTerrain(); // Smooth (or Sculpt with Shift): a click smooths one spot.
        return true;
    }
    // Sculpt: a click without a drag anchors the one-spot brush.
    brush_.anchored = true;
    brush_.spotAmount = 0;
    brush_.anchorPosition = event->position();
    updateTerrainCandidate(true);
    emitTerrainStatus();
    emit terrainToolChanged();
    return true;
}
bool Viewport::terrainKey(QKeyEvent *event) {
    if (tool_ == Tool::Contours)
        return contourKey(event);
    const auto modifiers = event->modifiers();
    if (event->key() == Qt::Key_Control || event->key() == Qt::Key_Shift) {
        brush_.modifiers = modifiers;
        if (brush_.terrain && !brush_.anchored)
            updateTerrainCandidate(terrainCandidateShown_);
        update();
        return false; // Other handlers may also watch modifiers.
    }
    if (!(modifiers & (Qt::ControlModifier | Qt::AltModifier | Qt::MetaModifier)) &&
        terrainOptions_.scope == TerrainToolOptions::Scope::Region && event->text() == "[") {
        // Region scope has no radius: [ starts a typed point, as in the drawing tools.
        emit measurementsRequested("[");
        event->accept();
        return true;
    }
    if (!(modifiers & (Qt::ControlModifier | Qt::AltModifier | Qt::MetaModifier)) &&
        (event->text() == "[" || event->text() == "]")) {
        stepTerrainRadius(event->text() == "]");
        event->accept();
        return true;
    }
    if (event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter) {
        if (brush_.anchored || brush_.regionClosed)
            commitTerrain();
        else if (terrainOptions_.scope == TerrainToolOptions::Scope::Region &&
                 brush_.region.size() >= 3) {
            brush_.regionClosed = true;
            updateTerrainCandidate(true);
            emitTerrainStatus();
            emit terrainToolChanged();
        }
        event->accept();
        return true;
    }
    if (event->key() == Qt::Key_Escape) {
        if (brush_.pressed || brush_.stroking || brush_.anchored || !brush_.region.empty()) {
            cancelTerrainOperation();
            emit message("Operation canceled");
            event->accept();
            return true;
        }
        return false; // Leaves the tool.
    }
    // Typing during a stroke moves focus to the Measurements box, which withdraws the stroke;
    // the box then takes the radius. Say so (UX §4.3: an entry never needs the pointer released,
    // but a stroke's amount comes from the strip).
    if ((brush_.pressed || brush_.stroking) && !event->text().isEmpty() &&
        QString("0123456789.,+-[<@").contains(event->text().front()) &&
        !(modifiers & (Qt::ControlModifier | Qt::AltModifier | Qt::MetaModifier))) {
        cancelTerrainOperation();
        emit message("Stroke cancelled · typing sets the Radius; release first to type an amount");
        terrainStatusShown_ = "Stroke cancelled · typing sets the Radius; release first to type an "
                              "amount";
        return false; // the key goes on to open the box
    }
    // '@' opens the Measurements box for a site position (R095.e2), as digits do.
    if (event->text() == "@") {
        emit measurementsRequested("@");
        event->accept();
        return true;
    }
    return false;
}
void Viewport::terrainKeyRelease(QKeyEvent *event) {
    if (!sculptTool(tool_))
        return;
    if (event->key() != Qt::Key_Control && event->key() != Qt::Key_Shift)
        return;
    brush_.modifiers = event->modifiers();
    if (brush_.terrain && !brush_.anchored)
        updateTerrainCandidate(terrainCandidateShown_);
    update();
}

// ---- The candidate -------------------------------------------------------------------------
QJsonObject Viewport::terrainCommand(std::optional<double> value) const {
    const bool smoothing = terrainSmoothing();
    QJsonObject command{{"command", "terrain.sculpt"},
                        {"terrain", QString::number(brush_.terrain)},
                        {"mode", smoothing ? "smooth" : "raise"},
                        {"falloff", terrainFalloffName(terrainOptions_.falloff)}};
    auto plan = [](Vec3 p) { return QJsonArray{p.x, p.y}; };
    if (terrainOptions_.scope == TerrainToolOptions::Scope::Region) {
        QJsonArray region;
        for (const auto &p : brush_.region)
            region.append(plan(p));
        command["region"] = region;
        command["falloffWidth"] = terrainOptions_.falloffWidth;
    } else {
        QJsonArray path;
        if (brush_.pressed || brush_.stroking || brush_.anchored)
            for (const auto &p : brush_.path)
                path.append(plan(p));
        else if (brush_.point)
            path.append(plan(*brush_.point));
        command["path"] = path;
        command["radius"] = terrainOptions_.radius;
    }
    if (smoothing) {
        command["strength"] = value.value_or(terrainOptions_.strength);
        command["iterations"] = terrainOptions_.passes;
    } else
        command["amount"] = value.value_or(terrainAmount());
    return command;
}
void Viewport::updateTerrainCandidate(bool show) {
    brush_.error.clear();
    terrainPreviewDirty_ = true;
    terrainCandidate_.reset();
    terrainCandidateShown_ = false;
    const bool region = terrainOptions_.scope == TerrainToolOptions::Scope::Region;
    if (!brush_.terrain || !isTerrain(doc_, brush_.terrain) || (region && !brush_.regionClosed) ||
        (!region && !brush_.point && brush_.path.empty())) {
        buildTerrainOverlay();
        update();
        return;
    }
    const auto command = terrainCommand();
    const bool smoothing = terrainSmoothing();
    try {
        if (!smoothing && command["amount"].toDouble() == 0) {
            // A one-spot brush before the pointer has moved: the dots show what will move.
            auto spot = command;
            spot["amount"] = 1.0;
            const auto toLocal = doc_.worldTransform(brush_.terrain).inverse();
            auto data = sculptData(spot, toLocal);
            validateTerrainSculptData(terrainSculptKind, *data);
            auto preview =
                previewTerrainSculpt(doc_, brush_.terrain, terrainSculptKind, data, false);
            terrainCandidate_ = std::make_shared<const TerrainSculptPreview>(std::move(preview));
        } else {
            const auto toLocal = doc_.worldTransform(brush_.terrain).inverse();
            auto data = sculptData(command, toLocal);
            const std::string kind = smoothing ? terrainSmoothKind : terrainSculptKind;
            validateTerrainSculptData(kind, *data);
            // A hover plans only; a shown candidate also builds its tiles (and is remembered for
            // the commit).
            terrainCandidate_ = std::make_shared<const TerrainSculptPreview>(
                previewTerrainSculpt(doc_, brush_.terrain, kind, data, show));
        }
        terrainCandidateShown_ = show && terrainCandidate_->edit.has_value();
        // The command a commit of this candidate runs, so the commit is what was previewed.
        terrainCandidateCommand_ = terrainCandidateShown_ ? command : QJsonObject{};
        terrainCandidateDocument_ = doc_.identity();
        terrainCandidateRevision_ = doc_.revision();
        // Before Enter the box shows the live value (UX §4.3).
        if (brush_.stroking || brush_.anchored || brush_.regionClosed)
            emit measurementPreview(
                smoothing ? QLocale().toString(command["strength"].toDouble(), 'f', 2)
                          : displayLength(command["amount"].toDouble(), doc_.displayUnits(),
                                          doc_.displayPrecision()));
    } catch (const std::exception &error) {
        brush_.error = error.what();
        terrainCandidate_.reset();
    }
    buildTerrainOverlay();
    update();
}
// The overlay's surface geometry (the changed region's outline, a region's falloff band, the
// rings), draped on the surface with one elevation query and kept until the brush changes, so a
// frame only projects it.
void Viewport::buildTerrainOverlay() {
    terrainOverlay_ = {};
    const auto terrain = isTerrain(doc_, brush_.terrain) ? brush_.terrain : Id{};
    if (!terrain)
        return;
    std::vector<std::vector<QPointF>> outline, band;
    std::vector<QPointF> ring, halfRing;
    auto regionPath = [&] {
        QPolygonF polygon;
        for (const auto &q : brush_.region)
            polygon << QPointF(q.x, q.y);
        QPainterPath plan;
        plan.addPolygon(polygon);
        plan.closeSubpath();
        if (terrainOptions_.falloffWidth > 0) {
            QPainterPathStroker stroker;
            stroker.setWidth(2 * terrainOptions_.falloffWidth);
            stroker.setJoinStyle(Qt::RoundJoin);
            plan = (plan + stroker.createStroke(plan)).simplified();
        }
        return plan;
    };
    auto polygons = [](const QPainterPath &path) {
        std::vector<std::vector<QPointF>> result;
        for (const auto &polygon : path.toSubpathPolygons())
            result.emplace_back(polygon.begin(), polygon.end());
        return result;
    };
    const bool region = terrainOptions_.scope == TerrainToolOptions::Scope::Region;
    if (region && brush_.regionClosed && terrainOptions_.falloffWidth > 0)
        band = polygons(regionPath());
    if (terrainCandidateShown_) {
        if (region)
            outline = band.empty() ? polygons(regionPath()) : band;
        else if (!brush_.path.empty()) {
            const double r = terrainOptions_.radius;
            QPainterPath plan;
            if (brush_.path.size() == 1)
                plan.addEllipse(QPointF(brush_.path[0].x, brush_.path[0].y), r, r);
            else {
                QPainterPath line(QPointF(brush_.path[0].x, brush_.path[0].y));
                for (std::size_t i = 1; i < brush_.path.size(); ++i)
                    line.lineTo(brush_.path[i].x, brush_.path[i].y);
                QPainterPathStroker stroker;
                stroker.setWidth(2 * r);
                stroker.setCapStyle(Qt::RoundCap);
                stroker.setJoinStyle(Qt::RoundJoin);
                stroker.setCurveThreshold(r * 0.02);
                plan = stroker.createStroke(line).simplified();
            }
            outline = polygons(plan);
        }
    }
    const auto centre =
        brush_.anchored && !brush_.path.empty() ? std::optional(brush_.path.front()) : brush_.point;
    if (!region && centre)
        for (int i = 0; i < ringSegments; ++i) {
            const double angle = 2 * std::numbers::pi * i / ringSegments;
            const QPointF direction(std::cos(angle), std::sin(angle));
            ring.push_back(QPointF(centre->x, centre->y) + direction * terrainOptions_.radius);
            halfRing.push_back(QPointF(centre->x, centre->y) +
                               direction * terrainOptions_.radius * 0.5);
        }
    // One query for every point.
    std::vector<std::array<double, 2>> plan;
    auto collect = [&](const std::vector<QPointF> &points) {
        for (const auto &q : points)
            plan.push_back({q.x(), q.y()});
    };
    for (const auto &points : outline)
        collect(points);
    for (const auto &points : band)
        collect(points);
    collect(ring);
    collect(halfRing);
    std::vector<TerrainElevation> heights;
    try {
        // terrainElevationAt takes at most terrainPointBatch points a call.
        for (std::size_t first = 0; first < plan.size(); first += terrainPointBatch) {
            const auto last = std::min(plan.size(), first + terrainPointBatch);
            const auto part = terrainElevationAt(
                doc_, terrain, {plan.begin() + std::ptrdiff_t(first), plan.begin() + std::ptrdiff_t(last)});
            heights.insert(heights.end(), part.begin(), part.end());
        }
    } catch (const std::exception &) {
        return;
    }
    const double fallback = centre ? centre->z : brush_.region.empty() ? 0 : brush_.region.front().z;
    std::size_t next = 0;
    auto drape = [&](const std::vector<QPointF> &points) {
        std::vector<Vec3> result;
        for (const auto &q : points) {
            const auto &h = heights[next++];
            result.push_back({q.x(), q.y(), h.found ? h.elevation : fallback});
        }
        return result;
    };
    for (const auto &points : outline)
        terrainOverlay_.outline.push_back(drape(points));
    for (const auto &points : band)
        terrainOverlay_.band.push_back(drape(points));
    terrainOverlay_.ring = drape(ring);
    terrainOverlay_.halfRing = drape(halfRing);
}
void Viewport::commitTerrain(std::optional<double> value) {
    if (!brush_.terrain)
        return;
    // A shown candidate commits the command it was built from (a modifier pressed since, or a
    // pointer sample the release adds, never changes it); a typed value or an unpreviewed click
    // builds the command from the brush.
    const auto command = !value && terrainCandidateShown_ && !terrainCandidateCommand_.isEmpty()
                             ? terrainCandidateCommand_
                             : terrainCommand(value);
    const bool smoothing = command["mode"] == "smooth";
    if (!smoothing && command["amount"].toDouble() == 0) {
        emit message("Move up or down to set the amount, or type it");
        return;
    }
    // Committing ends the operation; the candidate's buffers are dropped with it, and the next
    // frame draws the committed tiles.
    const auto terrain = brush_.terrain;
    try {
        if (!terrainSession_.active())
            terrainSession_.begin();
        QElapsedTimer timing;
        timing.start();
        terrainSession_.commit(command, BatchResponse::Changes);
        stats_.terrainCommandMs = timing.nsecsElapsed() / 1e6;
    } catch (const std::exception &error) {
        cancelTerrainOperation();
        brush_.terrain = terrain;
        emit message(QString("Nothing changed: %1").arg(error.what()));
        return;
    }
    cancelTerrainOperation(true);
    terrainCommitted_ = command;
    prefetchTerrainDigest(terrain);
    QElapsedTimer timing;
    timing.start();
    refresh(); // rebuilds the overlay on the committed surface
    emit changed();
    stats_.terrainRefreshMs = timing.nsecsElapsed() / 1e6;
    terrainStatusShown_.clear();
    emitTerrainStatus();
    emit terrainToolChanged();
}
// R096.f1b: the commit deferred the terrain's mesh digest (TerrainDigest); a worker computes it
// now, off the frame, so a later reader (save, describe) rarely waits for it. The task holds the
// digest weakly: a digest no record holds any more (an amend replaced it, the document closed) is
// skipped, and it never throws.
void Viewport::prefetchTerrainDigest(Id terrain) {
    const auto found = doc_.bodies().find(terrain);
    if (found == doc_.bodies().end() || !found->second->terrain ||
        !found->second->terrain->meshDigest.pending())
        return;
    QThreadPool::globalInstance()->start(found->second->terrain->meshDigest.prefetchTask());
}
void Viewport::amendTerrain(double value) {
    auto command = terrainCommitted_;
    const bool smoothing = command["mode"] == "smooth";
    command[smoothing ? "strength" : "amount"] = value;
    terrainSession_.commit(command, BatchResponse::Changes); // executeAmend: one entry, revised (UX §4.3)
    terrainCommitted_ = command;
    prefetchTerrainDigest(brush_.terrain);
    refresh();
    emit changed();
    emit message(QString("Revised the %1 · %2").arg(smoothing ? "smoothing" : "sculpt")
                     .arg(smoothing ? QLocale().toString(value, 'f', 2)
                                    : displayLength(value, doc_.displayUnits(),
                                                    doc_.displayPrecision())));
    emit terrainToolChanged();
}

// ---- Measurements box (UX §15.2, §15.7) ----------------------------------------------------
bool Viewport::terrainMeasurements(const QString &text) {
    if (tool_ == Tool::Contours)
        return contourMeasurements(text);
    const auto unit = inputUnit(doc_.displayUnits());
    const auto locale = QLocale();
    QString typed = text;
    std::optional<std::array<double, 2>> position;
    if (typed.startsWith('@')) {
        if (!doc_.site())
            throw std::runtime_error("A leading @ types a site position (E, N), but this document "
                                     "has no site origin. Import a terrain, or set one in File → "
                                     "Site settings….");
        const auto values = parseSitePosition(typed.mid(1), unit, locale);
        position = std::array<double, 2>{values[0] - doc_.site()->easting,
                                         values[1] - doc_.site()->northing};
    } else
        position = typedPoint(typed, unit);
    if (position) {
        // A typed position places the brush there (UX §15.7).
        Id terrain = brush_.terrain;
        std::optional<double> z;
        if (terrain)
            z = terrainElevation(terrain, (*position)[0], (*position)[1]);
        if (!z)
            for (const auto &[id, body] : doc_.bodies())
                if (body->kind == BodyKind::Terrain && selection_.inActiveHierarchy(doc_, id))
                    if ((z = terrainElevation(id, (*position)[0], (*position)[1]))) {
                        terrain = id;
                        break;
                    }
        if (!z)
            throw std::runtime_error("That position is not on a terrain");
        const Vec3 point{(*position)[0], (*position)[1], *z};
        if (terrainOptions_.scope == TerrainToolOptions::Scope::Region) {
            if (brush_.regionClosed)
                throw std::runtime_error("The region is closed; type the value or press Escape");
            if (brush_.region.empty()) {
                if (terrainSession_.canRevise())
                    terrainCommitted_ = {};
                terrainSession_.begin();
                brush_.terrain = terrain;
            } else if (terrain != brush_.terrain)
                throw std::runtime_error("The region's points must lie on one terrain");
            if (brush_.region.size() >= 3 && planDistance(point, brush_.region.front()) <= 1e-9) {
                brush_.regionClosed = true;
                updateTerrainCandidate(true);
            } else
                brush_.region.push_back(point);
        } else {
            cancelTerrainOperation();
            terrainSession_.begin();
            brush_.terrain = terrain;
            brush_.point = point;
            brush_.path = {point};
            if (tool_ == Tool::Smooth) {
                brush_.anchored = true;
                updateTerrainCandidate(true);
            } else {
                brush_.anchored = true;
                brush_.spotAmount = 0;
                brush_.anchorPosition = project(point);
                updateTerrainCandidate(true);
            }
        }
        emitTerrainStatus();
        emit terrainToolChanged();
        update();
        return true;
    }
    const auto step = terrainMeasurementLabel();
    auto number = [&] {
        if (step == "Strength") {
            bool ok = false;
            const double value = locale.toDouble(typed, &ok);
            if (!ok || !std::isfinite(value) || value < terrainToolStrengthMin || value > 1)
                throw std::runtime_error("The smoothing strength is 0.05 to 1");
            return value;
        }
        return parseLength(typed, unit, locale);
    };
    if (step == "Amount" || step == "Strength") {
        const double value = number();
        if (step == "Amount" && (value == 0 || std::abs(value) > terrainSculptAmountMax))
            throw std::runtime_error("The amount is a height above 0 and at most 1,000 m either "
                                     "way (negative lowers)");
        if (brush_.anchored || brush_.regionClosed) {
            const auto revision = doc_.revision();
            commitTerrain(value);
            return doc_.revision() != revision;
        }
        amendTerrain(value);
        return true;
    }
    if (step == "Radius") {
        const double value = number();
        if (value < terrainSculptRadiusMin || value > terrainSculptRadiusMax)
            throw std::runtime_error("The brush radius is 0.01 to 1,000 m");
        auto options = terrainOptions_;
        options.radius = value;
        setTerrainOptions(options);
        return true;
    }
    if (step == "Falloff width") {
        const double value = number();
        if (value < 0 || value > terrainSculptWidthMax)
            throw std::runtime_error("The falloff width is 0 to 1,000 m");
        auto options = terrainOptions_;
        options.falloffWidth = value;
        setTerrainOptions(options);
        return true;
    }
    throw std::runtime_error("Type the next point as [x, y] or @E, N, or press Enter to close the "
                             "region");
}

// ---- Drawing: the candidate in place of its tiles (ADR 0169 §5) ----------------------------
void Viewport::rebuildTerrainPreview() {
    terrainPreviewDirty_ = false;
    auto &gpu = terrainPreviewGpu_;
    gpu.active = false;
    gpu.hidden.clear();
    gpu.hiddenContours.clear();
    gpu.surfaceVertices.clear();
    transparentDirty_ = true;
    // A preview that cannot be drawn is withdrawn; it never takes the viewport down.
    try {
        uploadTerrainPreview();
    } catch (const std::exception &error) {
        gpu.active = false;
        gpu.hidden.clear();
        gpu.hiddenContours.clear();
        brush_.error = QString("The preview cannot be drawn: %1").arg(error.what());
    }
}
void Viewport::uploadTerrainPreview() {
    auto &gpu = terrainPreviewGpu_;
    // The brush's candidate (R096.f1), or the worker's (R096.f2: contour edits, stamps, drapes).
    const TerrainRegionResult *region = nullptr;
    Id terrain = 0;
    if (const auto candidate = terrainPreviewShown();
        candidate && candidate->edit && terrainCandidateDocument_ == doc_.identity() &&
        terrainCandidateRevision_ == doc_.revision()) {
        region = &candidate->edit->region;
        terrain = brush_.terrain;
    } else if (const auto shown = terrainEditPreviewShown(); shown && shown->candidate) {
        region = &shown->candidate->region;
        terrain = shown->terrain;
    }
    if (!region || !isTerrain(doc_, terrain))
        return;
    const auto &record = *doc_.bodies().at(terrain);
    const auto mesh = terrainEditMeshCache().get(doc_, terrain, true);
    const auto display = terrainPreviewDisplay(*mesh, *region);
    const auto world = doc_.worldTransform(terrain);
    const bool bands = record.terrain->display.shading == TerrainShading::ElevationBands &&
                       doc_.style().mode != ModelStyleMode::Monochrome;
    const auto light = normalized(Vec3{.3, -.5, .8});
    std::vector<Vertex> surface, lines, contours, index;
    // The tile body a key stands for now (a candidate takes the appearance of the tile it
    // replaces, as the commit does: decision 7).
    auto bodyOf = [&](const TerrainTileKey &key) -> BodyPtr {
        const auto *tile = mesh->tile(key);
        if (!tile)
            return nullptr;
        const auto found = doc_.bodies().find(tile->body);
        return found == doc_.bodies().end() ? nullptr : found->second;
    };
    for (const auto &key : display.hidden)
        if (const auto body = bodyOf(key))
            gpu.hidden.insert(body->id);
    for (const auto &key : region->replaced)
        if (const auto body = bodyOf(key))
            gpu.hiddenContours.insert(body->id);
    BodyPtr fallback;
    if (!region->replaced.empty())
        fallback = bodyOf(region->replaced.front());
    for (const auto &tile : display.tiles) {
        const auto &content = *tile.content;
        auto body = bodyOf(content.key);
        if (!body)
            body = fallback;
        std::vector<Id> faces;
        if (body && !tile.candidate)
            for (const auto &[face, record] : body->surface.faces)
                faces.push_back(face);
        const float dim = body && (!selection_.inActiveHierarchy(doc_, body->id) ||
                                   selection_.locked(doc_, body->id))
                              ? .35f
                              : 1.f;
        for (std::size_t t = 0; t < content.triangles.size(); ++t) {
            const auto &triangle = content.triangles[t];
            const std::array<Vec3, 3> p{content.vertices[triangle[0]], content.vertices[triangle[1]],
                                        content.vertices[triangle[2]]};
            const auto faceNormal = cross(p[1] - p[0], p[2] - p[0]);
            if (length(faceNormal) == 0)
                continue;
            SurfaceAppearance front{{.73f, .79f, .73f}, 1, 0}, back = front;
            if (body) {
                const Id face = t < faces.size() ? faces[t] : 0;
                front = surfaceAppearance(doc_.materials(), *body, face);
                back = surfaceAppearance(doc_.materials(), *body, face, true);
            }
            if (bands)
                front.color = back.color = terrainElevationBandColor(
                    (p[0].z + p[1].z + p[2].z) / 3, display.zMin, display.zMax);
            if (doc_.style().mode == ModelStyleMode::Monochrome) {
                front.color = doc_.style().front;
                back.color = doc_.style().back;
            }
            for (std::size_t i = 0; i < 3; ++i) {
                auto normal = world.vector(tile.normal(p[i], faceNormal * (1 / length(faceNormal))));
                normal = normal * (1 / std::max(length(normal), 1e-12));
                const auto position = world.point(p[i]);
                Vertex v{position.x, position.y, position.z, front.color[0], front.color[1],
                         front.color[2]};
                v.a = 1;
                v.br = back.color[0];
                v.bg = back.color[1];
                v.bb = back.color[2];
                v.ba = 1;
                if (doc_.solar().enabled) {
                    v.light = solarLight(normal);
                    v.backLight = solarLight(normal * -1);
                } else
                    v.light = v.backLight = .64f + .36f * float(std::abs(dot(normal, light)));
                v.dim = dim;
                surface.push_back(v);
            }
        }
        for (const auto &edge : tile.outerEdges)
            for (const auto corner : edge) {
                const auto position = world.point(content.vertices[corner]);
                lines.push_back(Vertex{position.x, position.y, position.z, doc_.style().edge[0],
                                       doc_.style().edge[1], doc_.style().edge[2]});
            }
    }
    // Contours of the candidate tiles; a kept tile's contours do not change.
    const auto view = terrainViews_.find(terrain);
    if (record.terrain->display.contours && view != terrainViews_.end() && view->second->contours &&
        !view->second->contours->tooDense) {
        const std::array<float, 3> color = colors_.dark ? std::array<float, 3>{.80f, .68f, .50f}
                                                        : std::array<float, 3>{.42f, .31f, .20f};
        const auto interval = record.terrain->display.contourInterval;
        const auto offset = terrainElevationOffset(doc_, terrain);
        for (const auto &tile : region->tiles)
            for (const auto &segment : terrainTileContours(*tile, interval, offset))
                for (const auto point : {segment.a, segment.b}) {
                    const auto p = world.point(point);
                    (segment.index ? index : contours)
                        .push_back(Vertex{p.x, p.y, p.z, color[0], color[1], color[2]});
                }
    } else
        gpu.hiddenContours.clear();
    upload(gpu.surface, surface);
    gpu.surfaceVertices = std::move(surface);
    transparentDirty_ = true; // the hidden tiles leave the translucent pass
    upload(gpu.lines, lines);
    upload(gpu.contours, contours);
    upload(gpu.indexContours, index);
    gpu.terrain = terrain;
    gpu.active = true;
}
void Viewport::drawTerrainPreview() {
    if (!terrainPreviewGpu_.active)
        return;
    draw(terrainPreviewGpu_.surface, GL_TRIANGLES);
}

// ---- Drawing: the brush, the dots, the outline and the label (UX §15.2) --------------------
void Viewport::paintTerrainBrush(QPainter &p) {
    if (!terrainToolActive())
        return;
    if (tool_ == Tool::Contours)
        return paintContourTool(p);
    p.save();
    p.setRenderHint(QPainter::Antialiasing);
    const auto terrain = isTerrain(doc_, brush_.terrain) ? brush_.terrain : Id{};
    const auto world = terrain ? doc_.worldTransform(terrain) : Transform{};
    const QColor ink = colors_.ink, halo = QColor(255, 255, 255, 220);
    auto polyline = [&](const std::vector<Vec3> &points, bool closed) {
        QPolygonF polygon;
        for (const auto &point : points)
            polygon << project(point);
        if (closed && !polygon.isEmpty())
            polygon << polygon.front();
        return polygon;
    };
    // The changed region, hatched (a black and white band, so not colour alone).
    const auto candidate = terrainPreviewShown();
    if (candidate && terrain) {
        for (const auto &points : terrainOverlay_.outline) {
            const auto screen = polyline(points, true);
            p.setBrush(Qt::NoBrush);
            p.setPen(QPen(ink, 8, Qt::SolidLine, Qt::FlatCap, Qt::RoundJoin));
            p.drawPolyline(screen);
            p.setPen(QPen(Qt::white, 5, Qt::SolidLine, Qt::FlatCap, Qt::RoundJoin));
            p.drawPolyline(screen);
            QPen hatch(ink, 5, Qt::CustomDashLine, Qt::FlatCap, Qt::RoundJoin);
            hatch.setDashPattern({0.6, 0.6});
            p.setPen(hatch);
            p.drawPolyline(screen);
        }
    }
    // Region scope: the polygon so far, and its falloff band dashed outside it.
    if (!brush_.region.empty()) {
        const auto screen = polyline(brush_.region, brush_.regionClosed);
        p.setBrush(Qt::NoBrush);
        p.setPen(QPen(halo, 4));
        p.drawPolyline(screen);
        p.setPen(QPen(ink, 2));
        p.drawPolyline(screen);
        if (!brush_.regionClosed && brush_.point) {
            p.setPen(QPen(ink, 1.5, Qt::DashLine));
            p.drawLine(screen.back(), project(*brush_.point));
        }
        p.setBrush(colors_.canvas);
        for (const auto &corner : screen)
            p.drawEllipse(corner, 3.5, 3.5);
        if (brush_.regionClosed && terrainOptions_.falloffWidth > 0) {
            p.setBrush(Qt::NoBrush);
            p.setPen(QPen(ink, 1.5, Qt::DashLine));
            for (const auto &points : terrainOverlay_.band)
                p.drawPolyline(polyline(points, true));
        }
    }
    // The brush: a solid ring at the radius and a dashed ring at half strength, lying on the
    // surface; amber and dashed when it catches no vertex.
    const auto centre = brush_.anchored && !brush_.path.empty() ? std::optional(brush_.path.front())
                                                                : brush_.point;
    const bool brush = terrainOptions_.scope == TerrainToolOptions::Scope::Brush;
    const bool empty = terrainCandidate_ && terrainCandidate_->moving.empty();
    if (brush && centre && terrain && !terrainOverlay_.ring.empty()) {
        const auto outer = polyline(terrainOverlay_.ring, true);
        p.setBrush(Qt::NoBrush);
        p.setPen(QPen(halo, 4));
        p.drawPolyline(outer);
        if (empty) {
            p.setPen(QPen(colors_.warningBorder.isValid() ? colors_.warningBorder : QColor("#e3b75c"),
                          2.5, Qt::DashLine));
            p.drawPolyline(outer);
        } else {
            p.setPen(QPen(ink, 2));
            p.drawPolyline(outer);
            if (terrainOptions_.falloff != TerrainFalloff::Constant) {
                // Smooth and linear falloff weigh one half at half the radius.
                p.setPen(QPen(ink, 1.25, Qt::DashLine));
                p.drawPolyline(polyline(terrainOverlay_.halfRing, true));
            }
        }
        p.setPen(QPen(ink, 1.5));
        p.setBrush(Qt::white);
        p.drawEllipse(project(*centre), 3.5, 3.5);
    }
    // Dots on the vertices that will move, sized and shaded by their weight (pads have none).
    if (terrainCandidate_ && terrain) {
        const bool lifted = candidate && !terrainSmoothing();
        const double amount = terrainAmount();
        p.setPen(Qt::NoPen);
        std::size_t drawn = 0;
        for (const auto &[position, weight] : terrainCandidate_->moving) {
            if (++drawn > dotLimit)
                break;
            auto point = position;
            if (lifted)
                point.z += amount * weight;
            const auto at = project(world.point(point));
            if (!rect().contains(at.toPoint()))
                continue;
            QColor dot = ink;
            dot.setAlphaF(float(0.35 + 0.65 * weight));
            p.setBrush(dot);
            const double size = 1.2 + 2.3 * weight;
            p.drawEllipse(at, size, size);
        }
    }
    // The label at the brush, and why it cannot act.
    const auto view = terrainBrush();
    QString text = view.label;
    QColor textColor = ink;
    if (!brush_.error.isEmpty()) {
        text = "× " + brush_.error; // a refusal is marked, not only coloured (UX §15.7)
        textColor = colors_.failure.isValid() ? colors_.failure : QColor("#a8322b");
    }
    const auto anchor = centre ? std::optional(project(*centre))
                               : (!brush_.region.empty() ? std::optional(project(brush_.region.back()))
                                                         : std::nullopt);
    if (!text.isEmpty() && anchor) {
        const auto metrics = p.fontMetrics();
        auto boldFont = p.font();
        boldFont.setBold(true);
        const QFontMetrics boldMetrics(boldFont);
        const auto lines = text.split('\n');
        int textWidth = 0;
        for (int i = 0; i < int(lines.size()); ++i)
            textWidth = std::max(textWidth, (i == 0 && brush_.error.isEmpty() ? boldMetrics : metrics)
                                                .horizontalAdvance(lines[i]));
        const int boxWidth = std::min(textWidth + 28, std::max(1, width() - 24)); // slack: bold glyphs
        const int boxHeight = int(lines.size()) * metrics.lineSpacing() + 10;
        const QRectF box(std::clamp(anchor->x() + 28, 12.0, double(std::max(12, width() - boxWidth - 12))),
                         std::clamp(anchor->y() - boxHeight - 24, 12.0,
                                    double(std::max(12, height() - boxHeight - 12))),
                         boxWidth, boxHeight);
        p.setPen(QPen(colors_.border, 1));
        p.setBrush(colors_.surface);
        p.drawRoundedRect(box, 4, 4);
        p.setPen(textColor);
        for (int i = 0; i < int(lines.size()); ++i) {
            auto font = p.font();
            font.setBold(i == 0 && brush_.error.isEmpty());
            p.setFont(font);
            p.drawText(QRectF(box.left() + 8, box.top() + 5 + i * metrics.lineSpacing(),
                              box.width() - 16, metrics.lineSpacing()),
                       Qt::AlignLeft | Qt::AlignVCenter,
                       p.fontMetrics().elidedText(lines[i], Qt::ElideRight, int(box.width()) - 16));
        }
    }
    p.restore();
}
} // namespace sketchy
