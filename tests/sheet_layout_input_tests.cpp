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
#include <QDialog>
#include <QDir>
#include <QJsonArray>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QRadioButton>
#include <QTemporaryDir>
#include <QTest>
#include <QTimer>
#include <QWheelEvent>
#include <functional>
#include <iostream>
// R091.f: model-view layout and editing in the Sheets workspace (ADR 0164 §§4, 5; design §§3,
// 5, 7, 8). Pointer input is sent to the canvas as real mouse events (with the modifiers each
// step holds); keys go to the focused widget. After every page operation the model's records
// are pointer-identical: page editing never modifies model geometry.
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
    Id groundPlan{}, southScene{}, northScene{}, gardenScene{};
};
// The R091.e drawing set: A-101 holds a current plan and a changed south elevation, A-201 the
// north elevation (broken once its scene is deleted) and a perspective garden view; A-301 and
// A-501 are empty.
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
    Fixture f;
    f.groundPlan = createScene(doc, "Ground plan", camera(0, 90, true, {5, 4, 0}));
    f.southScene = createScene(doc, "South elevation", camera(-90, 0, true, {5, 4, 1.4}));
    f.northScene = createScene(doc, "North elevation", camera(90, 0, true, {5, 4, 1.4}));
    f.gardenScene = createScene(doc, "Garden view", camera(-60, 25, false, {5, 4, 1}));
    f.groundFloor = createDrawingSheet(doc, "Ground floor", {420, 297}, "A-101", 10);
    f.elevations = createDrawingSheet(doc, "Elevations", {420, 297}, "A-201", 10);
    f.sections = createDrawingSheet(doc, "Sections", {420, 297}, "A-301", 10);
    f.schedule = createDrawingSheet(doc, "Window schedule", {420, 297}, "A-501", 10);
    f.plan = createDrawingView(
        doc, viewRecord(f.groundFloor, f.groundPlan, "Ground floor plan", {25, 24, 197, 196}));
    f.south = createDrawingView(
        doc, viewRecord(f.groundFloor, f.southScene, "South elevation", {240, 24, 155, 90}, 100));
    f.north = createDrawingView(
        doc, viewRecord(f.elevations, f.northScene, "North elevation", {25, 24, 176, 112}));
    auto perspective = viewRecord(f.elevations, f.gardenScene, "Garden view", {230, 24, 165, 112});
    perspective.content = DrawingViewContent::RasterAppearance;
    f.garden = createDrawingView(doc, perspective);
    batch(doc, {QJsonObject{{"command", "drawing_view.accept"}, {"all", true}}});
    box(8.2, 3, .6, .6, 4, "Chimney");
    batch(doc,
          {QJsonObject{{"command", "drawing_view.accept"}, {"view", QString::number(f.plan)}}});
    batch(doc,
          {QJsonObject{{"command", "drawing_view.accept"}, {"view", QString::number(f.garden)}}});
    saveDocument(doc, path);
    return f;
}
// Model records: bodies, definitions, instances, tags, materials and sections by pointer.
struct ModelRecords {
    std::map<Id, const void *> bodies;
    std::map<Id, const void *> scenes;
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
    QTest::qWait(200);
    QCoreApplication::processEvents();
}
size_t entries(const Document &doc) { return doc.history(0, 1).position; }
QString labelAt(const Document &doc, size_t index) {
    return QString::fromStdString(doc.history(index, 1).entries.front().label);
}
QString lastLabel(const Document &doc) {
    const auto cursor = entries(doc);
    return cursor ? labelAt(doc, cursor - 1) : QString{};
}
const DrawingViewRecord &view(Window &window, Id id) {
    check(window.document().drawingViews().contains(id), "View exists: " + std::to_string(id));
    return *window.document().drawingViews().at(id);
}
QPointF target(const DrawingViewRecord &record) {
    return {record.frame.xMm + record.frame.widthMm / 2 + record.offsetMm[0],
            record.frame.yMm + record.frame.heightMm / 2 + record.offsetMm[1]};
}
bool near(double a, double b, double tolerance = 1e-6) { return std::abs(a - b) <= tolerance; }
// Real mouse events with the modifiers held at each step.
void mouse(QWidget *widget, QEvent::Type type, QPointF position, Qt::MouseButton button,
           Qt::MouseButtons buttons, Qt::KeyboardModifiers modifiers) {
    QMouseEvent event(type, position, widget->mapToGlobal(position), button, buttons, modifiers);
    QCoreApplication::sendEvent(widget, &event);
    QCoreApplication::processEvents();
}
void press(SheetCanvas *canvas, QPointF at, Qt::KeyboardModifiers modifiers = {}) {
    mouse(canvas, QEvent::MouseButtonPress, at, Qt::LeftButton, Qt::LeftButton, modifiers);
}
void moveTo(SheetCanvas *canvas, QPointF from, QPointF to, Qt::KeyboardModifiers modifiers = {},
            Qt::MouseButtons buttons = Qt::LeftButton) {
    for (int step = 1; step <= 4; ++step)
        mouse(canvas, QEvent::MouseMove, from + (to - from) * step / 4., Qt::NoButton, buttons,
              modifiers);
}
void release(SheetCanvas *canvas, QPointF at, Qt::KeyboardModifiers modifiers = {}) {
    mouse(canvas, QEvent::MouseButtonRelease, at, Qt::LeftButton, Qt::NoButton, modifiers);
}
void drag(SheetCanvas *canvas, QPointF from, QPointF to, Qt::KeyboardModifiers modifiers = {}) {
    press(canvas, from, modifiers);
    moveTo(canvas, from, to, modifiers);
    release(canvas, to, modifiers);
}
// Integer logical pixels for a paper point, as a pointer delivers them.
QPointF pixel(SheetCanvas *canvas, QPointF paperMm) {
    const auto point = canvas->widgetPoint(paperMm);
    return {std::round(point.x()), std::round(point.y())};
}
QPointF paperAt(SheetCanvas *canvas, QPointF widget) {
    return canvas->paperToWidget().inverted().map(widget);
}
// Runs `body` on the named dialog once it shows (a modal dialog blocks its opener).
class DialogWatch {
  public:
    DialogWatch(QString name, std::function<void(QDialog &)> body)
        : name_(std::move(name)), body_(std::move(body)) {
        QObject::connect(&timer_, &QTimer::timeout, [this] {
            for (auto *widget : QApplication::topLevelWidgets())
                if (auto *dialog = qobject_cast<QDialog *>(widget);
                    dialog && dialog->isVisible() && dialog->objectName() == name_) {
                    timer_.stop();
                    seen_ = true;
                    try {
                        body_(*dialog);
                    } catch (...) {
                        failure_ = std::current_exception();
                        dialog->reject();
                    }
                    return;
                }
        });
        timer_.start(20);
    }
    void finish() {
        check(QTest::qWaitFor([&] { return seen_; }, 5000), "Dialog shown: " + name_.toStdString());
        if (failure_)
            std::rethrow_exception(failure_);
    }

  private:
    QString name_;
    std::function<void(QDialog &)> body_;
    QTimer timer_;
    bool seen_{};
    std::exception_ptr failure_;
};
QPushButton *buttonText(QDialog &dialog, const QString &text) {
    for (auto *button : dialog.findChildren<QPushButton *>())
        if (button->text().remove('&') == text)
            return button;
    throw std::runtime_error("Dialog button: " + text.toStdString());
}

void layout(Window &window, const QString &path, const Fixture &f) {
    auto &doc = window.document();
    auto *sheets = window.sheetWorkspace();
    auto *canvas = sheets->canvas();
    auto *tray = window.sheetsTray();
    auto *cache = window.sheetEvaluations();
    auto *status = window.findChild<QLabel *>("statusMessage");
    auto *toolLabel = window.findChild<QLabel *>("activeToolLabel");
    auto *stepLabel = window.findChild<QLabel *>("measurementStep");
    auto *measurements = window.findChild<QLineEdit *>("measurements");
    check(toolLabel && stepLabel && measurements, "Status bar widgets exist");
    window.activateWindow();
    check(QTest::qWaitForWindowActive(&window), "Window active");
    window.openPath(path);
    window.setWorkspace(Window::Workspace::Sheets);
    settle(window);
    // Opening shows the file-operation surface; under Xvfb (no window manager) the window may
    // not get activation back by itself. Opening is preparation here, not under test.
    window.activateWindow();
    check(QTest::qWaitForWindowActive(&window), "Window active after opening the drawing set");
    const auto model = modelRecords(doc);
    const auto initialViews = doc.drawingViews();
    const auto initialEntries = entries(doc);
    auto unchanged = [&](const std::string &what) {
        check(modelRecords(doc) == model, what + " leaves model records untouched");
    };

    // ---- Model view tool: typed size, perspective as not to scale
    std::cerr << "[layout] Model view tool: typed size, perspective as not to scale" << std::endl;
    sheets->setCurrentSheet(f.sections);
    settle(window);
    canvas->setFocus(Qt::OtherFocusReason);
    expectFocus(canvas, "Canvas focus");
    key(Qt::Key_W);
    check(canvas->tool() == SheetCanvas::Tool::View && toolLabel->text() == "Model view",
          "W chooses the Model view tool");
    check(status->text() == "Drag the frame, or type width,height · Shift keeps the view's "
                            "proportion · Alt ignores snapping · Esc cancels",
          "Model view instruction: " + status->text().toStdString());
    check(stepLabel->isVisible() && stepLabel->text() == "Size" && measurements->isEnabled(),
          "The Measurements box is labelled Size");
    auto *panel = tray->placePanel();
    check(panel->isVisible() && !window.findChild<QWidget *>("sheetInfoSection")->isVisible(),
          "The New model view panel replaces the Tray sections");
    check(
        panel->findChild<QComboBox *>("sheetPlaceModel")->currentText().endsWith("(this model)") &&
            panel->findChild<QComboBox *>("sheetPlaceModel")->count() == 1,
        "The model select offers this model only");
    {
        QStringList listed;
        for (auto *button : panel->scenes()->buttons())
            listed << button->accessibleName();
        check(listed == QStringList{"Ground plan, Top", "South elevation, Front",
                                    "North elevation, Back", "Garden view, Perspective"},
              "The scene list shows each scene's projection: " + listed.join(" | ").toStdString());
        check(panel->scaleChoice()->currentText() == "1:50",
              "An empty page proposes 1:50: " + panel->scaleChoice()->currentText().toStdString());
    }
    // Perspective: "Garden view" places as not to scale with rendered content.
    panel->setScene(f.gardenScene);
    check(!panel->scaleChoice()->isEnabled() &&
              panel->findChild<QLabel *>("sheetPlaceStyle")->text() == "From scene · Rendered",
          "A perspective scene has no scale to choose");
    canvas->setFocus(Qt::OtherFocusReason);
    const auto hover = pixel(canvas, {200, 150});
    mouse(canvas, QEvent::MouseMove, hover, Qt::NoButton, Qt::NoButton, {});
    const auto before = entries(doc);
    typeText("160,120");
    check(measurements->hasFocus() && measurements->text() == "160,120",
          "Typing a size goes to the Measurements box");
    key(Qt::Key_Return);
    check(entries(doc) == before + 1 && lastLabel(doc) == "Place view",
          "Typed width,height places one view");
    const auto gardenCopy = canvas->selectedView();
    {
        const auto &placed = view(window, gardenCopy);
        const auto at = paperAt(canvas, hover);
        check(placed.name == "Garden view 2" && placed.source.scene == f.gardenScene &&
                  near(placed.frame.widthMm, 160) && near(placed.frame.heightMm, 120) &&
                  near(placed.frame.xMm, at.x(), .051) && near(placed.frame.yMm, at.y(), .051) &&
                  placed.content == DrawingViewContent::RasterAppearance,
              "The typed view is 160 × 120 mm at the pointer, named uniquely");
    }
    unchanged("Place view");
    settle(window);
    check(cache->status(gardenCopy) && cache->status(gardenCopy)->notToScale &&
              canvas->describeView(gardenCopy).contains("Not to scale"),
          "The perspective view places as not to scale");

    // ---- Drag a frame with snapping; re-entry amends the size
    std::cerr << "[layout] Drag a frame with snapping; re-entry amends the size" << std::endl;
    panel->setScene(f.southScene);
    check(panel->scaleChoice()->isEnabled() &&
              panel->findChild<QLabel *>("sheetPlaceTitle")->text() == "2 · South elevation 2",
          "The panel titles the next view: " +
              panel->findChild<QLabel *>("sheetPlaceTitle")->text().toStdString());
    key(Qt::Key_Escape);
    check(canvas->tool() == SheetCanvas::Tool::Select, "Esc with nothing anchored leaves W");
    key(Qt::Key_W);
    check(tray->placePanel()->scene() == f.southScene, "The panel keeps the chosen scene");
    {
        // The pointer lands 0.3–0.4 mm from grid points; the 5 mm grid takes both corners.
        const auto from = pixel(canvas, {30.3, 180.4}), to = pixel(canvas, {130.2, 250.3});
        press(canvas, from);
        check(canvas->operationActive() && canvas->snapHint() == "Snapped to the 5 mm grid",
              "The first corner snaps to the grid: " + canvas->snapHint().toStdString());
        moveTo(canvas, from, to);
        const auto frame = canvas->previewFrame();
        check(frame && near(frame->left(), 30) && near(frame->top(), 180) &&
                  near(frame->width(), 100) && near(frame->height(), 70),
              "The preview frame snaps to the grid");
        check(measurements->text() == "100,70 mm",
              "The box shows the live size: " + measurements->text().toStdString());
        check(entries(doc) == before + 1, "Dragging commits nothing");
        // Alt ignores snapping while held.
        mouse(canvas, QEvent::MouseMove, to, Qt::NoButton, Qt::LeftButton, Qt::AltModifier);
        check(canvas->snapHint().isEmpty() && !near(canvas->previewFrame()->width(), 100, 1e-3),
              "Alt ignores snapping");
        mouse(canvas, QEvent::MouseMove, to, Qt::NoButton, Qt::LeftButton, {});
        release(canvas, to);
    }
    auto placed = canvas->selectedView();
    check(entries(doc) == before + 2 && lastLabel(doc) == "Place view" && placed != gardenCopy,
          "Releasing the drag places the view");
    {
        const auto &record = view(window, placed);
        check(record.frame == DrawingFrame{30, 180, 100, 70} && record.scaleDenominator == 50 &&
                  record.name == "South elevation 2" &&
                  record.content == DrawingViewContent::TechnicalLines,
              "Dragged frame, page scale and title");
    }
    unchanged("Place by drag");
    canvas->setFocus(Qt::OtherFocusReason);
    const auto viewCount = doc.drawingViews().size();
    typeText("120,80");
    key(Qt::Key_Return);
    // The amended entry creates the view again; allocators never rewind, so its ID is new.
    placed = canvas->selectedView();
    check(entries(doc) == before + 2 && doc.drawingViews().size() == viewCount &&
              view(window, placed).frame == DrawingFrame{30, 180, 120, 80},
          "Typing a new size amends the placement");
    check(lastLabel(doc) == "Place view", "The amended entry keeps its label");
    key(Qt::Key_Z, Qt::ControlModifier);
    check(!doc.drawingViews().contains(placed), "Undo removes the placed view in one step");
    action(window, "edit.redo")->trigger();
    check(doc.drawingViews().contains(placed) &&
              view(window, placed).frame == DrawingFrame{30, 180, 120, 80},
          "Redo places it again at the amended size");
    // Click–move–click is the same as a drag.
    {
        canvas->setFocus(Qt::OtherFocusReason);
        key(Qt::Key_Escape);
        key(Qt::Key_W);
        const auto from = pixel(canvas, {250, 180}), to = pixel(canvas, {350, 260});
        press(canvas, from);
        release(canvas, from);
        check(canvas->operationActive(), "A click anchors the first corner");
        moveTo(canvas, from, to, {}, Qt::NoButton);
        press(canvas, to);
        release(canvas, to);
        check(entries(doc) == before + 3 &&
                  view(window, canvas->selectedView()).frame == DrawingFrame{250, 180, 100, 80},
              "The second click places the frame");
        key(Qt::Key_Z, Qt::ControlModifier);
        check(entries(doc) == before + 2, "Undo the click-placed view");
        // Esc cancels an anchored frame without an entry.
        canvas->setFocus(Qt::OtherFocusReason);
        press(canvas, from);
        release(canvas, from);
        key(Qt::Key_Escape);
        check(!canvas->operationActive() && canvas->tool() == SheetCanvas::Tool::View &&
                  entries(doc) == before + 2,
              "Esc cancels the frame, then a second Esc leaves the tool");
        key(Qt::Key_Escape);
        check(canvas->tool() == SheetCanvas::Tool::Select && !panel->isVisible(),
              "Leaving W restores the Tray sections");
    }

    // ---- Selection, handles, move, snapping and nudge
    std::cerr << "[layout] Selection, handles, move, snapping and nudge" << std::endl;
    sheets->setCurrentSheet(f.groundFloor);
    settle(window);
    canvas->setFocus(Qt::OtherFocusReason);
    auto click = [&](QPointF at) {
        press(canvas, at);
        release(canvas, at);
    };
    click(canvas->viewRect(f.plan).center());
    check(canvas->selectedView() == f.plan, "Click selects the plan");
    check(status->text() == "Drag a handle to crop · Arrows nudge 1 mm · Shift+Arrows 10 mm · "
                            "Alt ignores snapping · U accepts a changed view",
          "Select instruction with a view: " + status->text().toStdString());
    check(stepLabel->text() == "Scale" && measurements->text() == "1:50",
          "The Measurements box shows the scale");
    {
        // Moving the south elevation 2.5 mm down: its top snaps back to the plan's top edge.
        const auto south = view(window, f.south);
        const auto from = canvas->viewRect(f.south).center();
        const auto to = from + QPointF(0, std::round(2.5 * canvas->pixelsPerMm()));
        press(canvas, from);
        moveTo(canvas, from, to);
        check(canvas->snapHint() == "Snapped to the top edge of view 1 and the 5 mm grid",
              "Snap hint: " + canvas->snapHint().toStdString());
        check(canvas->previewFrame() && near(canvas->previewFrame()->top(), 24) &&
                  near(canvas->previewFrame()->left(), 240),
              "The moved frame snaps to view 1's top edge and the grid");
        // Alt: the same drag does not snap.
        mouse(canvas, QEvent::MouseMove, to, Qt::NoButton, Qt::LeftButton, Qt::AltModifier);
        check(canvas->snapHint().isEmpty() && canvas->previewFrame()->top() > 25,
              "Alt drags without snapping");
        mouse(canvas, QEvent::MouseMove, to + QPointF(0, 1), Qt::NoButton, Qt::LeftButton, {});
        mouse(canvas, QEvent::MouseMove, to, Qt::NoButton, Qt::LeftButton, {});
        const auto entriesBefore = entries(doc);
        release(canvas, to);
        check(entries(doc) == entriesBefore && view(window, f.south).frame == south.frame,
              "A move that snaps back to where it started commits nothing");
    }
    {
        const auto plan = view(window, f.plan);
        const auto from = canvas->viewRect(f.plan).center();
        const auto to = from + QPointF(std::round(20 * canvas->pixelsPerMm()), 0);
        drag(canvas, from, to, Qt::AltModifier);
        const auto &moved = view(window, f.plan);
        check(lastLabel(doc) == "Move view" && moved.frame.xMm > plan.frame.xMm + 18 &&
                  moved.frame.yMm == plan.frame.yMm && moved.frame.widthMm == plan.frame.widthMm &&
                  moved.scaleDenominator == 50 && moved.offsetMm == plan.offsetMm,
              "Dragging the body moves the view; scale and offset stay");
        unchanged("Move view");
        key(Qt::Key_Z, Qt::ControlModifier);
        check(view(window, f.plan).frame == plan.frame, "Undo the move");
        canvas->setFocus(Qt::OtherFocusReason);
    }
    {
        // Arrows nudge 1 mm, Shift+Arrows 10 mm; consecutive nudges amend one entry.
        canvas->setSelectedView(f.plan);
        const auto plan = view(window, f.plan);
        const auto entriesBefore = entries(doc);
        key(Qt::Key_Right);
        key(Qt::Key_Right);
        key(Qt::Key_Right);
        key(Qt::Key_Down, Qt::ShiftModifier);
        check(entries(doc) == entriesBefore + 1 && lastLabel(doc) == "Nudge" &&
                  near(view(window, f.plan).frame.xMm, plan.frame.xMm + 3) &&
                  near(view(window, f.plan).frame.yMm, plan.frame.yMm + 10),
              "Four nudges are one Nudge entry");
        unchanged("Nudge");
        key(Qt::Key_Z, Qt::ControlModifier);
        check(view(window, f.plan).frame == plan.frame, "One undo reverts every nudge");
        action(window, "edit.redo")->trigger();
        check(near(view(window, f.plan).frame.xMm, plan.frame.xMm + 3), "Redo the nudges");
        key(Qt::Key_Z, Qt::ControlModifier);
        canvas->setFocus(Qt::OtherFocusReason);
    }
    {
        // Crop with the bottom-right handle: the frame shrinks, the drawing and scale stay.
        canvas->setSelectedView(f.plan);
        const auto plan = view(window, f.plan);
        const auto handle = canvas->viewRect(f.plan).bottomRight();
        const auto to = handle - QPointF(std::round(40 * canvas->pixelsPerMm()),
                                         std::round(30 * canvas->pixelsPerMm()));
        drag(canvas, handle, to);
        const auto &cropped = view(window, f.plan);
        check(lastLabel(doc) == "Crop view" && cropped.frame.xMm == plan.frame.xMm &&
                  cropped.frame.yMm == plan.frame.yMm &&
                  cropped.frame.widthMm < plan.frame.widthMm - 30 &&
                  cropped.scaleDenominator == plan.scaleDenominator,
              "The handle crops the frame; the scale never changes");
        check(std::fmod(cropped.frame.xMm + cropped.frame.widthMm, 5) < 1e-6 ||
                  std::abs(std::fmod(cropped.frame.xMm + cropped.frame.widthMm, 5) - 5) < 1e-6,
              "The dragged corner snaps to the grid");
        check(near(target(cropped).x(), target(plan).x()) &&
                  near(target(cropped).y(), target(plan).y()),
              "Cropping keeps the drawing where it is on paper");
        unchanged("Crop view");
        key(Qt::Key_Z, Qt::ControlModifier);
        check(view(window, f.plan).frame == plan.frame &&
                  view(window, f.plan).offsetMm == plan.offsetMm,
              "Undo the crop");
        canvas->setFocus(Qt::OtherFocusReason);
    }

    // ---- Tab cycles the views; the canvas keeps focus
    std::cerr << "[layout] Tab cycles the views; the canvas keeps focus" << std::endl;
    key(Qt::Key_Escape);
    check(canvas->selectedView() == 0, "Esc clears the selection");
    key(Qt::Key_Tab);
    check(canvas->selectedView() == f.plan && focused() == canvas, "Tab selects view 1");
    key(Qt::Key_Tab);
    check(canvas->selectedView() == f.south && focused() == canvas, "Tab selects view 2");
    key(Qt::Key_Tab);
    check(canvas->selectedView() == f.plan, "Tab wraps to view 1");
    key(Qt::Key_Backtab, Qt::ShiftModifier);
    check(canvas->selectedView() == f.south, "Shift+Tab goes back");
    check(QAccessible::queryAccessibleInterface(canvas)
              ->text(QAccessible::Description)
              .startsWith("View 2 · South elevation"),
          "The canvas announces the Tab-selected view");

    // ---- U accepts the selected view; Accept all
    std::cerr << "[layout] U accepts the selected view; Accept all" << std::endl;
    settle(window);
    check(cache->status(f.south)->state == DrawingViewState::Stale, "The south view is changed");
    check(action(window, "sheet.view.accept")->isEnabled(), "Accept view is available");
    key(Qt::Key_U);
    check(lastLabel(doc) == "Accept 1 view", "U accepts the selected view");
    settle(window);
    check(cache->status(f.south)->state == DrawingViewState::Current,
          "The accepted view is current");
    unchanged("Accept view");
    key(Qt::Key_Z, Qt::ControlModifier);
    settle(window);
    check(cache->status(f.south)->state == DrawingViewState::Stale, "Undo un-accepts");
    action(window, "sheet.view.acceptAll")->trigger();
    check(lastLabel(doc).startsWith("Accept "),
          "Accept all is one entry: " + lastLabel(doc).toStdString());
    settle(window);
    check(cache->status(f.south)->state == DrawingViewState::Current &&
              cache->status(placed)->state == DrawingViewState::Current,
          "Accept all accepts every changed view");
    key(Qt::Key_Z, Qt::ControlModifier);
    settle(window);
    canvas->setFocus(Qt::OtherFocusReason);

    // ---- Entity info: title, scale, style, each one entry
    std::cerr << "[layout] Entity info: title, scale, style, each one entry" << std::endl;
    canvas->setSelectedView(f.plan);
    {
        auto *title = window.findChild<QLineEdit *>("sheetInfoTitle");
        auto *scale = window.findChild<QComboBox *>("sheetInfoScale");
        auto *style = window.findChild<QComboBox *>("sheetInfoStyle");
        check(title && scale && style && title->text() == "Ground floor plan" &&
                  scale->currentText() == "1:50" && style->currentText() == "Hidden line",
              "Entity info shows editable title, scale and style");
        const auto entriesBefore = entries(doc);
        title->setFocus(Qt::TabFocusReason);
        expectFocus(title, "Title field");
        title->selectAll();
        typeText("Plan level 0");
        key(Qt::Key_Return);
        check(QTest::qWaitFor([&] { return view(window, f.plan).name == "Plan level 0"; }),
              "Enter renames the view");
        check(entries(doc) == entriesBefore + 1 && lastLabel(doc) == "Rename view",
              "Rename is one entry");
        scale->setFocus(Qt::TabFocusReason);
        expectFocus(scale, "Scale field");
        scale->lineEdit()->selectAll();
        typeText("1:100");
        key(Qt::Key_Return);
        check(QTest::qWaitFor([&] { return view(window, f.plan).scaleDenominator == 100; }),
              "A typed 1:N sets the scale");
        check(entries(doc) == entriesBefore + 2 && lastLabel(doc) == "Change scale",
              "Scale is one entry");
        key(Qt::Key_Down);
        check(QTest::qWaitFor([&] { return view(window, f.plan).scaleDenominator == 200; }),
              "Down picks the next scale from the list");
        style->setFocus(Qt::TabFocusReason);
        expectFocus(style, "Style field");
        key(Qt::Key_Down);
        check(QTest::qWaitFor([&] { return view(window, f.plan).hiddenLines; }) &&
                  lastLabel(doc) == "Change style",
              "The style select changes the content in one entry");
        unchanged("Entity info edits");
        for (int i = 0; i < 4; ++i)
            action(window, "edit.undo")->trigger();
        check(view(window, f.plan).name == "Ground floor plan" &&
                  view(window, f.plan).scaleDenominator == 50 && !view(window, f.plan).hiddenLines,
              "Undo restores each Entity info edit");
    }

    // ---- Measurements: Scale, size and move, with re-entry
    std::cerr << "[layout] Measurements: Scale, size and move, with re-entry" << std::endl;
    canvas->setFocus(Qt::OtherFocusReason);
    canvas->setSelectedView(f.plan);
    {
        const auto plan = view(window, f.plan);
        const auto entriesBefore = entries(doc);
        typeText("1:100");
        key(Qt::Key_Return);
        check(view(window, f.plan).scaleDenominator == 100 && lastLabel(doc) == "Change scale",
              "1:100 sets the scale");
        typeText("1:20");
        key(Qt::Key_Return);
        check(view(window, f.plan).scaleDenominator == 20 && entries(doc) == entriesBefore + 1,
              "Re-entry amends the scale");
        typeText("<10,5>");
        key(Qt::Key_Return);
        check(near(view(window, f.plan).frame.xMm, plan.frame.xMm + 10) &&
                  near(view(window, f.plan).frame.yMm, plan.frame.yMm + 5) &&
                  lastLabel(doc) == "Move view" && entries(doc) == entriesBefore + 2,
              "<dx,dy> moves the view");
        typeText("[50,40]");
        key(Qt::Key_Return);
        check(view(window, f.plan).frame.xMm == 50 && view(window, f.plan).frame.yMm == 40 &&
                  entries(doc) == entriesBefore + 2,
              "[x,y] re-enters the move with an absolute corner");
        typeText("150,100");
        key(Qt::Key_Return);
        check(view(window, f.plan).frame.widthMm == 150 &&
                  view(window, f.plan).frame.heightMm == 100 && lastLabel(doc) == "Crop view" &&
                  view(window, f.plan).scaleDenominator == 20,
              "w,h sets the crop size and keeps the scale");
        typeText("5,x");
        key(Qt::Key_Return);
        check(measurements->property("invalid").toBool() && !status->text().isEmpty() &&
                  entries(doc) == entriesBefore + 3,
              "Invalid input is refused and marked");
        key(Qt::Key_Escape);
        unchanged("Typed edits");
        for (int i = 0; i < 3; ++i)
            action(window, "edit.undo")->trigger();
        check(view(window, f.plan) == plan, "Undo restores the typed edits");
    }
    {
        // The Move tool: a drag, then a typed distance revises it along the same direction.
        canvas->setFocus(Qt::OtherFocusReason);
        key(Qt::Key_M);
        check(canvas->tool() == SheetCanvas::Tool::Move && toolLabel->text() == "Move",
              "M chooses Move");
        const auto south = view(window, f.south);
        const auto from = canvas->viewRect(f.south).center();
        drag(canvas, from, from + QPointF(0, std::round(30 * canvas->pixelsPerMm())),
             Qt::AltModifier);
        check(lastLabel(doc) == "Move view" && view(window, f.south).frame.yMm > south.frame.yMm,
              "Move drags the view");
        const auto entriesBefore = entries(doc);
        typeText("40");
        key(Qt::Key_Return);
        check(near(view(window, f.south).frame.yMm, south.frame.yMm + 40, 1e-6) &&
                  near(view(window, f.south).frame.xMm, south.frame.xMm, 1e-6) &&
                  entries(doc) == entriesBefore,
              "A typed distance amends the move along its direction");
        action(window, "edit.undo")->trigger();
        check(view(window, f.south).frame == south.frame, "Undo the move");
        canvas->setFocus(Qt::OtherFocusReason);
        key(Qt::Key_Escape);
        check(canvas->tool() == SheetCanvas::Tool::Select, "Esc leaves Move");
    }

    // ---- Edit-view: pan changes the offset, zoom the crop; Esc leaves
    std::cerr << "[layout] Edit-view: pan changes the offset, zoom the crop; Esc leaves"
              << std::endl;
    canvas->setSelectedView(f.plan);
    canvas->setFocus(Qt::OtherFocusReason);
    {
        const auto plan = view(window, f.plan);
        key(Qt::Key_Return);
        check(canvas->editView() == f.plan && canvas->editBanner()->isVisible(),
              "Enter opens edit-view with its banner");
        check(sheets->breadcrumb()->text().contains("A-101") &&
                  sheets->breadcrumb()->text().contains("View 1 · Ground floor plan"),
              "Breadcrumb: Sheets › A-101 › View 1 · Ground floor plan: " +
                  sheets->breadcrumb()->text().toStdString());
        check(toolLabel->text() == "Edit view" && stepLabel->text() == "Size", "Edit-view status");
        {
            const auto image = canvas->grab().toImage();
            const auto ratio = canvas->devicePixelRatioF();
            const auto outside = canvas->widgetPoint({300, 250}) * ratio;
            check(QColor(image.pixel(outside.toPoint())) != QColor(Qt::white),
                  "Everything outside the view dims");
        }
        const auto entriesBefore = entries(doc);
        const auto from = canvas->viewRect(f.plan).center();
        const auto delta =
            QPointF(std::round(10 * canvas->pixelsPerMm()), std::round(5 * canvas->pixelsPerMm()));
        drag(canvas, from, from + delta);
        const auto &panned = view(window, f.plan);
        check(
            lastLabel(doc) == "Pan view" && entries(doc) == entriesBefore + 1 &&
                panned.frame == plan.frame && panned.scaleDenominator == plan.scaleDenominator &&
                near(panned.offsetMm[0], plan.offsetMm[0] + delta.x() / canvas->pixelsPerMm(),
                     .06) &&
                near(panned.offsetMm[1], plan.offsetMm[1] + delta.y() / canvas->pixelsPerMm(), .06),
            "A drag pans the drawing: the offset changes, frame and scale stay");
        unchanged("Pan view");
        const auto pannedTarget = target(view(window, f.plan));
        const auto wheelAt = canvas->viewRect(f.plan).center();
        for (int notch = 0; notch < 2; ++notch) {
            QWheelEvent wheel(wheelAt, canvas->mapToGlobal(wheelAt), {}, QPoint(0, 120),
                              Qt::NoButton, {}, Qt::NoScrollPhase, false);
            QCoreApplication::sendEvent(canvas, &wheel);
        }
        const auto &zoomed = view(window, f.plan);
        check(lastLabel(doc) == "Crop view" && entries(doc) == entriesBefore + 2 &&
                  zoomed.frame.widthMm < plan.frame.widthMm * .9 &&
                  zoomed.scaleDenominator == plan.scaleDenominator &&
                  near(target(zoomed).x(), pannedTarget.x(), 1e-5) &&
                  near(target(zoomed).y(), pannedTarget.y(), 1e-5),
              "The wheel zooms the crop (one entry), keeping the scale and the drawing");
        key(Qt::Key_Right);
        check(lastLabel(doc) == "Pan view" && entries(doc) == entriesBefore + 3,
              "Arrows pan inside the view");
        unchanged("Edit-view");
        key(Qt::Key_Escape);
        check(canvas->editView() == 0 && !canvas->editBanner()->isVisible() &&
                  !sheets->breadcrumb()->text().contains("View 1"),
              "Esc leaves edit-view and commits nothing");
        check(entries(doc) == entriesBefore + 3, "Leaving commits nothing");
        for (int i = 0; i < 3; ++i)
            action(window, "edit.undo")->trigger();
        check(view(window, f.plan) == plan, "Undo each edit-view gesture");
        // Done leaves too.
        canvas->setFocus(Qt::OtherFocusReason);
        canvas->setSelectedView(f.plan);
        key(Qt::Key_Return);
        check(canvas->editView() == f.plan, "Enter again");
        canvas->doneButton()->click();
        check(canvas->editView() == 0 && focused() == canvas, "Done leaves edit-view");
    }

    // ---- Save as new scene…: a model entry, then a sheet entry
    std::cerr << "[layout] Save as new scene…: a model entry, then a sheet entry" << std::endl;
    {
        canvas->setSelectedView(f.plan);
        canvas->setFocus(Qt::OtherFocusReason);
        key(Qt::Key_Return);
        check(canvas->editView() == f.plan, "Edit-view for Save as new scene");
        // A pan first, so the new scene must carry it.
        const auto from = canvas->viewRect(f.plan).center();
        drag(canvas, from, from + QPointF(std::round(12 * canvas->pixelsPerMm()), 0));
        settle(window);
        const auto pannedFingerprint = cache->status(f.plan)->fingerprint;
        const auto plan = view(window, f.plan);
        const auto entriesBefore = entries(doc);
        const auto scenes = doc.scenes().size();
        DialogWatch watch("saveViewSceneDialog", [&](QDialog &dialog) {
            auto *name = dialog.findChild<QLineEdit *>("saveViewSceneName");
            check(name && name->text() == "Ground plan (A-101)",
                  "The proposed scene name names the page");
            name->selectAll();
            QTest::keyClicks(name, "Plan panned");
            QTest::keyClick(name, Qt::Key_Return);
        });
        canvas->saveSceneButton()->click();
        watch.finish();
        check(QTest::qWaitFor([&] { return window.isActiveWindow() && focused() == canvas; }),
              "Focus returns to the canvas after the prompt; got " + identity(focused()));
        check(entries(doc) == entriesBefore + 2 &&
                  labelAt(doc, entriesBefore) == "Save as new scene" &&
                  labelAt(doc, entriesBefore + 1) == "Use new scene",
              "Save as new scene is two entries: a model edit, then a sheet edit");
        check(doc.scenes().size() == scenes + 1, "A scene was added to the model");
        const auto &linked = view(window, f.plan);
        check(linked.source.scene != plan.source.scene &&
                  doc.scenes().at(linked.source.scene)->name == "Plan panned" &&
                  linked.offsetMm == std::array<double, 2>{0, 0} && linked.frame == plan.frame &&
                  linked.scaleDenominator == plan.scaleDenominator,
              "The view uses the new scene with no offset");
        check(modelRecords(doc).bodies == model.bodies, "Saving a scene changes no geometry");
        settle(window);
        check(cache->status(f.plan)->fingerprint == pannedFingerprint,
              "The view draws exactly what it drew before");
        action(window, "edit.undo")->trigger();
        check(view(window, f.plan) == plan, "Undo the sheet entry first");
        action(window, "edit.undo")->trigger();
        check(doc.scenes().size() == scenes, "Undo the model entry next");
        action(window, "edit.undo")->trigger();
        canvas->setFocus(Qt::OtherFocusReason);
        key(Qt::Key_Escape);
    }

    // ---- Show scene in model: the camera only
    std::cerr << "[layout] Show scene in model: the camera only" << std::endl;
    {
        canvas->setSelectedView(f.south);
        const auto revision = doc.revision();
        const auto entriesBefore = entries(doc);
        action(window, "sheet.view.showScene")->trigger();
        check(window.workspace() == Window::Workspace::Model && window.viewport()->orthographic() &&
                  doc.revision() == revision && entries(doc) == entriesBefore,
              "Show scene in model applies the scene camera without an edit");
        window.setWorkspace(Window::Workspace::Sheets);
        settle(window);
    }

    // ---- Choose scene… relinks a broken view, keeping frame and scale
    std::cerr << "[layout] Choose scene… relinks a broken view, keeping frame and scale"
              << std::endl;
    {
        batch(doc, {QJsonObject{{"command", "saved_scene.delete"},
                                {"scene", QString::number(f.northScene)}}});
        emit window.viewport()->changed();
        sheets->setCurrentSheet(f.elevations);
        settle(window);
        check(cache->status(f.north)->state == DrawingViewState::Broken, "North is broken");
        const auto north = view(window, f.north);
        const auto button = canvas->chooseSceneButton(f.north);
        check(!button.isEmpty(), "The broken card shows Choose scene…");
        DialogWatch watch("chooseSceneDialog", [&](QDialog &dialog) {
            auto *list = dialog.findChild<QComboBox *>("chooseSceneList");
            check(list && list->count() == 3, "Scenes with a camera are offered");
            list->setFocus(Qt::TabFocusReason);
            list->setCurrentIndex(list->findText("South elevation · Front"));
            buttonText(dialog, "Choose")->click();
        });
        click(button.center());
        watch.finish();
        check(QTest::qWaitFor([&] { return window.isActiveWindow() && focused() == canvas; }),
              "Focus returns to the canvas after Choose scene; got " + identity(focused()));
        const auto &relinked = view(window, f.north);
        check(lastLabel(doc) == "Choose scene" && relinked.source.scene == f.southScene &&
                  relinked.frame == north.frame &&
                  relinked.scaleDenominator == north.scaleDenominator,
              "Choose scene relinks the view and keeps its frame and scale");
        settle(window);
        check(cache->status(f.north)->state != DrawingViewState::Broken,
              "The relinked view draws again");
        check(modelRecords(doc).bodies == model.bodies, "Relinking changes no geometry");
        action(window, "edit.undo")->trigger();
        settle(window);
        check(cache->status(f.north)->state == DrawingViewState::Broken, "Undo relinks back");
        action(window, "edit.undo")->trigger(); // The scene deletion.
    }

    // ---- Delete and Backspace delete the selected view with its bound items -----------------
    std::cerr << "[layout] Delete a view" << std::endl;
    {
        sheets->setCurrentSheet(f.groundFloor);
        settle(window);
        // A leader label bound to the plan, so the ADR 0164 cascade has an item to take.
        Id slab = 0;
        for (const auto &[id, body] : doc.bodies())
            if (body->name == "Slab")
                slab = id;
        const auto vertex = doc.bodies().at(slab)->surface.vertices.begin()->first;
        batch(
            doc,
            {QJsonObject{
                {"command", "drawing_item.create"},
                {"sheet", QString::number(f.groundFloor)},
                {"kind", "label"},
                {"data",
                 QJsonObject{{"text", "Slab corner"},
                             {"textAt", QJsonObject{{"xMm", 300}, {"yMm", 250}}},
                             {"target",
                              QJsonObject{{"kind", "view"},
                                          {"view", QString::number(f.plan)},
                                          {"anchor",
                                           QJsonObject{{"kind", "vertex"},
                                                       {"body", QString::number(slab)},
                                                       {"vertex", QString::number(vertex)}}}}}}}}});
        emit window.viewport()->changed();
        settle(window);
        Id label = 0;
        for (const auto &[id, item] : doc.drawingItems())
            if (item->kind() == DrawingItemKind::Label)
                label = id;
        check(label != 0, "The bound label exists");
        const auto views = doc.drawingViews();
        const auto items = doc.drawingItems();
        const auto entriesBefore = entries(doc);
        // In edit-view Delete does nothing.
        canvas->setFocus(Qt::OtherFocusReason);
        canvas->setSelectedView(f.plan);
        key(Qt::Key_Return);
        check(canvas->editView() == f.plan && !action(window, "sheet.view.delete")->isEnabled(),
              "Delete view is unavailable in edit-view");
        key(Qt::Key_Delete);
        key(Qt::Key_Backspace);
        check(doc.drawingViews().contains(f.plan) && entries(doc) == entriesBefore,
              "Delete and Backspace do nothing in edit-view");
        key(Qt::Key_Escape);
        check(action(window, "sheet.view.delete")->isEnabled() &&
                  action(window, "sheet.view.delete")->isVisible() &&
                  !action(window, "edit.delete")->isVisible(),
              "Edit › Delete deletes the selected view in Sheets");
        key(Qt::Key_Delete);
        check(!doc.drawingViews().contains(f.plan) && !doc.drawingItems().contains(label) &&
                  entries(doc) == entriesBefore + 1 && lastLabel(doc) == "Delete view" &&
                  canvas->selectedView() == 0,
              "Delete removes the view and its bound label in one entry");
        unchanged("Delete view");
        action(window, "edit.undo")->trigger();
        check(doc.drawingViews() == views && doc.drawingItems() == items,
              "Undo restores the view and its label with their IDs");
        action(window, "edit.redo")->trigger();
        check(!doc.drawingViews().contains(f.plan) && !doc.drawingItems().contains(label),
              "Redo deletes them again");
        action(window, "edit.undo")->trigger();
        // Backspace is the same as Delete.
        canvas->setFocus(Qt::OtherFocusReason);
        canvas->setSelectedView(f.south);
        key(Qt::Key_Backspace);
        check(!doc.drawingViews().contains(f.south) && lastLabel(doc) == "Delete view",
              "Backspace deletes the selected view");
        unchanged("Backspace delete");
        action(window, "edit.undo")->trigger();
        check(doc.drawingViews() == views, "Undo restores it");
        canvas->setFocus(Qt::OtherFocusReason);
    }

    // ---- Undo everything, then redo everything
    std::cerr << "[layout] Undo everything, then redo everything" << std::endl;
    {
        const auto finalViews = doc.drawingViews();
        const auto finalEntries = entries(doc);
        while (entries(doc) > initialEntries)
            action(window, "edit.undo")->trigger();
        bool same = doc.drawingViews().size() == initialViews.size();
        for (const auto &[id, record] : initialViews)
            same = same && doc.drawingViews().contains(id) && *doc.drawingViews().at(id) == *record;
        check(same, "Undoing every page operation restores the views exactly");
        check(modelRecords(doc).bodies == model.bodies, "Model geometry never changed");
        while (entries(doc) < finalEntries)
            action(window, "edit.redo")->trigger();
        same = doc.drawingViews().size() == finalViews.size();
        for (const auto &[id, record] : finalViews)
            same = same && doc.drawingViews().contains(id) && *doc.drawingViews().at(id) == *record;
        check(same, "Redo replays every page operation");
    }
    doc.markSaved();
}

void screens(Window &window, const QString &path, const QString &directory, const QString &tag,
             const Fixture &f) {
    window.setWorkspace(Window::Workspace::Model);
    window.openPath(path);
    window.setWorkspace(Window::Workspace::Sheets);
    settle(window);
    auto *sheets = window.sheetWorkspace();
    auto *canvas = sheets->canvas();
    QDir().mkpath(directory);
    auto shoot = [&](const QString &name, bool wait = true) {
        if (wait) {
            settle(window);
            QTest::qWait(300);
            settle(window);
        }
        QCoreApplication::processEvents();
        check(window.grab().save(directory + "/" + tag + "-" + name + ".png"), "Screenshot saved");
    };
    for (const int theme : {1, 2}) {
        action(window, ("view.theme." + std::to_string(theme)).c_str())->trigger();
        const auto suffix = theme == 1 ? QString("light") : QString("dark");
        window.resize(1440, 900);
        // The W tool mid-drag with the New model view panel: a frame below view 2, its left
        // edge snapped to view 2's and its other sides to the grid.
        sheets->setCurrentSheet(f.groundFloor);
        canvas->fitPage();
        settle(window);
        canvas->setFocus(Qt::OtherFocusReason);
        action(window, "sheet.tool.view")->trigger();
        window.sheetsTray()->placePanel()->setScene(f.northScene);
        const auto from = canvas->widgetPoint({240.6, 135.3});
        const auto to = canvas->widgetPoint({395.6, 247.2});
        press(canvas, from);
        moveTo(canvas, from, to);
        shoot("place-" + suffix);
        mouse(canvas, QEvent::MouseButtonRelease, to, Qt::LeftButton, Qt::NoButton, {});
        action(window, "edit.undo")->trigger();
        action(window, "sheet.tool.select")->trigger();
        // The selected view with handles, mid-move with the snapping hint.
        sheets->setCurrentSheet(f.groundFloor);
        canvas->fitPage();
        settle(window);
        canvas->setSelectedView(f.south);
        const auto start = canvas->viewRect(f.south).center();
        press(canvas, start);
        moveTo(canvas, start, start + QPointF(0, std::round(2.5 * canvas->pixelsPerMm())));
        shoot("move-snap-" + suffix);
        QTest::keyClick(canvas, Qt::Key_Escape);
        mouse(canvas, QEvent::MouseButtonRelease, start, Qt::LeftButton, Qt::NoButton, {});
        // The selected view and the editable Entity info.
        canvas->setSelectedView(f.plan);
        shoot("selected-" + suffix);
        // Edit-view with its banner.
        canvas->setFocus(Qt::OtherFocusReason);
        QTest::keyClick(canvas, Qt::Key_Return);
        shoot("edit-view-" + suffix);
        QTest::keyClick(canvas, Qt::Key_Escape);
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
        // An unexpected modal (an edit refused with a message box) fails the run instead of
        // blocking it.
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
        check(QTest::qWaitForWindowExposed(&window), "Window exposed");
        check(QTest::qWaitFor([&] { return window.viewport()->rendererReady(); }, 10000),
              "Viewport renderer initialized in the Model workspace");
        const auto arguments = app.arguments();
        if (const auto at = arguments.indexOf("--screens"); at >= 0 && at + 2 < arguments.size()) {
            screens(window, path, arguments[at + 1], arguments[at + 2], fixture);
            std::cout << "Sheet layout screenshots written\n";
            window.document().markSaved();
            return 0;
        }
        layout(window, path, fixture);
        check(unexpected.empty(), "No unexpected message box: " + unexpected);
        std::cout << "Sheet layout: W by drag and typed size, perspective not to scale, move, "
                     "nudge (amended), crop with the scale kept, snapping and Alt, Tab cycling, "
                     "Entity info edits, U and Accept all, edit-view pan and zoom, Choose "
                     "scene…, Save as new scene… (two entries), Show scene in model, "
                     "Measurements re-entry, undo and redo passed\n";
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
