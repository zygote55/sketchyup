#include "app/terrain_section.hpp"
#include "app/terrain_actions.hpp"
#include "app/unit_display.hpp"
#include <QAction>
#include <QFormLayout>
#include <QFrame>
#include <QHBoxLayout>
#include <QLocale>
#include <QTimer>
#include <QVBoxLayout>
#include <limits>
namespace sketchy {
namespace {
QString count(std::size_t value) { return QLocale().toString(qulonglong(value)); }
QString plural(std::size_t value, const QString &one, const QString &many) {
    return count(value) + ' ' + (value == 1 ? one : many);
}
QLabel *field(QFormLayout *form, const QString &caption, const QString &name) {
    auto *label = new QLabel;
    label->setObjectName(name);
    label->setTextFormat(Qt::PlainText);
    label->setWordWrap(true);
    label->setTextInteractionFlags(Qt::TextSelectableByMouse | Qt::TextSelectableByKeyboard);
    label->setAccessibleName("Terrain " + caption.toLower());
    form->addRow(caption, label);
    return label;
}
} // namespace
TerrainSection::TerrainSection(Document &doc, Viewport &view, QWidget *parent)
    : CollapsibleSection("Terrain", new QWidget, true, parent), doc_(doc), view_(view) {
    setObjectName("terrainSection");
    auto *layout = new QVBoxLayout(content());
    layout->setContentsMargins(8, 4, 2, 8);
    layout->setSpacing(6);
    // The amber chip: the tiles were changed outside the terrain tools (ADR 0168 §2).
    chip_ = new QFrame;
    chip_->setObjectName("terrainChangedChip");
    auto *chipRow = new QHBoxLayout(chip_);
    chipRow->setContentsMargins(8, 4, 4, 4);
    chipRow->setSpacing(6);
    chipText_ = new QLabel("⚠ Changed since generated");
    chipText_->setObjectName("terrainChangedState");
    chipText_->setWordWrap(true); // A narrow Tray wraps the chip instead of clipping it.
    chipText_->setToolTip("The terrain's tiles no longer match the mesh generated from its "
                          "sources. Regenerate rebuilds it from the sources.");
    chipRow->addWidget(chipText_, 1);
    regenerate_ = new QPushButton("Regenerate");
    regenerate_->setObjectName("terrainRegenerate");
    regenerate_->setAccessibleName("Regenerate terrain");
    regenerate_->setAccessibleDescription(
        "The terrain changed since it was generated; Regenerate rebuilds it from its sources");
    regenerate_->setAutoDefault(false);
    chipRow->addWidget(regenerate_);
    layout->addWidget(chip_);
    chip_->hide();
    auto *form = new QFormLayout;
    form->setContentsMargins(0, 0, 0, 0);
    form->setRowWrapPolicy(QFormLayout::WrapLongRows);
    name_ = field(form, "Name", "terrainName");
    triangles_ = field(form, "Triangles", "terrainTriangles");
    source_ = field(form, "Source", "terrainSource");
    sources_ = field(form, "Sources", "terrainSources");
    elevation_ = field(form, "Elevation", "terrainElevation");
    generator_ = field(form, "Generator", "terrainGenerator");
    flat_ = field(form, "Flat areas", "terrainFlatAreas");
    layout->addLayout(form);
    // Full-width buttons, one per row, as Entity info's own (a narrow Tray never clips them).
    auto *buttons = new QVBoxLayout;
    buttons->setSpacing(4);
    settings_ = new QPushButton("Terrain settings…");
    settings_->setObjectName("terrainSettings");
    settings_->setAutoDefault(false);
    buttons->addWidget(settings_);
    explode_ = new QPushButton("Explode terrain…");
    explode_->setObjectName("terrainExplode");
    explode_->setAutoDefault(false);
    buttons->addWidget(explode_);
    layout->addLayout(buttons);
    error_ = new QLabel;
    error_->setObjectName("terrainSectionError");
    error_->setTextFormat(Qt::PlainText);
    error_->setWordWrap(true);
    layout->addWidget(error_);
    error_->hide();
    setTabOrder(regenerate_, settings_);
    setTabOrder(settings_, explode_);
    // The action rebuilds this section, so it runs once the click has returned.
    connect(regenerate_, &QPushButton::clicked, this, [this] {
        run("terrain.regenerate",
            [this] { regenerateTerrainInteractive(window(), doc_, view_, terrain_); });
    });
    connect(settings_, &QPushButton::clicked, this, [this] {
        run("terrain.settings",
            [this] { terrainSettingsInteractive(window(), doc_, view_, terrain_); });
    });
    connect(explode_, &QPushButton::clicked, this, [this] {
        run("terrain.explode",
            [this] { explodeTerrainInteractive(window(), doc_, view_, terrain_); });
    });
    hide();
}
void TerrainSection::run(const QString &id, const std::function<void()> &action) {
    QTimer::singleShot(0, this, [this, id, action] {
        if (!terrain_ || !isTerrain(doc_, terrain_))
            return;
        // The window's action of the same name (one path with the command search and its
        // edit fences) acts on the same selected terrain.
        if (auto *public_ = window()->findChild<QAction *>(id); public_ && public_->isEnabled()) {
            public_->trigger();
            return;
        }
        try {
            action();
        } catch (const std::exception &failure) {
            emit view_.message(QString::fromUtf8(failure.what()));
        }
    });
}
void TerrainSection::refresh() {
    const auto terrain = selectedTerrain(doc_, view_.selectionState());
    if (!terrain) {
        terrain_ = 0;
        cacheKey_.clear();
        hide();
        return;
    }
    const auto key = QString::fromStdString(doc_.identity()) + "/" +
                     QString::number(doc_.revision()) + "/" + QString::number(terrain) + "/" +
                     QString::number(int(doc_.displayUnits())) + "/" +
                     QString::number(doc_.displayPrecision());
    show();
    if (key == cacheKey_ && terrain == terrain_)
        return;
    terrain_ = terrain;
    cacheKey_ = key;
    const auto &body = *doc_.bodies().at(terrain);
    const auto &record = *body.terrain;
    const auto units = doc_.displayUnits();
    const auto precision = doc_.displayPrecision();
    name_->setText(QString::fromStdString(body.name));
    const auto tiles = terrainTiles(doc_, terrain);
    triangles_->setText(plural(terrainTriangleCount(doc_, terrain), "triangle", "triangles") +
                        " in " + plural(tiles.size(), "tile", "tiles"));
    // Source: the file is named by the site provenance when this terrain established the site.
    const auto &provenance = record.provenance;
    if (provenance.sha256.empty()) {
        source_->setText("Model edges (no source file)");
        source_->setToolTip({});
    } else {
        QString file;
        if (const auto &site = doc_.site();
            site && site->established.sha256 == provenance.sha256 &&
            !site->established.source.empty())
            file = QString::fromStdString(site->established.source) + " · ";
        const auto digest = QString::fromStdString(provenance.sha256);
        source_->setText(file + "SHA-256 " + digest.left(12) + "… · " +
                         QString::fromStdString(provenance.units) + " (" +
                         QLocale().toString(provenance.metresPerUnit, 'g', 10) + " m per unit)");
        source_->setToolTip("SHA-256 " + digest);
    }
    source_->setAccessibleDescription(source_->toolTip());
    std::size_t contours = 0, breaklines = 0, boundaries = 0, holes = 0, points = 0;
    for (const auto &source : record.sources)
        switch (source.role) {
        case TerrainSourceRole::Contour:
            ++contours;
            break;
        case TerrainSourceRole::Breakline:
            ++breaklines;
            break;
        case TerrainSourceRole::Boundary:
            ++boundaries;
            break;
        case TerrainSourceRole::Hole:
            ++holes;
            break;
        case TerrainSourceRole::Points:
            points += source.points.size();
            break;
        }
    QStringList roles;
    if (contours)
        roles << plural(contours, "contour", "contours");
    if (breaklines)
        roles << plural(breaklines, "breakline", "breaklines");
    if (boundaries)
        roles << plural(boundaries, "boundary", "boundaries");
    if (holes)
        roles << plural(holes, "hole", "holes");
    if (points)
        roles << plural(points, "spot point", "spot points");
    sources_->setText(roles.isEmpty() ? QString("None") : roles.join(" · "));
    // Elevation in world Z, as terrain.describe reports it.
    double low = std::numeric_limits<double>::infinity(), high = -low;
    const auto world = doc_.worldTransform(terrain);
    for (const auto id : tiles)
        for (const auto &[vertex, point] : doc_.bodies().at(id)->surface.vertices) {
            const auto z = world.point(point).z;
            low = std::min(low, z);
            high = std::max(high, z);
        }
    elevation_->setText(tiles.empty() ? QString("No mesh")
                                      : displayLength(low, units, precision) + " to " +
                                            displayLength(high, units, precision));
    generator_->setText(QString::fromStdString(record.generator));
    const auto flat = terrainFlatArea(doc_, terrain);
    flat_->setText(flat.triangles
                       ? plural(flat.triangles, "flat triangle", "flat triangles") + " · " +
                             displayMeasure(flat.area, 2, units, precision)
                       : QString("None"));
    const bool changed = terrainChangedSinceGenerated(doc_, terrain);
    chip_->setVisible(changed);
    setDetail(changed ? QString("Changed") : QString());
    const bool locked = view_.selectionState().locked(doc_, terrain);
    regenerate_->setEnabled(!locked);
    settings_->setEnabled(!locked);
    explode_->setEnabled(!locked);
    error_->setVisible(locked);
    error_->setText(locked ? QString("The terrain is locked. Unlock it to change it.") : QString());
}
} // namespace sketchy
