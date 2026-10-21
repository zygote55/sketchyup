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
#include "core/terrain_stamp.hpp"
#include "io/document_io.hpp"
#include <QAccessible>
#include <QAction>
#include <QApplication>
#include <QComboBox>
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
#include <QSpinBox>
#include <QTemporaryDir>
#include <QTest>
#include <QTimer>
#include <QToolBar>
#include <QToolButton>
#include <cmath>
#include <iostream>
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
    for (const auto *name : {"Sculpt", "Smooth", "Terrain settings…", "Regenerate terrain",
                             "Reset edits…", "Explode terrain…", "Create terrain from selection"})
        check(entries.contains(name), std::string("the Terrain menu has ") + name);
    for (const auto *name : {"Edit contours", "Stamp pad", "Drape"})
        check(!entries.contains(name),
              std::string("tools not built yet are not shown (f2): ") + name);
    check(action(window, "tool.29")->shortcut() == QKeySequence("Shift+S") &&
              action(window, "tool.30")->shortcut().isEmpty(),
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
    check(railActions[terrainSlot - 1]->isSeparator(), "the Terrain slot has its own group");
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
    check(drive(
              "terrainMenu",
              [&](QWidget &shown) {
                  auto *opened = qobject_cast<QMenu *>(&shown);
                  QStringList sections;
                  for (const auto *name : {"terrainMenuToolsHeading", "terrainMenuTerrainHeading"})
                      if (auto *label = opened->findChild<QLabel *>(name))
                          sections << label->text();
                  check(sections.contains("Tools") && sections.contains("Hill"),
                        "the menu heads the tools and the selected terrain's commands: " +
                            sections.join(", ").toStdString());
                  check(!action(window, "terrain.resetEdits")->isEnabled() &&
                            action(window, "terrain.settings")->isEnabled(),
                        "Reset edits is unavailable without edits");
              },
              [&] {
                  const auto at = window.menuBar()->actionGeometry(
                      window.menuBar()->actions()[menu]);
                  terrainMenu->popup(window.menuBar()->mapToGlobal(at.bottomLeft()));
                  QTest::qWait(200);
              }),
          "the Terrain menu opens");
    terrainMenu->close();
    // The context menu on a terrain: its tools, Terrain settings… and Explode terrain….
    view->setSelection(0);
    frame(view);
    const auto onTerrain = screen(window, terrain, 30, 30);
    check(drive(
              "viewportContextMenu",
              [&](QWidget &shown) {
                  QStringList items;
                  for (auto *entry : qobject_cast<QMenu *>(&shown)->actions())
                      if (!entry->isSeparator())
                          items << entry->text();
                  for (const auto *name :
                       {"Sculpt", "Smooth", "Terrain settings…", "Explode terrain…"})
                      check(items.contains(name),
                            std::string("the context menu offers ") + name + ": " +
                                items.join(", ").toStdString());
              },
              [&] { emit view->contextMenuRequested(onTerrain); }),
          "the context menu opens on a terrain");
    check(view->selectedBody() == terrain, "right-clicking a terrain selects it");
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
    const auto candidate = view->terrainPreviewShown();
    const double amount = brush.amount;
    const auto revision = doc.revision();
    press(view, at - QPoint(0, 40));
    release(view, at - QPoint(0, 40));
    check(doc.revision() == revision + 1 && lastSculpt(doc, terrain).amount == amount &&
              lastSculpt(doc, terrain).path.size() == 1,
          "a second click commits the one-spot amount");
    checkCommitted(doc, terrain, *candidate, "spot");
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
} // namespace

int main(int argc, char **argv) {
    QTemporaryDir preferences;
    qputenv("XDG_CONFIG_HOME", preferences.path().toUtf8());
    QApplication app(argc, argv);
    for (int i = 1; i + 1 < argc; ++i) {
        if (QString(argv[i]) == "--screenshots")
            shots = argv[i + 1];
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
        {"hover refusals", hoverRefusals}};
    try {
        if (!shots.isEmpty()) {
            screenshots(window);
            std::cout << "screenshots\n" << std::flush;
        }
        for (const auto &[name, test] : tests) {
            test(window);
            std::cout << name << "\n" << std::flush;
        }
    } catch (const std::exception &error) {
        std::cerr << "FAIL: " << error.what() << '\n';
        return 1;
    }
    return 0;
}
