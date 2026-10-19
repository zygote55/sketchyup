#include "app/file_operation.hpp"
#include "app/site_dialogs.hpp"
#include "app/terrain_actions.hpp"
#include "app/terrain_import_dialog.hpp"
#include "app/window.hpp"
#include "io/terrain_source.hpp"
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QJsonArray>
#include <QLocale>
#include <QMenu>
#include <QPushButton>
#include <QScopedValueRollback>
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
    const bool terrain = model && selectedTerrain(doc_, selection);
    for (const auto *id : {"terrain.settings", "terrain.regenerate", "terrain.explode"})
        findChild<QAction *>(id)->setEnabled(terrain);
    if (statusExplode_ && statusExplodeTerrain_ && !isTerrain(doc_, statusExplodeTerrain_))
        statusExplode_->hide();
}
// The viewport's context menu: Create terrain from selection, enabled when the selection has
// edges (curves select their edges).
void Window::showViewportMenu(const QPoint &position) {
    if (workspace_ != Workspace::Model)
        return;
    syncTerrainActions();
    QMenu menu(this);
    menu.setObjectName("viewportContextMenu");
    menu.setAccessibleName("Selection actions");
    menu.addAction(findChild<QAction *>("terrain.createFromSelection"));
    menu.exec(viewport_->mapToGlobal(position));
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
