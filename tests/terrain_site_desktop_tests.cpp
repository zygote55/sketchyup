// R095.e2 (ADR 0168 "Decisions > Interaction design", Appearance and Site): the desktop terrain
// display and site surface, offscreen. The Entity info Terrain section's Display controls (each
// change one undoable "Terrain display" edit that keeps the mesh digest); hidden tile seams and
// the contour lines the viewport draws; File > Site settings… without and with an origin, Set
// origin… and Re-anchor… through the dialogs (one edit, site coordinates unchanged), refused for
// a locked top-level body and the coordinate limit; `@E, N[, Z]` in the Measurements box; site
// coordinates in Entity info; and View > Site origin with its persisted state.
// `--screenshots DIR` saves the Display controls, the Site settings and Re-anchor dialogs and,
// when the viewport renders, the terrain; `--text-percent N` runs at that interface text size.
#include "app/collapsible_section.hpp"
#include "app/entity_info_panel.hpp"
#include "app/organization_panel.hpp"
#include "app/site_dialogs.hpp"
#include "app/terrain_actions.hpp"
#include "app/terrain_section.hpp"
#include "app/unit_display.hpp"
#include "app/window.hpp"
#include "automation/commands.hpp"
#include "core/terrain.hpp"
#include "core/terrain_display.hpp"
#include <QAction>
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFrame>
#include <QJsonArray>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSettings>
#include <QTemporaryDir>
#include <QTest>
#include <QTimer>
#include <cmath>
#include <iostream>
using namespace sketchy;
namespace {
QString shots, only; // `--only NAME` runs one part (for a rendering-only pass).
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
void fresh(Window &window) {
    window.viewport()->cancel();
    window.viewport()->setTool(Viewport::Tool::Select);
    window.document() = Document();
    window.viewport()->setSelection(0);
    window.viewport()->refresh();
    emit window.viewport()->changed();
}
void changed(Window &window) {
    window.viewport()->refresh();
    emit window.viewport()->changed();
    QTest::qWait(5);
}
std::string lastLabel(const Document &doc) {
    std::string label;
    for (const auto &entry : doc.history().entries)
        if (entry.applied)
            label = entry.label;
    return label;
}
// A wavy grid of nx x ny one-metre cells (two triangles each) as a terrain, at height `base`.
Id gridTerrain(Window &window, int nx, int ny, double base = 0) {
    TerrainMesh mesh;
    for (int j = 0; j <= ny; ++j)
        for (int i = 0; i <= nx; ++i)
            mesh.vertices.push_back(
                {double(i), double(j), base + 4 * std::sin(i * .15) * std::cos(j * .1) + .05 * i});
    auto at = [&](int i, int j) { return std::uint32_t(j * (nx + 1) + i); };
    for (int j = 0; j < ny; ++j)
        for (int i = 0; i < nx; ++i) {
            mesh.triangles.push_back({at(i, j), at(i + 1, j), at(i + 1, j + 1)});
            mesh.triangles.push_back({at(i, j), at(i + 1, j + 1), at(i, j + 1)});
        }
    TerrainRecord record;
    record.generator = "test/terrain-v1";
    record.parameters.boundary = TerrainBoundaryMode::ConvexHull;
    const auto terrain = createTerrain(window.document(), mesh, record, 0, "Hillside").terrain;
    changed(window);
    return terrain;
}
template <class T> T *widgetOf(Window &window) {
    for (auto *widget : window.findChildren<QWidget *>())
        if (auto *found = dynamic_cast<T *>(widget))
            return found;
    return nullptr;
}
EntityInfoPanel &infoPanel(Window &window) {
    widgetOf<OrganizationPanel>(window)->showInfo();
    auto *panel = widgetOf<EntityInfoPanel>(window);
    check(panel && panel->terrain(), "Entity info has a Terrain section");
    QTest::qWait(10);
    panel->refresh();
    return *panel;
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
void setSite(Window &window, double e, double n, double z) {
    setSiteOrigin(window.document(),
                  SiteOrigin{e, n, z, "EPSG:32633", {"hillside.dxf", std::string(64, 'b')}});
    changed(window);
}
// Renders the viewport when it can (a GL context offscreen); null otherwise.
QImage frame(Window &window) {
    auto *view = window.viewport();
    view->update();
    QTest::qWait(30);
    if (!view->rendererReady())
        return {};
    return view->grabFramebuffer();
}

// ---- Entity info: Display ------------------------------------------------------------------
void displayControls(Window &window) {
    fresh(window);
    auto &doc = window.document();
    const auto terrain = gridTerrain(window, 40, 30);
    select(window, terrain);
    auto &panel = infoPanel(window);
    auto &section = *panel.terrain();
    section.refresh();
    check(section.isVisible() && section.terrain() == terrain, "The Terrain section shows");
    auto *contours = section.contoursBox();
    auto *interval = section.intervalBox();
    auto *shading = section.shadingBox();
    check(contours->accessibleName() == "Terrain contour lines" &&
              interval->accessibleName() == "Terrain contour interval" &&
              shading->accessibleName() == "Terrain shading",
          "Display controls have accessible names");
    check(!contours->isChecked() && interval->value() == 1 && shading->currentIndex() == 0,
          "Defaults: no contours, 1 m, material");
    check(!section.contourStatus()->isVisible(), "No contour status while contours are off");
    // Tab order: Contour lines → Interval → Shading → Terrain settings….
    auto nextTab = [](QWidget *from) {
        auto *next = from->nextInFocusChain();
        // Skip the composite's own internal editor (a spin box's line edit).
        while (next != from && (from->isAncestorOf(next) ||
                                !((next->focusPolicy() & Qt::TabFocus) && next->isVisible() &&
                                  next->isEnabled())))
            next = next->nextInFocusChain();
        return next;
    };
    auto *afterContours = nextTab(contours);
    check(afterContours == interval || afterContours->parentWidget() == interval,
          "Tab goes from Contour lines to Interval");
    auto *afterInterval = nextTab(interval);
    check(afterInterval == shading, "Tab goes from Interval to Shading");
    check(nextTab(shading) == section.settingsButton(), "Then to Terrain settings…");
    const auto digest = terrainMeshDigest(doc, terrain);
    const auto revision = doc.revision();
    // Contour lines on: one edit.
    QTest::mouseClick(contours, Qt::LeftButton);
    QTest::qWait(10);
    check(doc.revision() == revision + 1 && lastLabel(doc) == "Terrain display",
          "Contour lines on is one Terrain display edit");
    const auto &record = *doc.bodies().at(terrain)->terrain;
    check(record.display.contours && terrainMeshDigest(doc, terrain) == digest &&
              record.meshDigest == digest && !terrainChangedSinceGenerated(doc, terrain),
          "The mesh digest is unchanged and the terrain is not changed since generated");
    panel.refresh();
    section.refresh();
    check(!section.changedChip()->isVisible(), "No Changed chip after a display edit");
    check(section.contourStatus()->isVisible() &&
              section.contourStatus()->text().contains("contour lines") &&
              section.contourStatus()->text().contains("index line"),
          "The contour status counts lines: " + section.contourStatus()->text().toStdString());
    const auto shown = window.viewport()->terrainContours(terrain);
    check(shown && shown->segments.size() > 100 && shown->interval == 1, "Contours computed");
    // Interval: one edit.
    interval->setValue(.5);
    QTest::qWait(10);
    check(doc.revision() == revision + 2 &&
              doc.bodies().at(terrain)->terrain->display.contourInterval == .5,
          "The interval is one edit");
    // Shading by elevation bands with the keyboard: one edit.
    shading->setFocus(Qt::TabFocusReason);
    QTest::keyClick(shading, Qt::Key_Down);
    QTest::qWait(10);
    check(doc.revision() == revision + 3 &&
              doc.bodies().at(terrain)->terrain->display.shading == TerrainShading::ElevationBands,
          "Shading is one edit");
    section.refresh();
    shot(section, "entity-info-display");
    // A dense interval: no lines, and the status says so.
    interval->setValue(.01);
    QTest::qWait(10);
    section.refresh();
    check(section.contourStatus()->text().startsWith("No contour lines") ||
              window.viewport()->terrainContours(terrain)->segments.size() <=
                  terrainContourSegmentLimit,
          "A dense interval is bounded");
    // Undo steps back through the edits; the digest never moved.
    action(window, "edit.undo")->trigger();
    action(window, "edit.undo")->trigger();
    check(doc.bodies().at(terrain)->terrain->display.contourInterval == .5 &&
              doc.bodies().at(terrain)->terrain->display.shading == TerrainShading::Material,
          "Undo restores the previous settings");
    action(window, "edit.undo")->trigger();
    action(window, "edit.undo")->trigger();
    check(doc.bodies().at(terrain)->terrain->display == TerrainDisplay{} &&
              terrainMeshDigest(doc, terrain) == digest,
          "Undo restores the defaults");
    panel.refresh();
    section.refresh();
    check(!contours->isChecked() && interval->value() == 1 && shading->currentIndex() == 0,
          "The controls follow Undo");
    // (A locked terrain cannot be selected, so its section never shows; the core refusal of a
    // locked terrain's display edit is covered by terrain_display_tests.)
}

// ---- Viewport: seams, contours and their rendering ------------------------------------------
void viewportDisplay(Window &window) {
    fresh(window);
    auto &doc = window.document();
    const auto terrain = gridTerrain(window, 60, 50); // 6,000 triangles in several tiles
    const auto tiles = terrainTiles(doc, terrain);
    check(tiles.size() > 2, "Several tiles");
    auto *view = window.viewport();
    frame(window);
    // Seams and interior edges are not drawn (nor picked); the outer boundary is.
    std::size_t drawn = 0, seams = 0;
    for (const auto id : tiles) {
        const auto &tile = *doc.bodies().at(id);
        for (const auto &[edge, record] : tile.topology.edges) {
            if (tile.edgeAppearances.contains(edge))
                continue;
            if (view->terrainEdgeDrawn(id, edge))
                ++drawn;
            else
                ++seams;
        }
    }
    const bool rendered = view->rendererReady();
    std::cout << "  viewport renderer " << (rendered ? "ready" : "not ready (no GL context)") << "\n";
    if (rendered) {
        check(drawn == 2 * (60 + 50) && seams > 0,
              "Only the outer boundary is drawn: " + std::to_string(drawn) + " drawn, " +
                  std::to_string(seams) + " seams");
        check(view->renderStats().terrainSurfaces >= 1, "The terrain surface was built");
    }
    // Contours and elevation bands.
    const auto plain = frame(window);
    executeBatch(doc, {{"apiVersion", 1},
                       {"documentId", QString::fromStdString(doc.identity())},
                       {"expectedRevision", QString::number(doc.revision())},
                       {"commands", QJsonArray{QJsonObject{{"command", "terrain.set_display"},
                                                           {"terrain", QString::number(terrain)},
                                                           {"contours", true},
                                                           {"contourInterval", .5},
                                                           {"shading", "elevationBands"}}}}});
    setSite(window, 512300, 5234500, 0);
    select(window, terrain);
    view->fit();
    const auto decorated = frame(window);
    if (rendered) {
        check(view->terrainContoursShown(terrain) &&
                  view->renderStats().contourSegments ==
                      view->terrainContoursShown(terrain)->segments.size(),
              "The viewport draws the contour segments");
        check(!plain.isNull() && !decorated.isNull() && plain != decorated,
              "Contours and bands change the rendered image");
        if (!shots.isEmpty())
            decorated.save(shots + "/viewport-terrain.png");
    }
    // The origin marker shows for a selected terrain with a site origin.
    check(view->siteOriginMarkerShown() && view->siteOriginMarkerLabel().startsWith("Site origin"),
          "The origin marker shows while the terrain is selected");
    // Contours are derived display: hiding the terrain hides them.
    auto hidden = std::make_shared<Body>(*doc.bodies().at(terrain));
    hidden->hidden = true;
    doc.apply({"Hide", {{terrain, doc.bodies().at(terrain), hidden}}}, doc.revision());
    changed(window);
    frame(window);
    if (rendered)
        check(!view->terrainContoursShown(terrain) && view->renderStats().contourSegments == 0,
              "Hidden terrain draws no contours");
}

// ---- Site settings, Set origin and Re-anchor ------------------------------------------------
void siteSettings(Window &window) {
    fresh(window);
    auto &doc = window.document();
    check(!window.viewport()->siteOriginMarkerShown(), "No marker without a site origin");
    bool opened = drive(
        "siteSettingsDialog",
        [&](QWidget &dialog) {
            check(child<QFrame>(dialog, "siteNoOrigin")->isVisible() &&
                      !child<QFrame>(dialog, "siteOrigin")->isVisible(),
                  "Without an origin the dialog explains how one is established");
            check(child<QLabel>(dialog, "siteNoOriginText")->text().contains("first terrain import"),
                  "It names the first terrain import");
            shot(dialog, "site-settings-none");
            const bool set = drive(
                "siteSetOriginDialog",
                [&](QWidget &origin) {
                    auto &setter = static_cast<SiteOriginDialog &>(origin);
                    setter.easting()->setValue(512300);
                    setter.northing()->setValue(5234500);
                    setter.elevation()->setValue(100);
                    child<QLineEdit>(origin, "siteOriginCrs")->setText("EPSG:32633");
                    QTest::qWait(300);
                    check(setter.canApply(), "Set origin is available");
                    click(origin, "siteOriginApply");
                },
                [&] { click(dialog, "siteSetOrigin"); });
            check(set, "Set origin… opens its dialog");
            check(doc.site() && doc.site()->easting == 512300 && doc.site()->elevation == 100 &&
                      doc.site()->crs == "EPSG:32633" && lastLabel(doc) == "Set site origin",
                  "Set origin is one site.set edit");
            check(child<QFrame>(dialog, "siteOrigin")->isVisible() &&
                      child<QLabel>(dialog, "siteEasting")->text().contains("512") &&
                      child<QLabel>(dialog, "siteCrs")->text() == "EPSG:32633",
                  "The dialog shows the new origin read-only");
            check(window.viewport()->siteOriginMarkerShown(),
                  "The origin marker shows while Site settings is open");
            for (const char *name : {"siteEasting", "siteNorthing", "siteElevation", "siteCrs",
                                     "siteSource", "siteSha256"})
                check(!child<QLabel>(dialog, name)->accessibleName().isEmpty(),
                      std::string(name) + " has an accessible name");
            click(dialog, "siteSettingsClose");
        },
        [&] { action(window, "file.siteSettings")->trigger(); });
    check(opened, "File > Site settings… opens the dialog");
    check(!window.viewport()->siteOriginMarkerShown(), "The marker hides when the dialog closes");
    // With provenance (an import's), for the screenshot.
    fresh(window);
    gridTerrain(window, 10, 10);
    setSite(window, 512300, 5234500, 0);
    drive(
        "siteSettingsDialog",
        [&](QWidget &dialog) {
            check(child<QLabel>(dialog, "siteSource")->text() == "hillside.dxf" &&
                      child<QLabel>(dialog, "siteSha256")->toolTip() == QString(64, 'b') &&
                      child<QLabel>(dialog, "siteSha256")->text().remove(' ').remove('\n') ==
                          QString(64, 'b'),
                  "Provenance: source and SHA-256");
            {
                auto *sha = child<QLabel>(dialog, "siteSha256");
                check(sha->height() >= sha->sizeHint().height(), "The digest is not clipped");
            }
            shot(dialog, "site-settings-origin");
            click(dialog, "siteSettingsClose");
        },
        [&] { action(window, "file.siteSettings")->trigger(); });
}
// Opens Site settings > Re-anchor… and runs `body` in the Re-anchor dialog.
void withReanchor(Window &window, const std::function<void(SiteOriginDialog &)> &body) {
    bool reached = false;
    drive(
        "siteSettingsDialog",
        [&](QWidget &dialog) {
            reached = drive(
                "siteReanchorDialog",
                [&](QWidget &reanchor) { body(static_cast<SiteOriginDialog &>(reanchor)); },
                [&] { click(dialog, "siteReanchor"); });
            click(dialog, "siteSettingsClose");
        },
        [&] { action(window, "file.siteSettings")->trigger(); });
    check(reached, "Re-anchor… opens its dialog");
}
void reanchor(Window &window) {
    fresh(window);
    auto &doc = window.document();
    const auto terrain = gridTerrain(window, 10, 10);
    const auto wire = doc.addWire(0, {1, 2, 3}, {4, 2, 3});
    setSite(window, 512300, 5234500, 100);
    const auto tile = terrainTiles(doc, terrain).front();
    const auto vertex = doc.bodies().at(tile)->surface.vertices.begin()->second;
    auto siteOf = [&](Id body, Vec3 local) {
        return modelToSite(*doc.site(), doc.worldTransform(body).point(local));
    };
    const auto before = siteOf(tile, vertex);
    const auto wireBefore = siteOf(wire, {1, 2, 3});
    const auto revision = doc.revision();
    withReanchor(window, [&](SiteOriginDialog &dialog) {
        check(!dialog.canApply() && !child<QPushButton>(dialog, "siteOriginApply")->isEnabled(),
              "Re-anchor waits for a different origin");
        check(child<QLabel>(dialog, "siteOriginExplanation")->text().contains("site coordinate"),
              "The dialog explains that site coordinates stay the same");
        dialog.easting()->setValue(512400);
        dialog.elevation()->setValue(90);
        QTest::qWait(300);
        check(dialog.canApply(), "A different origin can be applied");
        check(child<QLabel>(dialog, "siteReanchorDelta")->text().contains("100"),
              "The move is shown: " +
                  child<QLabel>(dialog, "siteReanchorDelta")->text().toStdString());
        shot(dialog, "site-reanchor");
        click(dialog, "siteOriginApply");
    });
    check(doc.revision() == revision + 1 && lastLabel(doc) == "Re-anchor site origin",
          "Re-anchor is one edit");
    check(doc.site()->easting == 512400 && doc.site()->elevation == 90, "The new origin");
    const auto after = siteOf(tile, vertex), wireAfter = siteOf(wire, {1, 2, 3});
    check(length(after - before) < 1e-9 && length(wireAfter - wireBefore) < 1e-9,
          "Site coordinates are unchanged");
    check(std::abs(doc.worldTransform(wire).point({1, 2, 3}).x - (1 - 100)) < 1e-9,
          "Model coordinates moved by the difference");
    action(window, "edit.undo")->trigger();
    check(doc.site()->easting == 512300 && doc.revision() != revision + 1,
          "Undo restores the origin");
    // A locked top-level body refuses, before anything is applied.
    auto locked = std::make_shared<Body>(*doc.bodies().at(wire));
    locked->name = "Fence";
    locked->locked = true;
    doc.apply({"Lock", {{wire, doc.bodies().at(wire), locked}}}, doc.revision());
    changed(window);
    const auto lockedRevision = doc.revision();
    withReanchor(window, [&](SiteOriginDialog &dialog) {
        dialog.easting()->setValue(512400);
        QTest::qWait(300);
        check(!dialog.canApply() && dialog.problem().contains("“Fence” is locked") &&
                  child<QFrame>(dialog, "terrainDialogBanner")->isVisible() &&
                  !child<QPushButton>(dialog, "siteOriginApply")->isEnabled(),
              "A locked top-level body refuses re-anchoring: " + dialog.problem().toStdString());
        shot(dialog, "site-reanchor-locked");
        QTest::keyClick(&dialog, Qt::Key_Return); // The disabled default does nothing.
        QTest::qWait(20);
        check(dialog.isVisible(), "Enter does not apply a refused re-anchor");
        click(dialog, "siteOriginCancel");
    });
    check(doc.revision() == lockedRevision, "Nothing changed");
    // The coordinate limit refuses too.
    auto unlocked = std::make_shared<Body>(*doc.bodies().at(wire));
    unlocked->locked = false;
    doc.apply({"Unlock", {{wire, doc.bodies().at(wire), unlocked}}}, doc.revision());
    changed(window);
    const auto limitRevision = doc.revision();
    withReanchor(window, [&](SiteOriginDialog &dialog) {
        dialog.easting()->setValue(512300 + 2.5e6);
        QTest::qWait(300);
        check(!dialog.canApply() && dialog.problem().startsWith("Re-anchor is not available") &&
                  child<QFrame>(dialog, "terrainDialogBanner")->isVisible(),
              "The coordinate limit refuses re-anchoring: " + dialog.problem().toStdString());
        click(dialog, "siteOriginCancel");
    });
    check(doc.revision() == limitRevision, "Nothing changed at the limit");
}

// ---- Measurements: @E, N[, Z] ----------------------------------------------------------------
void measurementsSite(Window &window) {
    fresh(window);
    auto &doc = window.document();
    auto *box = child<QLineEdit>(window, "measurements");
    auto *status = child<QLabel>(window, "statusMessage");
    auto enter = [&](const QString &text) {
        box->setFocus();
        box->setText(text);
        QTest::keyClick(box, Qt::Key_Return);
        QTest::qWait(10);
    };
    window.viewport()->setTool(Viewport::Tool::Line);
    const auto revision = doc.revision();
    enter("@512310m,5234510m,100m");
    check(doc.revision() == revision && status->text().contains("no site origin"),
          "Without a site origin @ is refused: " + status->text().toStdString());
    setSite(window, 512300, 5234500, 100);
    window.viewport()->setTool(Viewport::Tool::Line);
    const auto siteRevision = doc.revision();
    enter("@512310m,5234510m,100m");
    const auto first = status->text();
    enter("@512315m, 5234510m"); // Z from the drawing plane (the ground, model z = 0).
 check(doc.revision() != siteRevision, "Two site positions draw a line: " + first.toStdString() +
                                          " / " + status->text().toStdString());
    bool found = false;
    for (const auto &[id, body] : doc.bodies())
        for (const auto &[edge, record] : body->topology.edges) {
            const auto world = doc.worldTransform(id);
            const auto a = world.point(body->surface.vertices.at(record.a)),
                       b = world.point(body->surface.vertices.at(record.b));
            found |= (length(a - Vec3{10, 10, 0}) < 1e-9 && length(b - Vec3{15, 10, 0}) < 1e-9) ||
                     (length(b - Vec3{10, 10, 0}) < 1e-9 && length(a - Vec3{15, 10, 0}) < 1e-9);
        }
    if (!found)
        for (const auto &[id, body] : doc.bodies())
            for (const auto &[edge, record] : body->topology.edges) {
                const auto world = doc.worldTransform(id);
                const auto a = world.point(body->surface.vertices.at(record.a)),
                           b = world.point(body->surface.vertices.at(record.b));
                std::cerr << "edge " << a.x << ',' << a.y << ',' << a.z << " - " << b.x << ','
                          << b.y << ',' << b.z << '\n';
            }
    check(found, "The line runs from model (10, 10, 0) to (15, 10, 0)");
    window.viewport()->cancel();
    window.viewport()->setTool(Viewport::Tool::Select);
    const auto selectRevision = doc.revision();
    enter("@512310m,5234510m");
    check(doc.revision() == selectRevision && status->text().contains("tool that takes a point"),
          "A tool without points refuses @: " + status->text().toStdString());
    // Beyond the coordinate limit.
    window.viewport()->setTool(Viewport::Tool::Line);
    enter("@9512310m,5234510m");
    check(doc.revision() == selectRevision, "A site position beyond the limit is refused");
    window.viewport()->cancel();
    window.viewport()->setTool(Viewport::Tool::Select);
}

// ---- Entity info: site coordinates -----------------------------------------------------------
void entityInfoSite(Window &window) {
    fresh(window);
    auto &doc = window.document();
    const auto wire = doc.addWire(0, {1, 2, 3}, {4, 2, 3});
    changed(window);
    const auto edge = doc.bodies().at(wire)->topology.edges.begin()->first;
    window.viewport()->selectEntities({{wire, SelectionKind::Edge, edge}});
    auto &panel = infoPanel(window);
    auto *site = child<QLabel>(panel, "entityInfoSite");
    check(!site->isVisibleTo(&panel), "No site row without a site origin");
    setSite(window, 512300, 5234500, 100);
    window.viewport()->selectEntities({{wire, SelectionKind::Edge, edge}});
    panel.refresh();
    check(site->isVisibleTo(&panel) && site->accessibleName() == "Site coordinates",
          "The site row shows with a site origin");
    const auto text = site->text();
    check(text.contains("Start E") && text.contains("End E") && text.contains("model"),
          "An edge shows both endpoints in site and model coordinates: " + text.toStdString());
    const auto units = doc.displayUnits();
    const auto precision = doc.displayPrecision();
    check(text.contains("E " + displayLength(512301, units, precision)) &&
              text.contains("N " + displayLength(5234502, units, precision)) &&
              text.contains("Z " + displayLength(103, units, precision)),
          "Site values: " + text.toStdString());
    window.viewport()->setSelection(wire);
    panel.refresh();
    check(site->isVisibleTo(&panel) && site->text().startsWith("E ") &&
              !site->text().contains("Start"),
          "A body shows its origin in site coordinates");
    shot(panel, "entity-info-site");
}

// ---- View > Site origin ----------------------------------------------------------------------
void siteOriginToggle(Window &window) {
    fresh(window);
    auto *toggle = action(window, "view.siteOrigin");
    auto *view = window.viewport();
    check(toggle->isCheckable() && !toggle->isChecked(), "View > Site origin starts unchecked");
    const auto terrain = gridTerrain(window, 10, 10);
    setSite(window, 512300, 5234500, 0);
    select(window, 0);
    check(!view->siteOriginMarkerShown(), "Unchecked: hidden without a terrain selected");
    select(window, terrain);
    check(view->siteOriginMarkerShown(), "Shown while a terrain is selected");
    select(window, 0);
    toggle->trigger();
    check(toggle->isChecked() && view->siteOriginVisible() && view->siteOriginMarkerShown() &&
              QSettings("SketchyUp", "SketchyUp").value("siteOriginVisible").toBool(),
          "Checked: always shown, and the choice is saved");
    const auto &doc = window.document();
    check(view->siteOriginMarkerLabel() ==
              "Site origin · E " +
                  displayLength(512300, doc.displayUnits(), doc.displayPrecision()) + " · N " +
                  displayLength(5234500, doc.displayUnits(), doc.displayPrecision()) + " · Z " +
                  displayLength(0, doc.displayUnits(), doc.displayPrecision()),
          "The marker is labelled with the origin: " + view->siteOriginMarkerLabel().toStdString());
    {
        // A new window reads the saved choice.
        Window second;
        check(second.findChild<QAction *>("view.siteOrigin")->isChecked() &&
                  second.viewport()->siteOriginVisible(),
              "The toggle state is persisted");
    }
    toggle->trigger();
    check(!toggle->isChecked() && !view->siteOriginMarkerShown() &&
              !QSettings("SketchyUp", "SketchyUp").value("siteOriginVisible").toBool(),
          "Unchecked again: hidden and saved");
    fresh(window);
    toggle->trigger();
    check(!view->siteOriginMarkerShown(), "Never shown without a site origin");
    toggle->trigger();
}
} // namespace

int main(int argc, char **argv) {
    QTemporaryDir preferences;
    qputenv("XDG_CONFIG_HOME", preferences.path().toUtf8());
    QApplication app(argc, argv);
    for (int i = 1; i + 1 < argc; ++i) {
        if (QString(argv[i]) == "--screenshots")
            shots = argv[i + 1];
        if (QString(argv[i]) == "--only")
            only = argv[i + 1];
        if (QString(argv[i]) == "--text-percent")
            QSettings("SketchyUp", "SketchyUp")
                .setValue("interfaceTextPercent", QString(argv[i + 1]).toInt());
    }
    Window window;
    window.resize(1280, 860);
    window.show();
    if (!QTest::qWaitForWindowExposed(&window, 5000))
        return 2;
    try {
        if (only.isEmpty() || only == "displayControls") {
            displayControls(window);
            std::cout << "display controls\n" << std::flush;
        }
        if (only.isEmpty() || only == "viewportDisplay") {
            viewportDisplay(window);
            std::cout << "viewport display\n" << std::flush;
        }
        if (only.isEmpty() || only == "siteSettings") {
            siteSettings(window);
            std::cout << "site settings\n" << std::flush;
        }
        if (only.isEmpty() || only == "reanchor") {
            reanchor(window);
            std::cout << "re-anchor\n" << std::flush;
        }
        if (only.isEmpty() || only == "measurementsSite") {
            measurementsSite(window);
            std::cout << "measurements @\n" << std::flush;
        }
        if (only.isEmpty() || only == "entityInfoSite") {
            entityInfoSite(window);
            std::cout << "entity info site\n" << std::flush;
        }
        if (only.isEmpty() || only == "siteOriginToggle") {
            siteOriginToggle(window);
            std::cout << "view site origin\n" << std::flush;
        }
    } catch (const std::exception &error) {
        std::cerr << "FAIL: " << error.what() << '\n';
        return 1;
    }
    return 0;
}
