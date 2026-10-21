// R096.f1 (UX_DESIGN §15.1, §15.2, §15.7; ADR 0169 §5, §8, §9): the desktop terrain tools,
// driven by input events as the other native suites drive the viewport.
// - The Terrain menu between Draw and Camera, its tools (Sculpt, Smooth; f2 adds the others) and
//   the selected terrain's commands; the rail's Terrain slot (the last terrain tool used, its own
//   group); the context menu on a terrain; Shift+S and the shortcut editor's conflict check;
//   `[` and `]`.
// - The tool options strip: switcher, fields with Measurements-box syntax, accessible names,
//   Escape returning the keyboard to the viewport, F6 between the strip and the box, and wrapping
//   to a second row when narrow.
// - The terrain preview path: a press-drag stroke draws the candidate in place of the tiles it
//   replaces; release commits one "Sculpt terrain" edit whose tiles equal the previewed candidate
//   bit for bit; Escape leaves revision, history, dirty state, encoding and the frame unchanged.
// - The one-spot brush (click, move up, click), the amend of a commit by a typed value, Ctrl and
//   Shift, region scope (a selected face, a clicked polygon), an empty brush, pads without dots.
// - The keyboard-only path: [x, y] and @E, N, then a value; region points typed.
// - Hover refusals: off a terrain and with no terrain in the model.
// `--screenshots DIR` saves the Terrain menu and a Sculpt stroke; `--text-percent N` runs
// everything at that interface text size.
#include "app/terrain_tool_strip.hpp"
#include "app/window.hpp"
#include "automation/commands.hpp"
#include "automation/terrain_commands.hpp"
#include "core/terrain.hpp"
#include "core/terrain_edit_mesh.hpp"
#include "core/terrain_sculpt.hpp"
#include "core/terrain_source_edit.hpp"
#include "core/terrain_sources.hpp"
#include "core/terrain_stamp.hpp"
#include "io/document_io.hpp"
#include <QAccessible>
#include <QAction>
#include <QApplication>
#include <QComboBox>
#include <QContextMenuEvent>
#include <QDialog>
#include <QJsonArray>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QMenuBar>
#include <QMouseEvent>
#include <QPainter>
#include <QPushButton>
#include <QSettings>
#include <QSignalSpy>
#include <QSpinBox>
#include <QTemporaryDir>
#include <QTest>
#include <QTimer>
#include <QToolBar>
#include <QToolButton>
#include <QWidgetAction>
#include <cmath>
#include <iostream>
#include <numbers>
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
QAction *action(Window &window, const char *id) {
    auto *found = window.findChild<QAction *>(id);
    check(found != nullptr, std::string("Window has action ") + id);
    return found;
}
void shot(QWidget &widget, const QString &name, QWidget *popup = nullptr) {
    if (shots.isEmpty())
        return;
    QTest::qWait(40);
    auto image = widget.grab().toImage();
    // The viewport's own frame, with its painter overlays (brush, label, outline), over the grab.
    if (auto *window = qobject_cast<Window *>(&widget); window && window->viewport()->rendererReady()) {
        auto *view = window->viewport();
        QPainter painter(&image);
        const auto ratio = image.devicePixelRatio();
        painter.scale(1 / ratio, 1 / ratio);
        painter.drawImage(view->mapTo(window, QPoint()) * ratio, view->grabFramebuffer());
        for (auto *overlay : view->findChildren<QWidget *>(Qt::FindDirectChildrenOnly))
            if (overlay->isVisible())
                painter.drawPixmap(overlay->mapTo(window, QPoint()) * ratio, overlay->grab());
    }
    if (popup && popup->isVisible()) {
        // A popup is its own window: paint it where it is over the window's image.
        QPainter painter(&image);
        const auto ratio = image.devicePixelRatio();
        painter.scale(1 / ratio, 1 / ratio);
        painter.drawPixmap(widget.mapFromGlobal(popup->mapToGlobal(QPoint())) * ratio,
                           popup->grab());
    }
    image.save(shots + "/" + name + ".png");
}
// Runs `trigger`; when a visible top-level widget named `name` appears, runs `body` on it once.
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
                    QTest::qWait(20);
                    body(*widget);
                } catch (...) {
                    failure = std::current_exception();
                }
                if (auto *menu = qobject_cast<QMenu *>(widget))
                    menu->close();
                else if (auto *dialog = qobject_cast<QDialog *>(widget); dialog && dialog->isVisible())
                    dialog->reject();
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

// ---- Fixtures ------------------------------------------------------------------------------
void changed(Window &window) {
    window.viewport()->refresh();
    emit window.viewport()->changed();
    QTest::qWait(5);
}
void fresh(Window &window) {
    window.viewport()->setTool(Viewport::Tool::Select);
    window.viewport()->cancel();
    window.document() = Document();
    window.viewport()->setSelection(0);
    changed(window);
}
// A gridded hill: 1 m points over n x n metres, a dome `height` m high in the middle (several
// tiles).
Id hill(Window &window, int n = 61, double height = 4) {
    QByteArray file;
    const double c = (n - 1) / 2.0;
    for (int j = 0; j < n; ++j)
        for (int i = 0; i < n; ++i) {
            const double z =
                50 + height * std::exp(-((i - c) * (i - c) + (j - c) * (j - c)) / 300.0) + 0.002 * i;
            file += QByteArray::number(i) + ' ' + QByteArray::number(j) + ' ' +
                    QByteArray::number(z, 'f', 4) + '\n';
        }
    const QJsonObject options{{"format", "points"}, {"units", "m"}, {"columns", "XYZ"},
                              {"delimiter", "whitespace"}, {"header", false}, {"name", "Hill"}};
    const auto terrain =
        importTerrainSource(window.document(), file, options)["terrain"].toString().toULongLong();
    changed(window);
    auto *view = window.viewport();
    view->standardView(1); // top
    view->fit();
    QTest::qWait(30);
    return terrain;
}
// R096.f2: a hill of closed elliptic contour rings 3 m apart, 0.5 m lower each (ring k: radius
// 10 + 3 k along y, 1.3 times that along x, at 105 - 0.5 k m), as terrain_sculpt_tests builds it.
Id rings(Window &window, int count = 8, int vertices = 48) {
    QByteArray out = "0\nSECTION\n2\nHEADER\n9\n$ACADVER\n1\nAC1015\n9\n$INSUNITS\n70\n6\n0\n"
                     "ENDSEC\n0\nSECTION\n2\nENTITIES\n";
    for (int k = 0; k < count; ++k) {
        const double r = 10 + 3 * k, z = 105 - 0.5 * k;
        out += "0\nLWPOLYLINE\n5\n" + QByteArray::number(0x100 + k, 16).toUpper() +
               "\n8\nCONTOURS\n90\n" + QByteArray::number(vertices) + "\n70\n1\n38\n" +
               QByteArray::number(z, 'f', 4) + '\n';
        for (int i = 0; i < vertices; ++i) {
            const double t = 2 * std::numbers::pi * i / vertices + 0.05 * k;
            out += "10\n" + QByteArray::number(r * std::cos(t) * 1.3, 'f', 4) + "\n20\n" +
                   QByteArray::number(r * std::sin(t), 'f', 4) + '\n';
        }
    }
    out += "0\nENDSEC\n0\nEOF\n";
    const QJsonObject options{{"format", "dxf"}, {"units", "header"}, {"name", "Rings"}};
    const auto terrain =
        importTerrainSource(window.document(), out, options)["terrain"].toString().toULongLong();
    changed(window);
    auto *view = window.viewport();
    view->standardView(1); // top
    view->fit();
    QTest::qWait(30);
    return terrain;
}
// The current source of a ring (k from the top), in model coordinates.
const TerrainSource &ringSource(const Document &doc, Id terrain, int k) {
    const auto state = terrainSourceState(*doc.bodies().at(terrain)->terrain);
    static std::shared_ptr<const TerrainSourceState> held;
    held = state;
    for (const auto *source : state->sources())
        if (source->role == TerrainSourceRole::Contour && !source->points.empty() &&
            source->points.front().z == 105 - 0.5 * k)
            return *source;
    throw std::runtime_error("no ring " + std::to_string(k));
}
std::optional<double> ground(const Document &doc, Id terrain, double x, double y) {
    const auto found = terrainElevationAt(doc, terrain, {{x, y}});
    if (found.empty() || !found.front().found)
        return std::nullopt;
    return found.front().elevation;
}
// The pointer over model (x, y) on the surface. The points are nudged off the 1 m grid, whose
// vertices and edges the pick ray would meet exactly.
QPoint screen(Window &window, Id terrain, double x, double y) {
    x += 0.137;
    y += 0.071;
    const auto z = ground(window.document(), terrain, x, y).value_or(50);
    return window.viewport()->project({x, y, z}).toPoint();
}
void mouse(Viewport *view, QEvent::Type type, QPoint point, Qt::MouseButton button,
           Qt::MouseButtons buttons, Qt::KeyboardModifiers modifiers = Qt::NoModifier) {
    QMouseEvent event(type, QPointF(point), view->mapToGlobal(QPointF(point)), button, buttons,
                      modifiers);
    QCoreApplication::sendEvent(view, &event);
    QCoreApplication::processEvents();
}
void hover(Viewport *view, QPoint point, Qt::KeyboardModifiers modifiers = Qt::NoModifier) {
    mouse(view, QEvent::MouseMove, point, Qt::NoButton, Qt::NoButton, modifiers);
}
void press(Viewport *view, QPoint point, Qt::KeyboardModifiers modifiers = Qt::NoModifier) {
    mouse(view, QEvent::MouseButtonPress, point, Qt::LeftButton, Qt::LeftButton, modifiers);
}
void drag(Viewport *view, QPoint point, Qt::KeyboardModifiers modifiers = Qt::NoModifier) {
    mouse(view, QEvent::MouseMove, point, Qt::NoButton, Qt::LeftButton, modifiers);
}
void release(Viewport *view, QPoint point, Qt::KeyboardModifiers modifiers = Qt::NoModifier) {
    mouse(view, QEvent::MouseButtonRelease, point, Qt::LeftButton, Qt::NoButton, modifiers);
}
void frame(Viewport *view) {
    view->update();
    QTest::qWait(30);
    if (view->rendererReady())
        (void)view->grabFramebuffer();
}
void key(QWidget *target, Qt::Key k, Qt::KeyboardModifiers modifiers = Qt::NoModifier,
         const QString &text = {}) {
    QKeyEvent pressed(QEvent::KeyPress, k, modifiers, text);
    QCoreApplication::sendEvent(target, &pressed);
    QKeyEvent released(QEvent::KeyRelease, k, modifiers, text);
    QCoreApplication::sendEvent(target, &released);
    QCoreApplication::processEvents();
}
void enter(Window &window, const QString &text) {
    auto *box = child<QLineEdit>(window, "measurements");
    box->setFocus();
    box->setText(text);
    QTest::keyClick(box, Qt::Key_Return);
    QTest::qWait(10);
}
QString status(Window &window) { return child<QLabel>(window, "statusMessage")->text(); }
QString step(Window &window) { return child<QLabel>(window, "measurementStep")->text(); }
const TerrainRecord &recordOf(const Document &doc, Id terrain) {
    return *doc.bodies().at(terrain)->terrain;
}
const TerrainSculptData &lastSculpt(const Document &doc, Id terrain) {
    return dynamic_cast<const TerrainSculptData &>(*recordOf(doc, terrain).edits.back()->data);
}
std::map<TerrainTileKey, std::shared_ptr<const TerrainTileContent>> tileContents(const Document &doc,
                                                                                 Id terrain) {
    std::map<TerrainTileKey, std::shared_ptr<const TerrainTileContent>> result;
    for (const auto id : terrainTiles(doc, terrain))
        result[doc.bodies().at(id)->tileKey] = terrainTileContent(*doc.bodies().at(id));
    return result;
}
// The candidate's tiles equal the document's tiles with their keys, bit for bit.
void checkCommitted(const Document &doc, Id terrain, const TerrainSculptPreview &candidate,
                    const std::string &when) {
    check(candidate.edit.has_value(), when + ": the candidate applies");
    const auto tiles = tileContents(doc, terrain);
    for (const auto &tile : candidate.edit->region.tiles)
        check(tiles.contains(tile->key) && tiles.at(tile->key)->sameMesh(*tile),
              when + ": the committed tiles equal the previewed candidate bit for bit");
}
TerrainToolStrip &strip(Window &window) {
    auto *found = window.viewport()->findChild<TerrainToolStrip *>("terrainToolStrip");
    check(found != nullptr, "the viewport has the terrain tool strip");
    return *found;
}
void startTool(Window &window, Viewport::Tool tool, TerrainToolOptions options = {}) {
    action(window, tool == Viewport::Tool::Sculpt ? "tool.29" : "tool.30")->trigger();
    window.viewport()->setTerrainOptions(options);
    QTest::qWait(10);
}

// ---- Screenshots (--screenshots DIR): the Terrain menu and a Sculpt stroke at 1x --------------
void screenshots(Window &window) {
    fresh(window);
    auto &doc = window.document();
    const auto terrain = hill(window, 81, 14);
    auto display = doc.bodies().at(terrain)->terrain->display;
    display.contours = true;
    display.contourInterval = 0.5;
    setTerrainDisplay(doc, terrain, display);
    changed(window);
    auto *view = window.viewport();
    view->standardView(0);
    view->fit();
    QTest::qWait(30);
    view->setSelection(terrain);
    changed(window);
    auto *menuBar = window.menuBar();
    auto *terrainMenu = child<QMenu>(window, "terrainMenu");
    drive(
        "terrainMenu", [&](QWidget &shown) { shot(window, "terrain-menu", &shown); },
        [&] {
            QAction *entry = nullptr;
            for (auto *candidate : menuBar->actions())
                if (candidate->text() == "&Terrain")
                    entry = candidate;
            terrainMenu->popup(menuBar->mapToGlobal(menuBar->actionGeometry(entry).bottomLeft()));
            QTest::qWait(300);
        });
    terrainMenu->close();
    view->setSelection(0);
    TerrainToolOptions options;
    options.radius = 9;
    options.amount = 1.8;
    startTool(window, Viewport::Tool::Sculpt, options);
    const auto a = screen(window, terrain, 30, 44), b = screen(window, terrain, 46, 38);
    hover(view, a);
    press(view, a);
    for (int i = 1; i <= 10; ++i)
        drag(view, a + (b - a) * i / 10);
    frame(view);
    shot(window, "sculpt");
    key(view, Qt::Key_Escape);
    release(view, b);
    view->setTool(Viewport::Tool::Select);
}

// ---- Terrain menu, rail slot, context menu, shortcuts ----------------------------------------
void menuAndRail(Window &window) {
    fresh(window);
    const auto terrain = hill(window);
    auto *view = window.viewport();
    // The Terrain menu between Draw and Camera.
    QStringList titles;
    for (auto *entry : window.menuBar()->actions())
        titles << entry->text();
    const auto draw = titles.indexOf("&Draw"), menu = titles.indexOf("&Terrain"),
               camera = titles.indexOf("&Camera");
    check(draw >= 0 && menu == draw + 1 && camera == menu + 1,
          "the Terrain menu sits between Draw and Camera: " + titles.join(", ").toStdString());
    auto *terrainMenu = child<QMenu>(window, "terrainMenu");
    QStringList entries;
    for (auto *entry : terrainMenu->actions())
        if (!entry->isSeparator())
            entries << entry->text();
    for (const auto *name : {"Sculpt", "Smooth", "Edit contours", "Terrain settings…",
                             "Regenerate terrain", "Reset edits…", "Explode terrain…",
                             "Create terrain from selection"})
        check(entries.contains(name), std::string("the Terrain menu has ") + name);
    for (const auto *name : {"Stamp pad", "Drape"})
        check(!entries.contains(name),
              std::string("tools not built yet are not shown (f2b): ") + name);
    check(action(window, "tool.29")->shortcut() == QKeySequence("Shift+S") &&
              action(window, "tool.30")->shortcut().isEmpty() &&
              action(window, "tool.31")->shortcut().isEmpty(),
          "only Sculpt has a default shortcut, Shift+S");
    // The shortcut editor's conflict check: Shift+S is Sculpt's alone where it can be live, and
    // no command uses a plain [ or ].
    ShortcutMap defaults;
    ShortcutScopes scopes;
    for (auto *entry : window.findChildren<QAction *>()) {
        if (entry->objectName().isEmpty() || !entry->property("shortcutScope").isValid())
            continue;
        defaults[entry->objectName()] = entry->shortcut();
        scopes[entry->objectName()] = ShortcutScope(entry->property("shortcutScope").toInt());
    }
    ShortcutBindings bindings(defaults, {}, {}, scopes);
    check(bindings.conflicts("tool.30", QKeySequence("Shift+S")) == QStringList{"tool.29"},
          "the shortcut editor names Sculpt as Shift+S's only holder");
    for (const auto &[id, sequence] : defaults.asKeyValueRange())
        check(sequence != QKeySequence("[") && sequence != QKeySequence("]"),
              "[ and ] have no other model-scope meaning");
    // Shift+S starts Sculpt.
    window.activateWindow();
    QTest::qWaitForWindowActive(&window, 2000);
    view->setFocus();
    QTest::keyClick(view, Qt::Key_S, Qt::ShiftModifier);
    QTest::qWait(10);
    check(view->tool() == Viewport::Tool::Sculpt, "Shift+S starts Sculpt");
    view->setTool(Viewport::Tool::Select);
    // The rail's Terrain slot: after Tape, its own group, the last terrain tool used.
    auto *rail = child<QToolBar>(window, "toolRail");
    auto *slot = action(window, "railTerrain");
    const auto railActions = rail->actions();
    const auto tape = railActions.indexOf(action(window, "tool.14")),
               terrainSlot = railActions.indexOf(slot);
    check(tape >= 0 && terrainSlot > tape, "the Terrain slot comes after Tape");
    check(!slot->actionGroup() && railActions[terrainSlot + 1]->isSeparator(),
          "the Terrain slot ends the tools before the camera group (its own group on the narrow "
          "rail; no extra separator on the wide one)");
    auto *button = rail->widgetForAction(slot);
    check(button && button->accessibleName() == "Terrain tools: Sculpt",
          "the slot has an accessible name");
    slot->trigger();
    QTest::qWait(10);
    check(view->tool() == Viewport::Tool::Sculpt && slot->isChecked() && slot->text() == "Sculpt ▸",
          "the slot starts Sculpt at first and shows it: " + slot->text().toStdString());
    QTest::mouseClick(strip(window).toolButton(int(Viewport::Tool::Smooth)), Qt::LeftButton);
    QTest::qWait(10);
    check(view->tool() == Viewport::Tool::Smooth && slot->text() == "Smooth ▸",
          "the strip switches to Smooth and the slot shows it");
    view->setTool(Viewport::Tool::Select);
    check(!slot->isChecked(), "Select leaves the slot unchecked");
    slot->trigger();
    QTest::qWait(10);
    check(view->tool() == Viewport::Tool::Smooth, "the slot starts the last terrain tool used");
    view->setTool(Viewport::Tool::Select);
    // The selected terrain's name heads its commands; Reset edits needs edits.
    view->setSelection(terrain);
    changed(window);
    // Qt's Wayland popups need a compositor input serial that synthetic input cannot supply (as
    // in terrain_desktop_tests and narrow_layout_input_tests): there the menus are checked by
    // their contents and the requests, not by opening them.
    const bool wayland = QGuiApplication::platformName() == "wayland";
    auto checkMenu = [&](QMenu &opened) {
        QStringList sections;
        for (const auto *name : {"terrainMenuToolsHeading", "terrainMenuTerrainHeading"})
            if (auto *label = opened.findChild<QLabel *>(name))
                sections << label->text();
        for (auto *entry : opened.actions())
            if (auto *widget = qobject_cast<QWidgetAction *>(entry))
                if (auto *label = qobject_cast<QLabel *>(widget->defaultWidget()))
                    if (!sections.contains(label->text()))
                        sections << label->text();
        check(sections.contains("Tools") && sections.contains("Hill"),
              "the menu heads the tools and the selected terrain's commands: " +
                  sections.join(", ").toStdString());
        check(!action(window, "terrain.resetEdits")->isEnabled() &&
                  action(window, "terrain.settings")->isEnabled(),
              "Reset edits is unavailable without edits");
    };
    if (wayland) {
        emit terrainMenu->aboutToShow();
        checkMenu(*terrainMenu);
    } else {
        check(drive(
                  "terrainMenu", [&](QWidget &shown) { checkMenu(*qobject_cast<QMenu *>(&shown)); },
                  [&] {
                      const auto at = window.menuBar()->actionGeometry(
                          window.menuBar()->actions()[menu]);
                      terrainMenu->popup(window.menuBar()->mapToGlobal(at.bottomLeft()));
                      QTest::qWait(200);
                  }),
              "the Terrain menu opens");
        terrainMenu->close();
    }
    // The context menu on a terrain: its tools, Terrain settings… and Explode terrain….
    view->setSelection(0);
    frame(view);
    const auto onTerrain = screen(window, terrain, 30, 30);
    if (wayland) {
        QSignalSpy requested(view, &Viewport::contextMenuRequested);
        QContextMenuEvent event(QContextMenuEvent::Keyboard, onTerrain, view->mapToGlobal(onTerrain));
        QApplication::sendEvent(view, &event);
        QTest::qWait(20);
        check(requested.count() == 1 && action(window, "tool.29")->isEnabled(),
              "the keyboard requests the context menu, the tools available");
        view->setSelection(terrain);
        changed(window);
    } else {
        check(drive(
                  "viewportContextMenu",
                  [&](QWidget &shown) {
                      QStringList items;
                      for (auto *entry : qobject_cast<QMenu *>(&shown)->actions())
                          if (!entry->isSeparator())
                              items << entry->text();
                      for (const auto *name : {"Sculpt", "Smooth", "Edit contours",
                                               "Terrain settings…", "Explode terrain…"})
                          check(items.contains(name),
                                std::string("the context menu offers ") + name + ": " +
                                    items.join(", ").toStdString());
                  },
                  [&] { emit view->contextMenuRequested(onTerrain); }),
              "the context menu opens on a terrain");
        check(view->selectedBody() == terrain, "right-clicking a terrain selects it");
    }
    // Reset edits… after a change: confirmation with Cancel the default, then one edit.
    startTool(window, Viewport::Tool::Sculpt, {});
    enter(window, "[30,30]");
    enter(window, "0.4");
    check(recordOf(window.document(), terrain).edits.size() == 1, "a sculpt was made");
    view->setTool(Viewport::Tool::Select);
    view->setSelection(terrain);
    changed(window);
    check(action(window, "terrain.resetEdits")->isEnabled(), "Reset edits is available with edits");
    const auto revision = window.document().revision();
    check(drive(
              "terrainResetEditsDialog",
              [&](QWidget &dialog) {
                  check(child<QLabel>(dialog, "terrainResetEditsSummary")->text() ==
                            "Reset 1 change to Hill? The terrain is rebuilt from its sources.",
                        "the confirmation names the count and the terrain");
                  check(child<QPushButton>(dialog, "terrainResetEditsCancel")->isDefault(),
                        "Cancel is the default");
                  QTest::mouseClick(child<QPushButton>(dialog, "terrainResetEditsCancel"),
                                    Qt::LeftButton);
              },
              [&] { action(window, "terrain.resetEdits")->trigger(); }),
          "Reset edits asks first");
    check(window.document().revision() == revision, "Cancel changes nothing");
    check(drive(
              "terrainResetEditsDialog",
              [&](QWidget &dialog) {
                  QTest::mouseClick(child<QPushButton>(dialog, "terrainResetEditsConfirm"),
                                    Qt::LeftButton);
              },
              [&] { action(window, "terrain.resetEdits")->trigger(); }),
          "Reset edits confirms");
    check(recordOf(window.document(), terrain).edits.empty() &&
              window.document().history().entries.back().label == "Reset terrain edits",
          "Reset edits is one edit");
}

// ---- The tool options strip ---------------------------------------------------------------
void optionsStrip(Window &window) {
    fresh(window);
    hill(window);
    auto *view = window.viewport();
    auto &bar = strip(window);
    check(!bar.isVisible(), "no strip without a terrain tool");
    startTool(window, Viewport::Tool::Sculpt);
    check(bar.isVisible() && bar.amountField()->isVisible() && !bar.strengthField()->isVisible() &&
              bar.radiusField()->isVisible() && !bar.widthField()->isVisible(),
          "Sculpt shows Radius, Falloff and Amount");
    // Accessible names on every control (UX §15.7).
    for (auto *control : bar.findChildren<QWidget *>())
        if (control->isVisibleTo(&bar) && control->focusPolicy() != Qt::NoFocus) {
            auto *accessible = QAccessible::queryAccessibleInterface(control);
            check(accessible && !accessible->text(QAccessible::Name).isEmpty(),
                  "every strip control has an accessible name: " +
                      control->objectName().toStdString());
        }
    check(bar.toolButton(int(Viewport::Tool::Sculpt))->isChecked(), "the switcher shows Sculpt");
    // Fields take the Measurements box's syntax and units.
    auto *radius = bar.radiusField();
    radius->setFocus();
    radius->selectAll();
    QTest::keyClicks(radius, "350cm");
    QTest::keyClick(radius, Qt::Key_Return);
    check(std::abs(view->terrainOptions().radius - 3.5) < 1e-12, "350cm sets the radius to 3.5 m");
    check(radius->text() == "3.5 m", "the field shows the value in document units: " +
                                         radius->text().toStdString());
    radius->selectAll();
    QTest::keyClicks(radius, "lots");
    QTest::keyClick(radius, Qt::Key_Return);
    check(radius->property("invalid").toBool() && !radius->accessibleDescription().isEmpty() &&
              view->terrainOptions().radius == 3.5,
          "an unparsable value is marked, says why and changes nothing");
    // Escape restores the field and returns the keyboard to the viewport.
    QTest::keyClick(radius, Qt::Key_Escape);
    check(view->hasFocus() && radius->text() == "3.5 m" && !radius->property("invalid").toBool(),
          "Escape restores the field and focuses the viewport");
    auto *amount = bar.amountField();
    amount->setFocus();
    amount->selectAll();
    QTest::keyClicks(amount, "12\"");
    QTest::keyClick(amount, Qt::Key_Return);
    check(std::abs(view->terrainOptions().amount - 0.3048) < 1e-12, "12\" sets the amount");
    // F6 moves between the strip and the box.
    view->setFocus();
    QTest::keyClick(&window, Qt::Key_F6);
    QTest::qWait(10);
    check(bar.isAncestorOf(QApplication::focusWidget()), "F6 from the viewport reaches the strip");
    QTest::keyClick(QApplication::focusWidget(), Qt::Key_F6);
    QTest::qWait(10);
    check(QApplication::focusWidget() == child<QLineEdit>(window, "measurements"),
          "F6 from the strip reaches the Measurements box");
    QTest::keyClick(QApplication::focusWidget(), Qt::Key_F6, Qt::ShiftModifier);
    QTest::qWait(10);
    check(bar.isAncestorOf(QApplication::focusWidget()), "Shift+F6 returns to the strip");
    view->setFocus();
    // Smooth: Strength and Passes.
    QTest::mouseClick(bar.toolButton(int(Viewport::Tool::Smooth)), Qt::LeftButton);
    QTest::qWait(10);
    check(bar.strengthField()->isVisible() && bar.passesField()->isVisible() &&
              !bar.amountField()->isVisible(),
          "Smooth shows Strength and Passes");
    auto *strength = bar.strengthField();
    strength->setFocus();
    strength->selectAll();
    QTest::keyClicks(strength, "2");
    QTest::keyClick(strength, Qt::Key_Return);
    check(strength->property("invalid").toBool() && view->terrainOptions().strength == 0.5,
          "a strength above 1 is refused");
    QTest::keyClick(strength, Qt::Key_Escape);
    // Region scope shows the falloff width instead of the radius.
    QTest::mouseClick(bar.scopeButton(TerrainToolOptions::Scope::Region), Qt::LeftButton);
    QTest::qWait(10);
    check(view->terrainOptions().scope == TerrainToolOptions::Scope::Region &&
              bar.widthField()->isVisible() && !bar.radiusField()->isVisible(),
          "Region scope shows Falloff width");
    check(step(window) == "Falloff width", "the box is labelled Falloff width before a region");
    QTest::mouseClick(bar.scopeButton(TerrainToolOptions::Scope::Brush), Qt::LeftButton);
    // One row where it fits, two where it does not.
    const auto size = window.size();
    auto heightAt = [&](int width) {
        window.resize(width, size.height());
        QTest::qWait(60);
        bar.place(0);
        return bar.height();
    };
    const int wide = heightAt(2200), narrow = heightAt(900);
    check(narrow > wide + 10 && bar.strengthField()->isVisibleTo(&bar),
          "the strip is one row where it fits and wraps where it does not (" +
              std::to_string(wide) + " → " + std::to_string(narrow) + " px)");
    window.resize(size);
    QTest::qWait(60);
    view->setTool(Viewport::Tool::Select);
    check(!bar.isVisible(), "Select hides the strip");
}

// ---- Stroke: preview, commit equality, Escape ----------------------------------------------
void strokePreviewAndCommit(Window &window) {
    fresh(window);
    auto &doc = window.document();
    const auto terrain = hill(window);
    auto *view = window.viewport();
    TerrainToolOptions options;
    options.radius = 4;
    options.amount = 0.8;
    startTool(window, Viewport::Tool::Sculpt, options);
    check(step(window) == "Radius", "the box is labelled Radius before a stroke");
    check(status(window).contains("Ctrl: lower") && status(window).contains("Shift: smooth") &&
              status(window).contains("[ ]: radius"),
          "the status bar lists the modifiers: " + status(window).toStdString());
    const auto a = screen(window, terrain, 22, 30), b = screen(window, terrain, 38, 31);
    hover(view, a);
    auto brush = view->terrainBrush();
    check(brush.terrain == terrain && brush.vertices > 10 && !brush.empty && !brush.previewShown,
          "hovering shows the vertices that will move, without a preview");
    check(brush.ringPoints == 72 && brush.outlinePoints == 0,
          "the brush ring lies on the surface; no outline without a preview");
    check(brush.label.startsWith("+0.80 m · radius 4.00 m") && brush.label.contains("vertices"),
          "the label gives the amount, radius, vertices and tiles: " + brush.label.toStdString());
    // Escape leaves nothing: revision, history, dirty state, encoding and the frame.
    const auto encoding = encodeDocument(doc);
    const auto revision = doc.revision();
    const auto entries = doc.history().total;
    const bool dirty = doc.dirty();
    frame(view);
    const auto before = view->rendererReady() ? view->grabFramebuffer() : QImage();
    press(view, a);
    for (int i = 1; i <= 8; ++i)
        drag(view, a + (b - a) * i / 8);
    frame(view);
    brush = view->terrainBrush();
    check(brush.stroking && brush.previewShown, "a drag paints a stroke with a preview");
    check(brush.ringPoints == 72 && brush.outlinePoints > 20,
          "the changed region is outlined on the surface");
    if (view->rendererReady())
        check(!view->terrainPreviewHiddenTiles().empty(),
              "the candidate is drawn in place of the tiles it replaces");
    check(doc.revision() == revision && encodeDocument(doc) == encoding,
          "the preview changes nothing");
    key(view, Qt::Key_Escape);
    release(view, b);
    check(doc.revision() == revision && doc.history().total == entries && doc.dirty() == dirty &&
              encodeDocument(doc) == encoding,
          "Escape leaves revision, history, dirty state and encoding unchanged");
    check(!view->terrainBrush().previewShown && view->terrainPreviewHiddenTiles().empty(),
          "Escape withdraws the candidate");
    hover(view, a);
    frame(view);
    if (view->rendererReady())
        check(view->grabFramebuffer() == before, "Escape restores the frame");
    // The stroke again, committed: the committed tiles are the previewed candidate.
    press(view, a);
    for (int i = 1; i <= 8; ++i)
        drag(view, a + (b - a) * i / 8);
    frame(view);
    const auto candidate = view->terrainPreviewShown();
    check(candidate != nullptr, "the stroke has a candidate");
    release(view, b);
    check(doc.revision() == revision + 1 && doc.history().total == entries + 1 &&
              doc.history().entries.back().label == "Sculpt terrain",
          "release commits one Sculpt terrain edit");
    checkCommitted(doc, terrain, *candidate, "stroke");
    const auto &data = lastSculpt(doc, terrain);
    check(data.path.size() >= 2 && data.amount == 0.8 && data.radius == 4,
          "the stroke is swept at the strip's amount");
    frame(view);
    check(view->terrainPreviewHiddenTiles().empty(), "after the commit the tiles draw themselves");
    check(step(window) == "Amount", "after a stroke the box is labelled Amount");
    // Undo restores the tiles.
    doc.undo();
    changed(window);
    check(doc.revision() == revision + 2 && recordOf(doc, terrain).edits.empty(),
          "Undo removes the stroke");
    // Escape with no operation leaves the tool.
    view->setFocus();
    key(view, Qt::Key_Escape);
    check(view->tool() == Viewport::Tool::Select, "Escape with nothing in progress leaves the tool");
}

// ---- One spot (Smoove) and amend ------------------------------------------------------------
void spotAndAmend(Window &window) {
    fresh(window);
    auto &doc = window.document();
    const auto terrain = hill(window);
    auto *view = window.viewport();
    TerrainToolOptions options;
    options.radius = 5;
    startTool(window, Viewport::Tool::Sculpt, options);
    const auto at = screen(window, terrain, 30, 30);
    hover(view, at);
    press(view, at);
    release(view, at);
    auto brush = view->terrainBrush();
    check(brush.anchored && step(window) == "Amount", "a click anchors the one-spot brush");
    check(status(window).contains("Move up or down"), "the status bar says how to set it");
    hover(view, at - QPoint(0, 40));
    brush = view->terrainBrush();
    check(brush.amount > 0 && brush.previewShown, "moving up raises, with a preview");
    // Keys the strip's controls leave unhandled never reach the viewport (UX §4.2): Enter in
    // Passes or on a switcher button does not commit the spot.
    {
        auto &bar = strip(window);
        const auto before = doc.revision();
        bar.toolButton(int(Viewport::Tool::Sculpt))->setFocus();
        QTest::keyClick(bar.toolButton(int(Viewport::Tool::Sculpt)), Qt::Key_Return);
        QTest::keyClick(bar.toolButton(int(Viewport::Tool::Sculpt)), Qt::Key_BracketRight);
        check(doc.revision() == before && view->terrainBrush().anchored &&
                  view->terrainOptions().radius == 5,
              "keys on the strip stay in the strip");
        view->setFocus();
    }
    const auto candidate = view->terrainPreviewShown();
    const double amount = brush.amount;
    const auto revision = doc.revision();
    press(view, at - QPoint(0, 40));
    release(view, at - QPoint(0, 40));
    check(doc.revision() == revision + 1 && lastSculpt(doc, terrain).amount == amount &&
              lastSculpt(doc, terrain).path.size() == 1,
          "a second click commits the one-spot amount");
    checkCommitted(doc, terrain, *candidate, "spot");
    check(view->terrainBrush().ringPoints == 72, "the ring stays on the committed surface");
    // Choosing the active tool again keeps the revision and the instruction.
    action(window, "tool.29")->trigger();
    check(!view->terrainAmendable().isEmpty() && !status(window).isEmpty(),
          "re-choosing Sculpt keeps the revision and the status bar's instruction");
    // A typed value amends the same entry (UX §4.3).
    const auto entries = doc.history().total;
    check(step(window) == "Amount" && !view->terrainAmendable().isEmpty(),
          "after a commit the box amends the amount");
    check(status(window).contains("Type a new amount to revise it"),
          "the status bar says a typed amount revises it");
    enter(window, "0.25");
    check(doc.history().total == entries && lastSculpt(doc, terrain).amount == 0.25 &&
              recordOf(doc, terrain).edits.size() == 1 &&
              doc.history().entries.back().label == "Sculpt terrain",
          "a typed amount revises the same History entry");
    enter(window, "-1.5");
    check(doc.history().total == entries && lastSculpt(doc, terrain).amount == -1.5,
          "a second typed amount revises it again");
    // A new stroke ends the revision.
    press(view, at);
    check(view->terrainAmendable().isEmpty(), "a new operation ends the revision");
    key(view, Qt::Key_Escape);
    release(view, at);
    // Smooth: a click smooths one spot and a typed strength revises it.
    startTool(window, Viewport::Tool::Smooth, options);
    const auto smoothRevision = doc.revision();
    hover(view, at);
    press(view, at);
    release(view, at);
    check(doc.revision() == smoothRevision + 1 &&
              recordOf(doc, terrain).edits.back()->kind == terrainSmoothKind,
          "a Smooth click smooths one spot");
    check(step(window) == "Strength", "after a smooth the box is labelled Strength");
    enter(window, "0.9");
    check(lastSculpt(doc, terrain).strength == 0.9 && doc.revision() == smoothRevision + 2,
          "a typed strength revises the smooth");
}

// ---- Modifiers and [ ] ---------------------------------------------------------------------
void modifiers(Window &window) {
    fresh(window);
    auto &doc = window.document();
    const auto terrain = hill(window);
    auto *view = window.viewport();
    TerrainToolOptions options;
    options.radius = 4;
    options.amount = 0.6;
    startTool(window, Viewport::Tool::Sculpt, options);
    const auto a = screen(window, terrain, 24, 26), b = screen(window, terrain, 34, 27);
    auto stroke = [&](Qt::KeyboardModifiers modifiers) {
        hover(view, a, modifiers);
        press(view, a, modifiers);
        for (int i = 1; i <= 6; ++i)
            drag(view, a + (b - a) * i / 6, modifiers);
        release(view, b, modifiers);
    };
    stroke(Qt::ControlModifier);
    check(lastSculpt(doc, terrain).amount == -0.6 &&
              recordOf(doc, terrain).edits.back()->kind == terrainSculptKind,
          "Ctrl lowers");
    stroke(Qt::ShiftModifier);
    check(recordOf(doc, terrain).edits.back()->kind == terrainSmoothKind &&
              lastSculpt(doc, terrain).strength == 0.5,
          "Shift smooths while held");
    // [ and ] change the radius by 10 %.
    view->setFocus();
    key(view, Qt::Key_BracketRight, Qt::NoModifier, "]");
    check(std::abs(view->terrainOptions().radius - 4.4) < 1e-12, "] enlarges the radius by 10 %");
    key(view, Qt::Key_BracketLeft, Qt::NoModifier, "[");
    check(std::abs(view->terrainOptions().radius - 4) < 1e-12, "[ shrinks it back");
    check(!child<QLineEdit>(window, "measurements")->hasFocus(),
          "[ does not open the Measurements box in a terrain tool");
}

// ---- Region scope ---------------------------------------------------------------------------
void regionScope(Window &window) {
    fresh(window);
    auto &doc = window.document();
    const auto terrain = hill(window);
    auto *view = window.viewport();
    // A selected face (above the terrain) is the region.
    const auto face = doc.addFace({{{26, 26, 60}, {34, 26, 60}, {34, 34, 60}, {26, 34, 60}}});
    changed(window);
    const auto faceId = doc.bodies().at(face)->surface.faces.begin()->first;
    view->setSelection(face, faceId);
    TerrainToolOptions options;
    options.scope = TerrainToolOptions::Scope::Region;
    options.falloffWidth = 2;
    options.amount = 0.5;
    action(window, "tool.29")->trigger();
    view->setTerrainOptions(options);
    QTest::qWait(10);
    auto brush = view->terrainBrush();
    check(brush.regionClosed && brush.region.size() == 4 && brush.previewShown,
          "a selected face is the region, previewed");
    check(step(window) == "Amount", "a closed region waits for its amount");
    const auto revision = doc.revision();
    view->setFocus();
    key(view, Qt::Key_Return);
    const auto &data = lastSculpt(doc, terrain);
    check(doc.revision() == revision + 1 && !data.brush() && data.region.size() == 4 &&
              data.width == 2 && data.amount == 0.5,
          "Enter applies the region with its falloff width");
    // A clicked polygon, closed on its first point.
    view->setSelection(0);
    view->setTool(Viewport::Tool::Select);
    action(window, "tool.29")->trigger();
    QTest::qWait(10);
    const std::vector<QPoint> corners{screen(window, terrain, 10, 10), screen(window, terrain, 18, 10),
                                      screen(window, terrain, 18, 18)};
    for (const auto &corner : corners) {
        hover(view, corner);
        press(view, corner);
        release(view, corner);
    }
    check(view->terrainBrush().region.size() == 3 && !view->terrainBrush().regionClosed,
          "clicks add points");
    check(step(window) == "Point", "while drawing the box takes points");
    hover(view, corners.front() + QPoint(2, 1));
    press(view, corners.front() + QPoint(2, 1));
    release(view, corners.front() + QPoint(2, 1));
    check(view->terrainBrush().regionClosed && view->terrainBrush().previewShown,
          "clicking the first point closes the region");
    shot(window, "sculpt-region");
    const auto before = doc.revision();
    press(view, screen(window, terrain, 14, 13));
    release(view, screen(window, terrain, 14, 13));
    check(doc.revision() == before + 1 && lastSculpt(doc, terrain).region.size() == 3,
          "a click on the terrain applies the region");
    view->setTool(Viewport::Tool::Select);
    options.scope = TerrainToolOptions::Scope::Brush;
    view->setTerrainOptions(options);
}

// ---- Keyboard-only path (UX §15.7) ------------------------------------------------------------
void keyboardPath(Window &window) {
    fresh(window);
    auto &doc = window.document();
    const auto terrain = hill(window);
    auto *view = window.viewport();
    startTool(window, Viewport::Tool::Sculpt, {});
    const auto toLocal = doc.worldTransform(terrain).inverse();
    enter(window, "[30,30]");
    check(view->terrainBrush().anchored && step(window) == "Amount",
          "a typed position places the brush and asks for the amount");
    enter(window, "0.6");
    auto data = lastSculpt(doc, terrain);
    const auto local = toLocal.point({30, 30, 0});
    check(data.path.size() == 1 && data.path[0][0] == local.x + 0.0 &&
              data.path[0][1] == local.y + 0.0 && data.amount == 0.6,
          "Enter commits a one-spot brush at the typed position");
    // Site coordinates.
    if (!doc.site()) {
        setSiteOrigin(doc, SiteOrigin{512000, 5234000, 0, "EPSG:32633",
                                      {"hill.xyz", std::string(64, 'a')}});
        changed(window);
    }
    const auto site = *doc.site();
    view->setTool(Viewport::Tool::Select);
    startTool(window, Viewport::Tool::Sculpt, {});
    enter(window, QString("@%1,%2")
                      .arg(QString::number(site.easting + 25, 'f', 6),
                           QString::number(site.northing + 32, 'f', 6)));
    check(view->terrainBrush().anchored && view->terrainBrush().point &&
              std::abs(view->terrainBrush().point->x - 25) < 1e-9 &&
              std::abs(view->terrainBrush().point->y - 32) < 1e-9,
          "@E, N places the brush in site coordinates");
    enter(window, "-0.2");
    check(lastSculpt(doc, terrain).amount == -0.2, "a typed amount lowers");
    // Smooth by keyboard.
    view->setTool(Viewport::Tool::Select);
    startTool(window, Viewport::Tool::Smooth, {});
    enter(window, "[31,29]");
    check(step(window) == "Strength", "Smooth asks for the strength");
    enter(window, "0.7");
    check(recordOf(doc, terrain).edits.back()->kind == terrainSmoothKind &&
              lastSculpt(doc, terrain).strength == 0.7,
          "Enter commits a one-spot smooth");
    // A region typed point by point.
    view->setTool(Viewport::Tool::Select);
    TerrainToolOptions region;
    region.scope = TerrainToolOptions::Scope::Region;
    startTool(window, Viewport::Tool::Sculpt, region);
    for (const auto *point : {"[20,20]", "[28,20]", "[28,28]", "[20,28]"})
        enter(window, point);
    check(view->terrainBrush().region.size() == 4 && !view->terrainBrush().regionClosed,
          "typed points draw the region");
    enter(window, "[20,20]");
    check(view->terrainBrush().regionClosed, "typing the first point closes it");
    enter(window, "0.3");
    check(lastSculpt(doc, terrain).region.size() == 4 && lastSculpt(doc, terrain).amount == 0.3,
          "the typed amount applies the region");
    // A position off the terrain is refused.
    view->setTool(Viewport::Tool::Select);
    startTool(window, Viewport::Tool::Sculpt, {});
    const auto revision = doc.revision();
    enter(window, "[500,500]");
    check(doc.revision() == revision && status(window).contains("not on a terrain"),
          "a position off the terrain is refused: " + status(window).toStdString());
    view->setTool(Viewport::Tool::Select);
}

// ---- An empty brush and pads -----------------------------------------------------------------
void emptyBrushAndPads(Window &window) {
    fresh(window);
    auto &doc = window.document();
    const auto terrain = hill(window);
    auto *view = window.viewport();
    TerrainToolOptions tiny;
    tiny.radius = 0.05;
    startTool(window, Viewport::Tool::Sculpt, tiny);
    hover(view, screen(window, terrain, 30.5, 30.5));
    auto brush = view->terrainBrush();
    check(brush.empty && brush.label.contains("No vertices under the brush · ] enlarges it"),
          "a brush that catches no vertex says so: " + brush.label.toStdString());
    shot(window, "sculpt-empty");
    view->setTool(Viewport::Tool::Select);
    // A pad: its vertices get no dots.
    QJsonArray polygon;
    for (const auto &[x, y] : std::vector<std::pair<double, double>>{{27, 27}, {33, 27}, {33, 33}, {27, 33}})
        polygon.append(QJsonArray{x, y});
    const QJsonObject stamp{{"command", "terrain.stamp_pad"},
                            {"terrain", QString::number(terrain)},
                            {"footprint", QJsonObject{{"polygon", polygon}}},
                            {"elevation", 54.0},
                            {"mode", "daylight"}};
    executeBatch(doc, {{"apiVersion", 1},
                       {"documentId", QString::fromStdString(doc.identity())},
                       {"expectedRevision", QString::number(doc.revision())},
                       {"commands", QJsonArray{stamp}}});
    changed(window);
    TerrainToolOptions wide;
    wide.radius = 6;
    // The moving vertices (terrain frame) avoid the pad's footprint.
    std::size_t inside = 0;
    view->setTool(Viewport::Tool::Select);
    startTool(window, Viewport::Tool::Sculpt, wide);
    hover(view, screen(window, terrain, 30, 30));
    press(view, screen(window, terrain, 30, 30));
    release(view, screen(window, terrain, 30, 30));
    hover(view, screen(window, terrain, 30, 30) - QPoint(0, 30));
    const auto spot = view->terrainPreviewShown();
    check(spot != nullptr, "the spot over the pad previews");
    const auto frameToModel = doc.worldTransform(terrain);
    for (const auto &[p, w] : spot->moving) {
        const auto m = frameToModel.point(p);
        inside += m.x > 27 && m.x < 33 && m.y > 27 && m.y < 33;
    }
    check(inside == 0 && spot->plan.protectedVertices > 0,
          "pads are protected: no vertex on the pad moves or gets a dot");
    key(view, Qt::Key_Escape);
    view->setTool(Viewport::Tool::Select);
}

// ---- Hover refusals ----------------------------------------------------------------------------
void hoverRefusals(Window &window) {
    fresh(window);
    auto *view = window.viewport();
    startTool(window, Viewport::Tool::Sculpt);
    check(status(window) == "No terrain in this model. Import one with File → Import terrain…",
          "with no terrain the status bar says how to get one: " + status(window).toStdString());
    hover(view, {200, 200});
    check(view->cursor().shape() == Qt::ForbiddenCursor, "the cursor is not-allowed");
    view->setTool(Viewport::Tool::Select);
    const auto terrain = hill(window);
    auto &doc = window.document();
    doc.addFace({{{28, 28, 70}, {32, 28, 70}, {32, 32, 70}, {28, 32, 70}}});
    changed(window);
    startTool(window, Viewport::Tool::Smooth);
    hover(view, {4, 4});
    check(view->cursor().shape() == Qt::ForbiddenCursor &&
              status(window) == "Point at a terrain to smooth it",
          "off a terrain the cursor is not-allowed and the status names what to point at: " +
              status(window).toStdString());
    hover(view, screen(window, terrain, 30, 30)); // over the face above the terrain
    check(view->cursor().shape() == Qt::ForbiddenCursor && !view->terrainBrush().point,
          "over other geometry the tool refuses");
    hover(view, screen(window, terrain, 10, 12));
    check(view->cursor().shape() != Qt::ForbiddenCursor && status(window).startsWith("Smooth ·"),
          "over the terrain the tool's instruction returns");
    // R095.e1's refusal of a modelling tool still applies.
    action(window, "tool.3")->trigger();
    hover(view, {4, 4});
    hover(view, screen(window, terrain, 10, 12));
    check(view->refusedTerrain() == terrain, "Push/pull still refuses a tile");
    view->setTool(Viewport::Tool::Select);
}
// ---- Review regressions (#391) ---------------------------------------------------------------
void reviewRegressions(Window &window) {
    fresh(window);
    auto &doc = window.document();
    const auto terrain = hill(window);
    auto *view = window.viewport();
    TerrainToolOptions options;
    options.radius = 5;
    options.amount = 0.6;
    startTool(window, Viewport::Tool::Sculpt, options);
    const auto at = screen(window, terrain, 30, 30);
    // 1. A one-spot brush commits what it previewed: Shift pressed while anchored changes nothing
    // (the mode is the one it was anchored with).
    hover(view, at);
    press(view, at);
    release(view, at);
    hover(view, at - QPoint(0, 40));
    auto candidate = view->terrainPreviewShown();
    check(candidate && candidate->kind == terrainSculptKind, "the spot previews a raise");
    const double previewed = candidate->data->amount;
    view->setFocus();
    key(view, Qt::Key_Shift, Qt::ShiftModifier);
    hover(view, at - QPoint(0, 40), Qt::ShiftModifier);
    check(view->terrainPreviewShown() == candidate || !view->terrainBrush().smoothing,
          "Shift does not turn an anchored spot into a smooth");
    candidate = view->terrainPreviewShown();
    QKeyEvent enterKey(QEvent::KeyPress, Qt::Key_Return, Qt::ShiftModifier);
    QCoreApplication::sendEvent(view, &enterKey);
    QCoreApplication::processEvents();
    check(recordOf(doc, terrain).edits.back()->kind == terrainSculptKind &&
              lastSculpt(doc, terrain).amount == previewed,
          "Enter commits the previewed raise, not a smooth");
    checkCommitted(doc, terrain, *candidate, "spot across Shift");
    // ... and a stroke commits its last previewed command, whatever the release's modifiers.
    const auto a = screen(window, terrain, 24, 26), b = screen(window, terrain, 34, 27);
    hover(view, a);
    press(view, a);
    for (int i = 1; i <= 6; ++i)
        drag(view, a + (b - a) * i / 6);
    candidate = view->terrainPreviewShown();
    check(candidate && candidate->kind == terrainSculptKind, "the stroke previews a raise");
    release(view, b, Qt::ControlModifier | Qt::ShiftModifier);
    check(recordOf(doc, terrain).edits.back()->kind == terrainSculptKind &&
              lastSculpt(doc, terrain).amount == candidate->data->amount &&
              lastSculpt(doc, terrain).path.size() == candidate->data->path.size(),
          "a release with modifiers commits the previewed stroke");
    checkCommitted(doc, terrain, *candidate, "stroke released with modifiers");
    // 2. The tiles a preview hides leave the translucent pass and the sun shadows, and the
    // candidate casts shadows in their place.
    if (view->rendererReady()) {
        for (const auto id : terrainTiles(doc, terrain))
            view->setBodyOpacity(id, 0.6f);
        auto sun = doc.solar();
        sun.enabled = true;
        sun.latitude = 40;
        sun.longitude = -105;
        sun.time = {2010, 6, 21, 12, 0, 0, -420};
        doc.setSolar(sun);
        changed(window);
        hover(view, a);
        frame(view);
        const auto translucent = view->renderStats().transparentTriangles;
        const auto shadows = view->renderStats().shadowTriangles;
        check(translucent > 0 && shadows > 0, "translucent tiles and shadows are drawn");
        press(view, a);
        for (int i = 1; i <= 6; ++i)
            drag(view, a + (b - a) * i / 6);
        frame(view);
        const auto stats = view->renderStats();
        std::size_t hiddenTriangles = 0;
        for (const auto id : view->terrainPreviewHiddenTiles())
            hiddenTriangles += doc.bodies().at(id)->surface.faces.size();
        check(hiddenTriangles > 0 && stats.transparentTriangles == translucent - hiddenTriangles,
              "the hidden tiles leave the translucent pass (" +
                  std::to_string(stats.transparentTriangles) + " of " +
                  std::to_string(translucent) + ", " + std::to_string(hiddenTriangles) +
                  " hidden)");
        check(stats.shadowPreviewTriangles > 0 &&
                  stats.shadowTriangles == shadows - hiddenTriangles + stats.shadowPreviewTriangles,
              "the candidate casts shadows in place of the hidden tiles (" +
                  std::to_string(stats.shadowTriangles) + " cast, " + std::to_string(shadows) +
                  " before, " + std::to_string(stats.shadowPreviewTriangles) + " candidate)");
        key(view, Qt::Key_Escape);
        release(view, b);
        sun.enabled = false;
        doc.setSolar(sun);
        for (const auto id : terrainTiles(doc, terrain))
            view->setBodyOpacity(id, 1.f);
        changed(window);
    }
    // 4. A key released in the strip does not reach the viewport.
    {
        hover(view, at);
        check(!view->terrainBrush().smoothing, "no Shift: a raise");
        auto *field = strip(window).radiusField();
        field->setFocus();
        QKeyEvent released(QEvent::KeyRelease, Qt::Key_Control, Qt::ShiftModifier);
        QCoreApplication::sendEvent(field, &released);
        QCoreApplication::processEvents();
        check(!view->terrainBrush().smoothing, "a key released in the strip stays in the strip");
        view->setFocus();
    }
    // 6. Typing during a stroke withdraws it and says so; the typed text is a radius.
    {
        const auto revision = doc.revision();
        hover(view, a);
        press(view, a);
        for (int i = 1; i <= 6; ++i)
            drag(view, a + (b - a) * i / 6);
        QTest::keyClick(view, Qt::Key_3, Qt::NoModifier);
        QTest::qWait(10);
        release(view, b);
        check(doc.revision() == revision && !view->terrainBrush().previewShown,
              "typing during a stroke withdraws it");
        check(status(window) ==
                  "Stroke cancelled · typing sets the Radius; release first to type an amount",
              "the status bar says why: " + status(window).toStdString());
        check(step(window) == "Radius" &&
                  child<QLineEdit>(window, "measurements")->text() == "3",
              "the typed digit goes to the box as a radius");
        child<QLineEdit>(window, "measurements")->clear();
        view->setFocus();
    }
    // 5. A locked terrain refuses like no terrain: forbidden cursor, the reason in the status bar.
    {
        const auto before = doc.bodies().at(terrain);
        auto locked = std::make_shared<Body>(*before);
        locked->locked = true;
        doc.apply({"Lock", {{terrain, before, locked}}}, doc.revision());
        changed(window);
        hover(view, a);
        hover(view, at);
        check(view->cursor().shape() == Qt::ForbiddenCursor && !view->terrainBrush().refusal.isEmpty() &&
                  status(window) == view->terrainBrush().refusal,
              "a locked terrain shows the forbidden cursor and says why: " +
                  status(window).toStdString());
        doc.undo();
        changed(window);
    }
    view->setTool(Viewport::Tool::Select);
}
// ---- R096.f1b: the commit's incremental display equals a full rebuild ------------------------
// After a commit, the seam-ring tiles' normals are patched in place and only the changed rows of
// contour lines are re-uploaded; the frame must be the frame a full rebuild draws, bit for bit.
// The commit defers the terrain's digest and a worker computes it; readers see the final value.
void incrementalDisplay(Window &window) {
    fresh(window);
    auto &doc = window.document();
    const auto terrain = hill(window);
    auto *view = window.viewport();
    for (const auto shading : {TerrainShading::Material, TerrainShading::ElevationBands}) {
        auto display = doc.bodies().at(terrain)->terrain->display;
        display.contours = true;
        display.contourInterval = 0.25;
        display.shading = shading;
        setTerrainDisplay(doc, terrain, display);
        changed(window);
        TerrainToolOptions options;
        options.radius = 4;
        options.amount = shading == TerrainShading::Material ? 0.7 : 0.2;
        startTool(window, Viewport::Tool::Sculpt, options);
        // A stroke near a tile seam. With elevation bands it stays on the flank, so the
        // elevation range (and with it every tile's band colours) does not change.
        const bool bands = shading == TerrainShading::ElevationBands;
        const auto a = screen(window, terrain, bands ? 16 : 27, 29),
                   b = screen(window, terrain, bands ? 22 : 33, 31);
        hover(view, a);
        press(view, a);
        for (int i = 1; i <= 6; ++i)
            drag(view, a + (b - a) * i / 6);
        const auto patches = view->renderStats().terrainNormalPatches;
        release(view, b);
        view->setTool(Viewport::Tool::Select);
        hover(view, {4, 4});
        frame(view);
        if (!view->rendererReady())
            continue;
        check(view->renderStats().terrainNormalPatches > patches,
              "the commit patched the seam ring's normals in place");
        const auto incremental = view->grabFramebuffer();
        view->setPlacementMeshSharing(false); // drops every cache: a full rebuild
        frame(view);
        view->setPlacementMeshSharing(true);
        frame(view);
        const auto full = view->grabFramebuffer();
        check(incremental == full, "the incremental commit frame equals a full rebuild's");
    }
    // The deferred digest: the worker (or this read) gives the committed tiles' digest.
    std::vector<const Body *> tiles;
    for (const auto id : terrainTiles(doc, terrain))
        tiles.push_back(doc.bodies().at(id).get());
    check(std::string(doc.bodies().at(terrain)->terrain->meshDigest) == terrainMeshDigest(tiles) &&
              !terrainChangedSinceGenerated(doc, terrain),
          "the deferred digest is the committed tiles' digest");
}
// ---- R096.f2: Edit contours (UX §15.3, §15.7) ------------------------------------------------
QPoint at(Window &window, const Vec3 &model) { return window.viewport()->project(model).toPoint(); }
void idle(Viewport *view) {
    check(view->terrainPreviewWorker().waitForIdle(20000), "the preview worker finishes");
    QTest::qWait(5);
}
// The worker candidate's tiles equal the document's tiles with their keys, bit for bit.
void checkCommittedEdit(const Document &doc, Id terrain, const TerrainCommandPreview &preview,
                        const std::string &when) {
    check(preview.candidate.has_value() && !preview.refused(), when + ": the candidate applies");
    const auto tiles = tileContents(doc, terrain);
    for (const auto &tile : preview.candidate->region.tiles)
        check(tiles.contains(tile->key) && tiles.at(tile->key)->sameMesh(*tile),
              when + ": the committed tiles equal the previewed candidate bit for bit");
    for (const auto &key : preview.candidate->region.replaced) {
        const bool rebuilt = std::any_of(preview.candidate->region.tiles.begin(),
                                         preview.candidate->region.tiles.end(),
                                         [&](const auto &tile) { return tile->key == key; });
        check(rebuilt || !tiles.contains(key), when + ": a dropped tile is gone");
    }
}
struct Snapshot {
    QByteArray encoding;
    std::uint64_t revision{};
    std::size_t history{};
    bool dirty{};
    static Snapshot of(const Document &doc) {
        return {encodeDocument(doc), doc.revision(), doc.history().total, doc.dirty()};
    }
    bool same(const Document &doc) const {
        return doc.revision() == revision && doc.history().total == history &&
               doc.dirty() == dirty && encodeDocument(doc) == encoding;
    }
};
const TerrainSourceEditData &lastSourceEdit(const Document &doc, Id terrain) {
    return dynamic_cast<const TerrainSourceEditData &>(*recordOf(doc, terrain).edits.back()->data);
}
const TerrainSource *currentSource(const Document &doc, Id terrain, std::uint32_t id) {
    static std::shared_ptr<const TerrainSourceState> held;
    held = terrainSourceState(recordOf(doc, terrain));
    return held->find(id);
}
void startContours(Window &window) {
    action(window, "tool.31")->trigger();
    QTest::qWait(10);
}
// Selects ring k by clicking beside its vertex; returns the vertex's model position.
Vec3 selectRing(Window &window, Id terrain, int k, std::size_t vertex = 0) {
    auto *view = window.viewport();
    const auto &source = ringSource(window.document(), terrain, k);
    const auto world = window.document().worldTransform(terrain);
    const auto point = world.point(source.points[vertex]);
    const auto between = world.point((source.points[vertex] * 0.75 + source.points[vertex + 1] * 0.25));
    hover(view, at(window, between));
    press(view, at(window, between));
    release(view, at(window, between));
    check(view->contourTool().selected == source.id,
          "a click selects the source (ring " + std::to_string(k) + ")");
    return point;
}

void contoursShownAndSelected(Window &window) {
    fresh(window);
    auto &doc = window.document();
    const auto terrain = rings(window);
    auto *view = window.viewport();
    startContours(window);
    check(view->tool() == Viewport::Tool::Contours, "Edit contours starts");
    auto &bar = strip(window);
    check(bar.toolButton(int(Viewport::Tool::Contours))->isChecked() &&
              bar.contourButton("Add contour")->isVisible() &&
              bar.contourButton("Promote")->isVisible() && bar.roleField()->isVisibleTo(&bar) &&
              !bar.radiusField()->isVisible(),
          "the strip shows the Contours options");
    for (auto *control : bar.findChildren<QWidget *>())
        if (control->isVisibleTo(&bar) && control->focusPolicy() != Qt::NoFocus) {
            auto *accessible = QAccessible::queryAccessibleInterface(control);
            check(accessible && !accessible->text(QAccessible::Name).isEmpty(),
                  "every Contours control has an accessible name: " +
                      control->objectName().toStdString());
        }
    auto tool = view->contourTool();
    check(tool.terrain == terrain && tool.sources == 8, "the terrain's eight source contours show");
    // Hovering names a source.
    const auto &ring = ringSource(doc, terrain, 2);
    const auto world = doc.worldTransform(terrain);
    const auto middle = world.point((ring.points[5] + ring.points[6]) * 0.5);
    hover(view, at(window, middle));
    tool = view->contourTool();
    check(tool.hovered == ring.id &&
              tool.hoverLabel == QString("Contour 104.00 m · source %1").arg(ring.id),
          "hovering names the source: " + tool.hoverLabel.toStdString());
    check(status(window).startsWith("Contour 104.00 m · source"), "the status bar names it too");
    // Off the terrain the cursor is not-allowed.
    hover(view, {4, 4});
    check(view->cursor().shape() == Qt::ForbiddenCursor &&
              status(window) == "Point at a terrain to edit its contours",
          "off the terrain the tool refuses: " + status(window).toStdString());
    // A click selects it: square handles on its vertices, round ones at its midpoints.
    hover(view, at(window, middle));
    press(view, at(window, middle));
    release(view, at(window, middle));
    tool = view->contourTool();
    check(tool.selected == ring.id && tool.handles == 2 * 48 &&
              tool.selectedName == "Contour 104.00 m" && tool.elevation == 104.0 && tool.editable,
          "a click selects the source with 48 vertex and 48 midpoint handles");
    check(bar.sourceSummary()->text().contains("Contour 104.00 m") &&
              bar.elevationField()->text() == "104.00 m" &&
              bar.roleField()->currentText() == "Contour" && bar.roleField()->isEnabled(),
          "the strip names the source with its role and elevation: " +
              bar.elevationField()->text().toStdString());
    check(step(window) == "Elevation", "the box is labelled Elevation with a source selected");
    // Tab moves between its handles (§15.7).
    view->setFocus();
    QTest::keyClick(view, Qt::Key_Tab);
    check(view->contourTool().focus == 0, "Tab focuses the first vertex");
    QTest::keyClick(view, Qt::Key_Tab);
    check(view->contourTool().focus == 1, "Tab moves to the next handle (a midpoint)");
    QTest::keyClick(view, Qt::Key_Tab, Qt::ShiftModifier);
    check(view->contourTool().focus == 0, "Shift+Tab moves back");
    QTest::keyClick(view, Qt::Key_Escape);
    check(view->contourTool().selected == 0 && view->tool() == Viewport::Tool::Contours,
          "Escape clears the selection first");
    view->setTool(Viewport::Tool::Select);
}

void contourDragPreviewAndCommit(Window &window) {
    fresh(window);
    auto &doc = window.document();
    const auto terrain = rings(window);
    auto *view = window.viewport();
    startContours(window);
    const auto vertex = selectRing(window, terrain, 3, 6);
    const auto id = view->contourTool().selected;
    const auto before = Snapshot::of(doc);
    // Drag vertex 6 outward by 1 m: between ring 3 and ring 4, 3 m out.
    const Vec3 direction = normalized(Vec3{vertex.x, vertex.y, 0});
    const auto target = vertex + direction * 1.0;
    auto dragTo = [&](const Vec3 &to) {
        press(view, at(window, vertex));
        for (int i = 1; i <= 6; ++i)
            drag(view, at(window, vertex + (to - vertex) * (i / 6.0)));
        idle(view);
        frame(view);
    };
    dragTo(target);
    auto tool = view->contourTool();
    auto shown = view->terrainEditPreviewShown();
    check(tool.dragging && shown && view->terrainEditPreviewCurrent() && shown->candidate &&
              !shown->refused(),
          "the drag previews the re-triangulated cavity on the worker");
    check(!shown->outline.empty(), "the changed region is outlined");
    if (view->rendererReady())
        check(!view->terrainPreviewHiddenTiles().empty(),
              "the candidate is drawn in place of the tiles it replaces");
    check(before.same(doc), "the preview changes nothing");
    // Escape leaves no trace.
    key(view, Qt::Key_Escape);
    release(view, at(window, target));
    check(before.same(doc), "Escape leaves revision, history, dirty state and encoding unchanged");
    check(!view->terrainEditPreviewShown() && view->terrainPreviewHiddenTiles().empty(),
          "Escape withdraws the candidate");
    check(view->contourTool().selected == id, "Escape of a drag keeps the source selected");
    // The drag again, released: one "Edit contour" edit equal to the previewed candidate.
    dragTo(target);
    shown = view->terrainEditPreviewShown();
    check(shown && view->terrainEditPreviewCurrent() && shown->candidate, "the drag previews again");
    const auto command = view->terrainEditRequested();
    release(view, at(window, target));
    check(doc.revision() == before.revision + 1 && doc.history().total == before.history + 1 &&
              doc.history().entries.back().label == "Edit contour",
          "release commits one Edit contour edit");
    checkCommittedEdit(doc, terrain, *shown, "contour drag");
    check(command == shown->command, "the commit runs the previewed command");
    const auto &data = lastSourceEdit(doc, terrain);
    check(data.source == id && data.points.size() == 49, "the moved ring is stored");
    check(view->contourTool().selected == id && view->contourTool().focus == 12,
          "the source stays selected with the moved vertex focused");
    check(!view->terrainEditPreviewShown() && view->terrainPreviewHiddenTiles().empty(),
          "after the commit the tiles draw themselves");
    // A round handle inserts a vertex.
    const auto world = doc.worldTransform(terrain);
    const auto *moved = currentSource(doc, terrain, id);
    const auto midpoint = world.point((moved->points[20] + moved->points[21]) * 0.5);
    hover(view, at(window, midpoint));
    press(view, at(window, midpoint));
    const auto out = midpoint + normalized(Vec3{midpoint.x, midpoint.y, 0}) * 0.8;
    for (int i = 1; i <= 4; ++i)
        drag(view, at(window, midpoint + (out - midpoint) * (i / 4.0)));
    idle(view);
    release(view, at(window, out));
    check(lastSourceEdit(doc, terrain).points.size() == 50 &&
              doc.history().entries.back().label == "Edit contour",
          "dragging a round handle inserts a vertex");
    view->setTool(Viewport::Tool::Select);
}

void contourRefusal(Window &window) {
    fresh(window);
    auto &doc = window.document();
    const auto terrain = rings(window);
    {
        // Display contours (drawn faded in Edit contours) between the source rings.
        auto display = doc.bodies().at(terrain)->terrain->display;
        display.contours = true;
        display.contourInterval = 0.25;
        setTerrainDisplay(doc, terrain, display);
        changed(window);
    }
    auto *view = window.viewport();
    startContours(window);
    view->setFocus();
    hover(view, {4, 4});
    frame(view);
    const auto before = Snapshot::of(doc);
    const auto image = view->rendererReady() ? view->grabFramebuffer() : QImage();
    const auto vertex = selectRing(window, terrain, 3, 6);
    // Across ring 4 (3 m out): the moved run is refused at the pointer.
    const auto target = vertex + normalized(Vec3{vertex.x, vertex.y, 0}) * 4.5;
    press(view, at(window, vertex));
    for (int i = 1; i <= 6; ++i)
        drag(view, at(window, vertex + (target - vertex) * (i / 6.0)));
    idle(view);
    frame(view);
    auto tool = view->contourTool();
    const auto shown = view->terrainEditPreviewShown();
    check(shown && shown->refused() && !shown->candidate && view->terrainEditPreviewCurrent(),
          "a drag across another contour is refused and its preview withdrawn");
    check(tool.refused && tool.failure == "Crosses contour 103.00 m" && tool.failureAt,
          "the failure label names the crossed contour: " + tool.failure.toStdString());
    check(status(window).startsWith("Crosses contour 103.00 m at ") &&
              status(window).endsWith("Release to cancel, or move back."),
          "the status bar repeats it with the location: " + status(window).toStdString());
    check(child<QLabel>(window, "statusMessage")->property("terrainFailure").toBool(),
          "in the failure style");
    if (view->rendererReady())
        check(view->terrainPreviewHiddenTiles().empty(), "nothing is drawn in place of the tiles");
    shot(window, "contours");
    release(view, at(window, target));
    check(before.same(doc), "releasing a refused drag changes nothing");
    check(status(window).startsWith("Nothing changed: Crosses contour 103.00 m"),
          "the status bar says nothing changed: " + status(window).toStdString());
    // The frame is the one before (the selection cleared, the pointer off the terrain).
    view->setFocus();
    key(view, Qt::Key_Escape);
    hover(view, {4, 4});
    frame(view);
    if (!image.isNull())
        check(view->grabFramebuffer() == image, "the refused drag leaves the frame as it was");
    view->setTool(Viewport::Tool::Select);
}

void contourKeyboardAndStrip(Window &window) {
    fresh(window);
    auto &doc = window.document();
    const auto terrain = rings(window);
    auto *view = window.viewport();
    startContours(window);
    const auto world = doc.worldTransform(terrain);
    // A typed position selects the source passing nearest it.
    const auto &ring = ringSource(doc, terrain, 5);
    const auto id = ring.id;
    const auto onRing = world.point(ring.points[10]);
    enter(window, QString("[%1,%2]").arg(onRing.x + 0.3).arg(onRing.y));
    check(view->contourTool().selected == id, "a typed position selects the nearest source");
    // A typed elevation sets it: one Edit contour edit.
    const auto revision = doc.revision();
    enter(window, "102.70");
    check(doc.revision() == revision + 1 && lastSourceEdit(doc, terrain).source == id &&
              lastSourceEdit(doc, terrain).points.front().z ==
                  world.inverse().point({0, 0, 102.7}).z &&
              doc.history().entries.back().label == "Edit contour",
          "a typed number sets the selected source's elevation");
    check(view->contourTool().selected == id &&
              strip(window).elevationField()->text() == "102.70 m",
          "the strip shows the new elevation");
    // With no source selected, a typed elevation changes nothing and says why.
    view->selectTerrainSource(terrain, 0);
    const auto refused = Snapshot::of(doc);
    enter(window, "103");
    check(refused.same(doc) && status(window).contains("Select a source contour or breakline"),
          "an elevation needs a selected source: " + status(window).toStdString());
    enter(window, QString("[%1,%2]").arg(onRing.x + 0.3).arg(onRing.y));
    check(view->contourTool().selected == id, "the typed position selects it again");
    // Tab to a vertex, a typed position moves it (UX §15.7).
    view->setFocus();
    for (int i = 0; i < 9; ++i)
        QTest::keyClick(view, Qt::Key_Tab); // vertex 0, midpoint 0, ..., vertex 4
    check(view->contourTool().focus == 8, "Tab reaches vertex 4");
    const auto *source = currentSource(doc, terrain, id);
    check(source != nullptr, "ring 5 is current");
    const auto v4 = world.point(source->points[4]);
    const auto movedTo = v4 + normalized(Vec3{v4.x, v4.y, 0}) * 0.5;
    enter(window, QString("[%1,%2]").arg(movedTo.x, 0, 'f', 6).arg(movedTo.y, 0, 'f', 6));
    const auto &moved = lastSourceEdit(doc, terrain);
    check(moved.source == id && std::abs(world.point(moved.points[4]).x - movedTo.x) < 1e-6 &&
              std::abs(world.point(moved.points[4]).y - movedTo.y) < 1e-6,
          "a typed position moves the focused vertex");
    // Delete removes the focused vertex; Shift+Delete removes the source.
    view->setFocus();
    const auto points = moved.points.size();
    QTest::keyClick(view, Qt::Key_Delete);
    check(lastSourceEdit(doc, terrain).points.size() == points - 1, "Delete removes the vertex");
    auto *bar = &strip(window);
    // Role: a contour becomes a breakline and back.
    bar->roleField()->setCurrentIndex(1);
    emit bar->roleField()->activated(1);
    QTest::qWait(10);
    check(lastSourceEdit(doc, terrain).role == TerrainSourceRole::Breakline &&
              view->contourTool().role == TerrainSourceRole::Breakline,
          "Role switches the contour to a breakline");
    bar->roleField()->setCurrentIndex(0);
    emit bar->roleField()->activated(0);
    QTest::qWait(10);
    check(lastSourceEdit(doc, terrain).role == TerrainSourceRole::Contour, "and back");
    view->setFocus();
    const auto edits = recordOf(doc, terrain).edits.size();
    QTest::keyClick(view, Qt::Key_Delete, Qt::ShiftModifier);
    check(recordOf(doc, terrain).edits.size() == edits + 1 &&
              recordOf(doc, terrain).edits.back()->kind == terrainRemoveSourceKind &&
              view->contourTool().selected == 0 &&
              doc.history().entries.back().label == "Remove contour",
          "Shift+Delete removes the source");
    view->setTool(Viewport::Tool::Select);
}

void contourAddAndPromote(Window &window) {
    fresh(window);
    auto &doc = window.document();
    auto terrain = rings(window);
    auto *view = window.viewport();
    startContours(window);
    auto &bar = strip(window);
    // Add contour: clicks inside the innermost ring, an elevation typed first, Enter finishes.
    QTest::mouseClick(bar.contourButton("Add contour"), Qt::LeftButton);
    check(view->contourMode() == Viewport::ContourMode::AddContour &&
              bar.contourButton("Add contour")->isChecked() && step(window) == "Elevation",
          "Add contour waits for points, with the box labelled Elevation");
    enter(window, "105.2");
    view->setFocus();
    for (const auto &[x, y] : std::vector<std::pair<double, double>>{{-4, -1}, {0, 1.5}, {4, -1}}) {
        hover(view, screen(window, terrain, x, y));
        press(view, screen(window, terrain, x, y));
        release(view, screen(window, terrain, x, y));
    }
    idle(view);
    const auto shown = view->terrainEditPreviewShown();
    check(shown && shown->candidate && view->terrainEditPreviewCurrent() &&
              view->contourTool().adding.size() == 3,
          "the new contour previews on the worker");
    const auto revision = doc.revision();
    key(view, Qt::Key_Return);
    const auto &added = lastSourceEdit(doc, terrain);
    check(doc.revision() == revision + 1 && doc.history().entries.back().label == "Add contour" &&
              added.role == TerrainSourceRole::Contour && added.points.size() == 3 &&
              doc.worldTransform(terrain).point(added.points.front()).z == 105.2,
          "Enter adds the contour at the typed elevation");
    checkCommittedEdit(doc, terrain, *shown, "add contour");
    check(view->contourMode() == Viewport::ContourMode::Edit &&
              view->contourTool().selected == added.source,
          "the new contour is selected");
    // Add breakline: points take the surface's elevation.
    QTest::mouseClick(bar.contourButton("Add breakline"), Qt::LeftButton);
    view->setFocus();
    for (const auto &[x, y] : std::vector<std::pair<double, double>>{{-5, -5}, {5, -5.5}}) {
        hover(view, screen(window, terrain, x, y));
        press(view, screen(window, terrain, x, y));
        release(view, screen(window, terrain, x, y));
    }
    idle(view);
    key(view, Qt::Key_Return);
    check(lastSourceEdit(doc, terrain).role == TerrainSourceRole::Breakline &&
              doc.history().entries.back().label == "Add contour",
          "Add breakline draws a breakline");
    // Add spot: a click places it, a typed elevation adds it.
    QTest::mouseClick(bar.contourButton("Add spot"), Qt::LeftButton);
    view->setFocus();
    hover(view, screen(window, terrain, 2, 4));
    press(view, screen(window, terrain, 2, 4));
    release(view, screen(window, terrain, 2, 4));
    check(step(window) == "Elevation", "a placed spot waits for its elevation");
    enter(window, "105.6");
    check(lastSourceEdit(doc, terrain).role == TerrainSourceRole::Points &&
              doc.worldTransform(terrain).point(lastSourceEdit(doc, terrain).points.front()).z == 105.6,
          "a typed elevation adds the spot point");
    view->setTool(Viewport::Tool::Select);
    // Promote on a terrain made from points (no source contours).
    fresh(window);
    terrain = hill(window);
    auto display = doc.bodies().at(terrain)->terrain->display;
    display.contours = true;
    display.contourInterval = 0.5;
    setTerrainDisplay(doc, terrain, display);
    changed(window);
    startContours(window);
    check(view->contourTool().sources == 1, "a point terrain has only its spot points");
    QTest::mouseClick(strip(window).contourButton("Promote"), Qt::LeftButton);
    view->setFocus();
    hover(view, screen(window, terrain, 40, 30));
    idle(view);
    const auto promoted = view->terrainEditPreviewShown();
    const auto level = view->contourTool().promoteLevel;
    check(level && promoted && promoted->candidate && !promoted->points.empty(),
          "Promote previews the display contour under the pointer");
    check(std::abs(*level * 2 - std::round(*level * 2)) < 1e-9, "at a display contour level");
    press(view, screen(window, terrain, 40, 30));
    release(view, screen(window, terrain, 40, 30));
    const auto &made = lastSourceEdit(doc, terrain);
    check(recordOf(doc, terrain).edits.back()->kind == terrainAddSourceKind &&
              made.role == TerrainSourceRole::Contour &&
              doc.worldTransform(terrain).point(made.points.front()).z == *level,
          "a click promotes it to a source contour at its level");
    checkCommittedEdit(doc, terrain, *promoted, "promote");
    view->setTool(Viewport::Tool::Select);
}

// A pad protects its lines: its outline is shown as the pad's and cannot be moved.
void contourProtectedSources(Window &window) {
    fresh(window);
    auto &doc = window.document();
    const auto terrain = rings(window);
    auto *view = window.viewport();
    QJsonArray polygon;
    for (const auto &[x, y] : std::vector<std::pair<double, double>>{{30, -3}, {34, -3}, {34, 3}, {30, 3}})
        polygon.append(QJsonArray{x, y});
    const QJsonObject stamp{{"command", "terrain.stamp_pad"},
                            {"terrain", QString::number(terrain)},
                            {"footprint", QJsonObject{{"polygon", polygon}}},
                            {"elevation", 102.0},
                            {"mode", "daylight"}};
    executeBatch(doc, {{"apiVersion", 1},
                       {"documentId", QString::fromStdString(doc.identity())},
                       {"expectedRevision", QString::number(doc.revision())},
                       {"commands", QJsonArray{stamp}}});
    changed(window);
    check(recordOf(doc, terrain).edits.size() == 1, "the pad is stamped");
    startContours(window);
    // Select the pad's outline by a typed position on it.
    enter(window, "[30,0]");
    auto tool = view->contourTool();
    check(tool.selected && !tool.editable && tool.selectedName == "Pad outline" &&
              tool.selectedDetail.startsWith("change #1"),
          "a pad's outline is shown as the pad's: " + tool.selectedName.toStdString());
    auto &bar = strip(window);
    check(!bar.roleField()->isEnabled() && !bar.elevationField()->isEnabled() &&
              !bar.contourButton("Remove")->isEnabled(),
          "its Role, Elevation and Remove are unavailable");
    // Dragging its vertex is refused at the pointer and changes nothing.
    const auto before = Snapshot::of(doc);
    const auto world = doc.worldTransform(terrain);
    const auto *outline = currentSource(doc, terrain, tool.selected);
    check(outline != nullptr, "the outline is a current source");
    const auto corner = world.point(outline->points.front());
    view->setFocus();
    hover(view, at(window, corner));
    press(view, at(window, corner));
    for (int i = 1; i <= 4; ++i)
        drag(view, at(window, corner + Vec3{0.3 * i, 0.1 * i, 0}));
    idle(view);
    tool = view->contourTool();
    check(tool.refused && tool.failure.contains("Pad (change #1)"),
          "moving a pad's outline is refused, naming the pad: " + tool.failure.toStdString());
    release(view, at(window, corner + Vec3{1.2, 0.4, 0}));
    check(before.same(doc), "and changes nothing");
    view->setTool(Viewport::Tool::Select);
}

// The worker plans the newest request and never one a newer request replaced (latest wins).
void previewWorkerLatestWins(Window &window) {
    fresh(window);
    auto &doc = window.document();
    const auto terrain = rings(window);
    const auto &ring = ringSource(doc, terrain, 3);
    const auto world = doc.worldTransform(terrain);
    TerrainPreviewWorker worker;
    std::vector<std::pair<qulonglong, std::shared_ptr<const TerrainCommandPreview>>> results;
    QObject::connect(&worker, &TerrainPreviewWorker::finished,
                     [&](qulonglong generation, std::shared_ptr<const TerrainCommandPreview> p) {
                         results.emplace_back(generation, std::move(p));
                     });
    std::vector<QJsonObject> commands;
    for (int i = 1; i <= 5; ++i) {
        QJsonArray points;
        for (std::size_t j = 0; j < ring.points.size(); ++j) {
            auto p = world.point(ring.points[j]);
            if (j == 6)
                p = p + normalized(Vec3{p.x, p.y, 0}) * (0.2 * i);
            points.append(QJsonArray{p.x, p.y});
        }
        commands.push_back({{"command", "terrain.edit_source"},
                            {"terrain", QString::number(terrain)},
                            {"source", double(ring.id)},
                            {"points", points}});
    }
    for (const auto &command : commands)
        worker.request(prepareTerrainEditPreview(doc, command));
    check(worker.started() == 1 && worker.busy(), "the first request runs; the others wait");
    check(worker.waitForIdle(20000), "the worker finishes");
    check(worker.started() == 2, "only the newest waiting request is planned next");
    check(!results.empty() && results.back().first == worker.latest() &&
              results.back().second->command == commands.back(),
          "the newest plan is delivered last");
    for (std::size_t i = 1; i < results.size(); ++i)
        check(results[i].first > results[i - 1].first, "results are delivered newest last");
    // Cancel withdraws everything requested before it.
    results.clear();
    worker.request(prepareTerrainEditPreview(doc, commands.front()));
    worker.cancel();
    check(worker.waitForIdle(20000) && results.empty(), "a cancelled request is never delivered");
}
} // namespace

int main(int argc, char **argv) {
    QTemporaryDir preferences;
    qputenv("XDG_CONFIG_HOME", preferences.path().toUtf8());
    QApplication app(argc, argv);
    QString only; // --only TEXT runs the tests whose name contains TEXT
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
    // Screenshots are taken tall enough for the whole tool rail (its Terrain slot follows Tape,
    // New face and a separator).
    window.resize(1280, shots.isEmpty() ? 860 : 1080);
    window.show();
    if (!QTest::qWaitForWindowExposed(&window, 5000))
        return 2;
    const std::vector<std::pair<const char *, void (*)(Window &)>> tests{
        {"terrain menu, rail and shortcuts", menuAndRail},
        {"tool options strip", optionsStrip},
        {"stroke preview, commit and Escape", strokePreviewAndCommit},
        {"one spot and amend", spotAndAmend},
        {"modifiers and [ ]", modifiers},
        {"region scope", regionScope},
        {"keyboard path", keyboardPath},
        {"empty brush and pads", emptyBrushAndPads},
        {"hover refusals", hoverRefusals},
        {"review regressions", reviewRegressions},
        {"incremental display", incrementalDisplay},
        {"preview worker, latest wins", previewWorkerLatestWins},
        {"contours shown and selected", contoursShownAndSelected},
        {"contour drag, preview and commit", contourDragPreviewAndCommit},
        {"contour refusal", contourRefusal},
        {"contour keyboard and strip", contourKeyboardAndStrip},
        {"contour add and promote", contourAddAndPromote},
        {"contour protected sources", contourProtectedSources}};
    try {
        if (!shots.isEmpty()) {
            screenshots(window);
            std::cout << "screenshots\n" << std::flush;
        }
        for (const auto &[name, test] : tests) {
            if (!only.isEmpty() && !QString(name).contains(only))
                continue;
            test(window);
            std::cout << name << "\n" << std::flush;
        }
    } catch (const std::exception &error) {
        std::cerr << "FAIL: " << error.what() << '\n';
        return 1;
    }
    return 0;
}
