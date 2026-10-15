#include "app/sheet_evaluation.hpp"
#include "app/sheet_item_info.hpp"
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
#include "io/drawing_view.hpp"
#include <QAction>
#include <QApplication>
#include <QComboBox>
#include <QCryptographicHash>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
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
// R091.h: the ADR 0164 §6 acceptance script, "a small drawing set can be laid out and revised;
// referenced dimensions update and page editing never modifies source model geometry".
//
// 1. Lay out three pages from a model with no sheets: plan and elevation views, a perspective,
//    dimensions (one to vertices, one to a point on an edge), a leader label, text and a title
//    block that "+ Page" copies.
// 2. Revise the model: move geometry, delete a scene, erase an anchored edge.
// 3. Views show Changed and Broken; the vertex dimension's value updates; the edge dimension
//    breaks and Reattach repairs it; Choose scene repairs the broken view; Accept all clears
//    every Changed state.
// 4. The model's records are pointer-identical, and its encoded geometry byte-identical,
//    across every sheet-only operation.
// 5. Save, reopen and save again: the file round-trips byte for byte.
// Pointer input is sent to the canvas as real mouse events; keys go to the focused widget.
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
void batch(Window &window, const QJsonArray &commands) {
    auto &doc = window.document();
    executeBatch(doc, {{"apiVersion", 1},
                       {"documentId", QString::fromStdString(doc.identity())},
                       {"expectedRevision", QString::number(doc.revision())},
                       {"commands", commands}});
    emit window.viewport()->changed();
}
struct Fixture {
    Id groundPlan{}, southScene{}, northScene{}, gardenScene{};
};
// Site coordinates: the house's slab corner is the site origin, drawn 7 m west and 4 m south of
// the model origin so the scenes look at the model origin.
Vec3 site(double x, double y, double z) { return {x - 7, y - 4, z}; }
// A model with scenes and no sheets: a 10 × 8 m slab with walls, a chimney and a terrace.
Fixture writeFixture(const QString &path) {
    Document doc;
    auto box = [&](double x, double y, double w, double d, double h, const char *name) {
        x -= 7;
        y -= 4;
        const auto id =
            doc.addFace({{{x, y, 0}, {x + w, y, 0}, {x + w, y + d, 0}, {x, y + d, 0}}}, name);
        doc.extrude(id, doc.bodies().at(id)->surface.faces.begin()->first, h);
        return id;
    };
    box(0, 0, 10, 8, .2, "Slab");
    box(0, 7.7, 10, .3, 2.7, "North wall");
    box(0, 0, .3, 7.7, 2.7, "West wall");
    box(8.2, 3, .6, .6, 4, "Chimney");
    box(11, 0, 3, 3, .1, "Terrace");
    Fixture f;
    f.groundPlan = createScene(doc, "Ground plan", camera(0, 90, true, {0, 0, 0}));
    f.southScene = createScene(doc, "South elevation", camera(-90, 0, true, {0, 0, 1.4}));
    f.northScene = createScene(doc, "North elevation", camera(90, 0, true, {0, 0, 1.4}));
    f.gardenScene = createScene(doc, "Garden view", camera(-60, 25, false, {0, 0, 1}));
    saveDocument(doc, path);
    return f;
}
// Model records by pointer: page editing must never replace them.
struct ModelRecords {
    std::map<Id, const void *> bodies, scenes, definitions, tags, materials, sections;
    bool operator==(const ModelRecords &) const = default;
};
ModelRecords modelRecords(const Document &doc) {
    ModelRecords result;
    for (const auto &[id, body] : doc.bodies())
        result.bodies[id] = body.get();
    for (const auto &[id, scene] : doc.scenes())
        result.scenes[id] = scene.get();
    for (const auto &[id, definition] : doc.definitions())
        result.definitions[id] = definition.get();
    for (const auto &[id, tag] : doc.tags())
        result.tags[id] = tag.get();
    for (const auto &[id, material] : doc.materials())
        result.materials[id] = material.get();
    for (const auto &[id, section] : doc.sections())
        result.sections[id] = section.get();
    return result;
}
// The encoded document without its drawing-sheet tables and allocators, its schema version and
// its revision counter (which every edit advances): every model byte the native format writes.
QStringList strippedKeys;
QByteArray modelBytes(const Document &doc) {
    auto root = QJsonDocument::fromJson(encodeDocument(doc)).object();
    std::function<void(QJsonObject &, const QString &)> strip = [&](QJsonObject &object,
                                                                    const QString &path) {
        for (const auto &name : object.keys()) {
            if (name.startsWith("drawing") || name.startsWith("nextDrawing") ||
                (path.isEmpty() && (name == "version" || name == "format" || name == "revision"))) {
                object.remove(name);
                if (!strippedKeys.contains(path + name))
                    strippedKeys << path + name;
            } else if (path.isEmpty() && object[name].isObject()) {
                auto child = object[name].toObject();
                strip(child, name + ".");
                object[name] = child;
            }
        }
    };
    strip(root, {});
    return QJsonDocument(root).toJson(QJsonDocument::Compact);
}
QString sha256(const QByteArray &bytes) {
    return QString::fromLatin1(QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex());
}
QByteArray read(const QString &path) {
    QFile file(path);
    check(file.open(QIODevice::ReadOnly), "Read " + path.toStdString());
    return file.readAll();
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
// Every view on every page evaluated at the current revision.
void settleAll(Window &window) {
    auto *sheets = window.sheetWorkspace();
    const auto current = sheets->currentSheet();
    for (const auto sheet : orderedDrawingSheets(window.document())) {
        sheets->setCurrentSheet(sheet);
        settle(window);
    }
    sheets->setCurrentSheet(current);
    settle(window);
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
void mouse(QWidget *widget, QEvent::Type type, QPointF position, Qt::MouseButton button,
           Qt::MouseButtons buttons, Qt::KeyboardModifiers modifiers = {}) {
    QMouseEvent event(type, position, widget->mapToGlobal(position), button, buttons, modifiers);
    QCoreApplication::sendEvent(widget, &event);
    QCoreApplication::processEvents();
}
QPointF pixel(SheetCanvas *canvas, QPointF paperMm) {
    const auto point = canvas->widgetPoint(paperMm);
    return {std::round(point.x()), std::round(point.y())};
}
void hover(SheetCanvas *canvas, QPointF paperMm) {
    mouse(canvas, QEvent::MouseMove, pixel(canvas, paperMm), Qt::NoButton, Qt::NoButton);
}
void clickWidget(SheetCanvas *canvas, QPointF widget) {
    mouse(canvas, QEvent::MouseMove, widget, Qt::NoButton, Qt::NoButton);
    mouse(canvas, QEvent::MouseButtonPress, widget, Qt::LeftButton, Qt::LeftButton);
    mouse(canvas, QEvent::MouseButtonRelease, widget, Qt::LeftButton, Qt::NoButton);
}
void click(SheetCanvas *canvas, QPointF paperMm) { clickWidget(canvas, pixel(canvas, paperMm)); }
Id newest(const Document &doc, DrawingItemKind kind) {
    Id result = 0;
    for (const auto &[id, item] : doc.drawingItems())
        if (item->kind() == kind)
            result = std::max(result, id);
    return result;
}
const DrawingDimensionItem &dimension(const Document &doc, Id id) {
    check(doc.drawingItems().contains(id), "Dimension exists: " + std::to_string(id));
    return std::get<DrawingDimensionItem>(doc.drawingItems().at(id)->data);
}
QPointF paperOf(const Document &doc, Id view, Vec3 world) {
    const auto mapping = drawingViewMapping(doc, view);
    check(mapping.projection.has_value(), "The view projects");
    const auto sheet = mapping.projection->worldToSheet(world);
    return {sheet.x, sheet.y};
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

struct Results {
    QJsonObject layout, revision, repair, roundTrip;
};

Results accept(Window &window, const QString &path, const Fixture &f) {
    Results results;
    auto &doc = window.document();
    auto *sheets = window.sheetWorkspace();
    auto *canvas = sheets->canvas();
    auto *tray = window.sheetsTray();
    auto *cache = window.sheetEvaluations();
    auto *status = window.findChild<QLabel *>("statusMessage");
    auto *measurements = window.findChild<QLineEdit *>("measurements");
    check(status && measurements, "Status bar widgets exist");
    window.openPath(path);
    // Opening is preparation: under Xvfb with no window manager nothing else activates it.
    window.activateWindow();
    check(QTest::qWaitForWindowActive(&window), "Window active after opening the model");
    window.viewport()->setFocus();
    check(QTest::qWaitFor([&] { return window.viewport()->hasFocus(); }), "Viewport focus");
    check(doc.drawingSheets().empty(), "The model starts with no sheets");
    const auto startBytes = modelBytes(doc);
    auto model = modelRecords(doc);
    auto bytes = startBytes;
    size_t operations = 0;
    // After every page operation: model records pointer-identical, model bytes identical.
    auto unchanged = [&](const std::string &what) {
        ++operations;
        check(modelRecords(doc) == model, what + " leaves the model's records untouched");
        if (const auto now = modelBytes(doc); now != bytes) {
            // Name the top-level fields that differ.
            const auto was = QJsonDocument::fromJson(bytes).object();
            const auto is = QJsonDocument::fromJson(now).object();
            QStringList differing;
            for (const auto &name : is.keys() + was.keys())
                if (is.value(name) != was.value(name) && !differing.contains(name))
                    differing << name;
            check(false, what + " leaves the model's bytes identical; differing: " +
                             differing.join(", ").toStdString());
        }
    };
    auto focusCanvas = [&] {
        canvas->setFocus(Qt::OtherFocusReason);
        check(QTest::qWaitFor([&] { return canvas->hasFocus(); }),
              "Canvas focus; got " + identity(focused()));
    };
    auto typed = [&](const QString &text) {
        focusCanvas();
        typeText(text);
        check(measurements->hasFocus(), "Typing " + text.toStdString() + " opens Measurements");
        key(Qt::Key_Return);
    };
    // Places a view of `scene` at `at` with a typed size, at the given scale.
    auto place = [&](Id scene, const QString &scale, QPointF at, const QString &size) {
        focusCanvas();
        key(Qt::Key_W);
        check(canvas->tool() == SheetCanvas::Tool::View, "W chooses the Model view tool");
        auto *panel = tray->placePanel();
        panel->setScene(scene);
        focusCanvas();
        hover(canvas, at);
        const auto before = entries(doc);
        typed(size);
        check(entries(doc) == before + 1 && lastLabel(doc) == "Place view",
              "A typed size places one view");
        const auto view = canvas->selectedView();
        check(view && doc.drawingViews().at(view)->source.scene == scene,
              "The view shows the scene");
        focusCanvas();
        key(Qt::Key_Escape);
        key(Qt::Key_Escape);
        // A new view takes the page's scale (the first view's, once there is one).
        if (!scale.isEmpty() &&
            doc.drawingViews().at(view)->scaleDenominator != scale.section(':', 1).toDouble()) {
            // Select, then type the scale: the Measurements box's Scale step.
            key(Qt::Key_Space);
            canvas->setSelectedView(view);
            typed(scale);
            check(lastLabel(doc) == "Change scale" &&
                      doc.drawingViews().at(view)->scaleDenominator ==
                          scale.section(':', 1).toDouble(),
                  "A typed " + scale.toStdString() + " sets the view's scale; last entry " +
                      lastLabel(doc).toStdString() + "; tool " +
                      std::to_string(int(canvas->tool())) + "; step " +
                      window.findChild<QLabel *>("measurementStep")->text().toStdString() +
                      "; box " + measurements->text().toStdString() + "; status " +
                      status->text().toStdString() + "; selected " +
                      std::to_string(canvas->selectedView()));
            focusCanvas();
            key(Qt::Key_Escape);
        }
        return view;
    };

    // ---- 1. Layout ---------------------------------------------------------------------------
    std::cerr << "[acceptance] Workspace and first page" << std::endl;
    key(Qt::Key_S, Qt::ControlModifier | Qt::AltModifier);
    check(window.workspace() == Window::Workspace::Sheets && canvas->isVisible(),
          "Ctrl+Alt+S opens the Sheets workspace");
    auto before = entries(doc);
    sheets->addPageButton()->click();
    const auto page1 = sheets->currentSheet();
    check(entries(doc) == before + 1 && lastLabel(doc) == "Add page" && page1 &&
              doc.drawingSheets().at(page1)->paper == DrawingPaper{420, 297},
          "+ Page adds an A3 page");
    unchanged("Add page");
    settle(window);

    std::cerr << "[acceptance] Title block" << std::endl;
    action(window, "sheet.insert.titleBlock")->trigger();
    const auto block = newest(doc, DrawingItemKind::TitleBlock);
    check(lastLabel(doc) == "Add title block" && canvas->selectedItem() == block,
          "Insert › Title block adds a title block");
    focusCanvas();
    key(Qt::Key_Return);
    check(canvas->textEditField() == "project", "Enter edits the project in place");
    typeText("Garden house");
    key(Qt::Key_Tab);
    typeText("General arrangement");
    key(Qt::Key_Return);
    check(lastLabel(doc) == "Edit title block", "Title block fields commit as one entry");
    settle(window);
    {
        auto *number = tray->itemInfo()->findChild<QLineEdit *>("sheetItemSheetNumber");
        check(number && number->isVisible(), "Entity info shows the sheet number");
        number->setText("A-101");
        emit number->editingFinished();
        check(lastLabel(doc) == "Change sheet number" &&
                  doc.drawingSheets().at(page1)->number == "A-101",
              "The sheet number is set from Entity info");
    }
    unchanged("Title block");

    std::cerr << "[acceptance] Plan, elevation and perspective views" << std::endl;
    const auto plan = place(f.groundPlan, "1:100", {20, 20}, "200,150");
    const auto south = place(f.southScene, "1:100", {240, 20}, "160,90");
    unchanged("Place views");
    settle(window);
    check(cache->status(plan) && !cache->status(plan)->notToScale && cache->status(south) &&
              cache->status(plan)->state != DrawingViewState::Broken &&
              cache->status(south)->state != DrawingViewState::Broken,
          "Plan and elevation draw to scale");

    std::cerr << "[acceptance] Second page: + Page copies the title block" << std::endl;
    focusCanvas();
    before = entries(doc);
    key(Qt::Key_N, Qt::ControlModifier | Qt::ShiftModifier);
    const auto page2 = sheets->currentSheet();
    check(entries(doc) == before + 1 && page2 != page1 && doc.drawingSheets().size() == 2,
          "Ctrl+Shift+N adds a page");
    {
        const auto items = drawingItemsOnSheet(doc, page2);
        check(items.size() == 1 &&
                  doc.drawingItems().at(items[0])->kind() == DrawingItemKind::TitleBlock,
              "The second page has a copy of the title block");
    }
    unchanged("Second page");
    settle(window);
    const auto north = place(f.northScene, "1:100", {20, 20}, "160,90");
    const auto garden = place(f.gardenScene, {}, {200, 20}, "160,120");
    unchanged("Second page views");
    settle(window);
    check(cache->status(garden) && cache->status(garden)->notToScale &&
              canvas->describeView(garden).contains("Not to scale"),
          "The perspective view is not to scale");

    std::cerr << "[acceptance] Third page" << std::endl;
    sheets->addPageButton()->click();
    const auto page3 = sheets->currentSheet();
    check(doc.drawingSheets().size() == 3 && drawingItemsOnSheet(doc, page3).size() == 1,
          "A third page with its title block");
    unchanged("Third page");

    std::cerr << "[acceptance] Dimensions, label and text on the plan" << std::endl;
    sheets->setCurrentSheet(page1);
    settle(window);
    const auto chimney = bodyNamed(doc, "Chimney");
    const auto terrace = bodyNamed(doc, "Terrace");
    focusCanvas();
    key(Qt::Key_D);
    check(canvas->tool() == SheetCanvas::Tool::Dimension, "D chooses the Dimension tool");
    const auto corner = paperOf(doc, plan, site(0, 0, .2));
    const auto chimneyCorner = paperOf(doc, plan, site(8.2, 3, 4));
    click(canvas, corner + QPointF(.4, -.3));
    check(canvas->itemStep() == 1,
          "The slab corner is picked; status: " + status->text().toStdString());
    click(canvas, chimneyCorner);
    {
        const auto &frame = doc.drawingViews().at(plan)->frame;
        const auto rect = canvas->viewRect(plan);
        const auto at = pixel(canvas, chimneyCorner);
        check(canvas->itemStep() == 2,
              "The chimney corner is picked; status: " + status->text().toStdString() + "; frame " +
                  std::to_string(frame.xMm) + "," + std::to_string(frame.yMm) + " " +
                  std::to_string(frame.widthMm) + "x" + std::to_string(frame.heightMm) +
                  "; corner mm " + std::to_string(corner.x()) + "," + std::to_string(corner.y()) +
                  "; chimney mm " + std::to_string(chimneyCorner.x()) + "," +
                  std::to_string(chimneyCorner.y()) + "; view rect " + std::to_string(rect.x()) +
                  "," + std::to_string(rect.y()) + " " + std::to_string(rect.width()) + "x" +
                  std::to_string(rect.height()) + "; pixel " + std::to_string(at.x()) + "," +
                  std::to_string(at.y()));
    }
    before = entries(doc);
    typed("10");
    const auto toChimney = newest(doc, DrawingItemKind::Dimension);
    {
        const auto evaluation = evaluateDrawingItem(doc, toChimney);
        check(entries(doc) == before + 1 && lastLabel(doc) == "Add dimension" &&
                  evaluation.state == DrawingItemState::Current &&
                  near(*evaluation.valueMetres, std::hypot(8.2, 3), 1e-6) &&
                  evaluation.text == "8732",
              "The vertex dimension measures 8732 mm: " + evaluation.text);
        check(dimension(doc, toChimney).anchors[1].kind == AnchorKind::Vertex,
              "The chimney corner is a vertex anchor");
    }
    // A point on the terrace's south edge, away from its corners, is an edge anchor.
    const auto onEdge = paperOf(doc, plan, site(12.5, 0, .1));
    const auto slabCorner = paperOf(doc, plan, site(10, 0, .2));
    click(canvas, onEdge);
    check(canvas->itemStep() == 1,
          "The terrace edge point is picked; status: " + status->text().toStdString());
    click(canvas, slabCorner);
    check(canvas->itemStep() == 2,
          "The slab corner is picked; status: " + status->text().toStdString());
    before = entries(doc);
    typed("-8");
    const auto toTerrace = newest(doc, DrawingItemKind::Dimension);
    check(toTerrace != toChimney && entries(doc) == before + 1, "A second dimension");
    const auto edgeAnchor = dimension(doc, toTerrace).anchors[0];
    check(edgeAnchor.kind == AnchorKind::Edge && edgeAnchor.body == terrace,
          "The terrace point is an edge anchor");
    // The pointer lands on a whole logical pixel, about 5 cm of model at 1:100 on this canvas.
    check(near(*evaluateDrawingItem(doc, toTerrace).valueMetres, 2.5, .06),
          "The edge dimension measures about 2.5 m: " +
              std::to_string(*evaluateDrawingItem(doc, toTerrace).valueMetres));
    focusCanvas();
    key(Qt::Key_Escape);
    unchanged("Dimensions");
    key(Qt::Key_K);
    check(canvas->tool() == SheetCanvas::Tool::Label, "K chooses the Label tool");
    click(canvas, paperOf(doc, plan, site(10, 8, .2)));
    click(canvas, {150, 185});
    check(qobject_cast<QPlainTextEdit *>(focused()) != nullptr, "The label editor opens");
    typeText("Slab edge");
    key(Qt::Key_Escape);
    const auto label = newest(doc, DrawingItemKind::Label);
    check(lastLabel(doc) == "Add label" &&
              std::get<DrawingLabelItem>(doc.drawingItems().at(label)->data).target.kind ==
                  DrawingLabelTargetKind::View,
          "A leader label bound to the plan");
    focusCanvas();
    key(Qt::Key_X);
    click(canvas, {30, 200});
    check(qobject_cast<QPlainTextEdit *>(focused()) != nullptr, "The text editor opens");
    typeText("All dimensions in mm");
    key(Qt::Key_Escape);
    check(lastLabel(doc) == "Add text", "Text added");
    focusCanvas();
    key(Qt::Key_Space);
    unchanged("Label and text");

    std::cerr << "[acceptance] Accept the layout" << std::endl;
    settleAll(window);
    action(window, "sheet.view.acceptAll")->trigger();
    settleAll(window);
    for (const auto view : {plan, south, north, garden})
        check(cache->status(view)->state == DrawingViewState::Current,
              "Every view is current after Accept all: " + std::to_string(view));
    unchanged("Accept all");
    check(modelBytes(doc) == startBytes, "Laying out the set changed no model byte");
    results.layout = {
        {"pages", int(doc.drawingSheets().size())},  {"views", int(doc.drawingViews().size())},
        {"items", int(doc.drawingItems().size())},   {"historyEntries", int(entries(doc))},
        {"sheetOperationsChecked", int(operations)}, {"modelBytesSha256", sha256(startBytes)}};

    // ---- 2. Revise the model -----------------------------------------------------------------
    std::cerr << "[acceptance] Revise the model" << std::endl;
    window.setWorkspace(Window::Workspace::Model);
    batch(window, {QJsonObject{{"command", "geometry.translate"},
                               {"body", QString::number(chimney)},
                               {"delta", QJsonArray{1, 0, 0}}}});
    batch(window, {QJsonObject{{"command", "saved_scene.delete"},
                               {"scene", QString::number(f.northScene)}}});
    batch(window, {QJsonObject{{"command", "geometry.erase_edge"},
                               {"body", QString::number(edgeAnchor.body)},
                               {"edge", QString::number(edgeAnchor.entity)}}});
    const auto revisedBytes = modelBytes(doc);
    check(revisedBytes != startBytes, "The model edits change the model");
    model = modelRecords(doc);
    bytes = revisedBytes;
    window.setWorkspace(Window::Workspace::Sheets);
    settleAll(window);

    // ---- 3. States, updates and repairs ---------------------------------------------------------
    std::cerr << "[acceptance] Changed and Broken" << std::endl;
    for (const auto view : {plan, south, garden})
        check(cache->status(view)->state == DrawingViewState::Stale,
              "The moved chimney marks view " + std::to_string(view) + " Changed");
    check(canvas->describeView(plan).contains("Changed since accepted"),
          "The canvas describes the plan as changed: " + canvas->describeView(plan).toStdString());
    check(cache->status(north)->state == DrawingViewState::Broken &&
              cache->status(north)->reasons == std::vector<std::string>{"sceneMissing"},
          "Deleting its scene breaks the north elevation");
    {
        const auto vertex = evaluateDrawingItem(doc, toChimney);
        check(vertex.state == DrawingItemState::Current &&
                  near(*vertex.valueMetres, std::hypot(9.2, 3), 1e-6) && vertex.text == "9677",
              "The vertex dimension follows the moved chimney: " + vertex.text);
        const auto edge = evaluateDrawingItem(doc, toTerrace);
        check(edge.state == DrawingItemState::Broken &&
                  edge.reasons == std::vector<std::string>{"referenceMissing"},
              "Erasing the anchored edge breaks the edge dimension");
        const auto *row = tray->pages()->topLevelItem(0);
        check(row && row->data(1, Qt::AccessibleDescriptionRole).toString().contains("changed") &&
                  row->data(1, Qt::AccessibleDescriptionRole).toString().contains("broken"),
              "Pages describes the page's changed views and broken item: " +
                  row->data(1, Qt::AccessibleDescriptionRole).toString().toStdString());
    }
    results.revision = {
        {"modelEdits", QJsonArray{"geometry.translate chimney +1 m x",
                                  "saved_scene.delete North "
                                  "elevation",
                                  "geometry.erase_edge terrace south edge"}},
        {"changedViews", 3},
        {"brokenViews", 1},
        {"vertexDimension", QString::fromStdString(evaluateDrawingItem(doc, toChimney).text)},
        {"edgeDimension", "Broken: referenceMissing"}};

    std::cerr << "[acceptance] Reattach" << std::endl;
    canvas->setSelectedItem(toTerrace);
    settle(window);
    auto *reattach = tray->itemInfo()->findChild<QPushButton *>("sheetItemReattach");
    check(reattach && reattach->isVisible(), "Entity info offers Reattach");
    before = entries(doc);
    reattach->click();
    check(canvas->reattaching() && canvas->reattachPoints().size() == 1,
          "Reattach proposes a point");
    focusCanvas();
    key(Qt::Key_Return);
    check(entries(doc) == before + 1 && lastLabel(doc) == "Reattach dimension" &&
              evaluateDrawingItem(doc, toTerrace).state == DrawingItemState::Current,
          "Enter reattaches the dimension");
    unchanged("Reattach");

    std::cerr << "[acceptance] Choose scene" << std::endl;
    sheets->setCurrentSheet(page2);
    settle(window);
    {
        const auto button = canvas->chooseSceneButton(north);
        check(!button.isEmpty(), "The broken card offers Choose scene…");
        DialogWatch watch("chooseSceneDialog", [&](QDialog &dialog) {
            auto *list = dialog.findChild<QComboBox *>("chooseSceneList");
            check(list && list->findText("South elevation · Front") >= 0, "Scenes are offered");
            list->setCurrentIndex(list->findText("South elevation · Front"));
            buttonText(dialog, "Choose")->click();
        });
        clickWidget(canvas, button.center());
        watch.finish();
        check(QTest::qWaitFor([&] { return window.isActiveWindow() && focused() == canvas; }),
              "Focus returns to the canvas after Choose scene; got " + identity(focused()));
        check(lastLabel(doc) == "Choose scene" &&
                  doc.drawingViews().at(north)->source.scene == f.southScene,
              "Choose scene relinks the broken view");
    }
    unchanged("Choose scene");
    settleAll(window);
    check(cache->status(north)->state != DrawingViewState::Broken, "The relinked view draws");

    std::cerr << "[acceptance] Accept all" << std::endl;
    size_t changed = 0;
    for (const auto &[id, record] : doc.drawingViews())
        changed += cache->status(id)->state == DrawingViewState::Stale;
    check(changed >= 3, "At least three views are changed");
    before = entries(doc);
    action(window, "sheet.view.acceptAll")->trigger();
    check(entries(doc) == before + 1 && lastLabel(doc) == QString("Accept %1 views").arg(changed),
          "Accept all is one entry: " + lastLabel(doc).toStdString());
    settleAll(window);
    for (const auto &[id, record] : doc.drawingViews())
        check(cache->status(id)->state == DrawingViewState::Current,
              "Accept all clears Changed on view " + std::to_string(id));
    for (const auto &[id, item] : doc.drawingItems())
        check(evaluateDrawingItem(doc, id).state == DrawingItemState::Current,
              "Every item is current: " + std::to_string(id));
    unchanged("Accept all after the revision");
    check(modelBytes(doc) == revisedBytes, "Repairing the set changed no model byte");
    results.repair = {{"reattach", "Reattach dimension"},
                      {"chooseScene", "Choose scene"},
                      {"acceptAll", lastLabel(doc)},
                      {"viewsCurrent", int(doc.drawingViews().size())},
                      {"itemsCurrent", int(doc.drawingItems().size())},
                      {"sheetOperationsChecked", int(operations)},
                      {"modelBytesSha256", sha256(revisedBytes)}};

    // ---- 5. Save, reopen, save again ---------------------------------------------------------
    std::cerr << "[acceptance] Save, reopen, save again" << std::endl;
    const auto sheetCount = doc.drawingSheets().size();
    const auto viewCount = doc.drawingViews().size();
    const auto itemCount = doc.drawingItems().size();
    const auto dimensionText = evaluateDrawingItem(doc, toChimney).text;
    action(window, "file.save")->trigger();
    check(!doc.dirty(), "Saved");
    const auto first = read(path);
    check(encodeContainer(doc) == first, "The file holds the document");
    window.openPath(path);
    window.activateWindow();
    check(QTest::qWaitForWindowActive(&window), "Window active after reopening");
    check(doc.drawingSheets().size() == sheetCount && doc.drawingViews().size() == viewCount &&
              doc.drawingItems().size() == itemCount,
          "Pages, views and items reopen");
    check(modelBytes(doc) == revisedBytes, "The model reopens byte-identical");
    check(evaluateDrawingItem(doc, toChimney).text == dimensionText,
          "The dimension reopens with its value");
    window.setWorkspace(Window::Workspace::Sheets);
    settleAll(window);
    for (const auto &[id, record] : doc.drawingViews())
        check(cache->status(id)->state == DrawingViewState::Current,
              "Accepted views reopen current: " + std::to_string(id));
    // The second save writes a new file: nothing is left from the first.
    check(QFile::remove(path), "Remove the first save");
    action(window, "file.save")->trigger();
    const auto second = read(path);
    check(second == first, "Saving the reopened set writes the same bytes");
    check(encodeContainer(decodeContainer(second)) == second, "The container re-encodes exactly");
    results.roundTrip = {{"fileBytes", qint64(first.size())}, {"fileSha256", sha256(first)},
                         {"secondSaveIdentical", true},       {"pages", int(sheetCount)},
                         {"views", int(viewCount)},           {"items", int(itemCount)}};
    window.setWorkspace(Window::Workspace::Model);
    return results;
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
    QApplication::setAttribute(Qt::AA_DontUseNativeDialogs);
    QApplication app(argc, argv);
    app.setOrganizationName("SketchyUp");
    app.setApplicationName("SketchyUp");
    try {
        const auto path = files.filePath("garden-house.sketchyup");
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
        check(QTest::qWaitForWindowExposed(&window), "Window exposed");
        check(QTest::qWaitFor([&] { return window.viewport()->rendererReady(); }, 10000),
              "Viewport renderer initialized in the Model workspace");
        window.activateWindow();
        check(QTest::qWaitForWindowActive(&window), "Window active");
        const auto results = accept(window, path, fixture);
        check(unexpected.empty(), "No unexpected message box: " + unexpected);
        // --keep PATH copies the saved drawing set out, for the installed-package smoke.
        if (const auto at = app.arguments().indexOf("--keep");
            at >= 0 && at + 1 < app.arguments().size()) {
            QFile::remove(app.arguments()[at + 1]);
            check(QFile::copy(path, app.arguments()[at + 1]), "Keep the saved drawing set");
        }
        window.document().markSaved();
        const QJsonObject summary{{"suite", "sheet_acceptance_input_tests"},
                                  {"passed", true},
                                  {"platform", QGuiApplication::platformName()},
                                  {"scale", window.devicePixelRatioF()},
                                  {"modelBytesExclude", QJsonArray::fromStringList(strippedKeys)},
                                  {"layout", results.layout},
                                  {"revision", results.revision},
                                  {"repair", results.repair},
                                  {"roundTrip", results.roundTrip}};
        std::cout << QJsonDocument(summary).toJson(QJsonDocument::Compact).constData() << '\n';
        std::cout << "Sheet acceptance: three pages, four views (plan, elevations, perspective), "
                     "dimensions, a leader label, text and title blocks laid out; a moved "
                     "chimney, a deleted scene and an erased edge give Changed and Broken; the "
                     "dimension updated, Reattach and Choose scene repaired, Accept all cleared "
                     "Changed; model records and bytes untouched by every page operation; the "
                     "file round-trips\n";
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
