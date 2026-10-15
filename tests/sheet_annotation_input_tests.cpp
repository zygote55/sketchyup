#include "app/sheet_evaluation.hpp"
#include "app/sheet_item_info.hpp"
#include "app/sheet_items.hpp"
#include "app/sheet_workspace.hpp"
#include "app/sheets_tray.hpp"
#include "app/surface_format.hpp"
#include "app/viewport.hpp"
#include "app/window.hpp"
#include "automation/commands.hpp"
#include "core/drawing_item_records.hpp"
#include "core/drawing_sheets.hpp"
#include "core/scenes.hpp"
#include "io/document_io.hpp"
#include "io/drawing_items.hpp"
#include "presentation/unit_display.hpp"
#include <QAccessible>
#include <QAction>
#include <QApplication>
#include <QComboBox>
#include <QDir>
#include <QJsonArray>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QTemporaryDir>
#include <QTest>
#include <QTimer>
#include <QTreeWidget>
#include <cmath>
#include <functional>
#include <iostream>
// R091.g: page items in the Sheets workspace (ADR 0164 §§3–5, decisions 2 and 11; design §5).
// The Line, Rectangle, Text, Label and Dimension tools by pointer and by typed values, in-place
// text editing, title blocks with their automatic fields, z order, Tab, Delete, Reattach and the
// Pages tree. Pointer input is sent to the canvas as real mouse events; keys go to the focused
// widget. After every page operation the model's records are pointer-identical: page editing
// never modifies model geometry.
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
                             double scale) {
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
    Id groundFloor{}, elevations{};
    Id plan{}, garden{};
    Id planScene{}, gardenScene{};
};
// A3 page A-101 with a 10 × 8 m slab and a 0.6 m chimney in plan at 1:100 (the slab's centre
// (5, 4) at the frame centre (120, 95)) and a perspective garden view; A-201 is empty.
Fixture writeFixture(const QString &path) {
    Document doc;
    auto box = [&](double x, double y, double w, double d, double h, const char *name) {
        const auto id =
            doc.addFace({{{x, y, 0}, {x + w, y, 0}, {x + w, y + d, 0}, {x, y + d, 0}}}, name);
        doc.extrude(id, doc.bodies().at(id)->surface.faces.begin()->first, h);
        return id;
    };
    box(0, 0, 10, 8, .2, "Slab");
    box(8.2, 3, .6, .6, 1, "Chimney");
    Fixture f;
    f.planScene = createScene(doc, "Ground plan", camera(0, 90, true, {5, 4, 0}));
    f.gardenScene = createScene(doc, "Garden view", camera(-60, 25, false, {5, 4, 1}));
    f.groundFloor = createDrawingSheet(doc, "Ground floor", {420, 297}, "A-101", 10);
    f.elevations = createDrawingSheet(doc, "Elevations", {420, 297}, "A-201", 10);
    f.plan = createDrawingView(
        doc, viewRecord(f.groundFloor, f.planScene, "Ground floor plan", {20, 20, 200, 150}, 100));
    auto perspective =
        viewRecord(f.groundFloor, f.gardenScene, "Garden view", {240, 20, 160, 120}, 50);
    perspective.content = DrawingViewContent::RasterAppearance;
    f.garden = createDrawingView(doc, perspective);
    batch(doc, {QJsonObject{{"command", "drawing_view.accept"}, {"all", true}}});
    saveDocument(doc, path);
    return f;
}
// Model records by pointer: page editing must never replace them.
struct ModelRecords {
    std::map<Id, const void *> bodies, scenes;
    bool operator==(const ModelRecords &) const = default;
};
ModelRecords modelRecords(const Document &doc) {
    ModelRecords result;
    for (const auto &[id, body] : doc.bodies())
        result.bodies[id] = body.get();
    for (const auto &[id, scene] : doc.scenes())
        result.scenes[id] = scene.get();
    return result;
}
Id bodyNamed(const Document &doc, const std::string &name) {
    for (const auto &[id, body] : doc.bodies())
        if (body->name == name)
            return id;
    throw std::runtime_error("Body exists: " + name);
}
QAction *action(Window &window, const char *id) {
    auto *result = window.findChild<QAction *>(id);
    check(result, std::string("Action exists: ") + id);
    return result;
}
void settle(Window &window) {
    auto *cache = window.sheetEvaluations();
    const auto sheet = window.sheetWorkspace()->currentSheet();
    check(
        QTest::qWaitFor([&] { return cache->idle() && (!sheet || cache->current(sheet)); }, 20000),
        "Sheet evaluation settles");
    QTest::qWait(150);
    QCoreApplication::processEvents();
}
size_t entries(const Document &doc) { return doc.history(0, 1).position; }
QString lastLabel(const Document &doc) {
    const auto cursor = entries(doc);
    return cursor ? QString::fromStdString(doc.history(cursor - 1, 1).entries.front().label)
                  : QString{};
}
bool near(double a, double b, double tolerance = 1e-6) { return std::abs(a - b) <= tolerance; }
bool near(QPointF a, QPointF b, double tolerance = 1e-6) {
    return near(a.x(), b.x(), tolerance) && near(a.y(), b.y(), tolerance);
}
QPointF at(const DrawingPointMm &point) { return {point[0], point[1]}; }
void mouse(QWidget *widget, QEvent::Type type, QPointF position, Qt::MouseButton button,
           Qt::MouseButtons buttons, Qt::KeyboardModifiers modifiers) {
    QMouseEvent event(type, position, widget->mapToGlobal(position), button, buttons, modifiers);
    QCoreApplication::sendEvent(widget, &event);
    QCoreApplication::processEvents();
}
QPointF pixel(SheetCanvas *canvas, QPointF paperMm) {
    const auto point = canvas->widgetPoint(paperMm);
    return {std::round(point.x()), std::round(point.y())};
}
void hover(SheetCanvas *canvas, QPointF paperMm, Qt::KeyboardModifiers modifiers = {}) {
    mouse(canvas, QEvent::MouseMove, pixel(canvas, paperMm), Qt::NoButton, Qt::NoButton, modifiers);
}
void click(SheetCanvas *canvas, QPointF paperMm, Qt::KeyboardModifiers modifiers = {}) {
    const auto p = pixel(canvas, paperMm);
    mouse(canvas, QEvent::MouseMove, p, Qt::NoButton, Qt::NoButton, modifiers);
    mouse(canvas, QEvent::MouseButtonPress, p, Qt::LeftButton, Qt::LeftButton, modifiers);
    mouse(canvas, QEvent::MouseButtonRelease, p, Qt::LeftButton, Qt::NoButton, modifiers);
}
void drag(SheetCanvas *canvas, QPointF fromMm, QPointF toMm, Qt::KeyboardModifiers modifiers = {}) {
    const auto from = pixel(canvas, fromMm), to = pixel(canvas, toMm);
    mouse(canvas, QEvent::MouseMove, from, Qt::NoButton, Qt::NoButton, modifiers);
    mouse(canvas, QEvent::MouseButtonPress, from, Qt::LeftButton, Qt::LeftButton, modifiers);
    for (int step = 1; step <= 4; ++step)
        mouse(canvas, QEvent::MouseMove, from + (to - from) * step / 4., Qt::NoButton,
              Qt::LeftButton, modifiers);
    mouse(canvas, QEvent::MouseButtonRelease, to, Qt::LeftButton, Qt::NoButton, modifiers);
}
// The newest item of a kind (IDs never rewind).
Id newest(const Document &doc, DrawingItemKind kind) {
    Id result = 0;
    for (const auto &[id, item] : doc.drawingItems())
        if (item->kind() == kind)
            result = std::max(result, id);
    return result;
}
size_t count(const Document &doc, DrawingItemKind kind) {
    size_t result = 0;
    for (const auto &[id, item] : doc.drawingItems())
        result += item->kind() == kind;
    return result;
}
const DrawingItemRecord &item(const Document &doc, Id id) {
    check(doc.drawingItems().contains(id), "Item exists: " + std::to_string(id));
    return *doc.drawingItems().at(id);
}
template <class Kind> const Kind &data(const Document &doc, Id id) {
    return std::get<Kind>(item(doc, id).data);
}
QPointF paperOf(const Document &doc, Id view, Vec3 world) {
    const auto mapping = drawingViewMapping(doc, view);
    check(mapping.projection.has_value(), "The view projects");
    const auto sheet = mapping.projection->worldToSheet(world);
    return {sheet.x, sheet.y};
}
QPlainTextEdit *textEditor() { return qobject_cast<QPlainTextEdit *>(focused()); }

void annotate(Window &window, const QString &path, const Fixture &f) {
    auto &doc = window.document();
    auto *sheets = window.sheetWorkspace();
    auto *canvas = sheets->canvas();
    auto *tray = window.sheetsTray();
    auto *status = window.findChild<QLabel *>("statusMessage");
    auto *toolLabel = window.findChild<QLabel *>("activeToolLabel");
    auto *stepLabel = window.findChild<QLabel *>("measurementStep");
    auto *measurements = window.findChild<QLineEdit *>("measurements");
    check(status && toolLabel && stepLabel && measurements, "Status bar widgets exist");
    window.activateWindow();
    check(QTest::qWaitForWindowActive(&window), "Window active");
    window.openPath(path);
    window.setWorkspace(Window::Workspace::Sheets);
    sheets->setCurrentSheet(f.groundFloor);
    settle(window);
    auto model = modelRecords(doc);
    auto unchanged = [&](const std::string &what) {
        check(modelRecords(doc) == model, what + " leaves model records untouched");
    };
    auto focusCanvas = [&] {
        canvas->setFocus(Qt::OtherFocusReason);
        check(QTest::qWaitFor([&] { return canvas->hasFocus(); }), "Canvas focus");
    };
    auto typed = [&](const QString &text) {
        focusCanvas();
        typeText(text);
        check(measurements->hasFocus(), "Typing " + text.toStdString() + " opens Measurements");
        key(Qt::Key_Return);
    };
    auto tool = [&](Qt::Key code, SheetCanvas::Tool expected, const QString &name) {
        focusCanvas();
        key(code);
        check(canvas->tool() == expected && toolLabel->text() == name,
              "The key chooses " + name.toStdString() + ", got " + toolLabel->text().toStdString());
    };
    const auto slab = bodyNamed(doc, "Slab");
    auto chimney = bodyNamed(doc, "Chimney");

    // ---- Line: drag, Shift, click–move–click with a typed length, re-entry amends --------
    std::cerr << "[annotation] Line" << std::endl;
    tool(Qt::Key_L, SheetCanvas::Tool::Line, "Line");
    check(stepLabel->text() == "Length" && status->text().startsWith("Click or drag the line"),
          "Line's Measurements and instruction: " + status->text().toStdString());
    auto before = entries(doc);
    drag(canvas, {30, 240}, {130, 240});
    check(entries(doc) == before + 1 && lastLabel(doc) == "Add line", "A drag draws one line");
    const auto dragged = newest(doc, DrawingItemKind::Line);
    check(near(at(data<DrawingLineItem>(doc, dragged).from), {30, 240}) &&
              near(at(data<DrawingLineItem>(doc, dragged).to), {130, 240}),
          "The line snaps to the 5 mm grid");
    unchanged("Line by drag");
    drag(canvas, {30, 250}, {100, 262}, Qt::ShiftModifier);
    {
        const auto &line = data<DrawingLineItem>(doc, newest(doc, DrawingItemKind::Line));
        check(near(line.from[1], line.to[1]) && line.to[0] > line.from[0] + 60,
              "Shift keeps the line horizontal");
    }
    click(canvas, {30, 260});
    check(canvas->itemStep() == 1, "A click anchors the line's start");
    hover(canvas, {60, 260});
    check(measurements->text() == "30 mm",
          "The box shows the live length: " + measurements->text().toStdString());
    before = entries(doc);
    typed("50");
    auto typedLine = newest(doc, DrawingItemKind::Line);
    check(entries(doc) == before + 1 &&
              near(at(data<DrawingLineItem>(doc, typedLine).to), {80, 260}),
          "A typed length draws along the preview direction");
    const auto lines = count(doc, DrawingItemKind::Line);
    typed("80");
    typedLine = newest(doc, DrawingItemKind::Line);
    check(entries(doc) == before + 1 && count(doc, DrawingItemKind::Line) == lines &&
              near(at(data<DrawingLineItem>(doc, typedLine).to), {110, 260}),
          "Re-entry amends the line just drawn");
    // Esc cancels a started line without an entry.
    click(canvas, {150, 260});
    before = entries(doc);
    focusCanvas();
    key(Qt::Key_Escape);
    check(canvas->itemStep() == 0 && entries(doc) == before &&
              canvas->tool() == SheetCanvas::Tool::Line,
          "Esc cancels the line and keeps the tool");
    unchanged("Lines");

    // ---- Rectangle: drag, Shift square, typed width,height -------------------------------
    std::cerr << "[annotation] Rectangle" << std::endl;
    tool(Qt::Key_R, SheetCanvas::Tool::Rectangle, "Rectangle");
    before = entries(doc);
    drag(canvas, {30, 200}, {70, 215});
    check(entries(doc) == before + 1 && lastLabel(doc) == "Add rectangle",
          "A drag draws one rectangle");
    {
        const auto &frame =
            data<DrawingRectangleItem>(doc, newest(doc, DrawingItemKind::Rectangle)).frame;
        check(near(frame.xMm, 30) && near(frame.yMm, 200) && near(frame.widthMm, 40) &&
                  near(frame.heightMm, 15),
              "Rectangle frame from the drag: " + std::to_string(frame.xMm) + "," +
                  std::to_string(frame.yMm) + " " + std::to_string(frame.widthMm) + "×" +
                  std::to_string(frame.heightMm));
    }
    drag(canvas, {150, 240}, {185, 250}, Qt::ShiftModifier);
    {
        const auto &frame =
            data<DrawingRectangleItem>(doc, newest(doc, DrawingItemKind::Rectangle)).frame;
        check(near(frame.widthMm, frame.heightMm), "Shift draws a square");
    }
    click(canvas, {100, 250});
    hover(canvas, {110, 260});
    before = entries(doc);
    typed("40,20");
    {
        const auto &frame =
            data<DrawingRectangleItem>(doc, newest(doc, DrawingItemKind::Rectangle)).frame;
        check(entries(doc) == before + 1 && near(frame.xMm, 100) && near(frame.yMm, 250) &&
                  near(frame.widthMm, 40) && near(frame.heightMm, 20),
              "Typed width,height from the first corner");
    }
    unchanged("Rectangles");

    // ---- Text: click and type in place, a dragged wrap width, a typed position -------------
    std::cerr << "[annotation] Text" << std::endl;
    tool(Qt::Key_X, SheetCanvas::Tool::Text, "Text");
    check(stepLabel->text() == "Position", "Text's Measurements step is Position");
    before = entries(doc);
    click(canvas, {240, 170});
    check(QTest::qWaitFor([&] { return textEditor() != nullptr; }) && canvas->textEditing() &&
              focused()->objectName() == "sheetTextEditor",
          "A click opens the in-place editor: " + identity(focused()));
    typeText("Notes");
    check(entries(doc) == before, "Typing commits nothing yet");
    key(Qt::Key_Escape);
    const auto notes = newest(doc, DrawingItemKind::Text);
    check(entries(doc) == before + 1 && lastLabel(doc) == "Add text" &&
              data<DrawingTextItem>(doc, notes).text == "Notes" &&
              near(data<DrawingTextItem>(doc, notes).frame.xMm, 240) &&
              near(data<DrawingTextItem>(doc, notes).frame.yMm, 170) && canvas->hasFocus(),
          "Esc ends editing as one Add text entry");
    drag(canvas, {240, 185}, {300, 190});
    check(textEditor() != nullptr, "A drag sets a wrap width and opens the editor");
    typeText("All dimensions in mm. Check on site before ordering.");
    key(Qt::Key_Return, Qt::ControlModifier);
    {
        const auto &text = data<DrawingTextItem>(doc, newest(doc, DrawingItemKind::Text));
        check(lastLabel(doc) == "Add text" && near(text.frame.widthMm, 60) &&
                  text.text == "All dimensions in mm. Check on site before ordering.",
              "Ctrl+Enter ends editing; the text wraps at the dragged width");
    }
    // An empty editor creates nothing.
    before = entries(doc);
    click(canvas, {350, 200});
    key(Qt::Key_Escape);
    check(entries(doc) == before, "Nothing typed, nothing made");
    // Typed position and a typed text size.
    typed("14pt");
    check(canvas->textSize() == 14, "A typed size in points sets the text size");
    typed("[240,205]");
    check(textEditor() != nullptr, "A typed [x,y] opens the editor there");
    typeText("Typed");
    before = entries(doc);
    click(canvas, {380, 280}); // A click outside ends editing.
    {
        const auto id = newest(doc, DrawingItemKind::Text);
        const auto &text = data<DrawingTextItem>(doc, id);
        check(entries(doc) == before + 1 && text.text == "Typed" && near(text.frame.xMm, 240) &&
                  near(text.frame.yMm, 205) && near(text.font.sizePt, 14),
              "A click outside ends editing as one entry, at the typed position and size");
    }
    typed("10pt");
    unchanged("Text");

    // ---- In-place editing of a selected text (decision 11) --------------------------------
    std::cerr << "[annotation] Edit text in place" << std::endl;
    tool(Qt::Key_Space, SheetCanvas::Tool::Select, "Select");
    canvas->setSelectedItem(notes);
    focusCanvas();
    check(stepLabel->text() == "Move", "A selected item takes Move");
    // Typing alone never starts editing: X is the Text tool.
    key(Qt::Key_X);
    check(!canvas->textEditing() && canvas->tool() == SheetCanvas::Tool::Text,
          "Typing chooses a tool, never edits");
    key(Qt::Key_Escape);
    canvas->setSelectedItem(notes);
    focusCanvas();
    before = entries(doc);
    key(Qt::Key_Return);
    check(textEditor() && textEditor()->toPlainText() == "Notes", "Enter edits the text in place");
    // Plain keys type into the editor; nothing is committed until editing ends.
    key(Qt::Key_End);
    typeText(" LX");
    check(canvas->tool() == SheetCanvas::Tool::Select && entries(doc) == before,
          "Letters type text while editing");
    key(Qt::Key_Escape);
    check(entries(doc) == before + 1 && lastLabel(doc) == "Edit text" &&
              data<DrawingTextItem>(doc, notes).text == "Notes LX",
          "Esc ends editing as one Edit text entry");
    // Enter then Esc with no change commits nothing.
    key(Qt::Key_Return);
    key(Qt::Key_Escape);
    check(entries(doc) == before + 1, "An unchanged edit commits nothing");
    // A click outside ends editing.
    key(Qt::Key_Return);
    key(Qt::Key_End);
    key(Qt::Key_Backspace);
    key(Qt::Key_Backspace);
    key(Qt::Key_Backspace);
    click(canvas, {380, 150});
    check(entries(doc) == before + 2 && data<DrawingTextItem>(doc, notes).text == "Notes",
          "A click outside ends editing as one entry");
    action(window, "edit.undo")->trigger();
    check(data<DrawingTextItem>(doc, notes).text == "Notes LX", "Undo restores the text");
    action(window, "edit.redo")->trigger();
    check(data<DrawingTextItem>(doc, notes).text == "Notes", "Redo edits it again");
    unchanged("Text editing");

    // ---- Title block: inserted, edited in place and in Entity info, automatic fields --------
    std::cerr << "[annotation] Title block" << std::endl;
    before = entries(doc);
    action(window, "sheet.insert.titleBlock")->trigger();
    const auto block = newest(doc, DrawingItemKind::TitleBlock);
    check(entries(doc) == before + 1 && lastLabel(doc) == "Add title block" &&
              canvas->selectedItem() == block,
          "Insert › Title block adds one and selects it");
    {
        const auto &frame = data<DrawingTitleBlockItem>(doc, block).frame;
        check(near(frame.xMm, 250) && near(frame.yMm, 257) && near(frame.widthMm, 160) &&
                  near(frame.heightMm, 30),
              "The title block sits in the bottom-right corner of the margin");
        const auto evaluation = evaluateDrawingItem(doc, block);
        check(evaluation.titleBlock && evaluation.titleBlock->sheetNumber == "A-101" &&
                  evaluation.titleBlock->page == "1 of 2" &&
                  evaluation.titleBlock->scale == "1:100 @ A3",
              "Sheet number, page and scale fill in automatically");
    }
    focusCanvas();
    before = entries(doc);
    key(Qt::Key_Return);
    check(canvas->textEditField() == "project" && qobject_cast<QLineEdit *>(focused()),
          "Enter edits the title block's project in place");
    typeText("House extension");
    key(Qt::Key_Tab);
    check(canvas->textEditField() == "drawingTitle" && canvas->textEditing(),
          "Tab moves to the drawing title");
    typeText("Ground floor plan and south elevation");
    key(Qt::Key_Return);
    {
        const auto &fields = data<DrawingTitleBlockItem>(doc, block).fields;
        check(entries(doc) == before + 1 && lastLabel(doc) == "Edit title block" &&
                  fields.project == "House extension" &&
                  fields.drawingTitle == "Ground floor plan and south elevation",
              "Both fields commit as one entry");
    }
    settle(window);
    auto *info = tray->itemInfo();
    check(info->isVisible(), "Entity info shows the title block");
    auto *date = info->findChild<QLineEdit *>("sheetItemTitle_date");
    auto *revision = info->findChild<QLineEdit *>("sheetItemTitle_revision");
    auto *number = info->findChild<QLineEdit *>("sheetItemSheetNumber");
    check(date && date->isVisible() && revision && number && number->text() == "A-101" &&
              info->findChild<QLabel *>("sheetItemPage")->text() == "1 of 2" &&
              info->findChild<QLabel *>("sheetItemScale")->text() == "1:100 @ A3",
          "Entity info lists the fields and the automatic values");
    before = entries(doc);
    date->setFocus(Qt::OtherFocusReason);
    date->setText("2026-10-13");
    emit date->editingFinished();
    revision->setText("B");
    emit revision->editingFinished();
    check(entries(doc) == before + 2 &&
              data<DrawingTitleBlockItem>(doc, block).fields.revision == "B",
          "Each Entity info field is one entry");
    number->setText("A-102");
    emit number->editingFinished();
    check(lastLabel(doc) == "Change sheet number" &&
              evaluateDrawingItem(doc, block).titleBlock->sheetNumber == "A-102",
          "The sheet number follows the page's number");
    action(window, "edit.undo")->trigger();
    check(evaluateDrawingItem(doc, block).titleBlock->sheetNumber == "A-101", "Undo the number");
    // "+ Page" copies the current page's title block in the same entry.
    before = entries(doc);
    sheets->addPageButton()->click();
    const auto added = sheets->currentSheet();
    check(entries(doc) == before + 1 && lastLabel(doc) == "Add page" && added != f.groundFloor,
          "+ Page adds one page");
    {
        const auto items = drawingItemsOnSheet(doc, added);
        check(items.size() == 1 && item(doc, items[0]).kind() == DrawingItemKind::TitleBlock &&
                  data<DrawingTitleBlockItem>(doc, items[0]).fields ==
                      data<DrawingTitleBlockItem>(doc, block).fields,
              "The new page has a copy of the title block");
        check(evaluateDrawingItem(doc, items[0]).titleBlock->page == "3 of 3" &&
                  evaluateDrawingItem(doc, block).titleBlock->page == "1 of 3" &&
                  evaluateDrawingItem(doc, items[0]).titleBlock->scale == "—",
              "Page counts follow the new page; an empty page has no scale");
    }
    action(window, "edit.undo")->trigger();
    check(doc.drawingSheets().size() == 2, "Undo removes the page and its title block");
    sheets->setCurrentSheet(f.groundFloor);
    settle(window);
    unchanged("Title block");

    // ---- Dimension: clicks with snapping, a typed offset, re-entry, refusal ----------------
    std::cerr << "[annotation] Dimension" << std::endl;
    tool(Qt::Key_D, SheetCanvas::Tool::Dimension, "Dimension");
    check(!measurements->isEnabled() || stepLabel->text().isEmpty() || !stepLabel->isVisible(),
          "No Measurements before two points");
    const auto corner = paperOf(doc, f.plan, {0, 0, .2});
    const auto chimneyCorner = paperOf(doc, f.plan, {8.2, 3, 1});
    // The pointer near a vertex shows the snap.
    hover(canvas, corner + QPointF(.6, -.4));
    check(canvas->anchorMarker() && near(*canvas->anchorMarker(), corner, 1e-6) &&
              canvas->anchorKind() == "Endpoint",
          "The Dimension tool snaps to a model vertex in the view");
    click(canvas, corner + QPointF(.6, -.4));
    check(canvas->itemStep() == 1, "The first point is picked");
    click(canvas, chimneyCorner);
    check(canvas->itemStep() == 2 && stepLabel->text() == "Offset", "The second point is picked");
    hover(canvas, (corner + chimneyCorner) / 2 + QPointF(-3, -8));
    before = entries(doc);
    click(canvas, (corner + chimneyCorner) / 2 + QPointF(-3, -8));
    const auto diagonal = newest(doc, DrawingItemKind::Dimension);
    {
        check(entries(doc) == before + 1 && lastLabel(doc) == "Add dimension",
              "The third click places the dimension");
        const auto evaluation = evaluateDrawingItem(doc, diagonal);
        check(evaluation.state == DrawingItemState::Current && evaluation.valueMetres &&
                  near(*evaluation.valueMetres, std::hypot(8.2, 3), 1e-6) &&
                  evaluation.text == displayLength(*evaluation.valueMetres, doc.displayUnits(),
                                                   doc.displayPrecision())
                                         .toStdString(),
              "The dimension measures the model length at the view's scale in display units: " +
                  evaluation.text);
        check(data<DrawingDimensionItem>(doc, diagonal).offsetMm > 0,
              "The offset follows the pointer");
    }
    // A drag from one point to the other, then a typed offset; re-entry amends.
    const auto far = paperOf(doc, f.plan, {10, 0, .2});
    drag(canvas, corner, far);
    check(canvas->itemStep() == 2, "A drag picks both points");
    before = entries(doc);
    typed("12");
    auto across = newest(doc, DrawingItemKind::Dimension);
    check(entries(doc) == before + 1 &&
              near(data<DrawingDimensionItem>(doc, across).offsetMm, 12) &&
              near(*evaluateDrawingItem(doc, across).valueMetres, 10, 1e-6),
          "A typed offset places the dimension: 10 m");
    typed("15");
    across = newest(doc, DrawingItemKind::Dimension);
    check(entries(doc) == before + 1 && near(data<DrawingDimensionItem>(doc, across).offsetMm, 15),
          "Re-entry amends the offset");
    // A perspective view is refused with a message.
    before = entries(doc);
    click(canvas, {320, 80});
    check(canvas->itemStep() == 0 && entries(doc) == before &&
              status->text().contains("perspective"),
          "Dimension refuses a perspective view: " + status->text().toStdString());
    focusCanvas();
    key(Qt::Key_Escape);
    unchanged("Dimensions");

    // A model edit in the Model workspace updates the dimension.
    std::cerr << "[annotation] Model edit updates a dimension" << std::endl;
    window.setWorkspace(Window::Workspace::Model);
    batch(doc, {QJsonObject{{"command", "geometry.translate"},
                            {"body", QString::number(chimney)},
                            {"delta", QJsonArray{1, 0, 0}}}});
    emit window.viewport()->changed();
    window.setWorkspace(Window::Workspace::Sheets);
    settle(window);
    check(near(*evaluateDrawingItem(doc, diagonal).valueMetres, std::hypot(9.2, 3), 1e-6),
          "The dimension follows the moved geometry");
    {
        const auto shown = canvas->describeItem(diagonal);
        check(shown.contains(QString::fromStdString(evaluateDrawingItem(doc, diagonal).text)),
              "The canvas describes the new value: " + shown.toStdString());
    }
    // Deleting the anchored geometry breaks it; Reattach to the proposed point repairs it.
    std::cerr << "[annotation] Broken dimension and Reattach" << std::endl;
    window.setWorkspace(Window::Workspace::Model);
    batch(doc, {QJsonObject{{"command", "geometry.delete"}, {"body", QString::number(chimney)}}});
    emit window.viewport()->changed();
    window.setWorkspace(Window::Workspace::Sheets);
    settle(window);
    model = modelRecords(doc);
    {
        const auto evaluation = evaluateDrawingItem(doc, diagonal);
        check(evaluation.state == DrawingItemState::Broken &&
                  evaluation.reasons == std::vector<std::string>{"referenceMissing"},
              "Deleting the chimney breaks the dimension");
    }
    tool(Qt::Key_Space, SheetCanvas::Tool::Select, "Select");
    canvas->setSelectedItem(diagonal);
    settle(window);
    auto *reattach = info->findChild<QPushButton *>("sheetItemReattach");
    check(reattach && reattach->isVisible() &&
              info->findChild<QLabel *>("sheetItemStatus")->text() == "Broken: reference missing",
          "Entity info shows the broken state and Reattach");
    check(action(window, "sheet.item.reattach")->isEnabled(), "Reattach is available");
    {
        const auto *page = tray->pages()->topLevelItem(0);
        bool listed = false;
        for (int i = 0; i < page->childCount(); ++i)
            listed |= page->child(i)->data(0, Qt::UserRole + 1).toULongLong() == diagonal &&
                      page->child(i)->text(2) == "Broken" &&
                      page->child(i)->toolTip(1).endsWith("Broken: reference missing");
        check(listed, "Pages lists the broken dimension with its state");
    }
    before = entries(doc);
    reattach->click();
    check(canvas->reattaching() && canvas->reattachPoints().size() == 1 &&
              toolLabel->text() == "Reattach",
          "Reattach shows the proposed point");
    const auto proposed = canvas->reattachPoints().front();
    check(entries(doc) == before, "Proposing commits nothing");
    focusCanvas();
    key(Qt::Key_Return);
    {
        const auto evaluation = evaluateDrawingItem(doc, diagonal);
        check(entries(doc) == before + 1 && lastLabel(doc) == "Reattach dimension" &&
                  evaluation.state == DrawingItemState::Current &&
                  near(at((*evaluation.measuredMm)[1]), proposed, 1e-6) && !canvas->reattaching(),
              "Enter reattaches to the proposed point");
    }
    action(window, "edit.undo")->trigger();
    check(evaluateDrawingItem(doc, diagonal).state == DrawingItemState::Broken,
          "Undo breaks it again");
    // Clicking another anchor chooses it instead.
    canvas->setSelectedItem(diagonal);
    check(canvas->beginReattach(), "Reattach again");
    const auto other = paperOf(doc, f.plan, {10, 8, .2});
    click(canvas, other);
    check(near(canvas->reattachPoints().front(), other, 1e-6), "A click chooses another vertex");
    focusCanvas();
    key(Qt::Key_Return);
    check(near(*evaluateDrawingItem(doc, diagonal).valueMetres, std::hypot(10, 8), 1e-6),
          "The dimension measures to the chosen vertex");
    unchanged("Reattach");

    // ---- Label: a model point in a view, and a typed paper point ---------------------------
    std::cerr << "[annotation] Label" << std::endl;
    tool(Qt::Key_K, SheetCanvas::Tool::Label, "Label");
    const auto tip = paperOf(doc, f.plan, {10, 8, .2});
    click(canvas, tip);
    check(canvas->itemStep() == 1, "The leader's point is picked");
    before = entries(doc);
    click(canvas, {150, 175});
    check(textEditor() != nullptr, "The second click opens the label's editor");
    typeText(QString::fromUtf8("W2 · 1200 × 1350"));
    key(Qt::Key_Escape);
    const auto bound = newest(doc, DrawingItemKind::Label);
    {
        const auto &label = data<DrawingLabelItem>(doc, bound);
        check(entries(doc) == before + 1 && lastLabel(doc) == "Add label" &&
                  label.text == "W2 · 1200 × 1350" &&
                  label.target.kind == DrawingLabelTargetKind::View && label.target.view == f.plan,
              "A label bound to a model point in the view");
        check(near(at(*evaluateDrawingItem(doc, bound).tipMm), tip, 1e-6),
              "The leader ends at the model point");
    }
    typed("[300,150]");
    check(canvas->itemStep() == 1, "A typed point starts a paper label");
    typed("[320,140]");
    check(textEditor() != nullptr, "A second typed point opens the editor");
    typeText("Paper label");
    key(Qt::Key_Return, Qt::ControlModifier);
    const auto paperLabel = newest(doc, DrawingItemKind::Label);
    {
        const auto &label = data<DrawingLabelItem>(doc, paperLabel);
        check(label.target.kind == DrawingLabelTargetKind::Paper &&
                  near(at(label.target.pointMm), {300, 150}) && near(at(label.textAt), {320, 140}),
              "A label pointing at a paper point");
    }
    unchanged("Labels");

    // ---- Selection: move by drag, M, nudge (amended), typed moves ---------------------------
    std::cerr << "[annotation] Move and nudge" << std::endl;
    tool(Qt::Key_Space, SheetCanvas::Tool::Select, "Select");
    {
        const auto box = canvas->itemRect(paperLabel);
        const auto from = canvas->paperToWidget().inverted().map(box.center());
        before = entries(doc);
        drag(canvas, from, from + QPointF(10, 0));
        const auto &label = data<DrawingLabelItem>(doc, paperLabel);
        check(entries(doc) == before + 1 && lastLabel(doc) == "Move label" &&
                  near(label.textAt[0], 330, .01) && near(label.target.pointMm[0], 310, .01),
              "A drag moves the label with its paper point");
    }
    focusCanvas();
    before = entries(doc);
    key(Qt::Key_Right);
    key(Qt::Key_Right);
    key(Qt::Key_Down, Qt::ShiftModifier);
    check(entries(doc) == before + 1 && lastLabel(doc) == "Nudge" &&
              near(data<DrawingLabelItem>(doc, paperLabel).textAt[0], 332, .01) &&
              near(data<DrawingLabelItem>(doc, paperLabel).textAt[1], 150, .01),
          "Consecutive nudges amend one entry");
    typed("<-2,-10>");
    check(lastLabel(doc) == "Move label" &&
              near(at(data<DrawingLabelItem>(doc, paperLabel).textAt), {330, 140}, .01),
          "A typed <dx,dy> moves the item");
    // M picks an item up and drops it.
    tool(Qt::Key_M, SheetCanvas::Tool::Move, "Move");
    before = entries(doc);
    click(canvas, {80, 240});
    click(canvas, {80, 245});
    check(entries(doc) == before + 1 && lastLabel(doc) == "Move line" &&
              near(data<DrawingLineItem>(doc, dragged).from[1], 245, .01),
          "Move picks the line up and drops it");
    // A dimension moves by its offset.
    tool(Qt::Key_Space, SheetCanvas::Tool::Select, "Select");
    canvas->setSelectedItem(across);
    focusCanvas();
    typed("20");
    check(lastLabel(doc) == "Move dimension" &&
              near(data<DrawingDimensionItem>(doc, across).offsetMm, 20),
          "A selected dimension takes its offset");
    unchanged("Moves");

    // ---- Tab cycles views and items in paint order; the accessible children follow ----------
    std::cerr << "[annotation] Tab" << std::endl;
    canvas->setSelectedView(0);
    focusCanvas();
    const auto order = canvas->entries();
    check(order.size() == 2 + drawingItemsOnSheet(doc, f.groundFloor).size(),
          "Entries are the views and the items");
    for (const auto &entry : order) {
        key(Qt::Key_Tab);
        check(entry.item ? canvas->selectedItem() == entry.id : canvas->selectedView() == entry.id,
              "Tab reaches every view and item in paint order");
    }
    key(Qt::Key_Tab, Qt::ShiftModifier);
    check(order[order.size() - 2].item && canvas->selectedItem() == order[order.size() - 2].id,
          "Shift+Tab goes back");
    {
        auto *accessible = QAccessible::queryAccessibleInterface(canvas);
        check(accessible && accessible->childCount() == int(order.size()),
              "The canvas has an accessible child per view and item");
        const auto *last = accessible->child(int(order.size()) - 1);
        check(last && last->text(QAccessible::Name) == canvas->describeItem(order.back().id) &&
                  !last->text(QAccessible::Name).isEmpty(),
              "Item children are named for what they show");
        check(canvas->accessibleDescription() == canvas->describeItem(canvas->selectedItem()),
              "The canvas describes the selected item");
    }

    // ---- Z order: Bring forward and Send backward, from the keyboard and Pages -------------
    std::cerr << "[annotation] Z order" << std::endl;
    canvas->setSelectedItem(notes);
    focusCanvas();
    auto z = [&] { return item(doc, notes).z; };
    const auto start = z();
    before = entries(doc);
    key(Qt::Key_BracketRight, Qt::ControlModifier);
    check(z() == start + 1 && lastLabel(doc) == "Bring forward", "Ctrl+] brings the item forward");
    key(Qt::Key_BracketLeft, Qt::ControlModifier);
    check(z() == start && lastLabel(doc) == "Send backward" && entries(doc) == before + 2,
          "Ctrl+[ sends it backward");
    action(window, "edit.undo")->trigger();
    check(z() == start + 1, "Undo restores the order");
    action(window, "edit.redo")->trigger();
    // Pages: the item rows, selection both ways, Alt+Down and Delete.
    {
        auto *pages = tray->pages();
        auto *page = pages->topLevelItem(0);
        QTreeWidgetItem *row = nullptr;
        for (int i = 0; i < page->childCount(); ++i)
            if (page->child(i)->data(0, Qt::UserRole + 1).toULongLong() == notes)
                row = page->child(i);
        check(row && row->text(1) == "Text · Notes" && row->text(2) == "Current",
              "Pages lists the item with its state");
        check(pages->currentItem() == row, "The selected item's row is current in Pages");
        pages->setFocus(Qt::OtherFocusReason);
        key(Qt::Key_Down, Qt::AltModifier);
        check(QTest::qWaitFor([&] { return z() == start + 1; }) &&
                  lastLabel(doc) == "Bring forward",
              "Alt+Down in Pages brings the item forward: z " + std::to_string(z()) + " from " +
                  std::to_string(start) + ", " + lastLabel(doc).toStdString() + ", focus " +
                  identity(focused()) + ", current " +
                  (pages->currentItem() ? pages->currentItem()->text(1).toStdString() : "none"));
        // The order change rebuilt the tree.
        page = pages->topLevelItem(0);
        pages->setCurrentItem(page);
        canvas->setSelectedItem(0);
        for (int i = 0; i < page->childCount(); ++i)
            if (page->child(i)->data(0, Qt::UserRole + 1).toULongLong() == paperLabel &&
                page->child(i)->data(0, Qt::UserRole).toInt() == 3)
                pages->setCurrentItem(page->child(i));
        check(canvas->selectedItem() == paperLabel, "Choosing a row in Pages selects the item");
    }
    unchanged("Z order");

    // ---- Delete with undo and redo -----------------------------------------------------------
    std::cerr << "[annotation] Delete" << std::endl;
    canvas->setSelectedItem(dragged);
    focusCanvas();
    before = entries(doc);
    key(Qt::Key_Delete);
    check(!doc.drawingItems().contains(dragged) && entries(doc) == before + 1 &&
              lastLabel(doc) == "Delete line" && canvas->selectedItem() == 0,
          "Delete erases the selected item");
    action(window, "edit.undo")->trigger();
    check(doc.drawingItems().contains(dragged), "Undo restores it");
    action(window, "edit.redo")->trigger();
    check(!doc.drawingItems().contains(dragged), "Redo deletes it again");
    canvas->setSelectedItem(paperLabel);
    focusCanvas();
    key(Qt::Key_Backspace);
    check(!doc.drawingItems().contains(paperLabel) && lastLabel(doc) == "Delete label",
          "Backspace erases too");
    unchanged("Delete");

    // ---- Entity info edits per kind --------------------------------------------------------
    std::cerr << "[annotation] Entity info" << std::endl;
    canvas->setSelectedItem(across);
    settle(window);
    auto *orientation = info->findChild<QComboBox *>("sheetItemOrientation");
    check(orientation && orientation->isVisible() &&
              info->findChild<QLabel *>("sheetItemValue")->text() ==
                  QString::fromStdString(evaluateDrawingItem(doc, across).text),
          "Entity info · Dimension shows the value and orientation");
    before = entries(doc);
    orientation->setCurrentIndex(1);
    emit orientation->activated(1);
    check(entries(doc) == before + 1 && lastLabel(doc) == "Change orientation" &&
              data<DrawingDimensionItem>(doc, across).orientation ==
                  DrawingDimensionOrientation::Horizontal,
          "Orientation changes in one entry");
    canvas->setSelectedItem(typedLine);
    settle(window);
    auto *dash = info->findChild<QComboBox *>("sheetItemDash");
    check(dash && dash->isVisible() && !orientation->isVisible(), "Entity info · Line");
    dash->setCurrentIndex(1);
    emit dash->activated(1);
    check(data<DrawingLineItem>(doc, typedLine).dash == DrawingStrokeDash::Dashed,
          "Line style changes");
    unchanged("Entity info");
    check(modelRecords(doc) == model, "No sheet operation touched a model record");
}

void screens(Window &window, const QString &path, const QString &directory, const QString &tag,
             const Fixture &f) {
    auto &doc = window.document();
    window.setWorkspace(Window::Workspace::Model);
    window.openPath(path);
    window.setWorkspace(Window::Workspace::Sheets);
    window.activateWindow();
    (void)QTest::qWaitForWindowActive(&window);
    auto *sheets = window.sheetWorkspace();
    auto *canvas = sheets->canvas();
    sheets->setCurrentSheet(f.groundFloor);
    settle(window);
    QDir().mkpath(directory);
    auto shoot = [&](const QString &name) {
        settle(window);
        QTest::qWait(250);
        QCoreApplication::processEvents();
        check(window.grab().save(directory + "/" + tag + "-" + name + ".png"), "Screenshot saved");
    };
    auto commands = [&](const QJsonArray &list) {
        executeBatch(doc, {{"apiVersion", 1},
                           {"documentId", QString::fromStdString(doc.identity())},
                           {"expectedRevision", QString::number(doc.revision())},
                           {"commands", list}});
        emit window.viewport()->changed();
    };
    const auto slab = bodyNamed(doc, "Slab"), chimney = bodyNamed(doc, "Chimney");
    auto vertexAt = [&](Id body, Vec3 point) {
        const auto world = doc.worldTransform(body);
        for (const auto &[id, p] : doc.bodies().at(body)->surface.vertices)
            if (std::abs(world.point(p).x - point.x) < 1e-9 &&
                std::abs(world.point(p).y - point.y) < 1e-9 &&
                std::abs(world.point(p).z - point.z) < 1e-9)
                return QJsonObject{{"kind", "vertex"},
                                   {"body", QString::number(body)},
                                   {"vertex", QString::number(id)}};
        throw std::runtime_error("Vertex exists");
    };
    // The design's page: dimensions across and down the plan, a leader label, notes and a
    // title block.
    commands(
        {QJsonObject{{"command", "drawing_item.create"},
                     {"sheet", QString::number(f.groundFloor)},
                     {"kind", "dimension"},
                     {"data", QJsonObject{{"view", QString::number(f.plan)},
                                          {"anchors", QJsonArray{vertexAt(slab, {0, 8, .2}),
                                                                 vertexAt(slab, {10, 8, .2})}},
                                          {"offsetMm", 8}}}},
         QJsonObject{{"command", "drawing_item.create"},
                     {"sheet", QString::number(f.groundFloor)},
                     {"kind", "dimension"},
                     {"data", QJsonObject{{"view", QString::number(f.plan)},
                                          {"anchors", QJsonArray{vertexAt(slab, {10, 8, .2}),
                                                                 vertexAt(slab, {10, 0, .2})}},
                                          {"offsetMm", 8}}}},
         QJsonObject{{"command", "drawing_item.create"},
                     {"sheet", QString::number(f.groundFloor)},
                     {"kind", "dimension"},
                     {"data", QJsonObject{{"view", QString::number(f.plan)},
                                          {"anchors", QJsonArray{vertexAt(slab, {0, 0, .2}),
                                                                 vertexAt(chimney, {8.2, 3, 1})}},
                                          {"offsetMm", -6}}}},
         QJsonObject{
             {"command", "drawing_item.create"},
             {"sheet", QString::number(f.groundFloor)},
             {"kind", "label"},
             {"data",
              QJsonObject{{"text", QString::fromUtf8("W2 · 1200 × 1350")},
                          {"textAt", QJsonObject{{"xMm", 186}, {"yMm", 150}}},
                          {"target", QJsonObject{{"kind", "view"},
                                                 {"view", QString::number(f.plan)},
                                                 {"anchor", vertexAt(chimney, {8.8, 3, 1})}}}}}},
         QJsonObject{{"command", "drawing_item.create"},
                     {"sheet", QString::number(f.groundFloor)},
                     {"kind", "text"},
                     {"data", QJsonObject{{"frame", QJsonObject{{"xMm", 240},
                                                                {"yMm", 160},
                                                                {"widthMm", 90},
                                                                {"heightMm", 14}}},
                                          {"text", "Notes\nAll dimensions in mm. Check on site "
                                                   "before ordering."}}}},
         QJsonObject{
             {"command", "drawing_item.create"},
             {"sheet", QString::number(f.groundFloor)},
             {"kind", "titleBlock"},
             {"data",
              QJsonObject{
                  {"frame",
                   QJsonObject{{"xMm", 250}, {"yMm", 257}, {"widthMm", 160}, {"heightMm", 30}}},
                  {"fields", QJsonObject{{"project", "House extension"},
                                         {"drawingTitle", "Ground floor plan and south elevation"},
                                         {"date", "2026-10-13"},
                                         {"revision", "B"}}}}}}});
    const auto block = newest(doc, DrawingItemKind::TitleBlock);
    const auto broken = [&] {
        Id id = 0;
        for (const auto &[item, record] : doc.drawingItems())
            if (const auto *d = std::get_if<DrawingDimensionItem>(&record->data);
                d && d->offsetMm < 0)
                id = item;
        return id;
    }();
    for (const int theme : {1, 2}) {
        action(window, ("view.theme." + std::to_string(theme)).c_str())->trigger();
        const auto suffix = theme == 1 ? QString("light") : QString("dark");
        window.resize(1440, 900);
        sheets->setCurrentSheet(f.groundFloor);
        canvas->fitPage();
        canvas->setSelectedItem(block);
        shoot("page-items-" + suffix);
        // The Dimension tool mid-pick: the first point placed, the pointer snapped to a vertex.
        canvas->setFocus(Qt::OtherFocusReason);
        action(window, "sheet.tool.dimension")->trigger();
        click(canvas, paperOf(doc, f.plan, {0, 0, .2}));
        hover(canvas, paperOf(doc, f.plan, {10, 0, .2}) + QPointF(.5, .5));
        shoot("dimension-snap-" + suffix);
        QTest::keyClick(canvas, Qt::Key_Escape);
        action(window, "sheet.tool.select")->trigger();
        // Text edited in place.
        Id notes = newest(doc, DrawingItemKind::Text);
        canvas->setSelectedItem(notes);
        canvas->setFocus(Qt::OtherFocusReason);
        QTest::keyClick(canvas, Qt::Key_Return);
        auto *editor = canvas->textEditor();
        check(editor != nullptr, "The text edits in place");
        QTest::keyClick(editor, Qt::Key_End);
        QTest::keyClicks(editor, " Verify");
        shoot("text-editing-" + suffix);
        QTest::keyClick(editor, Qt::Key_Escape);
        action(window, "edit.undo")->trigger();
        // A broken dimension and its Reattach proposal.
        window.setWorkspace(Window::Workspace::Model);
        commands({QJsonObject{{"command", "geometry.delete"}, {"body", QString::number(chimney)}}});
        window.setWorkspace(Window::Workspace::Sheets);
        settle(window);
        canvas->setSelectedItem(broken);
        canvas->setFocus(Qt::OtherFocusReason);
        canvas->beginReattach();
        shoot("reattach-" + suffix);
        QTest::keyClick(canvas, Qt::Key_Escape);
        action(window, "edit.undo")->trigger();
        window.setWorkspace(Window::Workspace::Sheets);
        settle(window);
    }
}
} // namespace
int main(int argc, char **argv) {
    QTemporaryDir files;
    if (!files.isValid())
        return 2;
    qputenv("XDG_CONFIG_HOME", files.path().toUtf8());
    qputenv("XDG_DATA_HOME", files.path().toUtf8());
    qputenv("SKETCHYUP_SHEETS_WORKSPACE", "1");
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
        // An unexpected modal fails the run instead of blocking it.
        QTimer guard;
        std::string unexpected;
        QObject::connect(&guard, &QTimer::timeout, [&] {
            if (auto *box = qobject_cast<QMessageBox *>(QApplication::activeModalWidget())) {
                unexpected = box->text().toStdString();
                std::cerr << "Unexpected message box: " << unexpected << std::endl;
                box->reject();
            }
        });
        guard.start(500);
        Window window;
        window.resize(1440, 900);
        window.show();
        check(QTest::qWaitForWindowExposed(&window), "Window exposed with the Sheets flag");
        check(QTest::qWaitFor([&] { return window.viewport()->rendererReady(); }, 10000),
              "Viewport renderer initialized in the Model workspace");
        const auto arguments = app.arguments();
        if (const auto at = arguments.indexOf("--screens"); at >= 0 && at + 2 < arguments.size()) {
            screens(window, path, arguments[at + 1], arguments[at + 2], fixture);
            std::cout << "Sheet annotation screenshots written\n";
            window.document().markSaved();
            return 0;
        }
        annotate(window, path, fixture);
        check(unexpected.empty(), "No unexpected message box: " + unexpected);
        window.document().markSaved();
        std::cout << "Sheet annotations: Line, Rectangle, Text, Label and Dimension by pointer and "
                     "typed values, in-place editing (Enter, Esc, Ctrl+Enter, click outside), "
                     "title block fields, + Page copy, dimension update after a model edit, "
                     "broken dimension and Reattach, perspective refusal, z order, Tab, "
                     "Delete with undo and redo, Pages tree and Entity info passed; model "
                     "records untouched\n";
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
