#include "app/terrain_actions.hpp"
#include "app/accessible_names.hpp"
#include "app/terrain_report_view.hpp"
#include "app/terrain_role_table.hpp"
#include "app/viewport.hpp"
#include "automation/inspection.hpp"
#include "io/terrain_import.hpp"
#include <QApplication>
#include <QCheckBox>
#include <QClipboard>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFrame>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QJsonArray>
#include <QLabel>
#include <QLocale>
#include <QMessageBox>
#include <QPointer>
#include <QProgressBar>
#include <QPushButton>
#include <QStyle>
#include <QTableWidget>
#include <QTreeWidget>
#include <QVBoxLayout>
#include <array>
#include <cmath>
#include <cstring>
#include <limits>
#include <set>
#include <tuple>
namespace sketchy {
namespace {
QString count(std::size_t value) { return QLocale().toString(qulonglong(value)); }
QString plural(std::size_t value, const QString &one, const QString &many) {
    return count(value) + ' ' + (value == 1 ? one : many);
}
QLabel *text(const QString &value, const QString &name) {
    auto *label = new QLabel(value);
    label->setObjectName(name);
    label->setTextFormat(Qt::PlainText);
    label->setWordWrap(true);
    return label;
}
QLabel *title(const QString &value) {
    auto *label = text(value, "terrainDialogTitle");
    label->setAccessibleName(value);
    return label;
}
// A banner in the failure colour (refused, or not available) or the warning colour (notices).
QFrame *banner(const QString &value, bool failure) {
    auto *frame = new QFrame;
    frame->setObjectName("terrainDialogBanner");
    frame->setProperty("tone", failure ? "failure" : "notice");
    auto *row = new QHBoxLayout(frame);
    row->setContentsMargins(12, 8, 12, 8);
    auto *label = text(value, "terrainDialogBannerText");
    label->setAccessibleName(value);
    row->addWidget(label, 1);
    return frame;
}
QTreeWidget *reportTree(const QString &name) {
    auto *list = new QTreeWidget;
    list->setObjectName(name);
    list->setAccessibleName("Report: refusals, merges and notices");
    list->setHeaderHidden(true);
    list->setRootIsDecorated(true);
    list->setUniformRowHeights(true);
    list->setMinimumHeight(120);
    return list;
}
// The refusal's bounded report from a batch error (InspectionError details), or empty.
QJsonObject refusalReport(const std::exception &error) {
    if (const auto *inspection = dynamic_cast<const InspectionError *>(&error))
        return inspection->details()["terrainReport"].toObject();
    return {};
}
QString firstRefusal(const QJsonObject &terrain) {
    const auto refusals = terrain["refusals"].toArray();
    auto first = refusals.isEmpty() ? QString() : refusals.first().toObject()["message"].toString();
    if (first.endsWith('.'))
        first.chop(1);
    return first;
}
QString terrainName(const Document &doc, Id terrain) {
    return QString::fromStdString(doc.bodies().at(terrain)->name);
}
// Commits one terrain command and returns the batch response's operation record.
QJsonObject commitTerrain(Viewport &view, const QJsonObject &command) {
    const auto result = view.commitTerrainCommand(command);
    const auto operations = result["terrainOperations"].toArray();
    return operations.isEmpty() ? QJsonObject{} : operations.first().toObject();
}
} // namespace

Id terrainOfBody(const Document &doc, Id body) {
    const auto found = doc.bodies().find(body);
    if (found == doc.bodies().end())
        return 0;
    if (found->second->kind == BodyKind::Terrain)
        return body;
    if (found->second->terrainTile && isTerrain(doc, found->second->parent))
        return found->second->parent;
    return 0;
}
Id selectedTerrain(const Document &doc, const Selection &selection) {
    const auto &entities = selection.entities();
    if (entities.empty())
        return 0;
    Id terrain = 0;
    for (const auto &entity : entities) {
        const auto owner = terrainOfBody(doc, entity.body);
        if (!owner || (terrain && owner != terrain))
            return 0;
        terrain = owner;
    }
    return terrain;
}
QJsonArray selectedTerrainEdges(const Selection &selection) {
    QJsonArray edges;
    for (const auto &entity : selection.entities())
        if (entity.kind == SelectionKind::Edge)
            edges.append(QJsonObject{{"body", QString::number(entity.body)},
                                     {"edge", QString::number(entity.entity)}});
    return edges;
}
TerrainDrapeInput terrainDrapeInput(const Document &doc, const Selection &selection) {
    TerrainDrapeInput input;
    std::set<std::pair<Id, Id>> faces;
    for (const auto &entity : selection.entities()) {
        const auto found = doc.bodies().find(entity.body);
        if (found == doc.bodies().end() || terrainOfBody(doc, entity.body))
            continue;
        if (entity.kind == SelectionKind::Edge)
            input.edges.append(QJsonObject{{"body", QString::number(entity.body)},
                                           {"edge", QString::number(entity.entity)}});
        else if (entity.kind == SelectionKind::Face)
            faces.insert({entity.body, entity.entity});
        else if (entity.kind == SelectionKind::Body)
            for (const auto &[id, body] : doc.bodies()) {
                if (body->kind != BodyKind::Geometry || body->terrainTile)
                    continue;
                bool inside = false;
                for (Id at = id; at && !inside;) {
                    inside = at == entity.body;
                    const auto parent = doc.bodies().find(at);
                    at = parent == doc.bodies().end() ? 0 : parent->second->parent;
                }
                if (inside)
                    for (const auto &[face, record] : body->surface.faces)
                        faces.insert({id, face});
            }
    }
    for (const auto &[body, face] : faces)
        input.faces.append(
            QJsonObject{{"body", QString::number(body)}, {"face", QString::number(face)}});
    return input;
}
namespace {
using ContourPoints = std::set<std::tuple<double, double, double>>;
ContourPoints contourPoints(const TerrainSources &sources) {
    ContourPoints contour;
    for (const auto &source : sources)
        if (source.role == TerrainSourceRole::Contour)
            for (const auto &p : source.points)
                contour.insert({p.x, p.y, p.z});
    return contour;
}
// One face's flat-area test: three contour vertices at one elevation (within 1e-9 m); its planar
// area when it is flat. Shared by the full scan and the per-tile cache, so both give the same
// bits.
std::optional<double> flatTriangleArea(const Surface &surface, const Face &face,
                                       const ContourPoints &contour) {
    const auto &loop = face.loops.front();
    if (loop.size() != 3)
        return std::nullopt;
    std::array<Vec3, 3> p;
    bool onContours = true;
    for (int i = 0; i < 3; ++i) {
        p[i] = surface.vertices.at(loop[i]);
        onContours &= contour.contains({p[i].x, p[i].y, p[i].z});
    }
    if (!onContours || std::abs(p[1].z - p[0].z) > 1e-9 || std::abs(p[2].z - p[0].z) > 1e-9)
        return std::nullopt;
    return std::abs((p[1].x - p[0].x) * (p[2].y - p[0].y) - (p[2].x - p[0].x) * (p[1].y - p[0].y)) /
           2;
}
} // namespace
TerrainFlatArea terrainFlatArea(const Document &doc, Id terrain) {
    TerrainFlatArea flat;
    if (!isTerrain(doc, terrain))
        return flat;
    const auto contour = contourPoints(doc.bodies().at(terrain)->terrain->sources);
    for (const auto id : terrainTiles(doc, terrain)) {
        const auto &surface = doc.bodies().at(id)->surface;
        for (const auto &[face, record] : surface.faces)
            if (const auto area = flatTriangleArea(surface, record, contour)) {
                ++flat.triangles;
                flat.area += *area;
            }
    }
    return flat;
}
void TerrainStatisticsCache::clear() {
    document_.clear();
    terrain_ = 0;
    sources_ = TerrainSources();
    hasSources_ = false;
    world_ = Transform();
    contour_.reset();
    tiles_.clear();
}
TerrainStatistics TerrainStatisticsCache::get(const Document &doc, Id terrain) {
    TerrainStatistics result;
    if (!isTerrain(doc, terrain)) {
        clear(); // holds nothing for a terrain that is gone
        return result;
    }
    const auto &record = *doc.bodies().at(terrain)->terrain;
    const auto world = doc.worldTransform(terrain);
    // The base sources (flat areas) and the world transform (elevations, compared bit for bit:
    // -0.0 and 0.0 give different signed-zero results) feed every tile.
    const auto sameBits = [](const Transform &a, const Transform &b) {
        return std::memcmp(a.m.data(), b.m.data(), sizeof(a.m)) == 0;
    };
    if (doc.identity() != document_ || terrain != terrain_ || !hasSources_ ||
        sources_.block() != record.sources.block() || !sameBits(world_, world)) {
        clear();
        document_ = doc.identity();
        terrain_ = terrain;
        sources_ = record.sources;
        hasSources_ = true;
        world_ = world;
    }
    if (!contour_)
        contour_ = std::make_shared<const ContourPoints>(contourPoints(record.sources));
    const auto ids = terrainTiles(doc, terrain); // ID order, as the full scan
    ++generation_;
    result.tiles = ids.size();
    result.low = std::numeric_limits<double>::infinity();
    result.high = -result.low;
    for (const auto id : ids) {
        const auto &body = doc.bodies().at(id);
        auto found = tiles_.find(id);
        if (found == tiles_.end() || found->second.body != body) {
            // Scanned aside and stored only when complete, so a throw leaves no partial tile.
            Tile scanned;
            scanned.low = std::numeric_limits<double>::infinity();
            scanned.high = -scanned.low;
            for (const auto &[vertex, point] : body->surface.vertices) {
                const auto z = world.point(point).z;
                scanned.low = std::min(scanned.low, z);
                scanned.high = std::max(scanned.high, z);
            }
            for (const auto &[face, faceRecord] : body->surface.faces)
                if (const auto area = flatTriangleArea(body->surface, faceRecord, *contour_))
                    scanned.flatAreas.push_back(*area);
            scanned.body = body;
            ++scans_;
            found = tiles_.insert_or_assign(id, std::move(scanned)).first;
        }
        auto &tile = found->second;
        tile.generation = generation_;
        result.triangles += body->surface.faces.size();
        result.low = std::min(result.low, tile.low);
        result.high = std::max(result.high, tile.high);
        // The same additions in the same order as the full scan.
        for (const auto area : tile.flatAreas) {
            ++result.flat.triangles;
            result.flat.area += area;
        }
    }
    // Tiles the terrain no longer has (an edit's replaced tiles take new IDs).
    if (tiles_.size() != ids.size())
        std::erase_if(tiles_, [&](const auto &item) { return item.second.generation != generation_; });
    return result;
}
QString terrainToolRefusal(const QString &tool) {
    return tool + " does not edit terrain. Edit it with the terrain tools, or explode it to work "
                  "on it as ordinary geometry.";
}

// ---- Report --------------------------------------------------------------------------------
TerrainReportDialog::TerrainReportDialog(const QString &titleText, const QString &bannerText,
                                         bool refused, const QJsonObject &report,
                                         QWidget *parent)
    : Settled<QDialog>(parent), title_(titleText), report_(report) {
    setObjectName("terrainReportDialog");
    setWindowTitle(titleText);
    setModal(true);
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(20, 16, 20, 16);
    layout->setSpacing(10);
    layout->addWidget(title(titleText));
    layout->addWidget(banner(bannerText, refused));
    const auto terrain = report["terrain"].toObject();
    list_ = reportTree("terrainReportList");
    fillTerrainReport(list_, terrain, report["units"].toString());
    if (list_->topLevelItemCount())
        list_->setCurrentItem(list_->topLevelItem(0));
    layout->addWidget(list_, 1);
    list_->setVisible(list_->topLevelItemCount() > 0);
    auto *footer = new QHBoxLayout;
    auto *copy = new QPushButton("Copy report");
    copy->setObjectName("terrainReportCopy");
    copy->setAccessibleName("Copy report as text");
    copy->setAutoDefault(false);
    footer->addWidget(copy);
    footer->addStretch(1);
    auto *close = new QPushButton("Close");
    close->setObjectName("terrainReportClose");
    close->setAutoDefault(false);
    close->setDefault(true);
    footer->addWidget(close);
    layout->addLayout(footer);
    connect(copy, &QPushButton::clicked, this,
            [this] { QGuiApplication::clipboard()->setText(reportText()); });
    connect(close, &QPushButton::clicked, this, &QDialog::accept);
    resize(560, 420);
}
QString TerrainReportDialog::reportText() const {
    const auto terrain = report_["terrain"].toObject();
    QStringList out{title_ + " report", "Outcome: " + terrain["outcome"].toString()};
    if (terrain.contains("triangles"))
        out << QString("Triangles: %1 · Tiles: %2")
                   .arg(count(std::size_t(terrain["triangles"].toDouble())),
                        count(std::size_t(terrain["tiles"].toDouble())));
    out << terrainReportLines(terrain, report_["units"].toString());
    return out.join('\n') + '\n';
}

// ---- Explode -------------------------------------------------------------------------------
TerrainExplodeDialog::TerrainExplodeDialog(const Document &doc, Id terrain, QWidget *parent,
                                           std::optional<std::size_t> faceLimit)
    : Settled<QDialog>(parent), budget_(terrainExplodeBudget(doc, terrain)) {
    if (faceLimit)
        budget_.faceLimit = *faceLimit;
    setObjectName("terrainExplodeDialog");
    setWindowTitle("Explode terrain");
    setModal(true);
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(20, 16, 20, 16);
    layout->setSpacing(10);
    layout->addWidget(title("Explode terrain"));
    const auto name = terrainName(doc, terrain);
    layout->addWidget(text(QString("“%1” becomes an ordinary group of its %2. Its sources and "
                                   "settings are removed, so it can no longer be regenerated. "
                                   "Undo restores it.")
                               .arg(name, plural(budget_.terrainFaces, "triangle", "triangles")),
                           "terrainExplodeSummary"));
    auto *grid = new QGridLayout;
    grid->setHorizontalSpacing(12);
    grid->setVerticalSpacing(6);
    grid->setColumnStretch(1, 1);
    auto row = [&](int at, const QString &caption, const QString &value, const QString &name) {
        auto *label = text(caption, "terrainBudgetCaption");
        grid->addWidget(label, at, 0, Qt::AlignTop);
        auto *figure = text(value, name);
        figure->setAccessibleName(caption + ": " + value);
        grid->addWidget(figure, at, 1);
    };
    row(0, "Modelling faces",
        QString("%1 now + %2 = %3 of %4")
            .arg(count(budget_.modelFaces), count(budget_.terrainFaces),
                 count(budget_.facesAfter()), count(budget_.faceLimit)),
        "terrainExplodeFaces");
    const auto remaining = budget_.documentTerrainTriangles > budget_.terrainFaces
                               ? budget_.documentTerrainTriangles - budget_.terrainFaces
                               : 0;
    row(1, "Terrain triangles",
        QString("%1 now − %2 = %3 of %4 (%2 freed)")
            .arg(count(budget_.documentTerrainTriangles), count(budget_.terrainFaces),
                 count(remaining), count(budget_.terrainLimit)),
        "terrainExplodeTriangles");
    layout->addLayout(grid);
    if (!budget_.fits())
        layout->addWidget(banner(
            QString("Explode is not available: the model would have %1 faces, over the %2-face "
                    "modelling limit. Remove other geometry first, or keep it as a terrain.")
                .arg(count(budget_.facesAfter()), count(budget_.faceLimit)),
            true));
    layout->addStretch(1);
    auto *buttons = new QHBoxLayout;
    buttons->addStretch(1);
    auto *cancel = new QPushButton("Cancel");
    cancel->setObjectName("terrainExplodeCancel");
    cancel->setAutoDefault(false);
    cancel->setDefault(true); // Cancel is the default (ADR 0168, Interaction design).
    auto *explode = new QPushButton("Explode");
    explode->setObjectName("terrainExplodeConfirm");
    explode->setAccessibleName("Explode terrain");
    explode->setAutoDefault(false);
    explode->setEnabled(budget_.fits());
    explode->setToolTip(budget_.fits() ? QString("Turn the terrain into ordinary geometry")
                                       : QString("The faces would exceed the modelling limit"));
    buttons->addWidget(cancel);
    buttons->addWidget(explode);
    layout->addLayout(buttons);
    connect(cancel, &QPushButton::clicked, this, &QDialog::reject);
    connect(explode, &QPushButton::clicked, this, [this] {
        if (canExplode())
            accept();
    });
    cancel->setFocus(Qt::OtherFocusReason);
    resize(520, sizeHint().height());
}

// ---- Regenerate ----------------------------------------------------------------------------
TerrainRegenerateDialog::TerrainRegenerateDialog(const TerrainPaint &paint, QWidget *parent)
    : Settled<QDialog>(parent) {
    setObjectName("terrainRegenerateDialog");
    setWindowTitle("Regenerate terrain");
    setModal(true);
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(20, 16, 20, 16);
    layout->setSpacing(10);
    layout->addWidget(title("Regenerate terrain"));
    layout->addWidget(text("Regenerate rebuilds the terrain from its stored sources. Paint on its "
                           "tiles cannot follow the new mesh and will be removed:",
                           "terrainRegenerateSummary"));
    QStringList lost;
    if (paint.faces)
        lost << "• " + plural(paint.faces, "face", "faces") + " with their own paint";
    if (paint.tiles)
        lost << "• " + plural(paint.tiles, "tile", "tiles") + " with their own material or colour";
    auto *list = text(lost.join('\n'), "terrainRegeneratePaint");
    list->setAccessibleName(lost.join(", "));
    layout->addWidget(list);
    layout->addWidget(text("The terrain's own material is kept. Undo restores the paint.",
                           "terrainDialogNote"));
    auto *buttons = new QHBoxLayout;
    buttons->addStretch(1);
    auto *cancel = new QPushButton("Cancel");
    cancel->setObjectName("terrainRegenerateCancel");
    cancel->setAutoDefault(false);
    cancel->setDefault(true);
    auto *regenerate = new QPushButton("Regenerate");
    regenerate->setObjectName("terrainRegenerateConfirm");
    regenerate->setAutoDefault(false);
    buttons->addWidget(cancel);
    buttons->addWidget(regenerate);
    layout->addLayout(buttons);
    connect(cancel, &QPushButton::clicked, this, &QDialog::reject);
    connect(regenerate, &QPushButton::clicked, this, &QDialog::accept);
    cancel->setFocus(Qt::OtherFocusReason);
    resize(480, sizeHint().height());
}

TerrainResetEditsDialog::TerrainResetEditsDialog(const Document &doc, Id terrain, QWidget *parent)
    : Settled<QDialog>(parent) {
    setObjectName("terrainResetEditsDialog");
    setWindowTitle("Reset terrain edits");
    setModal(true);
    const auto &body = *doc.bodies().at(terrain);
    const auto count = body.terrain->edits.size();
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(20, 16, 20, 16);
    layout->setSpacing(10);
    layout->addWidget(title("Reset terrain edits"));
    layout->addWidget(text(QString("Reset %1 to %2? The terrain is rebuilt from its sources.")
                               .arg(plural(count, "change", "changes"),
                                    QString::fromStdString(body.name)),
                           "terrainResetEditsSummary"));
    layout->addWidget(text("Undo restores the changes.", "terrainDialogNote"));
    auto *buttons = new QHBoxLayout;
    buttons->addStretch(1);
    auto *cancel = new QPushButton("Cancel");
    cancel->setObjectName("terrainResetEditsCancel");
    cancel->setAutoDefault(false);
    cancel->setDefault(true);
    auto *reset = new QPushButton("Reset edits");
    reset->setObjectName("terrainResetEditsConfirm");
    reset->setAutoDefault(false);
    buttons->addWidget(cancel);
    buttons->addWidget(reset);
    layout->addLayout(buttons);
    connect(cancel, &QPushButton::clicked, this, &QDialog::reject);
    connect(reset, &QPushButton::clicked, this, &QDialog::accept);
    cancel->setFocus(Qt::OtherFocusReason);
    resize(480, sizeHint().height());
}

// ---- Settings ------------------------------------------------------------------------------
struct TerrainSettingsDryRun {
    QJsonObject command;
    TerrainParameters parameters;
    bool ok{}, unchanged{};
    QString message;
    QJsonObject report;
    std::size_t triangles{}, tiles{};
};
namespace {
std::shared_ptr<TerrainSettingsDryRun> settingsDryRun(std::shared_ptr<const TerrainRecord> record,
                                                      TerrainParameters parameters,
                                                      QJsonObject command, std::size_t other,
                                                      TerrainPaint paint) {
    auto run = std::make_shared<TerrainSettingsDryRun>();
    run->command = std::move(command);
    run->parameters = parameters;
    // The paint notices regenerate adds (R095.c2): tiles are rebuilt fresh.
    TerrainReport notices;
    if (paint.faces)
        notices.add(TerrainReport::Severity::Notice, "face-paint-dropped", paint.faces,
                    "Per-face paint on the old tiles cannot follow a new mesh and was removed; "
                    "the terrain's own material is kept.");
    if (paint.tiles)
        notices.add(TerrainReport::Severity::Notice, "tile-paint-dropped", paint.tiles,
                    "Materials and colours of individual tiles were removed with the old tiles; "
                    "the terrain's own material is kept.");
    auto result = runTerrainRegenerate(*record, parameters, other, std::move(notices));
    run->ok = result.ok;
    run->message = QString::fromStdString(result.message);
    run->report = result.report;
    run->triangles = result.mesh.triangles.size();
    run->tiles = result.tiles;
    // Restating the stored parameters is not a settings change (Regenerate rebuilds a
    // terrain with its own settings), and through the batch it may commit nothing.
    run->unchanged = parameters == record->parameters;
    return run;
}
} // namespace

TerrainSettingsDialog::TerrainSettingsDialog(const Document &doc, Id terrain, QWidget *parent)
    : Settled<QDialog>(parent), doc_(doc), terrain_(terrain) {
    if (!isTerrain(doc, terrain))
        throw std::runtime_error("Select a terrain to change its settings");
    const auto &body = *doc.bodies().at(terrain);
    record_ = std::make_shared<const TerrainRecord>(*body.terrain);
    paint_ = terrainTilePaint(doc, terrain);
    const auto total = doc.materializedTotals().terrainTriangles;
    const auto own = terrainTriangleCount(doc, terrain);
    otherTriangles_ = total > own ? total - own : 0;
    pool_.setMaxThreadCount(1);
    rerun_.setSingleShot(true);
    connect(&rerun_, &QTimer::timeout, this, [this] { start(); });

    setObjectName("terrainSettingsDialog");
    setWindowTitle("Terrain settings");
    setModal(true);
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(20, 16, 20, 16);
    layout->setSpacing(10);
    layout->addWidget(title("Terrain settings"));
    layout->addWidget(text(QString("“%1” · %2 · the settings rebuild the mesh from its stored "
                                   "sources in one undoable edit")
                               .arg(QString::fromStdString(body.name),
                                    plural(own, "triangle", "triangles")),
                           "terrainDialogNote"));
    auto *grid = new QGridLayout;
    grid->setHorizontalSpacing(12);
    grid->setVerticalSpacing(8);
    grid->setColumnStretch(1, 1);
    boundary_ = new QComboBox;
    boundary_->setObjectName("terrainSettingsBoundary");
    boundary_->setAccessibleName("Boundary mode");
    boundary_->addItem("Convex hull", "convexHull");
    boundary_->addItem("Limit edge length", "maxEdgeLength");
    boundary_->addItem("Boundary source", "boundary");
    grid->addWidget(text("Boundary", "terrainBudgetCaption"), 0, 0);
    grid->addWidget(boundary_, 0, 1);
    maxEdge_ = new QDoubleSpinBox;
    maxEdge_->setObjectName("terrainSettingsMaxEdge");
    maxEdge_->setAccessibleName("Maximum edge length in metres");
    maxEdge_->setRange(0, 2e6);
    maxEdge_->setDecimals(3);
    maxEdge_->setSuffix(" m");
    maxEdge_->setSpecialValueText("Not set");
    maxEdge_->setKeyboardTracking(false);
    grid->addWidget(text("Max edge length", "terrainBudgetCaption"), 1, 0);
    grid->addWidget(maxEdge_, 1, 1);
    flip_ = new QCheckBox("Flip flat triangles");
    flip_->setObjectName("terrainSettingsFlip");
    grid->addWidget(flip_, 2, 0, 1, 2);
    steiner_ = new QCheckBox("Fill remaining flat areas with Steiner points");
    steiner_->setObjectName("terrainSettingsSteiner");
    grid->addWidget(steiner_, 3, 0, 1, 2);
    layout->addLayout(grid);
    status_ = text({}, "terrainSettingsStatus");
    layout->addWidget(status_);
    list_ = reportTree("terrainSettingsReport");
    layout->addWidget(list_, 1);
    auto *footer = new QHBoxLayout;
    copy_ = new QPushButton("Copy report");
    copy_->setObjectName("terrainSettingsCopyReport");
    copy_->setAccessibleName("Copy report as text");
    copy_->setAutoDefault(false);
    footer->addWidget(copy_);
    footer->addStretch(1);
    auto *cancel = new QPushButton("Cancel");
    cancel->setObjectName("terrainSettingsCancel");
    cancel->setAutoDefault(false);
    footer->addWidget(cancel);
    apply_ = new QPushButton("Apply");
    apply_->setObjectName("terrainSettingsApply");
    apply_->setAccessibleName("Apply terrain settings");
    apply_->setProperty("accentFill", true);
    apply_->setAutoDefault(false);
    apply_->setDefault(true);
    apply_->setEnabled(false);
    footer->addWidget(apply_);
    layout->addLayout(footer);

    // The stored parameters.
    const auto &parameters = record_->parameters;
    boundary_->setCurrentIndex(parameters.boundary == TerrainBoundaryMode::ConvexHull ? 0
                               : parameters.boundary == TerrainBoundaryMode::MaxEdgeLength ? 1
                                                                                            : 2);
    maxEdge_->setValue(parameters.maxEdgeLength);
    flip_->setChecked(parameters.flipFlatTriangles);
    steiner_->setChecked(parameters.steinerFill);

    connect(boundary_, &QComboBox::currentIndexChanged, this, [this] { changed(0); });
    connect(maxEdge_, &QDoubleSpinBox::valueChanged, this, [this] { changed(300); });
    connect(flip_, &QCheckBox::toggled, this, [this] { changed(0); });
    connect(steiner_, &QCheckBox::toggled, this, [this] { changed(0); });
    connect(copy_, &QPushButton::clicked, this, [this] {
        const auto terrain = report()["terrain"].toObject();
        QStringList out{"Terrain settings dry run", status_->text()};
        out << terrainReportLines(terrain, "m");
        QGuiApplication::clipboard()->setText(out.join('\n') + '\n');
    });
    connect(cancel, &QPushButton::clicked, this, &QDialog::reject);
    connect(apply_, &QPushButton::clicked, this, [this] {
        if (canApply())
            accept();
    });
    nameInternalEditors(this);
    resize(560, 500);
    changed(0);
}
TerrainSettingsDialog::~TerrainSettingsDialog() {
    rerun_.stop();
    pool_.waitForDone();
}
TerrainParameters TerrainSettingsDialog::chosen() const {
    TerrainParameters parameters;
    const auto mode = boundary_->currentData().toString();
    parameters.boundary = mode == "convexHull"      ? TerrainBoundaryMode::ConvexHull
                          : mode == "maxEdgeLength" ? TerrainBoundaryMode::MaxEdgeLength
                                                    : TerrainBoundaryMode::Boundary;
    parameters.maxEdgeLength = maxEdge_->value();
    parameters.flipFlatTriangles = flip_->isChecked();
    parameters.steinerFill = steiner_->isChecked();
    return parameters;
}
QJsonObject TerrainSettingsDialog::command() const {
    const auto parameters = chosen();
    return {{"command", "terrain.set_parameters"},
            {"terrain", QString::number(terrain_)},
            {"boundary", boundary_->currentData().toString()},
            {"maxEdgeLength", parameters.maxEdgeLength},
            {"flipFlatTriangles", parameters.flipFlatTriangles},
            {"steinerFill", parameters.steinerFill}};
}
bool TerrainSettingsDialog::checking() const { return running_ || rerun_.isActive(); }
bool TerrainSettingsDialog::canApply() const {
    return problem_.isEmpty() && !checking() && dryRun_ && dryRun_->command == command() &&
           dryRun_->ok && !dryRun_->unchanged;
}
QJsonObject TerrainSettingsDialog::report() const {
    return dryRun_ && problem_.isEmpty() ? dryRun_->report : QJsonObject();
}
void TerrainSettingsDialog::changed(int delayMs) {
    // Contradictory choices are refused before any triangulation, as the command does.
    problem_.clear();
    try {
        validateTerrainParameters(chosen(), record_->sources);
    } catch (const std::exception &error) {
        problem_ = QString::fromUtf8(error.what());
    }
    if (problem_.isEmpty())
        rerun_.start(delayMs);
    else
        rerun_.stop();
    refresh();
}
void TerrainSettingsDialog::start() {
    if (running_) {
        rerun_.start(50);
        return;
    }
    running_ = true;
    const auto generation = ++generation_;
    QPointer<TerrainSettingsDialog> guard(this);
    // The worker owns copies of its inputs and never reads the document.
    pool_.start([record = record_, parameters = chosen(), command = command(),
                 other = otherTriangles_, paint = paint_, generation, guard] {
        std::shared_ptr<TerrainSettingsDryRun> result;
        try {
            result = settingsDryRun(record, parameters, command, other, paint);
        } catch (const std::exception &error) {
            result = std::make_shared<TerrainSettingsDryRun>();
            result->command = command;
            result->message = QString::fromUtf8(error.what());
        }
        QMetaObject::invokeMethod(
            qApp,
            [guard, generation, result = std::move(result)]() mutable {
                if (guard)
                    guard->finished(generation, std::move(result));
            },
            Qt::QueuedConnection);
    });
    refresh();
}
void TerrainSettingsDialog::finished(std::uint64_t generation,
                                     std::shared_ptr<TerrainSettingsDryRun> result) {
    running_ = false;
    if (generation == generation_)
        dryRun_ = std::move(result);
    if (dryRun_ && dryRun_->command != command() && problem_.isEmpty() && !rerun_.isActive())
        rerun_.start(0);
    refresh();
}
void TerrainSettingsDialog::refresh() {
    const bool current = dryRun_ && dryRun_->command == command() && !checking();
    QString status;
    bool refused = false;
    if (!problem_.isEmpty()) {
        status = "Cannot apply: " + problem_;
        refused = true;
    } else if (!current)
        status = "Checking…";
    else if (!dryRun_->ok) {
        refused = true;
        const auto first = firstRefusal(dryRun_->report["terrain"].toObject());
        status = "Refused: nothing would change. " +
                 (first.isEmpty() ? dryRun_->message : first + '.');
    } else if (dryRun_->unchanged)
        status = "These are the terrain's current settings.";
    else {
        const auto notices = dryRun_->report["terrain"].toObject()["notices"].toArray().size();
        status = QString("Ready: %1 in %2 · %3")
                     .arg(plural(dryRun_->triangles, "triangle", "triangles"),
                          plural(dryRun_->tiles, "tile", "tiles"),
                          plural(std::size_t(notices), "notice", "notices"));
    }
    status_->setText(status);
    status_->setAccessibleName(status);
    status_->setProperty("refused", refused);
    status_->style()->unpolish(status_);
    status_->style()->polish(status_);
    // The report of the current dry run (kept while a new one runs, for steadiness).
    const QSignalBlocker block(list_);
    list_->clear();
    if (problem_.isEmpty() && dryRun_)
        fillTerrainReport(list_, dryRun_->report["terrain"].toObject(), "m");
    if (list_->topLevelItemCount())
        list_->setCurrentItem(list_->topLevelItem(0));
    copy_->setEnabled(problem_.isEmpty() && dryRun_);
    apply_->setEnabled(canApply());
    apply_->setToolTip(apply_->isEnabled() ? QString("Rebuild the terrain with these settings")
                                           : status);
}

// ---- Create terrain from selection: the roles step (R095.h) --------------------------------
struct TerrainSelectionDryRun {
    QJsonObject command;
    bool ok{};
    bool cancelled{}; // D-044: stopped by its TerrainRunControl; never shown
    QString message;
    QJsonObject report;
    std::size_t triangles{}, tiles{};
};
namespace {
constexpr const char *tagRoleCodes[]{"auto", "contour", "breakline", "boundary", "hole", "ignore"};
TerrainImportRole tagRole(const QString &code) {
    return terrainImportRole(code).value_or(TerrainImportRole::Auto);
}
} // namespace

TerrainSelectionRolesDialog::TerrainSelectionRolesDialog(const Document &doc,
                                                         const std::vector<TerrainEdgeRef> &edges,
                                                         QWidget *parent)
    : Settled<QDialog>(parent), doc_(doc), tags_(terrainEdgesByTag(doc, edges)) {
    pool_.setMaxThreadCount(1);
    rerun_.setSingleShot(true);
    connect(&rerun_, &QTimer::timeout, this, [this] { start(); });
    progressPoll_.setInterval(50);
    connect(&progressPoll_, &QTimer::timeout, this, [this] { refresh(); });

    setObjectName("terrainSelectionRolesDialog");
    setWindowTitle("Create terrain");
    setModal(true);
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(20, 16, 20, 16);
    layout->setSpacing(10);
    layout->addWidget(title("Create terrain"));
    layout->addWidget(text(QString("%1 in %2. Choose a role for each tag; Automatic takes contours "
                                   "and breaklines from Z. The terrain is one undoable edit.")
                               .arg(plural(edges.size(), "selected edge", "selected edges"),
                                    plural(tags_.size(), "tag", "tags")),
                           "terrainDialogNote"));
    const QString labels[]{"Automatic", "Contour", "Breakline", "Boundary", "Hole", "Ignore"};
    std::vector<TerrainRoleTable::Choice> choices;
    for (std::size_t i = 0; i < std::size(tagRoleCodes); ++i)
        choices.push_back({tagRoleCodes[i], labels[i]});
    table_ = new TerrainRoleTable(std::move(choices), {"terrainSelectionRoles",
                                                       "terrainSelectionRole",
                                                       "terrainBudgetCaption"});
    table_->setHeadings("Tag", "Tag roles");
    std::vector<TerrainRoleTable::Row> rows;
    for (const auto &tag : tags_) {
        const auto name = QString::fromStdString(terrainTagName(doc, tag.tag));
        QStringList shape;
        if (tag.closed)
            shape << plural(tag.closed, "closed loop", "closed loops");
        if (tag.open())
            shape << plural(tag.open(), "open chain", "open chains");
        const QString automatic = !tag.varyingZ    ? "contour"
                                  : !tag.constantZ ? "breakline"
                                                   : "contour or breakline by Z";
        rows.push_back({QString::number(tag.tag), name,
                        plural(tag.edges.size(), "edge", "edges") +
                            (shape.isEmpty() ? QString() : " · " + shape.join(", ")),
                        automatic, name});
    }
    table_->setRows(std::move(rows));
    layout->addWidget(table_);
    status_ = text({}, "terrainSelectionStatus");
    layout->addWidget(status_);
    list_ = reportTree("terrainSelectionReport");
    layout->addWidget(list_, 1);
    auto *footer = new QHBoxLayout;
    // D-044: the running dry run's progress; shown only while one runs. It takes no focus.
    progress_ = new QProgressBar;
    progress_->setObjectName("terrainSelectionProgress");
    progress_->setAccessibleName("Checking progress");
    progress_->setRange(0, 100);
    progress_->setTextVisible(false);
    progress_->setFocusPolicy(Qt::NoFocus);
    progress_->setFixedWidth(progress_->fontMetrics().horizontalAdvance(QLatin1Char('M')) * 9);
    progress_->hide();
    footer->addWidget(progress_);
    footer->addStretch(1);
    auto *cancel = new QPushButton("Cancel");
    cancel->setObjectName("terrainSelectionCancel");
    cancel->setAutoDefault(false);
    footer->addWidget(cancel);
    create_ = new QPushButton("Create");
    create_->setObjectName("terrainSelectionCreate");
    create_->setAccessibleName("Create terrain");
    create_->setProperty("accentFill", true);
    create_->setAutoDefault(false);
    create_->setDefault(true);
    create_->setEnabled(false);
    footer->addWidget(create_);
    layout->addLayout(footer);

    table_->changed = [this] { changed(0); };
    connect(cancel, &QPushButton::clicked, this, &QDialog::reject);
    connect(create_, &QPushButton::clicked, this, [this] {
        if (canCreate())
            accept();
    });
    nameInternalEditors(this);
    // Focus starts in the table (first row): Enter moves to its Role, Tab runs Role, report,
    // Cancel and Create (the default).
    table_->table()->setFocus(Qt::OtherFocusReason);
    resize(600, 520);
    changed(0);
}
TerrainSelectionRolesDialog::~TerrainSelectionRolesDialog() {
    rerun_.stop();
    progressPoll_.stop();
    // Cancelled, a running dry run stops at its next checkpoint, so this waits that long at most.
    cancelRun();
    pool_.waitForDone();
}
void TerrainSelectionRolesDialog::done(int result) {
    // Cancel, Escape, closing the window and Create: a running dry run is no longer wanted.
    closed_ = true;
    rerun_.stop();
    cancelRun();
    Settled<QDialog>::done(result);
}
void TerrainSelectionRolesDialog::cancelRun() {
    if (control_)
        control_->cancel.store(true, std::memory_order_relaxed);
}
std::map<Id, TerrainImportRole> TerrainSelectionRolesDialog::chosen() const {
    std::map<Id, TerrainImportRole> roles;
    for (const auto &[key, code] : table_->roles())
        roles[Id(key.toULongLong())] = tagRole(code);
    return roles;
}
QJsonObject TerrainSelectionRolesDialog::command() const { return command_; }
bool TerrainSelectionRolesDialog::checking() const { return running_ || rerun_.isActive(); }
bool TerrainSelectionRolesDialog::canCreate() const {
    return problem_.isEmpty() && !checking() && dryRun_ && dryRun_->command == command_ &&
           dryRun_->ok;
}
QJsonObject TerrainSelectionRolesDialog::report() const {
    return dryRun_ && problem_.isEmpty() ? dryRun_->report : QJsonObject();
}
QString TerrainSelectionRolesDialog::status() const { return status_->text(); }
void TerrainSelectionRolesDialog::changed(int delayMs) {
    // The roles step names what it can before any work; the command's own checks follow.
    problem_.clear();
    request_.reset();
    const auto roles = chosen();
    QStringList problems;
    for (const auto &problem : terrainTagRoleProblems(doc_, tags_, roles))
        problems << QString::fromStdString(problem);
    command_ = terrainCreateFromEdgesCommand(terrainTagRoleSelection(tags_, roles));
    if (problems.isEmpty()) {
        try {
            request_ = parseTerrainEdgeRequest(doc_, command_);
        } catch (const std::exception &error) {
            problems << QString::fromUtf8(error.what());
        }
    }
    problem_ = problems.join('\n');
    // D-044: the running dry run is for the old roles; stop it.
    cancelRun();
    if (problem_.isEmpty())
        rerun_.start(delayMs);
    else
        rerun_.stop();
    refresh();
}
void TerrainSelectionRolesDialog::start() {
    if (closed_)
        return;
    if (running_) {
        rerun_.start(50);
        return;
    }
    if (!request_)
        return;
    running_ = true;
    const auto generation = ++generation_;
    QPointer<TerrainSelectionRolesDialog> guard(this);
    control_ = std::make_shared<TerrainRunControl>();
    // The worker owns a copy of the chained request and never reads the document. It shares
    // only its TerrainRunControl with the dialog.
    pool_.start([request = *request_, command = command_, generation, guard, control = control_] {
        auto result = std::make_shared<TerrainSelectionDryRun>();
        result->command = command;
        auto progress = terrainRunProgress(control);
        try {
            const auto run = runTerrainEdgeRequest(request, &progress);
            result->cancelled = run.cancelled;
            result->ok = run.ok;
            result->message = QString::fromStdString(run.message);
            result->report = run.report;
            result->triangles = run.mesh.triangles.size();
            result->tiles = run.tiles;
        } catch (const std::exception &error) {
            result->message = QString::fromUtf8(error.what());
        }
        QMetaObject::invokeMethod(
            qApp,
            [guard, generation, result = std::move(result)]() mutable {
                if (guard)
                    guard->finished(generation, std::move(result));
            },
            Qt::QueuedConnection);
    });
    progressPoll_.start();
    refresh();
}
void TerrainSelectionRolesDialog::finished(std::uint64_t generation,
                                           std::shared_ptr<TerrainSelectionDryRun> result) {
    running_ = false;
    control_.reset();
    progressPoll_.stop();
    // A cancelled run is always stale.
    if (generation == generation_ && !result->cancelled)
        dryRun_ = std::move(result);
    if (dryRun_ && dryRun_->command != command_ && problem_.isEmpty() && !rerun_.isActive())
        rerun_.start(0);
    refresh();
}
void TerrainSelectionRolesDialog::refresh() {
    const bool current = dryRun_ && dryRun_->command == command_ && !checking();
    QString status;
    bool refused = false;
    if (!problem_.isEmpty()) {
        status = "Cannot create: " + problem_;
        refused = true;
    } else if (!current) {
        // D-044: the running dry run's stage; "Checking…" while it waits to start.
        const auto stage = terrainRunStageText(control_ ? control_->stage.load() : -1);
        status = stage.isEmpty() ? QString("Checking…") : "Checking: " + stage + "…";
    } else if (!dryRun_->ok) {
        refused = true;
        const auto first = firstRefusal(dryRun_->report["terrain"].toObject());
        status = "Refused: nothing would change. " +
                 (first.isEmpty() ? dryRun_->message : first + '.');
    } else {
        const auto notices = dryRun_->report["terrain"].toObject()["notices"].toArray().size();
        status = QString("Ready: %1 in %2 · %3")
                     .arg(plural(dryRun_->triangles, "triangle", "triangles"),
                          plural(dryRun_->tiles, "tile", "tiles"),
                          plural(std::size_t(notices), "notice", "notices"));
    }
    if (control_)
        progress_->setValue(control_->permille.load() / 10);
    progress_->setVisible(running_ && problem_.isEmpty());
    // Polled while a run is on the worker: only a changed status touches the label and list.
    if (running_ && status == status_->text())
        return;
    status_->setText(status);
    status_->setAccessibleName(status);
    status_->setProperty("refused", refused);
    status_->style()->unpolish(status_);
    status_->style()->polish(status_);
    const QSignalBlocker block(list_);
    list_->clear();
    if (problem_.isEmpty() && dryRun_)
        fillTerrainReport(list_, dryRun_->report["terrain"].toObject(), "m");
    if (list_->topLevelItemCount())
        list_->setCurrentItem(list_->topLevelItem(0));
    create_->setEnabled(canCreate());
    create_->setToolTip(create_->isEnabled() ? QString("Create the terrain with these roles")
                                             : status);
}

// ---- Actions -------------------------------------------------------------------------------
bool explodeTerrainInteractive(QWidget *parent, Document &doc, Viewport &view, Id terrain) {
    if (!isTerrain(doc, terrain))
        throw std::runtime_error("Select a terrain to explode it");
    TerrainExplodeDialog dialog(doc, terrain, parent);
    if (dialog.exec() != QDialog::Accepted || !isTerrain(doc, terrain))
        return false;
    view.commitTerrainCommand(
        QJsonObject{{"command", "terrain.explode"}, {"body", QString::number(terrain)}});
    view.setSelection(terrain);
    return true;
}
bool terrainSettingsInteractive(QWidget *parent, Document &doc, Viewport &view, Id terrain) {
    if (!isTerrain(doc, terrain))
        throw std::runtime_error("Select a terrain to change its settings");
    TerrainSettingsDialog dialog(doc, terrain, parent);
    const auto revision = doc.revision();
    if (dialog.exec() != QDialog::Accepted)
        return false;
    if (doc.revision() != revision)
        throw std::runtime_error("The model changed while the settings were open. Try again.");
    try {
        commitTerrain(view, dialog.command());
    } catch (const std::exception &error) {
        const auto report = refusalReport(error);
        if (report.isEmpty())
            throw;
        TerrainReportDialog(
            "Terrain settings",
            "Terrain settings refused. Nothing was changed. " + QString::fromUtf8(error.what()),
            true, report, parent)
            .exec();
        return false;
    }
    return true;
}
bool regenerateTerrainInteractive(QWidget *parent, Document &doc, Viewport &view, Id terrain) {
    if (!isTerrain(doc, terrain))
        throw std::runtime_error("Select a terrain to regenerate it");
    // Paint on the tiles cannot follow a new mesh: confirm before it is dropped.
    if (const auto paint = terrainTilePaint(doc, terrain); paint.faces || paint.tiles) {
        TerrainRegenerateDialog confirm(paint, parent);
        if (confirm.exec() != QDialog::Accepted || !isTerrain(doc, terrain))
            return false;
    }
    try {
        const auto operation = commitTerrain(
            view, {{"command", "terrain.regenerate"}, {"terrain", QString::number(terrain)}});
        return operation["changed"].toBool();
    } catch (const std::exception &error) {
        const auto report = refusalReport(error);
        if (report.isEmpty())
            throw;
        TerrainReportDialog("Regenerate terrain",
                            "Regenerate refused. Nothing was changed. " +
                                QString::fromUtf8(error.what()),
                            true, report, parent)
            .exec();
        return false;
    }
}
bool resetTerrainEditsInteractive(QWidget *parent, Document &doc, Viewport &view, Id terrain) {
    if (!isTerrain(doc, terrain))
        throw std::runtime_error("Select a terrain to reset its edits");
    if (doc.bodies().at(terrain)->terrain->edits.empty())
        throw std::runtime_error("This terrain has no edits to reset");
    TerrainResetEditsDialog confirm(doc, terrain, parent);
    if (confirm.exec() != QDialog::Accepted || !isTerrain(doc, terrain))
        return false;
    try {
        const auto operation = commitTerrain(
            view, {{"command", "terrain.reset_edits"}, {"terrain", QString::number(terrain)}});
        return operation["changed"].toBool();
    } catch (const std::exception &error) {
        const auto report = refusalReport(error);
        if (report.isEmpty())
            throw;
        TerrainReportDialog("Reset terrain edits",
                            "Reset edits refused. Nothing was changed. " +
                                QString::fromUtf8(error.what()),
                            true, report, parent)
            .exec();
        return false;
    }
}
bool createTerrainFromSelectionInteractive(QWidget *parent, Document &doc, Viewport &view) {
    const auto edges = selectedTerrainEdges(view.selectionState());
    if (edges.isEmpty())
        throw std::runtime_error("Select edges or curves to create a terrain from them");
    QJsonObject command{{"command", "terrain.create_from_edges"}, {"edges", edges}};
    // R095.h: a closed loop or more than one tag opens the roles step; otherwise it stays one
    // click. A selection the command would refuse (a locked or missing edge) skips the step, so
    // the command's refusal is shown as before.
    std::vector<TerrainEdgeRef> refs;
    for (const auto &edge : edges)
        refs.push_back({Id(edge.toObject()["body"].toString().toULongLong()),
                        Id(edge.toObject()["edge"].toString().toULongLong())});
    bool roles = false;
    try {
        roles = terrainSelectionNeedsRoles(doc, refs);
    } catch (const std::exception &) {
    }
    if (roles) {
        TerrainSelectionRolesDialog step(doc, refs, parent);
        const auto revision = doc.revision();
        if (step.exec() != QDialog::Accepted)
            return false;
        if (doc.revision() != revision)
            throw std::runtime_error("The model changed while the roles were open. Try again.");
        command = step.command();
    }
    QJsonObject operation;
    try {
        operation = commitTerrain(view, command);
    } catch (const std::exception &error) {
        const auto report = refusalReport(error);
        if (report.isEmpty())
            throw;
        auto first = firstRefusal(report["terrain"].toObject());
        TerrainReportDialog("Create terrain",
                            "Terrain creation refused. Nothing was changed. " +
                                (first.isEmpty() ? QString::fromUtf8(error.what()) : first + '.'),
                            true, report, parent)
            .exec();
        return false;
    }
    const auto terrain = Id(operation["terrain"].toString().toULongLong());
    if (doc.bodies().contains(terrain))
        view.setSelection(terrain);
    const auto report = operation["terrainReport"].toObject();
    const auto notices = report["terrain"].toObject()["notices"].toArray().size();
    if (notices)
        TerrainReportDialog(
            "Create terrain",
            QString("Created “%1” with %2. Undo removes it.")
                .arg(doc.bodies().contains(terrain) ? terrainName(doc, terrain) : QString("Terrain"),
                     plural(std::size_t(notices), "notice", "notices")),
            false, report, parent)
            .exec();
    return true;
}
} // namespace sketchy
