#include "app/file_operation.hpp"
#include "app/site_dialogs.hpp"
#include "app/terrain_actions.hpp"
#include "app/terrain_import_dialog.hpp"
#include "app/terrain_tool_strip.hpp"
#include "app/window.hpp"
#include "io/terrain_source.hpp"
#include <QActionGroup>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QJsonArray>
#include <QLocale>
#include <QMenu>
#include <QPushButton>
#include <QScopedValueRollback>
#include <QMenuBar>
#include <QToolBar>
#include <QToolButton>
#include <QWidgetAction>
namespace sketchy {
// R095.d (ADR 0168 "Decisions > Interaction design"): File > Import terrain…. Unlike the other
// importers, terrain import adds to the open document (owner decision 11).
void Window::importTerrainDialog() {
    if (fileOperation_) {
        status_->setText("Wait for the current file operation to finish.");
        return;
    }
    QFileDialog file(this, "Import terrain");
    file.setObjectName("terrainImportFileDialog");
    file.setNameFilters({"Terrain sources (*.dxf *.csv *.txt *.xyz *.pts)", "DXF contours (*.dxf)",
                         "Point files (*.csv *.txt *.xyz *.pts)"});
    file.setFileMode(QFileDialog::ExistingFile);
    if (file.exec() != QDialog::Accepted || file.selectedFiles().isEmpty())
        return;
    importTerrainPath(file.selectedFiles().front());
}
void Window::importTerrainPath(const QString &path) {
    if (fileOperation_) {
        status_->setText("Wait for the current file operation to finish.");
        return;
    }
    const QFileInfo info(path);
    if (info.size() > TerrainSourceLimits::bytes)
        throw std::runtime_error("A terrain source is at most 64 MiB; this file is larger.");
    QByteArray bytes;
    {
        QScopedValueRollback busy(fileOperation_, true);
        bytes = runFileOperation(this, "Reading terrain source…", [path] {
            QFile source(path);
            if (!source.open(QIODevice::ReadOnly))
                throw std::runtime_error("Could not open the terrain source: " +
                                         source.errorString().toStdString());
            auto data = source.read(TerrainSourceLimits::bytes + 1);
            if (data.size() > TerrainSourceLimits::bytes)
                throw std::runtime_error("A terrain source is at most 64 MiB; this file is larger.");
            return data;
        });
    }
    if (bytes.isEmpty())
        throw std::runtime_error("The terrain source is empty.");
    TerrainImportDialog dialog(doc_, {info.fileName(), bytes, sheetColors_}, this);
    terrainImport_ = &dialog;
    const bool imported = dialog.exec() == QDialog::Accepted;
    terrainImport_.clear();
    activateWindow();
    if (!imported) {
        viewport_->setFocus(Qt::OtherFocusReason);
        return;
    }
    const auto &operation = dialog.operation();
    const auto terrain = Id(operation["terrain"].toString().toULongLong());
    viewport_->cancel();
    sync();
    if (doc_.bodies().contains(terrain))
        viewport_->setSelection(terrain);
    const auto report = operation["importReport"].toObject()["terrain"].toObject();
    status_->setText(QString("Imported a terrain: %1 triangles%2 · Undo removes it")
                         .arg(QLocale().toString(qulonglong(report["triangles"].toDouble())))
                         .arg(operation["siteEstablished"].toBool()
                                  ? QString(" · site origin set")
                                  : QString()));
    viewport_->setFocus(Qt::OtherFocusReason);
}

// R095.e1 (ADR 0168 "Decisions > Interaction design"): the terrain actions act on the selection
// through the same commands as automation; the Entity info Terrain section's buttons trigger
// them. They are in the command search; Create terrain from selection is also the viewport's
// context-menu entry.
void Window::setupTerrainActions() {
    auto *create = action("terrain.createFromSelection", "Create terrain from selection", {}, [this] {
        createTerrainFromSelectionInteractive(this, doc_, *viewport_);
        sync();
        viewport_->setFocus(Qt::OtherFocusReason);
    });
    create->setProperty("command", "terrain.create_from_edges");
    create->setProperty("requiresSelection", true);
    auto terrainAction = [this](const QString &id, const QString &title, const char *command,
                                bool (*act)(QWidget *, Document &, Viewport &, Id)) {
        auto *entry = action(id, title, {}, [this, act] {
            const auto terrain = selectedTerrain(doc_, viewport_->selectionState());
            if (!terrain)
                throw std::runtime_error("Select a terrain first");
            act(this, doc_, *viewport_, terrain);
            sync();
            viewport_->setFocus(Qt::OtherFocusReason);
        });
        entry->setProperty("command", command);
        entry->setProperty("requiresSelection", true);
    };
    terrainAction("terrain.settings", "Terrain settings…", "terrain.set_parameters",
                  &terrainSettingsInteractive);
    terrainAction("terrain.regenerate", "Regenerate terrain", "terrain.regenerate",
                  &regenerateTerrainInteractive);
    terrainAction("terrain.explode", "Explode terrain…", "terrain.explode",
                  &explodeTerrainInteractive);
    // R096.f1: Reset edits… (UX §15.1, §15.6).
    terrainAction("terrain.resetEdits", "Reset edits…", "terrain.reset_edits",
                  &resetTerrainEditsInteractive);
    // The tool refusal (ADR 0168, Tools on terrain): the status bar names the tool and offers
    // Explode, which opens the same confirmation. The pointer may leave the tile to reach the
    // button, so the refusal stays until a tool hovers something else or the tool changes.
    statusExplode_ = new QPushButton("Explode terrain…");
    statusExplode_->setObjectName("statusExplodeTerrain");
    statusExplode_->setAccessibleName("Explode the hovered terrain");
    statusExplode_->setAutoDefault(false);
    statusExplode_->hide();
    if (auto *footer = qobject_cast<QHBoxLayout *>(status_->parentWidget()->layout()))
        footer->insertWidget(footer->indexOf(status_) + 1, statusExplode_);
    connect(statusExplode_, &QPushButton::clicked, this, [this] {
        run([this] {
            if (!isTerrain(doc_, statusExplodeTerrain_))
                throw std::runtime_error("The terrain no longer exists");
            explodeTerrainInteractive(this, doc_, *viewport_, statusExplodeTerrain_);
            sync();
            viewport_->setFocus(Qt::OtherFocusReason);
        });
    });
    connect(viewport_, &Viewport::terrainToolRefused, this,
            [this](qulonglong terrain, const QString &text) {
                if (terrain) {
                    if (status_->text() != terrainStatusShown_)
                        terrainStatusSaved_ = status_->text();
                    terrainStatusShown_ = text;
                    status_->setText(text);
                    statusExplodeTerrain_ = terrain;
                    statusExplode_->show();
                    return;
                }
                statusExplode_->hide();
                statusExplodeTerrain_ = 0;
                if (status_->text() == terrainStatusShown_)
                    status_->setText(terrainStatusSaved_);
                terrainStatusShown_.clear();
            });
    connect(viewport_, &Viewport::contextMenuRequested, this,
            [this](const QPoint &position) { showViewportMenu(position); });
}
void Window::syncTerrainActions() {
    const auto &selection = viewport_->selectionState();
    const bool model = workspace_ == Workspace::Model && !assistantFenced_;
    findChild<QAction *>("terrain.createFromSelection")
        ->setEnabled(model && !selectedTerrainEdges(selection).isEmpty());
    const auto selected = model ? selectedTerrain(doc_, selection) : 0;
    const bool terrain = selected != 0;
    for (const auto *id : {"terrain.settings", "terrain.regenerate", "terrain.explode"})
        findChild<QAction *>(id)->setEnabled(terrain);
    findChild<QAction *>("terrain.resetEdits")
        ->setEnabled(terrain && !doc_.bodies().at(selected)->terrain->edits.empty());
    // The terrain tools need a terrain in the model to act on; without one they still start and
    // say so (UX §15.1), so they stay enabled.
    for (const auto *id : {"tool.29", "tool.30"})
        if (auto *tool = findChild<QAction *>(id))
            tool->setEnabled(model);
    if (statusExplode_ && statusExplodeTerrain_ && !isTerrain(doc_, statusExplodeTerrain_))
        statusExplode_->hide();
}
// The viewport's context menu: Create terrain from selection, enabled when the selection has
// edges (curves select their edges). R096.f1 (UX §15.1): right-clicking a terrain (or with one
// selected) offers its tools, Terrain settings… and Explode terrain…; a terrain under the pointer
// is selected first, so the actions name it.
void Window::showViewportMenu(const QPoint &position) {
    if (workspace_ != Workspace::Model)
        return;
    Id terrain = 0;
    if (const auto [body, face] = viewport_->pick(position); body)
        terrain = terrainOfBody(doc_, body);
    if (terrain && selectedTerrain(doc_, viewport_->selectionState()) != terrain &&
        !viewport_->selectionState().context()) {
        viewport_->setSelection(terrain);
        sync();
    }
    if (!terrain)
        terrain = selectedTerrain(doc_, viewport_->selectionState());
    syncTerrainActions();
    QMenu menu(this);
    menu.setObjectName("viewportContextMenu");
    menu.setAccessibleName("Selection actions");
    if (terrain) {
        // The terrain's name heads its entries (a label: the style draws a section as a bare
        // separator).
        auto *label = new QLabel(QString::fromStdString(doc_.bodies().at(terrain)->name));
        label->setObjectName("viewportMenuTerrainHeading");
        label->setContentsMargins(14, 6, 14, 2);
        if (auto *themed = static_cast<QWidgetAction *>(terrainHeading_)->defaultWidget())
            label->setStyleSheet(themed->styleSheet());
        auto *heading = new QWidgetAction(&menu);
        heading->setDefaultWidget(label);
        heading->setEnabled(false);
        menu.addAction(heading);
        for (const auto *id : {"tool.29", "tool.30"})
            menu.addAction(findChild<QAction *>(id));
        menu.addSeparator();
        menu.addAction(findChild<QAction *>("terrain.settings"));
        menu.addAction(findChild<QAction *>("terrain.explode"));
        menu.addSeparator();
    }
    menu.addAction(findChild<QAction *>("terrain.createFromSelection"));
    menu.exec(viewport_->mapToGlobal(position));
}

// ---- R096.f1: the terrain tools (UX_DESIGN §15.1) -------------------------------------------
void Window::setupTerrainTools(QActionGroup *group, QToolBar *rail) {
    auto add = [&](const QString &name, const QString &key, Viewport::Tool mode) {
        auto *entry = action("tool." + QString::number(int(mode)), name, QKeySequence(key),
                             [this, mode] { tool(mode, {}); });
        entry->setCheckable(true);
        group->addAction(entry);
        entry->setProperty("command", "terrain.sculpt");
        return entry;
    };
    // Only Sculpt has a default (Shift+S, unused in the model scope); Smooth can be assigned in
    // the shortcut editor.
    add("Sculpt", "Shift+S", Viewport::Tool::Sculpt);
    add("Smooth", "", Viewport::Tool::Smooth);
    // The rail's Terrain slot: one control in its own group after Tape. It shows the last
    // terrain tool used ("Terrain" until one is, as the approved mockup draws it) and starts it
    // (Sculpt at first).
    railTools_.emplace_back(rail->addSeparator(), -1);
    terrainRail_ = new QAction("Terrain ▸", this);
    terrainRail_->setObjectName("railTerrain");
    terrainRail_->setCheckable(true);
    terrainRail_->setProperty("sketchyupRailSlot", true);
    connect(terrainRail_, &QAction::triggered, this, [this] {
        if (auto *entry = findChild<QAction *>("tool." + QString::number(int(lastTerrainTool_))))
            entry->trigger();
        syncTerrainTools();
    });
    rail->addAction(terrainRail_);
    railTools_.emplace_back(terrainRail_, RailTerrain);
    // The options strip on the viewport.
    terrainStrip_ = new TerrainToolStrip(*viewport_);
    connect(terrainStrip_, &TerrainToolStrip::toolRequested, this, [this](int mode) {
        if (auto *entry = findChild<QAction *>("tool." + QString::number(mode)))
            entry->trigger();
        // The switcher keeps the keyboard: the strip stays where it was.
        if (auto *button = terrainStrip_->toolButton(mode))
            button->setFocus(Qt::OtherFocusReason);
    });
    connect(terrainStrip_, &TerrainToolStrip::problem, this, [this](const QString &text) {
        if (!text.isEmpty())
            status_->setText(text);
    });
    syncTerrainTools();
}
void Window::setupTerrainMenu() {
    terrainMenu_ = menuBar()->addMenu("&Terrain");
    terrainMenu_->setObjectName("terrainMenu");
    // Headings as the mockup draws them: muted text above each group (the style draws a QMenu
    // section as a bare separator). They are labels, not items, so keyboard navigation skips them.
    auto heading = [this](const QString &text, const QString &name) {
        auto *label = new QLabel(text);
        label->setObjectName(name);
        label->setContentsMargins(14, 6, 14, 2);
        label->setProperty("terrainMenuHeading", true);
        auto *entry = new QWidgetAction(terrainMenu_);
        entry->setDefaultWidget(label);
        entry->setEnabled(false);
        terrainMenu_->addAction(entry);
        return entry;
    };
    heading("Tools", "terrainMenuToolsHeading");
    // Edit contours, Stamp pad and Drape join the tools with R096.f2.
    for (const auto *id : {"tool.29", "tool.30"})
        terrainMenu_->addAction(findChild<QAction *>(id));
    terrainMenu_->addSeparator();
    terrainHeading_ = heading("No terrain selected", "terrainMenuTerrainHeading");
    for (const auto *id :
         {"terrain.settings", "terrain.regenerate", "terrain.resetEdits", "terrain.explode"})
        terrainMenu_->addAction(findChild<QAction *>(id));
    terrainMenu_->addSeparator();
    terrainMenu_->addAction(findChild<QAction *>("terrain.createFromSelection"));
    connect(terrainMenu_, &QMenu::aboutToShow, this, [this] {
        syncTerrainActions();
        const auto terrain = selectedTerrain(doc_, viewport_->selectionState());
        if (auto *label = qobject_cast<QLabel *>(
                static_cast<QWidgetAction *>(terrainHeading_)->defaultWidget())) {
            label->setText(terrain ? QString::fromStdString(doc_.bodies().at(terrain)->name)
                                   : QString("No terrain selected"));
        }
    });
}
void Window::syncTerrainTools() {
    if (!terrainRail_)
        return;
    const auto mode = viewport_->tool();
    const bool active = Viewport::terrainTool(mode);
    if (active) {
        lastTerrainTool_ = mode;
        terrainToolUsed_ = true;
    }
    const QString name = lastTerrainTool_ == Viewport::Tool::Smooth ? "Smooth" : "Sculpt";
    terrainRail_->setText((terrainToolUsed_ ? name : QString("Terrain")) + " ▸");
    const auto *entry = findChild<QAction *>("tool." + QString::number(int(lastTerrainTool_)));
    const auto key = entry ? entry->shortcut().toString(QKeySequence::NativeText) : QString();
    terrainRail_->setToolTip("Terrain tools: " + name + (key.isEmpty() ? "" : " (" + key + ")"));
    terrainRail_->setChecked(active);
    terrainRail_->setProperty("accessibleName", "Terrain tools: " + name);
    if (auto *button = modelRail_ ? modelRail_->widgetForAction(terrainRail_) : nullptr) {
        button->setAccessibleName("Terrain tools: " + name);
        button->setAccessibleDescription("Starts " + name + "; the strip switches terrain tools");
    }
    if (terrainStrip_)
        terrainStrip_->sync();
}
// R095.e2 (ADR 0168 "Decisions > Interaction design", Site): File > Site settings…. Re-anchor
// and Set origin commit from their own dialogs (site.reanchor, site.set), one edit each.
void Window::siteSettings() {
    if (workspace_ != Workspace::Model)
        setWorkspace(Workspace::Model);
    viewport_->cancel();
    SiteSettingsDialog dialog(doc_, *viewport_, this);
    dialog.exec();
    sync();
    viewport_->setFocus(Qt::OtherFocusReason);
}
} // namespace sketchy
