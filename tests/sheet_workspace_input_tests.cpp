#include "app/sheet_evaluation.hpp"
#include "app/sheet_workspace.hpp"
#include "app/sheets_tray.hpp"
#include "app/surface_format.hpp"
#include "app/window.hpp"
#include "automation/commands.hpp"
#include "core/drawing_item_records.hpp"
#include "core/drawing_sheets.hpp"
#include "core/scenes.hpp"
#include "io/document_io.hpp"
#include <QAccessible>
#include <QAction>
#include <QApplication>
#include <QComboBox>
#include <QDir>
#include <QJsonArray>
#include <QMenuBar>
#include <QPushButton>
#include <QStackedWidget>
#include <QTemporaryDir>
#include <QTest>
#include <QToolBar>
#include <QToolButton>
#include <QWheelEvent>
#include <iostream>
// R091.e: the Sheets workspace shell (ADR 0164 §§1, 2, 4, 7). Real key events go to the
// focused widget; the document is a small drawing set with a changed, a broken and a
// perspective view.
using namespace sketchy;
namespace {
void check(bool value, const std::string &message) {
    if (!value)
        throw std::runtime_error(message);
}
QWidget *focused() { return QApplication::focusWidget(); }
std::string identity(const QWidget *widget) {
    if (!widget)
        return "none";
    return std::string(widget->metaObject()->className()) + "/" +
           widget->objectName().toStdString();
}
void key(Qt::Key code, Qt::KeyboardModifiers modifiers = Qt::NoModifier) {
    check(QTest::qWaitFor([] { return focused() != nullptr; }), "A widget has keyboard focus");
    QTest::keyClick(focused(), code, modifiers);
    QCoreApplication::processEvents();
}
void typeText(const QString &text) {
    for (const auto ch : text) {
        check(QTest::qWaitFor([] { return focused() != nullptr; }), "Typing has a focus owner");
        QTest::keyClicks(focused(), QString(ch));
        QCoreApplication::processEvents();
    }
}
bool within(const QWidget *container) {
    return focused() && (focused() == container || container->isAncestorOf(focused()));
}
void expectFocus(const QWidget *widget, const std::string &via) {
    check(QTest::qWaitFor([&] { return within(widget); }),
          via + ": expected focus in " + identity(widget) + ", got " + identity(focused()));
}
SceneSnapshot camera(double yaw, double pitch, bool orthographic, Vec3 target) {
    SceneSnapshot snapshot;
    snapshot.camera = SceneCamera{};
    snapshot.camera->target = target;
    snapshot.camera->yaw = yaw;
    snapshot.camera->pitch = pitch;
    snapshot.camera->distance = 30;
    snapshot.camera->orthographic = orthographic;
    return snapshot;
}
DrawingViewRecord viewRecord(Id sheet, Id scene, std::string name, DrawingFrame frame,
                             double scale = 50) {
    DrawingViewRecord record;
    record.name = std::move(name);
    record.sheet = sheet;
    record.source = {DrawingViewSourceKind::Scene, scene};
    record.frame = frame;
    record.scaleDenominator = scale;
    return record;
}
void batch(Document &doc, const QJsonArray &commands) {
    executeBatch(doc, {{"apiVersion", 1},
                       {"documentId", QString::fromStdString(doc.identity())},
                       {"expectedRevision", QString::number(doc.revision())},
                       {"commands", commands}});
}
struct Fixture {
    Id groundFloor{}, elevations{}, sections{}, schedule{};
    Id plan{}, south{}, north{}, garden{};
    Id northScene{};
};
// A house with four pages: A-101 holds a current plan and a changed south elevation, A-201 the
// north elevation and a perspective garden view. The tests delete the north elevation scene in
// the session, which breaks its view.
Fixture writeFixture(const QString &path) {
    Document doc;
    auto box = [&](double x, double y, double w, double d, double h, const char *name) {
        const auto id =
            doc.addFace({{{x, y, 0}, {x + w, y, 0}, {x + w, y + d, 0}, {x, y + d, 0}}}, name);
        doc.extrude(id, doc.bodies().at(id)->surface.faces.begin()->first, h);
        return id;
    };
    box(0, 0, 10, 8, .2, "Slab");
    box(0, 0, .3, 8, 2.7, "West wall");
    box(9.7, 0, .3, 8, 2.7, "East wall");
    box(0, 7.7, 10, .3, 2.7, "North wall");
    box(0, 0, 4, .3, 2.7, "South wall west");
    box(6, 0, 4, .3, 2.7, "South wall east");
    const auto groundPlan = createScene(doc, "Ground plan", camera(0, 90, true, {5, 4, 0}));
    const auto southScene = createScene(doc, "South elevation", camera(-90, 0, true, {5, 4, 1.4}));
    const auto northScene = createScene(doc, "North elevation", camera(90, 0, true, {5, 4, 1.4}));
    const auto gardenScene = createScene(doc, "Garden view", camera(-60, 25, false, {5, 4, 1}));
    Fixture f;
    f.groundFloor = createDrawingSheet(doc, "Ground floor", {420, 297}, "A-101", 10);
    f.elevations = createDrawingSheet(doc, "Elevations", {420, 297}, "A-201", 10);
    f.sections = createDrawingSheet(doc, "Sections", {420, 297}, "A-301", 10);
    f.schedule = createDrawingSheet(doc, "Window schedule", {420, 297}, "A-501", 10);
    f.plan = createDrawingView(
        doc, viewRecord(f.groundFloor, groundPlan, "Ground floor plan", {25, 24, 197, 196}));
    f.south = createDrawingView(
        doc, viewRecord(f.groundFloor, southScene, "South elevation", {240, 24, 155, 90}, 100));
    f.north = createDrawingView(
        doc, viewRecord(f.elevations, northScene, "North elevation", {25, 24, 176, 112}));
    auto perspective = viewRecord(f.elevations, gardenScene, "Garden view", {230, 24, 165, 112});
    perspective.content = DrawingViewContent::RasterAppearance;
    f.garden = createDrawingView(doc, perspective);
    DrawingTextItem notes;
    notes.frame = {240, 150, 90, 20};
    notes.text = "Notes\nAll dimensions in mm. Check on site before ordering.";
    createDrawingItem(doc, f.groundFloor, notes);
    batch(doc, {QJsonObject{{"command", "drawing_view.accept"}, {"all", true}}});
    // A chimney on the east wall changes the south elevation. The plan and garden views are
    // accepted again afterwards, so only the elevation shows "Changed".
    box(8.2, 3, .6, .6, 4, "Chimney");
    batch(doc,
          {QJsonObject{{"command", "drawing_view.accept"}, {"view", QString::number(f.plan)}}});
    batch(doc,
          {QJsonObject{{"command", "drawing_view.accept"}, {"view", QString::number(f.garden)}}});
    f.northScene = northScene;
    saveDocument(doc, path);
    return f;
}
std::map<Id, const void *> modelPointers(const Document &doc) {
    std::map<Id, const void *> result;
    for (const auto &[id, body] : doc.bodies())
        result[id] = body.get();
    return result;
}
QAction *action(Window &window, const char *id) {
    auto *result = window.findChild<QAction *>(id);
    check(result, std::string("Action exists: ") + id);
    return result;
}
void deleteScene(Window &window, Id scene) {
    batch(window.document(),
          {QJsonObject{{"command", "saved_scene.delete"}, {"scene", QString::number(scene)}}});
    emit window.viewport()->changed();
}
void settle(Window &window) {
    auto *cache = window.sheetEvaluations();
    const auto sheet = window.sheetWorkspace()->currentSheet();
    check(
        QTest::qWaitFor([&] { return cache->idle() && (!sheet || cache->current(sheet)); }, 20000),
        "Sheet evaluation settles");
    QTest::qWait(200); // Raster capture runs after evaluation (120 ms debounce).
    QCoreApplication::processEvents();
}
QString lastLabel(const Document &doc) {
    const auto cursor = doc.history(0, 1).position;
    return cursor ? QString::fromStdString(doc.history(cursor - 1, 1).entries.front().label)
                  : QString{};
}
QTreeWidgetItem *pageRow(QTreeWidget *tree, Id sheet) {
    for (int i = 0; i < tree->topLevelItemCount(); ++i)
        if (tree->topLevelItem(i)->data(0, Qt::UserRole + 1).toULongLong() == sheet)
            return tree->topLevelItem(i);
    return nullptr;
}
bool hasColour(const QImage &image, QRect area, QColor colour) {
    area = area.intersected(image.rect());
    for (int y = area.top(); y <= area.bottom(); ++y)
        for (int x = area.left(); x <= area.right(); ++x)
            if (QColor(image.pixel(x, y)) == colour)
                return true;
    return false;
}
QImage canvasImage(SheetCanvas *canvas) {
    auto image = canvas->grab().toImage().convertToFormat(QImage::Format_RGB32);
    // Logical-pixel lookups: sample at device pixels.
    return image;
}
QRect devicePixels(const QRectF &logical, qreal ratio) {
    return QRectF(logical.topLeft() * ratio, logical.size() * ratio).toAlignedRect();
}

void flagOff() {
    qunsetenv("SKETCHYUP_SHEETS_WORKSPACE");
    Window window;
    window.show();
    check(QTest::qWaitForWindowExposed(&window), "Window exposed without the Sheets flag");
    check(!window.findChild<QWidget *>("workspaceToggle") &&
              !window.findChild<QAction *>("view.workspace.toggle") &&
              !window.findChild<QWidget *>("sheetCanvas") && !window.sheetWorkspace(),
          "Without SKETCHYUP_SHEETS_WORKSPACE=1 there is no workspace toggle or canvas");
    window.activateWindow();
    check(QTest::qWaitForWindowActive(&window), "Window active without the flag");
    window.viewport()->setFocus();
    expectFocus(window.viewport(), "Initial focus");
    key(Qt::Key_S, Qt::ControlModifier | Qt::AltModifier);
    check(window.workspace() == Window::Workspace::Model && window.viewport()->isVisible(),
          "Ctrl+Alt+S does nothing without the flag");
}

void workspace(Window &window, const QString &path, const Fixture &f) {
    auto *sheets = window.sheetWorkspace();
    auto *canvas = sheets->canvas();
    auto *tray = window.sheetsTray();
    window.activateWindow();
    check(QTest::qWaitForWindowActive(&window), "Window active");
    window.viewport()->setFocus();
    expectFocus(window.viewport(), "Initial model focus");
    check(window.findChild<QWidget *>("workspaceToggle")->isVisible(),
          "The Model | Sheets toggle is in the top bar");

    // Toggle with Ctrl+Alt+S and back with the toggle buttons.
    key(Qt::Key_S, Qt::ControlModifier | Qt::AltModifier);
    check(window.workspace() == Window::Workspace::Sheets && canvas->isVisible() &&
              !window.viewport()->isVisible(),
          "Ctrl+Alt+S shows the Sheets workspace");
    expectFocus(canvas, "Switching to Sheets focuses the page canvas");
    check(window.findChild<QLabel *>("activeToolLabel")->isVisible() &&
              window.findChild<QLabel *>("activeToolLabel")->text() == "Select" &&
              window.findChild<QLabel *>("measurementUnits")->text() == "Paper units: mm",
          "Sheets status bar: active tool and paper units");
    check(window.findChild<QToolBar *>("sheetRail")->isVisible() &&
              !window.findChild<QToolBar *>("toolRail")->isVisible(),
          "The sheet rail replaces the model rail");
    check(!action(window, "tool.0")->isEnabled() &&
              action(window, "sheet.tool.select")->isEnabled(),
          "Model tools are inactive in Sheets and sheet tools active");
    key(Qt::Key_S, Qt::ControlModifier | Qt::AltModifier);
    check(window.workspace() == Window::Workspace::Model && window.viewport()->isVisible(),
          "Ctrl+Alt+S returns to Model");
    expectFocus(window.viewport(), "Switching to Model focuses the viewport");
    check(action(window, "tool.0")->isEnabled() &&
              !action(window, "sheet.tool.select")->isEnabled(),
          "Sheet tools are inactive in Model");
    auto *sheetsButton = window.findChild<QPushButton *>("workspaceSheets");
    sheetsButton->setFocus(Qt::TabFocusReason);
    expectFocus(sheetsButton, "Toggle button focus");
    key(Qt::Key_Space);
    check(window.workspace() == Window::Workspace::Sheets && sheetsButton->isChecked(),
          "The Sheets toggle button switches workspace from the keyboard");

    // The drawing set.
    window.setWorkspace(Window::Workspace::Model);
    window.openPath(path);
    window.setWorkspace(Window::Workspace::Sheets);
    settle(window);
    check(window.sheetEvaluations()->status(f.north) &&
              window.sheetEvaluations()->status(f.north)->state != DrawingViewState::Broken,
          "The north elevation draws while its scene exists");
    deleteScene(window, f.northScene);
    settle(window);
    const auto before = modelPointers(window.document());
    auto *tabs = sheets->pageTabs();
    check(tabs->count() == 4 && tabs->tabText(0) == "A-101 Ground floor" &&
              sheets->currentSheet() == f.groundFloor,
          "Page tabs list the four pages, first page current");
    check(sheets->breadcrumb()->text().contains("Sheets") &&
              sheets->breadcrumb()->text().contains("A-101 Ground floor"),
          "Breadcrumb: Sheets › A-101 Ground floor");
    check(sheets->paperLine()->text() == "A3 landscape · 420 × 297 mm · page 1 of 4",
          "Paper line: " + sheets->paperLine()->text().toStdString());
    auto *cache = window.sheetEvaluations();
    check(cache->status(f.plan) && cache->status(f.plan)->state == DrawingViewState::Current,
          "The plan view is current");
    check(cache->status(f.south) && cache->status(f.south)->state == DrawingViewState::Stale,
          "The south elevation is changed since accepted");

    // The amber chip on the changed view and no chip on the current one (screen pixels).
    {
        const auto ratio = canvas->devicePixelRatioF();
        const auto image = canvasImage(canvas);
        const auto paper = themeColors(false);
        const auto south = canvas->viewRect(f.south);
        const QRectF corner(south.right() - 90, south.top(), 90, 30);
        check(hasColour(image, devicePixels(corner, ratio), paper.warningSurface),
              "The changed view shows the amber Changed chip");
        const auto plan = canvas->viewRect(f.plan);
        check(!hasColour(image, devicePixels(QRectF(plan.right() - 90, plan.top(), 90, 30), ratio),
                         paper.warningSurface),
              "The current view has no chip");
        check(canvas->describeView(f.south) ==
                  "View 2 · South elevation · 1:100 · Changed since accepted",
              "Accessible description of the changed view: " +
                  canvas->describeView(f.south).toStdString());
    }

    // Accessible canvas: page name and one child per view.
    {
        auto *accessible = QAccessible::queryAccessibleInterface(canvas);
        check(accessible && accessible->role() == QAccessible::Canvas &&
                  accessible->text(QAccessible::Name) == "Page A-101 Ground floor, A3 landscape",
              "Canvas accessible name names the page");
        // R091.g: views first, then the page's items (the notes text).
        check(accessible->childCount() == 3 && accessible->child(0) &&
                  accessible->child(0)
                      ->text(QAccessible::Name)
                      .startsWith("View 1 · Ground floor plan · 1:50 · Current"),
              "Canvas exposes its views as accessible children");
        check(accessible->child(2)->text(QAccessible::Name).startsWith("Text · "),
              "Then its items: " + accessible->child(2)->text(QAccessible::Name).toStdString());
    }

    // F6 regions: search, sheet rail, canvas, page tabs, Tray, (Assistant), Measurements.
    canvas->setFocus(Qt::OtherFocusReason);
    expectFocus(canvas, "Canvas focus");
    key(Qt::Key_F6);
    expectFocus(tabs, "F6 from the canvas reaches the page tabs");
    key(Qt::Key_F6);
    expectFocus(tray, "F6 reaches the Sheets Tray");
    check(focused() == tray->pages(), "The Tray's F6 stop is the Pages tree");
    key(Qt::Key_F6);
    expectFocus(window.findChild<QPushButton *>("commandSearch"),
                "F6 skips the disabled Measurements box and reaches command search");
    key(Qt::Key_F6);
    expectFocus(window.findChild<QToolBar *>("sheetRail"), "F6 reaches the sheet rail");
    check(qobject_cast<QToolButton *>(focused()) &&
              static_cast<QToolButton *>(focused())->defaultAction() ==
                  action(window, "sheet.tool.select"),
          "The rail's stop is the active tool");
    key(Qt::Key_F6);
    expectFocus(canvas, "F6 returns to the canvas");
    key(Qt::Key_F6, Qt::ShiftModifier);
    expectFocus(window.findChild<QToolBar *>("sheetRail"), "Shift+F6 goes back to the rail");
    key(Qt::Key_F6);
    expectFocus(canvas, "F6 back to the canvas");

    // Tab reaches the page tabs and the Pages tree and comes back to the canvas. With Select,
    // Tab cycles the page's views instead (R091.f), so the cycle starts from Pan.
    key(Qt::Key_H);
    {
        bool tabsReached = false, treeReached = false, canvasAgain = false;
        for (int i = 0; i < 60 && !canvasAgain; ++i) {
            key(Qt::Key_Tab);
            tabsReached |= focused() == tabs;
            treeReached |= focused() == tray->pages();
            canvasAgain = tabsReached && treeReached && focused() == canvas;
        }
        check(tabsReached && treeReached && canvasAgain,
              "Tab cycles through the page tabs, the Pages tree and back to the canvas");
    }
    key(Qt::Key_Escape);
    check(canvas->tool() == SheetCanvas::Tool::Select, "Esc returns to Select");

    // The Pages tree lists pages with their views and items and the view states.
    auto *tree = tray->pages();
    {
        auto *ground = pageRow(tree, f.groundFloor);
        check(ground && ground->text(0) == "A-101" && ground->text(1) == "Ground floor" &&
                  ground->data(1, Qt::AccessibleDescriptionRole) == "1 changed" &&
                  ground->childCount() == 3,
              "A-101 row: number, name, one changed, two views and one item");
        // R091.g: state is a leading glyph on the name; the words are its description.
        check(ground->child(1)->data(1, Qt::AccessibleDescriptionRole) == "Changed" &&
                  !ground->child(1)->data(1, Qt::UserRole + 2).toList().isEmpty() &&
                  ground->child(0)->data(1, Qt::UserRole + 2).toList().isEmpty() &&
                  ground->child(2)->text(1).startsWith("Text · Notes"),
              "View states and items in paint order");
        auto *elevations = pageRow(tree, f.elevations);
        check(elevations && elevations->data(1, Qt::AccessibleDescriptionRole) == "1 broken",
              "A-201 row has one broken view");
        check(tree->columnCount() == 2 &&
                  tree->horizontalScrollBarPolicy() == Qt::ScrollBarAlwaysOff,
              "Pages has no state column and never scrolls sideways");
    }

    // Page tabs by keyboard: Right arrow shows the next page; the broken card on A-201.
    tabs->setFocus(Qt::TabFocusReason);
    expectFocus(tabs, "Page tabs focus");
    key(Qt::Key_Right);
    check(sheets->currentSheet() == f.elevations && tabs->currentIndex() == 1,
          "Right arrow on the page tabs shows A-201");
    settle(window);
    check(sheets->paperLine()->text().endsWith("page 2 of 4"), "Paper line follows the page");
    {
        const auto status = cache->status(f.north);
        check(status && status->state == DrawingViewState::Broken &&
                  status->reasons.front() == "sceneMissing",
              "The north elevation is broken: scene missing");
        check(canvas->describeView(f.north).endsWith("Broken: scene missing"),
              "Broken state in the accessible description");
        check(canvas->brokenMessage(f.north) == "Scene “North elevation” was deleted",
              "The broken card names the deleted scene: " +
                  canvas->brokenMessage(f.north).toStdString());
        const auto ratio = canvas->devicePixelRatioF();
        const auto image = canvasImage(canvas);
        const auto paper = themeColors(false);
        const auto north = canvas->viewRect(f.north);
        check(hasColour(image, devicePixels(north.adjusted(4, 4, -4, -4), ratio),
                        paper.failureSurface),
              "The broken view is drawn as a red cross-hatched card");
        const auto garden = cache->status(f.garden);
        check(garden && garden->notToScale && garden->state == DrawingViewState::Current,
              "The perspective view is current and not to scale");
        check(QTest::qWaitFor([&] { return canvas->paintedRasters() >= 1; }, 5000),
              "The perspective view paints a real raster image from the viewport");
    }

    // Zoom and pan: Z, typed zoom, arrows; H and arrows; wheel; Esc; Shift+Z fits.
    canvas->setFocus(Qt::OtherFocusReason);
    expectFocus(canvas, "Canvas focus for zoom");
    check(canvas->fitted(), "A page starts fitted");
    key(Qt::Key_Z);
    check(canvas->tool() == SheetCanvas::Tool::Zoom &&
              window.findChild<QLabel *>("activeToolLabel")->text() == "Zoom",
          "Z selects Zoom");
    auto *measurements = window.findChild<QLineEdit *>("measurements");
    check(measurements->isEnabled(), "Zoom accepts typed values");
    typeText("200%");
    check(measurements->hasFocus() && measurements->text() == "200%",
          "Typing a zoom goes to the Measurements box");
    key(Qt::Key_Return);
    check(std::abs(canvas->zoom() - 2) < 1e-9 && !canvas->fitted(), "200% zooms to actual size x2");
    expectFocus(canvas, "Enter returns focus to the canvas");
    key(Qt::Key_Up);
    check(std::abs(canvas->zoom() - 2.5) < 1e-9, "Up zooms in with the Zoom tool");
    key(Qt::Key_Escape);
    check(canvas->tool() == SheetCanvas::Tool::Select, "Esc leaves Zoom for Select");
    key(Qt::Key_H);
    check(canvas->tool() == SheetCanvas::Tool::Pan, "H selects Pan");
    const auto origin = canvas->paperOrigin();
    key(Qt::Key_Left);
    check(std::abs(canvas->paperOrigin().x() - origin.x() - 40) < 1e-6,
          "Left arrow pans the page with the Pan tool");
    const auto center = QPoint(canvas->width() / 2, canvas->height() / 2);
    QTest::mousePress(canvas, Qt::LeftButton, {}, center);
    QTest::mouseMove(canvas, center + QPoint(30, 20));
    QTest::mouseRelease(canvas, Qt::LeftButton, {}, center + QPoint(30, 20));
    check(std::abs(canvas->paperOrigin().x() - origin.x() - 70) < 1.5 &&
              std::abs(canvas->paperOrigin().y() - origin.y() - 20) < 1.5,
          "Dragging with Pan moves the page");
    {
        const auto zoom = canvas->zoom();
        const QPointF at(canvas->width() / 3., canvas->height() / 3.);
        const auto paperAt = (at - canvas->paperOrigin()) / canvas->pixelsPerMm();
        QWheelEvent wheel(at, canvas->mapToGlobal(at), {}, QPoint(0, 120), Qt::NoButton, {},
                          Qt::NoScrollPhase, false);
        QCoreApplication::sendEvent(canvas, &wheel);
        check(canvas->zoom() > zoom &&
                  QLineF((at - canvas->paperOrigin()) / canvas->pixelsPerMm(), paperAt).length() <
                      1e-6,
              "The wheel zooms about the cursor");
    }
    key(Qt::Key_Escape);
    check(canvas->tool() == SheetCanvas::Tool::Select, "Esc leaves Pan for Select");
    key(Qt::Key_Z, Qt::ShiftModifier);
    {
        const auto paper = QRectF(canvas->paperOrigin(), QSizeF(420, 297) * canvas->pixelsPerMm());
        check(canvas->fitted() &&
                  QRectF(canvas->rect()).adjusted(23, 23, -23, -23).contains(paper) &&
                  (std::abs(paper.width() - (canvas->width() - 48)) < 1 ||
                   std::abs(paper.height() - (canvas->height() - 48)) < 1),
              "Shift+Z fits the page with a 24 px border");
    }

    // Page operations, each one history entry, with undo; model records never change.
    const auto pages = window.document().drawingSheets().size();
    key(Qt::Key_N, Qt::ControlModifier | Qt::ShiftModifier);
    check(window.document().drawingSheets().size() == pages + 1 &&
              lastLabel(window.document()) == "Add page" && tabs->count() == int(pages + 1) &&
              tabs->currentIndex() == int(pages),
          "Ctrl+Shift+N adds a page and shows it");
    check(modelPointers(window.document()) == before, "Add page leaves model records untouched");
    expectFocus(canvas, "Add page returns to the canvas");
    key(Qt::Key_Z, Qt::ControlModifier);
    check(window.document().drawingSheets().size() == pages && tabs->count() == int(pages),
          "Undo removes the added page");
    expectFocus(canvas, "Undo keeps focus on the canvas in Sheets");

    tree->setFocus(Qt::OtherFocusReason);
    tree->setCurrentItem(pageRow(tree, f.groundFloor), 1);
    expectFocus(tree, "Pages tree focus");
    key(Qt::Key_F2);
    check(QTest::qWaitFor([&] { return qobject_cast<QLineEdit *>(focused()) != nullptr; }),
          "F2 edits the page name in place");
    static_cast<QLineEdit *>(focused())->selectAll();
    typeText("Plans");
    key(Qt::Key_Return);
    check(QTest::qWaitFor(
              [&] { return window.document().drawingSheets().at(f.groundFloor)->name == "Plans"; }),
          "Enter commits the new page name");
    check(lastLabel(window.document()) == "Rename page" && tabs->tabText(0) == "A-101 Plans",
          "Rename is one history entry and updates the tab");
    check(modelPointers(window.document()) == before, "Rename leaves model records untouched");
    QTest::qWait(50);
    tree->setFocus(Qt::OtherFocusReason);
    tree->setCurrentItem(pageRow(tree, f.groundFloor), 1);
    expectFocus(tree, "Pages tree focus for reorder");
    key(Qt::Key_Down, Qt::AltModifier);
    QTest::qWait(50);
    check(orderedDrawingSheets(window.document()) ==
                  std::vector<Id>{f.elevations, f.groundFloor, f.sections, f.schedule} &&
              lastLabel(window.document()) == "Move page" && tabs->tabText(1) == "A-101 Plans",
          "Alt+Down moves the page later");
    check(modelPointers(window.document()) == before, "Reorder leaves model records untouched");
    tree->setCurrentItem(pageRow(tree, f.groundFloor), 1);
    key(Qt::Key_Delete);
    QTest::qWait(50);
    check(!window.document().drawingSheets().contains(f.groundFloor) &&
              !window.document().drawingViews().contains(f.plan) &&
              lastLabel(window.document()) == "Delete page" && tabs->count() == 3,
          "Delete removes the page and its views in one entry");
    check(modelPointers(window.document()) == before, "Delete leaves model records untouched");
    for (int step = 0; step < 3; ++step)
        key(Qt::Key_Z, Qt::ControlModifier);
    check(window.document().drawingSheets().at(f.groundFloor)->name == "Ground floor" &&
              window.document().drawingViews().contains(f.plan) &&
              orderedDrawingSheets(window.document()) ==
                  std::vector<Id>{f.groundFloor, f.elevations, f.sections, f.schedule} &&
              tabs->count() == 4,
          "Undo restores the page, its order and its name");
    check(modelPointers(window.document()) == before,
          "Undo of page operations keeps model records");

    // Accept all from the References banner.
    sheets->setCurrentSheet(f.groundFloor);
    settle(window);
    auto *accept = window.findChild<QPushButton *>("sheetAcceptAll");
    check(accept && accept->isVisible(), "The References banner offers Accept all");
    check(window.findChild<QLabel *>("referencesBannerText")->text() ==
              "1 view changed since accepted",
          "Banner wording");
    accept->setFocus(Qt::TabFocusReason);
    expectFocus(accept, "Accept all focus");
    key(Qt::Key_Space);
    check(lastLabel(window.document()) == "Accept 1 view", "Accept all is one labelled entry");
    check(modelPointers(window.document()) == before, "Accept leaves model records untouched");
    settle(window);
    check(cache->status(f.south)->state == DrawingViewState::Current && !accept->isVisible(),
          "Accept all makes the changed view current and hides the banner");
    action(window, "edit.undo")->trigger();
    settle(window);
    check(cache->status(f.south)->state == DrawingViewState::Stale, "Undo restores the change");

    // Selecting a view on the canvas shows it in Entity info and the Pages tree.
    canvas->setFocus(Qt::OtherFocusReason);
    QTest::mouseClick(canvas, Qt::LeftButton, {}, canvas->viewRect(f.plan).center().toPoint());
    check(canvas->selectedView() == f.plan, "Click selects a view");
    check(window.findChild<QLineEdit *>("sheetInfoTitle")->text() == "Ground floor plan" &&
              window.findChild<QComboBox *>("sheetInfoScale")->currentText() == "1:50" &&
              window.findChild<QLabel *>("sheetInfoCrop")->text() == "On · 197 × 196 mm on paper" &&
              window.findChild<QLabel *>("sheetInfoStatus")
                  ->text()
                  .startsWith("Current · model revision"),
          "Entity info shows the selected view");
    check(tree->currentItem() &&
              tree->currentItem()->data(0, Qt::UserRole + 1).toULongLong() == f.plan,
          "Selection mirrors into the Pages tree");
    check(QAccessible::queryAccessibleInterface(canvas)
              ->text(QAccessible::Description)
              .startsWith("View 1 · Ground floor plan"),
          "The canvas description announces the selected view");
    key(Qt::Key_Escape);
    check(canvas->selectedView() == 0, "Esc clears the selection");
}

void screens(Window &window, const QString &path, const QString &directory, const QString &tag,
             const Fixture &f) {
    window.setWorkspace(Window::Workspace::Model);
    window.openPath(path);
    window.setWorkspace(Window::Workspace::Sheets);
    settle(window);
    deleteScene(window, f.northScene);
    auto *sheets = window.sheetWorkspace();
    QDir().mkpath(directory);
    auto shoot = [&](const QString &name) {
        settle(window);
        QTest::qWait(300);
        settle(window);
        const auto pixmap = window.grab();
        check(pixmap.save(directory + "/" + tag + "-" + name + ".png"), "Screenshot saved");
    };
    for (const int theme : {1, 2}) {
        action(window, ("view.theme." + std::to_string(theme)).c_str())->trigger();
        const auto suffix = theme == 1 ? QString("light") : QString("dark");
        window.resize(1440, 900);
        sheets->setCurrentSheet(f.groundFloor);
        sheets->canvas()->fitPage();
        sheets->canvas()->setSelectedView(f.plan);
        shoot("sheets-a101-" + suffix);
        sheets->setCurrentSheet(f.elevations);
        sheets->canvas()->fitPage();
        shoot("sheets-a201-" + suffix);
        window.resize(900, 800);
        sheets->setCurrentSheet(f.groundFloor);
        sheets->canvas()->fitPage();
        shoot("sheets-900-" + suffix);
    }
}
} // namespace
int main(int argc, char **argv) {
    QTemporaryDir files;
    if (!files.isValid())
        return 2;
    qputenv("XDG_CONFIG_HOME", files.path().toUtf8());
    qputenv("XDG_DATA_HOME", files.path().toUtf8());
    QSurfaceFormat format;
    format.setVersion(3, 3);
    format.setProfile(QSurfaceFormat::CoreProfile);
    format.setDepthBufferSize(24);
    sketchy::setDefaultViewportFormat(format);
    QApplication app(argc, argv);
    app.setOrganizationName("SketchyUp");
    app.setApplicationName("SketchyUp");
    try {
        const auto path = files.filePath("house.sketchyup");
        const auto fixture = writeFixture(path);
        const auto arguments = app.arguments();
        const auto screensAt = arguments.indexOf("--screens");
        if (screensAt < 0)
            flagOff();
        qputenv("SKETCHYUP_SHEETS_WORKSPACE", "1");
        Window window;
        window.resize(1440, 900);
        window.show();
        check(QTest::qWaitForWindowExposed(&window), "Window exposed with the Sheets flag");
        check(QTest::qWaitFor([&] { return window.viewport()->rendererReady(); }, 10000),
              "Viewport renderer initialized in the Model workspace");
        if (screensAt >= 0 && screensAt + 2 < arguments.size()) {
            screens(window, path, arguments[screensAt + 1], arguments[screensAt + 2], fixture);
            std::cout << "Sheets workspace screenshots written\n";
            return 0;
        }
        workspace(window, path, fixture);
        std::cout << "Sheets workspace: flag, toggle, F6 regions, Tab, page tabs, Pages tree, "
                     "page add/rename/move/delete with undo, changed chip, broken card, raster "
                     "view, Accept all, zoom, pan and fit passed\n";
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
