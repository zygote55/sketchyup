// R096.f2 (ADR 0169 §5; UX_DESIGN §15.3–15.5): the worker preview of the terrain tools that plan
// off the UI thread (Edit contours here; Stamp pad and Drape use it too).
//
// - A tool asks for the preview of the command it would commit (requestTerrainEditPreview): the
//   command is parsed on the document now, as the command parses it, and planned on the worker
//   (TerrainPreviewWorker, latest wins). A command refused before planning is shown at once.
// - The newest plan delivered is shown: its candidate tiles are drawn in place of the tiles they
//   replace (the f1 display path), or its refusal is marked at the pointer. A plan of another
//   revision is never shown; Escape, a tool change and a document change withdraw it.
// - A commit runs the same command through the batch API (ToolSession) as one labelled edit, so
//   the committed tiles are the previewed candidate's (the plan is a pure function of the mesh,
//   the current sources and the command).
#include "app/unit_display.hpp"
#include "app/viewport.hpp"
#include "automation/commands.hpp"
#include "core/terrain_stamp.hpp"
#include <QElapsedTimer>
#include <QPainter>
#include <QTimer>
namespace sketchy {
std::shared_ptr<const TerrainCommandPreview> Viewport::terrainEditPreviewShown() const {
    return terrainEditShown_;
}
bool Viewport::terrainEditPreviewCurrent() const {
    return terrainEditShown_ && !terrainEditRequest_.command.isEmpty() &&
           terrainEditShown_->command == terrainEditRequest_.command;
}
void Viewport::requestTerrainEditPreview(const QJsonObject &command) {
    if (command == terrainEditRequest_.command && !command.isEmpty() &&
        terrainEditRequest_.document == doc_.identity() &&
        terrainEditRequest_.revision == doc_.revision())
        return; // already asked for
    terrainEditRequest_ = {command, 0, {}, doc_.identity(), doc_.revision()};
    try {
        terrainEditRequest_.generation =
            terrainWorker_->request(prepareTerrainEditPreview(doc_, command));
        if (!terrainEditPaintWait_->isActive())
            terrainEditPaintWait_->start();
        return; // the frame follows the plan (updateAfterTerrainEdit)
    } catch (const std::exception &error) {
        // Refused before planning (the command's own checks): nothing is planned or drawn.
        terrainEditRequest_.error = QString::fromUtf8(error.what());
        terrainWorker_->cancel();
        if (terrainEditShown_) {
            terrainEditShown_.reset();
            terrainPreviewDirty_ = true;
        }
    }
    update();
}
void Viewport::updateAfterTerrainEdit() {
    if (!terrainEditPaintWait_->isActive())
        update();
}
void Viewport::withdrawTerrainEditPreview() {
    terrainEditPaintWait_->stop();
    if (terrainWorker_)
        terrainWorker_->cancel();
    terrainEditRequest_ = {};
    if (terrainEditShown_) {
        terrainEditShown_.reset();
        terrainPreviewDirty_ = true;
    }
    update();
}
void Viewport::terrainEditPreviewFinished(qulonglong,
                                          std::shared_ptr<const TerrainCommandPreview> preview) {
    // A plan of another document or revision, or for a tool no longer active, is dropped.
    if (!preview || !terrainTool(tool_) || terrainEditRequest_.command.isEmpty() ||
        preview->document != doc_.identity() || preview->revision != doc_.revision())
        return;
    terrainEditShown_ = std::move(preview);
    terrainPreviewDirty_ = true;
    if (terrainEditPreviewCurrent())
        terrainEditPaintWait_->stop();
    stats_.terrainPlanMs = terrainEditShown_->planMs;
    emitTerrainStatus();
    emit terrainToolChanged();
    update();
}
void Viewport::holdTerrainStatus(const QString &text) {
    terrainStatusShown_ = text;
    terrainStatusHeld_ = true;
    terrainFailureStatus_.clear();
    emit message(text);
}
bool Viewport::commitTerrainEdit(const QJsonObject &command, const QString &done,
                                 QString *error) {
    QJsonObject result;
    try {
        terrainSession_.begin(); // a new edit, never an amend of the last one
        QElapsedTimer timing;
        timing.start();
        result = terrainSession_.commit(command, BatchResponse::Changes);
        stats_.terrainCommandMs = double(timing.nsecsElapsed()) / 1e6;
    } catch (const std::exception &failure) {
        terrainSession_.cancel();
        if (error)
            *error = QString::fromUtf8(failure.what());
        holdTerrainStatus(QString("Nothing changed: %1").arg(QString::fromUtf8(failure.what())));
        return false;
    }
    terrainCommitted_ = {};
    withdrawTerrainEditPreview();
    const auto terrain = Id(command["terrain"].toString().toULongLong());
    rememberTerrainReport(terrain, result);
    prefetchTerrainDigest(terrain);
    QElapsedTimer timing;
    timing.start();
    refresh();
    emit changed();
    stats_.terrainRefreshMs = double(timing.nsecsElapsed()) / 1e6;
    holdTerrainStatus(done);
    emit terrainToolChanged();
    return true;
}
// The last change's or replay's report per terrain (the Terrain edits notices, UX §15.6).
void Viewport::rememberTerrainReport(Id terrain, const QJsonObject &batchResult) {
    const auto operations = batchResult["terrainOperations"].toArray();
    if (operations.isEmpty())
        return;
    const auto operation = operations.last().toObject();
    const auto report = operation["terrainReport"].toObject();
    if (report.isEmpty())
        return;
    const auto command = operation["command"].toString();
    const QString title = command == "terrain.stamp_pad"       ? "Stamp pad"
                          : command == "terrain.drape"         ? "Drape on terrain"
                          : command == "terrain.sculpt"        ? "Sculpt terrain"
                          : command == "terrain.edit_source"   ? "Edit contour"
                          : command == "terrain.add_source"    ? "Add contour"
                          : command == "terrain.remove_source" ? "Remove contour"
                                                               : "Terrain change";
    setLastTerrainReport(terrain, report, title);
}
// A report stays the terrain's last while its edit list is the one it was made for (an Undo or a
// later change replaces or hides it).
void Viewport::setLastTerrainReport(Id terrain, const QJsonObject &report, const QString &title) {
    std::vector<TerrainEditPtr> edits;
    if (isTerrain(doc_, terrain))
        edits = doc_.bodies().at(terrain)->terrain->edits;
    terrainReports_[terrain] = {report, title, std::move(edits), doc_.saveStamp()};
    ++terrainReportGeneration_;
}
QJsonObject Viewport::lastTerrainReport(Id terrain) const {
    const auto found = terrainReports_.find(terrain);
    if (found == terrainReports_.end() || !isTerrain(doc_, terrain) ||
        !found->second.document || !doc_.owns(*found->second.document) ||
        found->second.edits != doc_.bodies().at(terrain)->terrain->edits)
        return {};
    return found->second.report;
}
QString Viewport::lastTerrainReportTitle(Id terrain) const {
    const auto found = terrainReports_.find(terrain);
    return found == terrainReports_.end() ? QString{} : found->second.title;
}
QString Viewport::sitePlace(const Vec3 &model) const {
    const auto units = doc_.displayUnits();
    const int precision = doc_.displayPrecision() == fullDisplayPrecision ? 1 : doc_.displayPrecision();
    if (const auto &site = doc_.site()) {
        const auto at = modelToSite(*site, model);
        return QString("E %1, N %2")
            .arg(displayLength(at.x, units, precision), displayLength(at.y, units, precision));
    }
    return QString("x %1, y %2")
        .arg(displayLength(model.x, units, precision), displayLength(model.y, units, precision));
}
QString Viewport::terrainSourceName(const TerrainCommandPreview &preview,
                                    std::uint32_t source) const {
    if (!source || !preview.sources)
        return "a pad's grading ray";
    const auto *found = preview.sources->find(source);
    if (!found)
        return QString("source %1").arg(source);
    if (const auto pad = preview.sources->padSourceOwner(source))
        return QString("the outline of Pad (change #%1)").arg(pad);
    if (const auto drape = preview.sources->drapeSourceOwner(source))
        return QString("a draped line (change #%1)").arg(drape);
    switch (found->role) {
    case TerrainSourceRole::Contour: {
        const double z = found->points.empty() ? 0 : preview.world.point(found->points.front()).z;
        return "contour " + displayLength(z, doc_.displayUnits(), 2);
    }
    case TerrainSourceRole::Breakline:
        return QString("breakline %1").arg(source);
    case TerrainSourceRole::Points:
        return "spot points";
    default:
        return "the outline";
    }
}
Viewport::TerrainEditFailure Viewport::terrainEditFailure(const TerrainCommandPreview &preview) const {
    TerrainEditFailure failure;
    if (!preview.refused())
        return failure;
    const TerrainEditProblem *problem = nullptr;
    std::size_t index = 0;
    for (std::size_t i = 0; i < preview.problems.size() && !problem; ++i)
        if (!preview.problems[i].locations.empty()) {
            problem = &preview.problems[i];
            index = i;
        }
    if (!problem) {
        failure.label = QString::fromUtf8(preview.message.c_str());
        failure.status = failure.label;
        return failure;
    }
    const auto &location = problem->locations.front();
    const auto at = preview.world.point({location.xy.x, location.xy.y, 0});
    // The first conflict lies on the surface (or on the pad, for a stamp).
    Vec3 model = at;
    if (const auto z = terrainElevation(preview.terrain, at.x, at.y))
        model.z = *z;
    failure.at = model;
    const auto &kind = problem->kind;
    const bool pad = preview.kind == terrainStampKind;
    auto change = [&](std::uint64_t edit) { return QString(" (change #%1)").arg(edit); };
    if (kind == "crossing-constraints") {
        const auto crossed = index < preview.crossed.size() && !preview.crossed[index].empty()
                                 ? preview.crossed[index].front()
                                 : 0;
        failure.label = (pad ? "Slope crosses " : "Crosses ") + terrainSourceName(preview, crossed);
    } else if (kind == "outside-outline")
        failure.label = pad ? "Ray at " + sitePlace(model) + " leaves the terrain"
                            : QString("Leaves the terrain's outline");
    else if (kind == "daylight-distance")
        failure.label = "Ray at " + sitePlace(model) + " finds no daylight within the maximum "
                                                       "distance";
    else if (kind == "inside-pad")
        failure.label = (pad ? "Overlaps Pad" : "Enters Pad") + change(location.sourceB);
    else if (kind == "crossed-by-change") {
        const QString what = preview.role == TerrainSourceRole::Breakline ? "This breakline"
                                                                          : "This contour";
        const auto owner = location.sourceB;
        const bool drape = preview.sources && owner &&
                           std::any_of(preview.sources->sources().begin(),
                                       preview.sources->sources().end(), [&](const auto *s) {
                                           return preview.sources->drapeSourceOwner(s->id) == owner;
                                       });
        failure.label = drape ? what + " is crossed by Drape" + change(owner) +
                                    "; remove that change to re-elevate it alone"
                              : what + " shares a vertex with Pad" + change(owner) +
                                    "; change or remove the pad to re-elevate it alone";
    } else if (kind == "pad-source")
        failure.label =
            "This line is the outline or daylight line of Pad" +
            change(preview.sources ? preview.sources->padSourceOwner(std::uint32_t(location.sourceA))
                                   : 0) +
            "; change the pad in Terrain edits";
    else if (kind == "drape-source")
        failure.label =
            "This line was draped on the terrain" +
            change(preview.sources ? preview.sources->drapeSourceOwner(std::uint32_t(location.sourceA))
                                   : 0) +
            "; remove the drape in Terrain edits";
    else if (kind == "conflicting-duplicate")
        failure.label = "Meets another source at another elevation";
    else if (kind == "triangle-budget")
        failure.label = "Needs more terrain triangles than the document allows";
    else
        failure.label = QString::fromUtf8(problem->message.c_str());
    failure.status = failure.label + " at " + sitePlace(model) + ".";
    return failure;
}
// The refusal of the shown plan at the pointer: an × at the first conflict (UX §15.3–§15.5;
// not colour alone). The tools draw their own geometry and labels.
void Viewport::paintTerrainEditPreview(QPainter &p) {
    const auto shown = terrainEditPreviewShown();
    if (!shown || !shown->refused())
        return;
    const auto failure = terrainEditFailure(*shown);
    if (!failure.at)
        return;
    const auto at = project(*failure.at);
    const QColor red = colors_.failure.isValid() ? colors_.failure : QColor("#a8322b");
    p.save();
    p.setRenderHint(QPainter::Antialiasing);
    for (const auto &[colour, width] : {std::pair{QColor(Qt::white), 5.0}, std::pair{red, 2.4}}) {
        p.setPen(QPen(colour, width, Qt::SolidLine, Qt::RoundCap));
        p.drawLine(at + QPointF(-6, -6), at + QPointF(6, 6));
        p.drawLine(at + QPointF(6, -6), at + QPointF(-6, 6));
    }
    p.restore();
}
} // namespace sketchy
