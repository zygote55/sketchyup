// R095.e1 (ADR 0168 "Decisions > Interaction design"): the desktop terrain surface, offscreen.
// The Entity info Terrain section's fields; the "Changed since generated" chip (a geometry
// change of a tile changes the mesh digest, paint does not) with Regenerate as one undoable
// edit and its paint confirmation; Terrain settings apply, cancel and invalid choices; the
// Explode confirmation within and over the modelling budget; Create terrain from selection
// (success, a refusal with its report, edges in nested groups, the context menu; R095.h: the
// roles step, when it appears, an invalid boundary and a boundary and hole set by keyboard); and the
// refusing tools' cursor and status message over a tile with its Explode link.
// `--screenshots DIR` saves the section with the chip, the dialogs and the status message;
// `--text-percent N` runs everything at that interface text size.
#include "app/collapsible_section.hpp"
#include "app/entity_info_panel.hpp"
#include "app/organization_panel.hpp"
#include "app/terrain_actions.hpp"
#include "app/terrain_role_table.hpp"
#include "app/terrain_section.hpp"
#include "app/window.hpp"
#include "automation/commands.hpp"
#include "automation/terrain_commands.hpp"
#include "core/groups.hpp"
#include "core/tags.hpp"
#include "core/terrain.hpp"
#include "native_dialog_audit.hpp"
#include "terrain_import_fixtures.hpp"
#include "terrain_run_hold.hpp"
#include <QAccessible>
#include <QAction>
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QContextMenuEvent>
#include <QDoubleSpinBox>
#include <QElapsedTimer>
#include <QFrame>
#include <QJsonArray>
#include <QLabel>
#include <QMenu>
#include <QProgressBar>
#include <QPushButton>
#include <QScrollArea>
#include <QSettings>
#include <QTemporaryDir>
#include <QSignalSpy>
#include <QTableWidget>
#include <QTest>
#include <QTimer>
#include <QTreeWidget>
#include <bit>
#include <tuple>
#include <set>
#include <iterator>
#include <algorithm>
#include <cmath>
#include <iostream>
#include <limits>
using namespace sketchy;
namespace {
QString shots;
void check(bool ok, const std::string &message) {
    if (!ok)
        throw std::runtime_error(message);
}
template <class T> T *child(QWidget &parent, const char *name) {
    auto *widget = parent.findChild<T *>(name);
    check(widget != nullptr, std::string("Has ") + name);
    return widget;
}
void shot(QWidget &widget, const QString &name) {
    if (shots.isEmpty())
        return;
    QTest::qWait(30);
    widget.grab().save(shots + "/" + name + ".png");
}
// Runs `trigger`; when a visible top-level widget named `name` appears, runs `body` on it once
// (the body closes it). Returns whether it appeared.
bool drive(const QString &name, const std::function<void(QWidget &)> &body,
           const std::function<void()> &trigger) {
    bool handled = false;
    std::exception_ptr failure;
    QTimer timer;
    QObject::connect(&timer, &QTimer::timeout, [&] {
        if (handled)
            return;
        for (auto *widget : QApplication::topLevelWidgets())
            if (widget->isVisible() && widget->objectName() == name) {
                handled = true;
                try {
                    QTest::qWait(10);
                    body(*widget);
                } catch (...) {
                    failure = std::current_exception();
                    if (auto *dialog = qobject_cast<QDialog *>(widget); dialog && dialog->isVisible())
                        dialog->reject();
                }
                return;
            }
    });
    timer.start(20);
    trigger();
    timer.stop();
    if (failure)
        std::rethrow_exception(failure);
    return handled;
}
void click(QWidget &dialog, const char *name) {
    auto *button = child<QPushButton>(dialog, name);
    check(button->isEnabled(), std::string(name) + " is enabled");
    QTest::mouseClick(button, Qt::LeftButton);
}

// ---- Fixtures ------------------------------------------------------------------------------
void fresh(Window &window) {
    window.viewport()->cancel();
    window.document() = Document();
    window.viewport()->setSelection(0);
    window.viewport()->refresh();
    emit window.viewport()->changed();
}
Id polyline(Document &doc, const std::vector<Vec3> &points, bool closed) {
    Id body = 0;
    for (std::size_t i = 0; i + 1 < points.size(); ++i)
        body = doc.addWire(body, points[i], points[i + 1]);
    if (closed)
        doc.addWire(body, points.back(), points.front());
    return body;
}
std::vector<Vec3> square(double half, double z, double cx = 0, double cy = 0) {
    return {{cx - half, cy - half, z},
            {cx + half, cy - half, z},
            {cx + half, cy + half, z},
            {cx - half, cy + half, z}};
}
SelectionSet edgesOf(const Document &doc, Id body) {
    SelectionSet edges;
    for (const auto &[id, edge] : doc.bodies().at(body)->topology.edges)
        edges.insert({body, SelectionKind::Edge, id});
    return edges;
}
SelectionSet join(std::initializer_list<SelectionSet> sets) {
    SelectionSet all;
    for (const auto &set : sets)
        all.insert(set.begin(), set.end());
    return all;
}
// Three nested square contours at 10, 11 and 12 m (60, 40 and 20 m wide), as wire bodies.
std::vector<Id> hillEdges(Document &doc) {
    return {polyline(doc, square(30, 10), true), polyline(doc, square(20, 11), true),
            polyline(doc, square(10, 12), true)};
}
QJsonObject run(Document &doc, const QJsonObject &command) {
    const QJsonObject request{{"apiVersion", 1},
                              {"documentId", QString::fromStdString(doc.identity())},
                              {"expectedRevision", QString::number(doc.revision())},
                              {"commands", QJsonArray{command}}};
    return executeBatch(doc, request)["terrainOperations"].toArray().at(0).toObject();
}
// The hill as a terrain, through terrain.create_from_edges.
Id hillTerrain(Window &window) {
    auto &doc = window.document();
    QJsonArray edges;
    for (const auto body : hillEdges(doc))
        for (const auto &[id, edge] : doc.bodies().at(body)->topology.edges)
            edges.append(QJsonObject{{"body", QString::number(body)}, {"edge", QString::number(id)}});
    const auto operation =
        run(doc, {{"command", "terrain.create_from_edges"}, {"edges", edges}, {"name", "Hill"}});
    window.viewport()->refresh();
    emit window.viewport()->changed();
    return operation["terrain"].toString().toULongLong();
}
// Changes one tile vertex in an edit that also touches the record (as only terrain operations
// may), so the stored mesh digest no longer matches the tiles.
void changeTileGeometry(Window &window, Id terrain) {
    auto &doc = window.document();
    const auto tileId = terrainTiles(doc, terrain).front();
    const auto before = doc.bodies().at(tileId);
    auto tile = std::make_shared<Body>(*before);
    tile->surface.vertices.begin()->second.z += 0.5;
    auto edited = std::make_shared<Body>(*doc.bodies().at(terrain));
    edited->terrain->generator = "edited/terrain-v1";
    doc.apply({"Edit tile", {{terrain, doc.bodies().at(terrain), edited}, {tileId, before, tile}}},
              doc.revision());
    window.viewport()->refresh();
    emit window.viewport()->changed();
}
void paintTileFace(Window &window, Id terrain) {
    auto &doc = window.document();
    const auto tileId = terrainTiles(doc, terrain).front();
    const auto before = doc.bodies().at(tileId);
    auto tile = std::make_shared<Body>(*before);
    tile->faceColors[tile->surface.faces.begin()->first] = {1, 0, 0};
    doc.apply({"Paint", {{tileId, before, tile}}}, doc.revision());
    window.viewport()->refresh();
    emit window.viewport()->changed();
}
template <class T> T *widgetOf(Window &window) {
    for (auto *widget : window.findChildren<QWidget *>())
        if (auto *found = dynamic_cast<T *>(widget))
            return found;
    return nullptr;
}
EntityInfoPanel *infoPanel(Window &window) { return widgetOf<EntityInfoPanel>(window); }
TerrainSection &section(Window &window) {
    widgetOf<OrganizationPanel>(window)->showInfo();
    auto *panel = infoPanel(window);
    check(panel && panel->terrain(), "Entity info has a Terrain section");
    QTest::qWait(10);
    panel->refresh();
    return *panel->terrain();
}
void select(Window &window, Id body) {
    window.viewport()->setSelection(body);
    QTest::qWait(5);
}
QAction *action(Window &window, const char *id) {
    auto *found = window.findChild<QAction *>(id);
    check(found != nullptr, std::string("Window has action ") + id);
    return found;
}

// ---- Entity info ---------------------------------------------------------------------------
void sectionFields(Window &window) {
    fresh(window);
    auto &doc = window.document();
    const auto terrain = hillTerrain(window);
    auto &terrainInfo = section(window);
    select(window, 0);
    terrainInfo.refresh();
    check(!terrainInfo.isVisible(), "The Terrain section is hidden without a terrain selected");
    select(window, terrain);
    terrainInfo.refresh();
    check(terrainInfo.isVisible() && terrainInfo.terrain() == terrain,
          "Selecting a terrain shows the Terrain section");
    const auto text = [&](const char *name) { return child<QLabel>(terrainInfo, name)->text(); };
    check(text("terrainName") == "Hill", "The name is shown");
    const auto triangles = terrainTriangleCount(doc, terrain);
    check(text("terrainTriangles") ==
              QString("%1 triangles in 1 tile").arg(QLocale().toString(qulonglong(triangles))),
          "Triangle and tile counts: " + text("terrainTriangles").toStdString());
    check(text("terrainSource") == "Model edges (no source file)",
          "A terrain from edges has no source file");
    check(text("terrainSources") == "3 contours", "Sources by role: " + text("terrainSources").toStdString());
    check(text("terrainElevation").contains("10") && text("terrainElevation").contains("12"),
          "The elevation range: " + text("terrainElevation").toStdString());
    check(text("terrainGenerator") == terrainGenerator, "The generator is shown");
    // The summit square is two flat triangles of 20 x 20 m that only spot heights resolve.
    const auto flat = terrainFlatArea(doc, terrain);
    check(flat.triangles == 2 && std::abs(flat.area - 400) < 1e-9,
          "The flat-area result counts the summit");
    check(text("terrainFlatAreas").startsWith("2 flat triangles"),
          "The flat areas are shown: " + text("terrainFlatAreas").toStdString());
    check(!terrainInfo.changedChip()->isVisible(), "A generated terrain has no Changed chip");
    // A tile selected inside the open terrain shows its terrain too.
    window.viewport()->enterContext(terrain);
    const auto tile = terrainTiles(doc, terrain).front();
    window.viewport()->selectEntities(
        {{tile, SelectionKind::Face, doc.bodies().at(tile)->surface.faces.begin()->first}});
    terrainInfo.refresh();
    check(terrainInfo.terrain() == terrain, "A tile face selects its terrain's section");
    window.viewport()->enterContext(0);
    // An imported terrain names its file and digest.
    fresh(window);
    const QJsonObject import{
        {"command", "terrain.import"},
        {"data", QString::fromLatin1(terrain_fixture::terrainContoursMetres().toBase64())},
        {"format", "dxf"},
        {"sourceName", "site.dxf"},
        {"roles", QJsonObject{{"BOUNDARY", "boundary"}, {"POND", "hole"}}}};
    const auto imported = run(window.document(), import)["terrain"].toString().toULongLong();
    window.viewport()->refresh();
    emit window.viewport()->changed();
    select(window, imported);
    terrainInfo.refresh();
    const auto sha = window.document().bodies().at(imported)->terrain->provenance.sha256;
    check(text("terrainSource").startsWith("site.dxf · SHA-256 " +
                                           QString::fromStdString(sha).left(12)),
          "The source file and digest: " + text("terrainSource").toStdString());
    check(text("terrainSources").contains("contours") && text("terrainSources").contains("1 boundary") &&
              text("terrainSources").contains("1 hole"),
          "Imported sources by role: " + text("terrainSources").toStdString());
    // Every Tab stop of the section has an accessible name.
    for (auto *widget : terrainInfo.findChildren<QWidget *>()) {
        if (!widget->isVisibleTo(&terrainInfo) || !(widget->focusPolicy() & Qt::TabFocus))
            continue;
        auto *accessible = QAccessible::queryAccessibleInterface(widget);
        check(accessible && !accessible->text(QAccessible::Name).trimmed().isEmpty(),
              "Named: " + widget->objectName().toStdString());
    }
}

void changedChipAndRegenerate(Window &window) {
    fresh(window);
    auto &doc = window.document();
    const auto terrain = hillTerrain(window);
    auto &terrainInfo = section(window);
    select(window, terrain);
    // Paint on a tile does not change the mesh digest: no chip.
    const auto digest = terrainMeshDigest(doc, terrain);
    paintTileFace(window, terrain);
    terrainInfo.refresh();
    check(terrainMeshDigest(doc, terrain) == digest && !terrainChangedSinceGenerated(doc, terrain),
          "Painting a tile keeps the mesh digest");
    check(!terrainInfo.changedChip()->isVisible(), "Paint shows no Changed chip");
    // A geometry change of a tile changes it: the amber chip with Regenerate.
    changeTileGeometry(window, terrain);
    terrainInfo.refresh();
    check(terrainMeshDigest(doc, terrain) != digest && terrainChangedSinceGenerated(doc, terrain),
          "A tile geometry change changes the mesh digest");
    check(terrainInfo.changedChip()->isVisible() && terrainInfo.regenerateButton()->isEnabled(),
          "The Changed chip offers Regenerate");
    check(terrainInfo.detail() == "Changed", "The header says Changed");
    if (!shots.isEmpty()) {
        // Show the Terrain section: collapse Entity info above it for the picture.
        auto *info = dynamic_cast<CollapsibleSection *>(
            infoPanel(window)->findChild<QWidget *>("entityInfoSection"));
        check(info != nullptr, "Entity info is a collapsible section");
        info->setExpanded(false);
        QTest::qWait(30);
        shot(terrainInfo, "entity-info-terrain-section");
        shot(*infoPanel(window), "entity-info-terrain-changed");
        shot(window, "window-terrain-changed");
        info->setExpanded(true);
    }
    // Regenerate confirms first, because the painted face would lose its paint. Cancel keeps
    // everything.
    const auto history = doc.history().total;
    check(drive(
              "terrainRegenerateDialog",
              [&](QWidget &dialog) {
                  check(child<QLabel>(dialog, "terrainRegeneratePaint")->text() ==
                            "• 1 face with their own paint",
                        "The confirmation lists the paint counts");
                  check(child<QPushButton>(dialog, "terrainRegenerateCancel")->isDefault(),
                        "Cancel is the default");
                  click(dialog, "terrainRegenerateCancel");
              },
              [&] {
                  QTest::mouseClick(terrainInfo.regenerateButton(), Qt::LeftButton);
                  QTest::qWait(50);
              }),
          "The paint confirmation appears");
    check(doc.history().total == history && terrainChangedSinceGenerated(doc, terrain),
          "Cancel regenerates nothing");
    check(drive(
              "terrainRegenerateDialog",
              [&](QWidget &dialog) { click(dialog, "terrainRegenerateConfirm"); },
              [&] {
                  QTest::mouseClick(terrainInfo.regenerateButton(), Qt::LeftButton);
                  QTest::qWait(50);
              }),
          "The paint confirmation appears again");
    check(doc.history().total == history + 1 &&
              doc.history().entries.back().label == "Regenerate terrain",
          "Regenerate is one labelled edit");
    check(!terrainChangedSinceGenerated(doc, terrain), "The regenerated terrain is current");
    terrainInfo.refresh();
    check(!terrainInfo.changedChip()->isVisible(), "The chip goes away after Regenerate");
    doc.undo();
    window.viewport()->refresh();
    emit window.viewport()->changed();
    terrainInfo.refresh();
    check(terrainChangedSinceGenerated(doc, terrain) && terrainInfo.changedChip()->isVisible(),
          "Undo brings the change, and the chip, back");
    // Without paint, Regenerate needs no confirmation.
    fresh(window);
    const auto plain = hillTerrain(window);
    changeTileGeometry(window, plain);
    select(window, plain);
    auto &again = section(window);
    const auto before = window.document().history().total;
    QTest::mouseClick(again.regenerateButton(), Qt::LeftButton);
    QTest::qWait(50);
    check(window.document().history().total == before + 1 &&
              !terrainChangedSinceGenerated(window.document(), plain),
          "An unpainted terrain regenerates without a confirmation");
}

// ---- Terrain settings ----------------------------------------------------------------------
void settle(TerrainSettingsDialog &dialog) {
    check(QTest::qWaitFor([&] { return !dialog.checking(); }, 30000), "The dry run settles");
    QTest::qWait(5);
}
void settle(TerrainSelectionRolesDialog &dialog) {
    check(QTest::qWaitFor([&] { return !dialog.checking(); }, 30000), "The roles dry run settles");
    QTest::qWait(5);
}
// A key sent to the dialog's focus widget (no real input: the same offscreen, X11 and Wayland).
void press(QWidget &dialog, Qt::Key key, Qt::KeyboardModifiers modifiers = Qt::NoModifier) {
    auto *target = dialog.focusWidget() ? dialog.focusWidget() : &dialog;
    QTest::keyClick(target, key, modifiers);
    QTest::qWait(5);
}
// Tab until the named button has the focus.
void tabTo(QWidget &dialog, const char *name) {
    auto *target = child<QPushButton>(dialog, name);
    for (int i = 0; i < 12 && dialog.focusWidget() != target; ++i)
        press(dialog, Qt::Key_Tab);
    check(dialog.focusWidget() == target, std::string("Tab reaches ") + name);
}
void settings(Window &window) {
    fresh(window);
    auto &doc = window.document();
    const auto terrain = hillTerrain(window);
    select(window, terrain);
    auto &terrainInfo = section(window);
    check(doc.bodies().at(terrain)->terrain->parameters.boundary ==
              TerrainBoundaryMode::ConvexHull,
          "A terrain from edges starts with the convex hull");
    const auto history = doc.history().total;
    // Cancel changes nothing.
    check(drive(
              "terrainSettingsDialog",
              [&](QWidget &widget) {
                  auto &dialog = static_cast<TerrainSettingsDialog &>(widget);
                  settle(dialog);
                  check(!dialog.canApply() &&
                            child<QLabel>(dialog, "terrainSettingsStatus")->text() ==
                                "These are the terrain's current settings.",
                        "Apply waits for a change");
                  child<QCheckBox>(dialog, "terrainSettingsFlip")->setChecked(false);
                  settle(dialog);
                  check(dialog.canApply(), "A change can be applied");
                  click(dialog, "terrainSettingsCancel");
              },
              [&] {
                  QTest::mouseClick(terrainInfo.settingsButton(), Qt::LeftButton);
                  QTest::qWait(50);
              }),
          "Terrain settings… opens the dialog");
    check(doc.history().total == history, "Cancel applies nothing");
    // Invalid combinations: Boundary source without a stored boundary; Limit edge length with
    // no length. Then a valid one applies as one edit.
    check(drive(
              "terrainSettingsDialog",
              [&](QWidget &widget) {
                  auto &dialog = static_cast<TerrainSettingsDialog &>(widget);
                  settle(dialog);
                  auto *boundary = child<QComboBox>(dialog, "terrainSettingsBoundary");
                  boundary->setCurrentIndex(boundary->findData("boundary"));
                  settle(dialog);
                  check(!dialog.canApply() && !dialog.problem().isEmpty() &&
                            child<QLabel>(dialog, "terrainSettingsStatus")
                                ->text()
                                .startsWith("Cannot apply: "),
                        "Boundary source without a boundary cannot apply");
                  boundary->setCurrentIndex(boundary->findData("maxEdgeLength"));
                  settle(dialog);
                  check(!dialog.canApply() && !dialog.problem().isEmpty(),
                        "Limit edge length without a length cannot apply");
                  child<QDoubleSpinBox>(dialog, "terrainSettingsMaxEdge")->setValue(15);
                  child<QCheckBox>(dialog, "terrainSettingsSteiner")->setChecked(true);
                  settle(dialog);
                  check(dialog.canApply() && dialog.problem().isEmpty(),
                        "A length makes the choice valid");
                  check(child<QLabel>(dialog, "terrainSettingsStatus")->text().startsWith("Ready: "),
                        "The dry run reports the result");
                  check(dialog.report()["terrain"].toObject()["outcome"].toString().startsWith(
                            "regenerated"),
                        "The dry run is the regenerate report");
                  shot(dialog, "terrain-settings");
                  QStringList findings;
                  child<QDoubleSpinBox>(dialog, "terrainSettingsMaxEdge")
                      ->setFocus(Qt::TabFocusReason);
                  const auto json = dialog_audit::auditOpen(dialog, findings);
                  check(findings.isEmpty(),
                        "Keyboard audit of Terrain settings: " + findings.join("; ").toStdString());
                  check(json["defaultButton"] == "Apply terrain settings",
                        "Apply is the default button");
                  click(dialog, "terrainSettingsApply");
              },
              [&] { action(window, "terrain.settings")->trigger(); }),
          "The command search action opens the dialog");
    check(doc.history().total == history + 1 &&
              doc.history().entries.back().label == "Terrain settings",
          "Apply is one labelled edit");
    const auto &parameters = doc.bodies().at(terrain)->terrain->parameters;
    check(parameters.boundary == TerrainBoundaryMode::MaxEdgeLength &&
              parameters.maxEdgeLength == 15 && parameters.steinerFill,
          "The settings are stored");
    doc.undo();
    check(doc.bodies().at(terrain)->terrain->parameters.boundary ==
              TerrainBoundaryMode::ConvexHull,
          "Undo restores the settings");
    window.viewport()->refresh();
    emit window.viewport()->changed();
}

// ---- Explode -------------------------------------------------------------------------------
void explode(Window &window) {
    fresh(window);
    auto &doc = window.document();
    const auto terrain = hillTerrain(window);
    select(window, terrain);
    const auto triangles = terrainTriangleCount(doc, terrain);
    // Over budget: with a small modelling limit the confirmation explains it and Explode is
    // disabled.
    {
        TerrainExplodeDialog dialog(doc, terrain, &window, triangles - 1);
        dialog.show();
        check(QTest::qWaitForWindowExposed(&dialog), "The over-budget confirmation shows");
        check(!dialog.canExplode() && !child<QPushButton>(dialog, "terrainExplodeConfirm")->isEnabled(),
              "Explode is disabled over the modelling limit");
        check(child<QLabel>(dialog, "terrainDialogBannerText")
                  ->text()
                  .startsWith("Explode is not available: the model would have"),
              "The confirmation explains the limit");
        shot(dialog, "terrain-explode-over-budget");
        dialog.reject();
    }
    const auto history = doc.history().total;
    check(drive(
              "terrainExplodeDialog",
              [&](QWidget &widget) {
                  auto &dialog = static_cast<TerrainExplodeDialog &>(widget);
                  check(dialog.canExplode(), "The hill fits the modelling budget");
                  const auto faces = child<QLabel>(dialog, "terrainExplodeFaces")->text();
                  check(faces == QString("0 now + %1 = %1 of %2")
                                     .arg(QLocale().toString(qulonglong(triangles)),
                                          QLocale().toString(qulonglong(dialog.budget().faceLimit))),
                        "The modelling budget: " + faces.toStdString());
                  check(child<QLabel>(dialog, "terrainExplodeTriangles")->text().contains("freed"),
                        "The terrain budget names the freed triangles");
                  check(child<QPushButton>(dialog, "terrainExplodeCancel")->isDefault(),
                        "Cancel is the default");
                  shot(dialog, "terrain-explode");
                  QStringList findings;
                  child<QPushButton>(dialog, "terrainExplodeCancel")->setFocus(Qt::TabFocusReason);
                  const auto json = dialog_audit::auditOpen(dialog, findings);
                  check(findings.isEmpty(),
                        "Keyboard audit of Explode: " + findings.join("; ").toStdString());
                  // Enter takes the default: Cancel.
                  QTest::keyClick(QApplication::focusWidget(), Qt::Key_Return);
              },
              [&] { action(window, "terrain.explode")->trigger(); }),
          "Explode terrain… opens the confirmation");
    check(doc.history().total == history && isTerrain(doc, terrain), "Enter cancels");
    auto &terrainInfo = section(window);
    check(drive(
              "terrainExplodeDialog",
              [&](QWidget &dialog) { click(dialog, "terrainExplodeConfirm"); },
              [&] {
                  QTest::mouseClick(terrainInfo.explodeButton(), Qt::LeftButton);
                  QTest::qWait(50);
              }),
          "The section's Explode terrain… opens the confirmation");
    check(doc.history().total == history + 1 &&
              doc.history().entries.back().label == "Explode terrain" &&
              doc.bodies().at(terrain)->kind == BodyKind::Group &&
              !doc.bodies().at(terrain)->terrain,
          "Explode is one labelled edit that leaves an ordinary group");
    doc.undo();
    check(isTerrain(doc, terrain), "Undo restores the terrain");
    window.viewport()->refresh();
    emit window.viewport()->changed();
}

// ---- Create terrain from selection ---------------------------------------------------------
void createFromSelection(Window &window) {
    fresh(window);
    auto &doc = window.document();
    const auto bodies = hillEdges(doc);
    window.viewport()->refresh();
    emit window.viewport()->changed();
    auto *create = action(window, "terrain.createFromSelection");
    select(window, 0);
    check(!create->isEnabled(), "Create terrain from selection needs edges");
    window.viewport()->selectEntities(
        join({edgesOf(doc, bodies[0]), edgesOf(doc, bodies[1]), edgesOf(doc, bodies[2])}));
    QTest::qWait(5);
    check(create->isEnabled(), "Selected edges enable Create terrain from selection");
    // The context menu offers it: the Menu key (or Shift+F10) on the viewport. Qt's Wayland
    // popups need a compositor input serial that synthetic input cannot supply (as in
    // narrow_layout_input_tests), so there the test checks the request and the menu's action.
    if (QGuiApplication::platformName() == "wayland") {
        QSignalSpy requested(window.viewport(), &Viewport::contextMenuRequested);
        QContextMenuEvent event(QContextMenuEvent::Keyboard, QPoint(20, 20),
                                window.viewport()->mapToGlobal(QPoint(20, 20)));
        QApplication::sendEvent(window.viewport(), &event);
        QTest::qWait(50);
        check(requested.count() == 1 && create->isEnabled(),
              "The keyboard requests the viewport context menu, with its action enabled");
    } else {
        check(drive(
                  "viewportContextMenu",
                  [&](QWidget &widget) {
                      auto &menu = static_cast<QMenu &>(widget);
                      check(menu.actions().size() == 1 && menu.actions().front() == create &&
                                create->isEnabled(),
                            "The context menu has Create terrain from selection, enabled");
                      menu.close();
                  },
                  [&] {
                      QContextMenuEvent event(QContextMenuEvent::Keyboard, QPoint(20, 20),
                                              window.viewport()->mapToGlobal(QPoint(20, 20)));
                      QApplication::sendEvent(window.viewport(), &event);
                  }),
              "The keyboard opens the viewport context menu");
    }
    const auto history = doc.history().total;
    // R095.h: closed contours open the roles step; Enter on its default button (Create) keeps
    // every tag Automatic, as the one-click path. The summit's flat triangles are a notice, so
    // the report follows the edit.
    bool step = false;
    auto acceptStep = [&] {
        step = drive(
            "terrainSelectionRolesDialog",
            [&](QWidget &widget) {
                auto &dialog = static_cast<TerrainSelectionRolesDialog &>(widget);
                settle(dialog);
                check(dialog.roleTable()->table()->rowCount() == 1 &&
                          dialog.roleTable()->table()->item(0, 0)->text() == "Untagged" &&
                          dialog.roleTable()->table()->item(0, 2)->text() == "Automatic: contour",
                      "The step lists the Untagged edges as Automatic");
                check(child<QPushButton>(dialog, "terrainSelectionCreate")->isDefault() &&
                          dialog.canCreate(),
                      "Create is the default and enabled");
                check(dialog.focusWidget() == dialog.roleTable()->table(),
                      "Focus starts in the table");
                // Enter in the table moves to the Role; Tab then reaches Create.
                press(dialog, Qt::Key_Return);
                check(dialog.focusWidget() == child<QComboBox>(dialog, "terrainSelectionRole"),
                      "Enter in the table moves to the Role");
                tabTo(dialog, "terrainSelectionCreate");
                press(dialog, Qt::Key_Return);
            },
            [&] { create->trigger(); });
    };
    check(drive(
              "terrainReportDialog",
              [&](QWidget &widget) {
                  auto &dialog = static_cast<TerrainReportDialog &>(widget);
                  check(child<QLabel>(dialog, "terrainDialogBannerText")->text() ==
                            "Created “Terrain” with 1 notice. Undo removes it.",
                        "The banner says it was created with notices");
                  check(dialog.reportText().contains("[flat-triangles]"),
                        "The report lists the flat triangles");
                  check(child<QPushButton>(dialog, "terrainReportClose")->isDefault(),
                        "Close is the default");
                  click(dialog, "terrainReportClose");
              },
              acceptStep),
          "Notices show the report");
    check(step, "Closed loops open the roles step");
    check(doc.history().total == history + 1 &&
              doc.history().entries.back().label == "Create terrain",
          "Create terrain is one labelled edit");
    const auto terrain = window.viewport()->selectedBody();
    check(isTerrain(doc, terrain), "The new terrain is selected");
    for (const auto body : bodies)
        check(doc.bodies().contains(body), "The source edges stay in the model");
    doc.undo();
    check(!isTerrain(doc, terrain), "Undo removes the terrain");
    window.viewport()->refresh();
    emit window.viewport()->changed();

    // Crossing lines in one tag, without a closed loop: one click, as before R095.h. They
    // refuse with the bounded report, located; nothing changes.
    fresh(window);
    auto &crossing = window.document();
    const auto a = polyline(crossing, {{-10, 0, 0}, {0, 0.5, 0}, {10, 0, 0}}, false);
    const auto b = polyline(crossing, {{0, -10, 1}, {0.5, 0, 1}, {0, 10, 1}}, false);
    window.viewport()->refresh();
    emit window.viewport()->changed();
    window.viewport()->selectEntities(join({edgesOf(crossing, a), edgesOf(crossing, b)}));
    const auto revision = crossing.revision();
    const auto before = crossing.history().total;
    check(drive(
              "terrainReportDialog",
              [&](QWidget &widget) {
                  auto &dialog = static_cast<TerrainReportDialog &>(widget);
                  check(child<QLabel>(dialog, "terrainDialogBannerText")
                            ->text()
                            .startsWith("Terrain creation refused. Nothing was changed."),
                        "The banner says nothing was changed");
                  auto *list = dialog.reportList();
                  check(list->topLevelItemCount() >= 1 &&
                            list->topLevelItem(0)->text(0).startsWith("Refusals"),
                        "The report lists the refusals");
                  auto *kind = list->topLevelItem(0)->child(0);
                  check(kind && kind->childCount() > 0 &&
                            kind->child(0)->text(0).contains("body"),
                        "The refusal is located by its edges");
                  check(dialog.reportText().contains("crossing"), "Copy report has the kinds");
                  shot(dialog, "terrain-create-refused");
                  click(dialog, "terrainReportClose");
              },
              [&] {
                  check(!drive(
                            "terrainSelectionRolesDialog",
                            [](QWidget &dialog) { static_cast<QDialog &>(dialog).reject(); },
                            [&] { action(window, "terrain.createFromSelection")->trigger(); }),
                        "One tag without a closed loop skips the roles step");
              }),
          "A refusal shows the report");
    check(crossing.revision() == revision && crossing.history().total == before,
          "A refusal changes nothing");

    // Edges inside nested groups, through world transforms.
    fresh(window);
    auto &grouped = window.document();
    const auto outerEdges = polyline(grouped, square(5, 0), true);
    const auto innerEdges = polyline(grouped, square(2, 1), true);
    const auto inner = createGroup(grouped, {outerEdges, innerEdges});
    grouped.transform(inner, Transform::translation({100, 20, 5}));
    const auto outer = createGroup(grouped, {inner});
    grouped.transform(outer, Transform::translation({-50, 0, 1}));
    window.viewport()->refresh();
    emit window.viewport()->changed();
    window.viewport()->enterContext(outer);
    window.viewport()->enterContext(inner);
    window.viewport()->selectEntities(
        join({edgesOf(grouped, outerEdges), edgesOf(grouped, innerEdges)}));
    QTest::qWait(5);
    check(window.viewport()->selectionState().entities().size() == 8,
          "The edges inside the nested group are selected");
    drive(
        "terrainReportDialog", [&](QWidget &dialog) { click(dialog, "terrainReportClose"); },
        [&] {
            drive(
                "terrainSelectionRolesDialog",
                [&](QWidget &widget) {
                    auto &dialog = static_cast<TerrainSelectionRolesDialog &>(widget);
                    settle(dialog);
                    click(dialog, "terrainSelectionCreate");
                },
                [&] { action(window, "terrain.createFromSelection")->trigger(); });
        });
    Id made = 0;
    for (const auto &[id, body] : grouped.bodies())
        if (body->terrain)
            made = id;
    check(made, "Edges in nested groups make a terrain");
    std::set<std::array<double, 3>> vertices;
    for (const auto id : terrainTiles(grouped, made))
        for (const auto &[vertex, p] : grouped.bodies().at(id)->surface.vertices)
            vertices.insert({p.x, p.y, p.z});
    const auto world = grouped.worldTransform(outerEdges);
    for (const auto &[id, p] : grouped.bodies().at(outerEdges)->surface.vertices) {
        const auto w = world.point(p);
        check(vertices.contains({w.x, w.y, w.z}), "Grouped edges are taken in world coordinates");
    }
    window.viewport()->enterContext(0);
}


// ---- R095.h: the roles step ----------------------------------------------------------------
// Hill contours (Untagged), a boundary loop tagged "Site", a pond loop tagged "Pond" on the
// summit and an open line tagged "Notes" off to the side, all selected.
struct RolesFixture {
    std::vector<Id> hill;
    Id site{}, pond{}, notes{};
    Id siteTag{}, pondTag{}, notesTag{};
};
RolesFixture rolesFixture(Window &window) {
    fresh(window);
    auto &doc = window.document();
    RolesFixture f;
    f.hill = hillEdges(doc);
    f.site = polyline(doc, square(25, 10.5), true);
    f.pond = polyline(doc, square(3, 12), true);
    f.notes = polyline(doc, {{40, 40, 9}, {45, 40, 9}}, false);
    f.siteTag = createTag(doc, "Site");
    f.pondTag = createTag(doc, "Pond");
    f.notesTag = createTag(doc, "Notes");
    assignTag(doc, f.site, f.siteTag);
    assignTag(doc, f.pond, f.pondTag);
    assignTag(doc, f.notes, f.notesTag);
    window.viewport()->refresh();
    emit window.viewport()->changed();
    window.viewport()->selectEntities(join({edgesOf(doc, f.hill[0]), edgesOf(doc, f.hill[1]),
                                            edgesOf(doc, f.hill[2]), edgesOf(doc, f.site),
                                            edgesOf(doc, f.pond), edgesOf(doc, f.notes)}));
    QTest::qWait(5);
    return f;
}
// Focus the table, move to `row` with the arrow keys, Tab to the Role combo and step down
// `steps` choices from Automatic (the closed combo steps through its items without a popup).
void chooseRole(QWidget &dialog, QTableWidget *table, int row, int steps) {
    table->setFocus(Qt::BacktabFocusReason);
    QTest::qWait(5);
    check(dialog.focusWidget() == table, "The table has the focus");
    while (table->currentRow() < row)
        press(dialog, Qt::Key_Down);
    while (table->currentRow() > row)
        press(dialog, Qt::Key_Up);
    press(dialog, Qt::Key_Tab);
    auto *combo = child<QComboBox>(dialog, "terrainSelectionRole");
    check(dialog.focusWidget() == combo, "Tab reaches the Role combo");
    for (int i = 0; i < steps; ++i)
        press(dialog, Qt::Key_Down);
}
void selectionRoles(Window &window) {
    auto f = rolesFixture(window);
    auto &doc = window.document();
    auto *create = action(window, "terrain.createFromSelection");
    // An invalid boundary: the open "Notes" line. The inline error names the tag and the count;
    // Create is disabled and Enter does nothing; Cancel changes nothing.
    const auto revision = doc.revision();
    const auto history = doc.history().total;
    check(drive(
              "terrainSelectionRolesDialog",
              [&](QWidget &widget) {
                  auto &dialog = static_cast<TerrainSelectionRolesDialog &>(widget);
                  settle(dialog);
                  auto *table = dialog.roleTable()->table();
                  check(table->rowCount() == 4 && table->item(0, 0)->text() == "Untagged" &&
                            table->item(1, 0)->text() == "Site" &&
                            table->item(2, 0)->text() == "Pond" &&
                            table->item(3, 0)->text() == "Notes",
                        "One row per tag, Untagged first");
                  check(table->item(0, 1)->text() == "12 edges · 3 closed loops" &&
                            table->item(3, 1)->text() == "1 edge · 1 open chain",
                        "The contents count edges, loops and open chains: " +
                            table->item(3, 1)->text().toStdString());
                  check(table->accessibleName() == "Tag roles", "The table is named");
                  check(dialog.canCreate(), "Automatic roles can be created");
                  chooseRole(dialog, table, 3, 3); // Notes: Boundary
                  auto *combo = child<QComboBox>(dialog, "terrainSelectionRole");
                  check(combo->currentData() == "boundary" &&
                            combo->accessibleName() == "Role for Notes",
                        "The combo is named for its tag and set by keyboard");
                  settle(dialog);
                  auto *status = child<QLabel>(dialog, "terrainSelectionStatus");
                  check(status->text() == "Cannot create: Boundary “Notes” has 1 open chain; a "
                                          "boundary is one closed loop.",
                        "The inline error names the tag and the count: " +
                            status->text().toStdString());
                  check(status->property("refused").toBool() && !dialog.canCreate() &&
                            !child<QPushButton>(dialog, "terrainSelectionCreate")->isEnabled(),
                        "Create is disabled");
                  shot(dialog, "terrain-selection-roles-invalid");
                  press(dialog, Qt::Key_Tab); // to the report: Enter goes to the dialog
                  press(dialog, Qt::Key_Return);
                  check(dialog.isVisible(), "Enter does not create with an invalid boundary");
                  click(dialog, "terrainSelectionCancel");
              },
              [&] { create->trigger(); }),
          "The roles step appears for closed loops and several tags");
    check(doc.revision() == revision && doc.history().total == history, "Cancel changes nothing");

    // Site as the boundary and Pond as a hole, by keyboard; Notes ignored. Create is one edit.
    bool created = false;
    drive(
        "terrainReportDialog", [&](QWidget &dialog) { click(dialog, "terrainReportClose"); },
        [&] {
            created = drive(
                "terrainSelectionRolesDialog",
                [&](QWidget &widget) {
                    auto &dialog = static_cast<TerrainSelectionRolesDialog &>(widget);
                    settle(dialog);
                    auto *table = dialog.roleTable()->table();
                    chooseRole(dialog, table, 1, 3); // Site: Boundary
                    chooseRole(dialog, table, 2, 4); // Pond: Hole
                    chooseRole(dialog, table, 3, 5); // Notes: Ignore
                    settle(dialog);
                    check(table->item(1, 2)->text() == "Boundary" &&
                              table->item(2, 2)->text() == "Hole" &&
                              table->item(3, 2)->text() == "Ignore",
                          "The table shows the chosen roles");
                    const auto command = dialog.command();
                    check(command["boundary"].toArray().size() == 4 &&
                              command["holes"].toArray().size() == 1 &&
                              command["edges"].toArray().size() == 12,
                          "The roles reach the command");
                    check(dialog.canCreate() && child<QLabel>(dialog, "terrainSelectionStatus")
                                                    ->text()
                                                    .startsWith("Ready: "),
                          "Valid roles are ready");
                    shot(dialog, "terrain-selection-roles");
                    tabTo(dialog, "terrainSelectionCreate");
                    press(dialog, Qt::Key_Return);
                },
                [&] { create->trigger(); });
        });
    check(created, "The roles step appears again");
    check(doc.history().total == history + 1 &&
              doc.history().entries.back().label == "Create terrain",
          "Create terrain is one labelled edit");
    const auto terrain = window.viewport()->selectedBody();
    check(isTerrain(doc, terrain), "The new terrain is selected");
    std::size_t boundaries = 0, holes = 0;
    for (const auto &source : doc.bodies().at(terrain)->terrain->sources) {
        boundaries += source.role == TerrainSourceRole::Boundary;
        holes += source.role == TerrainSourceRole::Hole;
        check(source.reference.find("body " + std::to_string(f.notes) + " ") == std::string::npos,
              "Ignore drops the Notes edges");
    }
    const auto found = terrainElevationAt(doc, terrain, {{0, 0}, {15, 0}, {27, 0}});
    check(boundaries == 1 && holes == 1 && !found[0].found && found[1].found && !found[2].found,
          "The boundary and the hole shape the terrain");
    doc.undo();
    window.viewport()->refresh();
    emit window.viewport()->changed();
    check(!isTerrain(doc, terrain) && doc.history().position == history,
          "Undo removes the terrain");
}

// D-044: the roles step's dry run, held at the start of triangulation, shows its stage and a
// determinate progress bar without taking the focus; a changed role cancels it and starts
// again; Cancel during a run closes at once without waiting for it and changes nothing.
void selectionRolesProgress(Window &window) {
    rolesFixture(window);
    auto &doc = window.document();
    const auto revision = doc.revision();
    const auto history = doc.history().total;
    test::TerrainRunHold hold;
    QElapsedTimer clock;
    check(drive(
              "terrainSelectionRolesDialog",
              [&](QWidget &widget) {
                  auto &dialog = static_cast<TerrainSelectionRolesDialog &>(widget);
                  check(QTest::qWaitFor([&] { return hold.holding == 1; }, 10000),
                        "The roles dry run reaches triangulation");
                  auto *status = child<QLabel>(dialog, "terrainSelectionStatus");
                  check(QTest::qWaitFor(
                            [&] { return status->text() == "Checking: triangulating…"; }, 5000),
                        "The status names the stage: " + status->text().toStdString());
                  auto *bar = dialog.progressBar();
                  check(bar->isVisible() && bar->objectName() == "terrainSelectionProgress" &&
                            !bar->accessibleName().isEmpty() && bar->focusPolicy() == Qt::NoFocus &&
                            bar->value() >= 30 && bar->value() < 95,
                        "A determinate, named progress bar shows the run");
                  check(!dialog.canCreate() && dialog.running(), "Create waits for the run");
                  auto *table = dialog.roleTable()->table();
                  check(dialog.focusWidget() == table, "Progress does not take the focus");
                  // A changed role cancels the stale run and starts another.
                  // Each step of the combo is a change; every run before the last is cancelled.
                  chooseRole(dialog, table, 3, 5); // Notes: Ignore
                  check(QTest::qWaitFor(
                            [&] {
                                return hold.held >= 2 && hold.holding == 1 &&
                                       hold.cancelled == hold.held - 1;
                            },
                            10000),
                        "A changed role cancels the running dry run and starts again");
                  check(dialog.focusWidget() == child<QComboBox>(dialog, "terrainSelectionRole"),
                        "The focus stays on the Role");
                  clock.start();
                  click(dialog, "terrainSelectionCancel");
              },
              [&] { action(window, "terrain.createFromSelection")->trigger(); }),
          "The roles step opens");
    const auto elapsed = clock.elapsed();
    std::cout << "roles step Cancel during a run: closed in " << elapsed << " ms\n";
    check(hold.cancelled == hold.held && hold.holding == 0, "Cancel cancels the running dry run");
    check(elapsed < 2000, "The roles step closes without waiting for the run");
    check(doc.revision() == revision && doc.history().total == history,
          "Cancel during a run changes nothing");
}

// ---- Tools on terrain ----------------------------------------------------------------------
// A pointer move sent to the viewport, as the other input suites hover: QTest::mouseMove moves
// the real cursor, which on X11 at 2x does not reach the viewport as a move event.
void hover(Viewport *view, QPoint point) {
    QMouseEvent event(QEvent::MouseMove, QPointF(point), view->mapToGlobal(QPointF(point)),
                      Qt::NoButton, Qt::NoButton, Qt::NoModifier);
    QCoreApplication::sendEvent(view, &event);
    QCoreApplication::processEvents();
}
void toolsOnTerrain(Window &window) {
    fresh(window);
    auto &doc = window.document();
    const auto terrain = hillTerrain(window);
    auto *view = window.viewport();
    view->setSelection(0);
    view->standardView(1); // top
    view->fit();
    QTest::qWait(30);
    auto *status = child<QLabel>(window, "statusMessage");
    auto *link = child<QPushButton>(window, "statusExplodeTerrain");
    const auto onTerrain = view->project({3, 4, 12}).toPoint();
    const QPoint offTerrain(4, 4);
    // Push/pull refuses a tile wherever it is.
    action(window, "tool.3")->trigger();
    hover(view, offTerrain);
    hover(view, onTerrain);
    QTest::qWait(20);
    check(view->refusedTerrain() == terrain && view->cursor().shape() == Qt::ForbiddenCursor,
          "Push/pull shows the not-allowed cursor over a tile");
    check(status->text() == "Push/pull does not edit terrain. Edit it with the terrain tools, or "
                            "explode it to work on it as ordinary geometry.",
          "The status bar says why: " + status->text().toStdString());
    check(link->isVisible(), "The status bar links to Explode");
    shot(window, "tool-refusal-status");
    // The link opens the same confirmation.
    check(drive(
              "terrainExplodeDialog",
              [&](QWidget &dialog) { click(dialog, "terrainExplodeCancel"); },
              [&] { QTest::mouseClick(link, Qt::LeftButton); }),
          "The status link opens the Explode confirmation");
    check(isTerrain(doc, terrain), "Cancel keeps the terrain");
    // Leaving the tile clears it.
    hover(view, offTerrain);
    QTest::qWait(20);
    check(!view->refusedTerrain() && view->cursor().shape() == Qt::CrossCursor && !link->isVisible(),
          "Off the terrain the tool cursor and status return");
    // Paint and Select are allowed; Move moves the whole terrain from outside it.
    for (const char *id : {"tool.20", "tool.0", "tool.17"}) {
        action(window, id)->trigger();
        hover(view, offTerrain);
        hover(view, onTerrain);
        QTest::qWait(20);
        check(!view->refusedTerrain() && view->cursor().shape() != Qt::ForbiddenCursor,
              std::string(id) + " is allowed over the terrain");
    }
    // Inside the open terrain, Move would move tile geometry: refused.
    view->enterContext(terrain);
    action(window, "tool.17")->trigger();
    hover(view, offTerrain);
    hover(view, onTerrain);
    QTest::qWait(20);
    check(view->refusedTerrain() == terrain && status->text().startsWith("Move does not edit terrain."),
          "Move refuses a tile inside the open terrain");
    view->enterContext(0);
    action(window, "tool.0")->trigger();
    check(!view->refusedTerrain() && !link->isVisible(), "Changing the tool clears the refusal");
}
} // namespace

// ---- R096.f1c: the section's figures, per tile --------------------------------------------
// Rings of contours (flat triangles between points of one ring) over several tiles.
QByteArray ringContours(int rings, int vertices) {
    QByteArray out = "0\nSECTION\n2\nHEADER\n9\n$ACADVER\n1\nAC1015\n9\n$INSUNITS\n70\n6\n0\n"
                     "ENDSEC\n0\nSECTION\n2\nENTITIES\n";
    for (int k = 0; k < rings; ++k) {
        const double r = 10 + 3 * k, z = 105 - 0.5 * k;
        out += "0\nLWPOLYLINE\n5\n" + QByteArray::number(0x100 + k, 16).toUpper() +
               "\n8\nCONTOURS\n90\n" + QByteArray::number(vertices) + "\n70\n1\n38\n" +
               QByteArray::number(z, 'f', 4) + '\n';
        for (int i = 0; i < vertices; ++i) {
            const double t = 2 * 3.14159265358979323846 * i / vertices + 0.05 * k;
            out += "10\n" + QByteArray::number(r * std::cos(t) * 1.3, 'f', 4) + "\n20\n" +
                   QByteArray::number(r * std::sin(t), 'f', 4) + '\n';
        }
    }
    out += "0\nENDSEC\n0\nEOF\n";
    return out;
}
// The full scan the section made before R096.f1c.
TerrainStatistics fullScan(const Document &doc, Id terrain) {
    TerrainStatistics full;
    const auto tiles = terrainTiles(doc, terrain);
    full.tiles = tiles.size();
    full.triangles = terrainTriangleCount(doc, terrain);
    full.low = std::numeric_limits<double>::infinity();
    full.high = -full.low;
    const auto world = doc.worldTransform(terrain);
    for (const auto id : tiles)
        for (const auto &[vertex, point] : doc.bodies().at(id)->surface.vertices) {
            const auto z = world.point(point).z;
            full.low = std::min(full.low, z);
            full.high = std::max(full.high, z);
        }
    full.flat = terrainFlatArea(doc, terrain);
    return full;
}
bool sameBits(double a, double b) {
    return std::bit_cast<std::uint64_t>(a) == std::bit_cast<std::uint64_t>(b);
}
QJsonObject batchOf(const Document &doc, QJsonArray commands) {
    return {{"apiVersion", 1},
            {"documentId", QString::fromStdString(doc.identity())},
            {"expectedRevision", QString::number(doc.revision())},
            {"commands", commands}};
}
void sectionStatistics(Window &window) {
    fresh(window);
    auto &doc = window.document();
    const QJsonObject options{{"format", "dxf"}, {"units", "header"}, {"name", "Rings"}};
    const auto terrain =
        importTerrainSource(doc, ringContours(24, 120), options)["terrain"].toString().toULongLong();
    TerrainStatisticsCache cache;
    auto same = [&](Id which, const std::string &when) {
        const auto cached = cache.get(doc, which);
        const auto full = fullScan(doc, which);
        check(cached.tiles == full.tiles && cached.triangles == full.triangles &&
                  sameBits(cached.low, full.low) && sameBits(cached.high, full.high) &&
                  cached.flat.triangles == full.flat.triangles &&
                  sameBits(cached.flat.area, full.flat.area),
              "the per-tile figures equal the full scan " + when + " (" +
                  std::to_string(cached.flat.triangles) + " flat triangles, " +
                  std::to_string(cached.tiles) + " tiles)");
        check(cache.heldTiles() == cached.tiles, "the cache holds exactly the terrain's tiles " + when);
        return cached;
    };
    const auto first = same(terrain, "after the import");
    check(first.tiles > 4 && first.flat.triangles > 0,
          "the rings have several tiles and flat areas");
    check(cache.tileScans() == first.tiles, "the first read scans every tile");
    auto stroke = [&](const char *mode, std::vector<std::array<double, 2>> path, double radius) {
        const auto world = doc.worldTransform(terrain);
        QJsonArray points;
        for (const auto &p : path) {
            const auto m = world.point({p[0], p[1], 0});
            points.append(QJsonArray{m.x, m.y});
        }
        const bool smoothing = std::string(mode) == "smooth",
                   lowering = std::string(mode) == "lower"; // a raise by a negative amount
        QJsonObject command{{"command", "terrain.sculpt"},
                            {"terrain", QString::number(terrain)},
                            {"mode", smoothing ? "smooth" : "raise"},
                            {"path", points},
                            {"radius", radius}};
        command[smoothing ? "strength" : "amount"] = smoothing ? 0.8 : lowering ? -0.6 : 0.6;
        run(doc, command);
    };
    // Each edit rescans only the tiles it replaced; the replaced tiles take new IDs, so the old
    // ones leave the cache.
    for (const auto &[mode, path, radius] :
         std::vector<std::tuple<const char *, std::vector<std::array<double, 2>>, double>>{
             {"raise", {{20, 0}, {28, 3}}, 4},
             {"smooth", {{-30, -10}, {-24, -6}}, 5},
             {"lower", {{0, 40}, {6, 44}}, 6}}) {
        const auto before = terrainTiles(doc, terrain);
        const auto scans = cache.tileScans();
        stroke(mode, path, radius);
        const auto after = terrainTiles(doc, terrain);
        std::vector<Id> gone, added;
        std::set_difference(before.begin(), before.end(), after.begin(), after.end(),
                            std::back_inserter(gone));
        std::set_difference(after.begin(), after.end(), before.begin(), before.end(),
                            std::back_inserter(added));
        const auto read = same(terrain, std::string("after a ") + mode);
        const auto rescanned = cache.tileScans() - scans;
        check(!gone.empty() && rescanned == added.size() && rescanned < read.tiles,
              std::string("a ") + mode + " rescans exactly its new tiles (" +
                  std::to_string(rescanned) + " of " + std::to_string(read.tiles) + ", " +
                  std::to_string(added.size()) + " new)");
    }
    auto scans = cache.tileScans();
    same(terrain, "on a second read");
    check(cache.tileScans() == scans, "an unchanged terrain rescans nothing");
    doc.undo();
    same(terrain, "after Undo");
    doc.redo();
    same(terrain, "after Redo");
    doc.undo();
    doc.undo();
    same(terrain, "after two Undos");
    // The section's own figures, refreshed after an edit, equal a new section's (a full scan)
    // label for label, and it rescans only the replaced tiles.
    window.viewport()->refresh();
    emit window.viewport()->changed();
    select(window, terrain);
    auto &info = section(window);
    const char *const labels[] = {"terrainTriangles", "terrainElevation", "terrainFlatAreas"};
    auto sameLabels = [&](const std::string &when) {
        TerrainSection reference(doc, *window.viewport());
        reference.refresh();
        for (const auto *name : labels)
            check(info.findChild<QLabel *>(name)->text() ==
                      reference.findChild<QLabel *>(name)->text(),
                  std::string("the section's ") + name + " equals a full scan's " + when + ": " +
                      info.findChild<QLabel *>(name)->text().toStdString());
    };
    sameLabels("when shown");
    const auto sectionScans = info.statistics().tileScans();
    const auto beforeStroke = terrainTiles(doc, terrain);
    stroke("raise", {{-20, 20}, {-14, 24}}, 5);
    std::vector<Id> strokeTiles;
    {
        const auto afterStroke = terrainTiles(doc, terrain);
        std::set_difference(afterStroke.begin(), afterStroke.end(), beforeStroke.begin(),
                            beforeStroke.end(), std::back_inserter(strokeTiles));
    }
    window.viewport()->refresh();
    emit window.viewport()->changed();
    info.refresh();
    sameLabels("after a sculpt");
    check(info.statistics().tileScans() - sectionScans == strokeTiles.size(),
          "the section rescans exactly the stroke's new tiles");
    doc.undo();
    window.viewport()->refresh();
    emit window.viewport()->changed();
    info.refresh();
    sameLabels("after Undo");
    // Hidden, it holds nothing.
    select(window, 0);
    info.refresh();
    check(info.statistics().heldTiles() == 0, "a hidden section holds no tiles");
    // A moved terrain changes every world elevation: everything is rescanned.
    executeBatch(doc, batchOf(doc, {QJsonObject{{"command", "geometry.translate"},
                                                {"body", QString::number(terrain)},
                                                {"delta", QJsonArray{3.0, -2.0, 1.25}}}}));
    scans = cache.tileScans();
    auto moved = same(terrain, "after moving the terrain");
    check(cache.tileScans() - scans == moved.tiles, "a move rescans every tile");
    // So does moving a group that holds it.
    const auto group = createGroup(doc, {terrain}, "Site");
    same(terrain, "after grouping the terrain");
    executeBatch(doc, batchOf(doc, {QJsonObject{{"command", "geometry.translate"},
                                                {"body", QString::number(group)},
                                                {"delta", QJsonArray{-1.0, 0.5, -2.5}}}}));
    scans = cache.tileScans();
    moved = same(terrain, "after moving its group");
    check(cache.tileScans() - scans == moved.tiles, "a parent's move rescans every tile");
    // Two terrains: switching reads the other one's figures, exactly.
    const auto second =
        importTerrainSource(doc, ringContours(10, 60), options)["terrain"].toString().toULongLong();
    same(second, "for a second terrain");
    same(terrain, "back on the first terrain");
    same(second, "on the second again");
    // A terrain that is gone (its import undone) holds nothing.
    doc.undo();
    const auto gone = cache.get(doc, second);
    check(gone.tiles == 0 && gone.triangles == 0 && cache.heldTiles() == 0,
          "a deleted terrain has no figures and the cache holds no tiles");
    // A terrain with no mesh (every tile erased in a terrain edit): "No mesh", as the full scan.
    {
        auto edited = std::make_shared<Body>(*doc.bodies().at(terrain));
        edited->terrain->generator = "edited/terrain-v1";
        Edit erase{"Erase tiles", {{terrain, doc.bodies().at(terrain), edited}}};
        for (const auto id : terrainTiles(doc, terrain))
            erase.changes.push_back({id, doc.bodies().at(id), nullptr});
        doc.apply(std::move(erase), doc.revision());
        const auto empty = same(terrain, "with no mesh");
        check(empty.tiles == 0 && empty.flat.triangles == 0 && cache.heldTiles() == 0,
              "a terrain without tiles has no figures");
        doc.undo();
        same(terrain, "after the tiles come back");
    }
}

int main(int argc, char **argv) {
    QTemporaryDir preferences;
    qputenv("XDG_CONFIG_HOME", preferences.path().toUtf8());
    QApplication app(argc, argv);
    for (int i = 1; i + 1 < argc; ++i) {
        if (QString(argv[i]) == "--screenshots")
            shots = argv[i + 1];
        if (QString(argv[i]) == "--text-percent")
            QSettings("SketchyUp", "SketchyUp").setValue("interfaceTextPercent", QString(argv[i + 1]).toInt());
    }
    Window window;
    window.resize(1280, 860);
    window.show();
    if (!QTest::qWaitForWindowExposed(&window, 5000))
        return 2;
    try {
        sectionFields(window);
        std::cout << "terrain section fields\n" << std::flush;
        changedChipAndRegenerate(window);
        std::cout << "changed chip and regenerate\n" << std::flush;
        settings(window);
        std::cout << "terrain settings\n" << std::flush;
        explode(window);
        std::cout << "explode confirmation\n" << std::flush;
        createFromSelection(window);
        std::cout << "create terrain from selection\n" << std::flush;
        selectionRoles(window);
        std::cout << "create terrain from selection: roles step\n" << std::flush;
        selectionRolesProgress(window);
        std::cout << "create terrain from selection: roles step progress and cancel\n"
                  << std::flush;
        toolsOnTerrain(window);
        std::cout << "tools on terrain\n" << std::flush;
        sectionStatistics(window);
        std::cout << "section statistics\n" << std::flush;
    } catch (const std::exception &error) {
        std::cerr << "FAIL: " << error.what() << '\n';
        return 1;
    }
    return 0;
}
