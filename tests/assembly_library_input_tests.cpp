// R094.f (ADR 0166 §6.3, §10): the Tray's Components tab. The tab (index 13) and its View
// action, the Show filter, the cards by keyboard and their accessible text, the values form
// (live validation, Use defaults, Cancel and Esc), the placing ghost and opening outline,
// placing a 1000 mm door on a wall face with the opening cut in one entry and re-cut from
// Entity info in one entry, keyboard placement on a selected face, reuse of the in-model
// definition ("In this model"), the Measurements box ([x,y,z] and re-entry amending), and the
// window, table, bookshelf, cabinet and stair at their defaults. Real key and mouse events;
// run on X11 and Wayland at 1× and 2×.
#include "app/collapsible_section.hpp"
#include "app/component_parameters_section.hpp"
#include "app/components_tab.hpp"
#include "app/parameter_value_field.hpp"
#include "app/surface_format.hpp"
#include "app/window.hpp"
#include "core/hosted_components.hpp"
#include <QAccessible>
#include <QAction>
#include <QApplication>
#include <QComboBox>
#include <QElapsedTimer>
#include <QLabel>
#include <QLineEdit>
#include <QListView>
#include <QMouseEvent>
#include <QPushButton>
#include <QSettings>
#include <QTabWidget>
#include <QTemporaryDir>
#include <QTest>
#include <cmath>
#include <cstdio>
#include <iostream>
using namespace sketchy;
namespace {
std::size_t checks = 0;
void check(bool value, const std::string &message) {
    ++checks;
    if (!value)
        throw std::runtime_error(message);
}
std::string latestLabel(const Document &doc) {
    const auto position = doc.history().position;
    return position ? doc.history(position - 1, 1).entries.at(0).label : std::string{};
}
struct Fixture {
    Window &window;
    Document &doc;
    Viewport &view;
    QTabWidget *tabs{};
    ComponentsTab *tab{};
    QString status;
    Id wall{}, face{};
    explicit Fixture(Window &w)
        : window(w), doc(w.document()), view(*w.viewport()),
          tabs(w.findChild<QTabWidget *>("organizationTabs")),
          tab(w.findChild<ComponentsTab *>("componentsTab")) {
        QObject::connect(&view, &Viewport::message, [this](const QString &text) { status = text; });
        doc.setDisplayUnits(DisplayUnit::Millimeters);
        // A 6 m long, 200 mm thick, 2.7 m high wall; its front face (normal -y) at y = 0.
        wall = doc.addFace({{{0, 0, 0}, {6, 0, 0}, {6, .2, 0}, {0, .2, 0}}}, "Wall");
        doc.extrude(wall, doc.bodies().at(wall)->surface.faces.begin()->first, 2.7);
        for (const auto &[id, record] : doc.bodies().at(wall)->surface.faces) {
            (void)record;
            if (doc.bodies().at(wall)->surface.normal(id).y < -.99)
                face = id;
        }
        view.refresh();
        emit view.changed();
    }
    QListView *cards() const { return tab->cards(); }
    int row(const QString &name) const {
        for (int i = 0; i < cards()->model()->rowCount(); ++i)
            if (cards()->model()->index(i, 0).data().toString() == name)
                return i;
        throw std::runtime_error("No card for " + name.toStdString());
    }
    QString caption(const QString &name) const {
        return cards()->model()->index(row(name), 0).data(Qt::UserRole + 1).toString();
    }
    void focus(QWidget *widget) {
        widget->setFocus(Qt::TabFocusReason);
        check(QTest::qWaitFor([&] { return widget->hasFocus(); }),
              "Focus reaches " + widget->objectName().toStdString());
    }
    // Starts placing a card from the keyboard: focus the cards, move to it, Enter.
    void startByKeyboard(const QString &name) {
        tabs->setCurrentWidget(tab);
        focus(cards());
        const int target = row(name);
        QTest::keyClick(cards(), Qt::Key_Home);
        for (int i = 0; i < 20 && cards()->currentIndex().row() != target; ++i)
            QTest::keyClick(cards(), Qt::Key_Right);
        check(cards()->currentIndex().row() == target, "Arrows reach the " + name.toStdString());
        QTest::keyClick(cards(), Qt::Key_Return);
        check(QTest::qWaitFor([&] { return view.assemblyPlacing() && tab->valuesSection()->isVisible(); }),
              "Enter on the " + name.toStdString() + " card starts placing it");
    }
    ParameterValueField *field(const char *name) {
        auto *field = tab->field(name);
        check(field, std::string("Values form row for ") + name);
        return field;
    }
    void type(const char *name, const QString &text) {
        auto *edit = qobject_cast<QLineEdit *>(field(name)->control());
        check(edit, std::string(name) + " is a line edit");
        focus(edit);
        edit->selectAll();
        QTest::keyClick(edit, Qt::Key_Backspace);
        QTest::keyClicks(edit, text);
        QTest::keyClick(edit, Qt::Key_Return);
    }
    // Moves the pointer to `point` until the ghost shows: the first move can come before the
    // viewport has drawn its pick buffer, so it is repeated with a one-pixel wobble.
    bool hover(QPoint point) {
        // QTest::mouseMove maps through the window, which with QT_SCALE_FACTOR=2 does not reach
        // the viewport reliably; the move event is sent to the viewport, as Qt delivers it.
        auto move = [&](QPoint at) {
            QMouseEvent event(QEvent::MouseMove, QPointF(at), view.mapToGlobal(QPointF(at)),
                              Qt::NoButton, Qt::NoButton, Qt::NoModifier);
            QApplication::sendEvent(&view, &event);
        };
        for (int i = 0; i < 100; ++i) {
            move(point + QPoint(i % 2, 0));
            QTest::qWait(50);
            if (view.assemblyGhostEdges() > 0) {
                move(point);
                QTest::qWait(20);
                return view.assemblyGhostEdges() > 0;
            }
        }
        return false;
    }
    QPoint onWall(double x, double z) const {
        return view.project(doc.worldTransform(wall).point({x, 0, z})).toPoint();
    }
    double openingWidth(Id placement) const {
        const auto &opening = doc.hostedComponents().hosts.at(wall)->openings.at(placement);
        double low = 1e9, high = -1e9;
        for (const auto &corner : opening.profile.corners) {
            low = std::min(low, corner.point.x);
            high = std::max(high, corner.point.x);
        }
        return high - low;
    }
    std::size_t doors() const {
        std::size_t count = 0;
        for (const auto &[root, instance] : doc.instances()) {
            (void)root;
            count += doc.definitions().at(instance->definition)->name == "Interior door";
        }
        return count;
    }
};
void tabAndCards(Fixture &f) {
    check(f.tabs->count() == 14 && f.tabs->tabText(13) == "Components" &&
              f.tabs->widget(13) == f.tab,
          "Components is the Tray's tab 13, after Quantities");
    f.window.findChild<QAction *>("view.components")->trigger();
    check(QTest::qWaitFor([&] { return f.tabs->currentIndex() == 13 && f.tab->isVisible(); }),
          "View → Components shows the tab");
    check(QTest::qWaitFor([&] { return f.tab->isAncestorOf(QApplication::focusWidget()); }),
          "View → Components moves focus into the tab");
    auto *filter = f.tab->filter();
    check(filter->count() == 2 && filter->itemText(0) == "Configurable assemblies" &&
              filter->itemText(1) == "All components" && filter->currentIndex() == 0 &&
              filter->accessibleName() == "Show",
          "Show offers Configurable assemblies and All components");
    check(f.cards()->model()->rowCount() == 6, "Six configurable assemblies are shown");
    const std::vector<std::pair<QString, QString>> expected{
        {"Interior door", "Cuts opening · 4 parameters"},
        {"Casement window", "Cuts opening · 5 parameters"},
        {"Dining table", "5 parameters"},
        {"Bookshelf", "4 parameters"},
        {"Base cabinet", "6 parameters"},
        {"Straight stair", "5 parameters"}};
    for (const auto &[name, caption] : expected)
        check(f.caption(name) == caption,
              "Card " + name.toStdString() + " reads " + caption.toStdString() + ", got " +
                  f.caption(name).toStdString());
    // Accessible text: "Interior door, cuts opening, 4 parameters".
    auto *list = QAccessible::queryAccessibleInterface(f.cards());
    check(list != nullptr, "The cards have an accessible interface");
    bool spoken = false;
    for (int i = 0; i < list->childCount(); ++i)
        if (auto *child = list->child(i))
            spoken |= child->text(QAccessible::Name) == "Interior door, cuts opening, 4 parameters";
    check(spoken, "A card's accessible name reads its name, cuts opening and parameter count");
    auto *about = f.tab->findChild<QLabel *>("assembliesAboutText");
    check(about && about->isVisible() &&
              about->text() == "Bundled with SketchyUp and saved into your model when placed. "
                               "SketchUp Dynamic Components are not imported or converted.",
          "About these assemblies says Dynamic Components are not imported");
    // One Tab stop: Tab from the filter reaches the cards, Tab again leaves them.
    f.focus(filter);
    QTest::keyClick(filter, Qt::Key_Tab);
    check(QTest::qWaitFor([&] { return f.cards()->hasFocus(); }), "Tab reaches the cards");
    QTest::keyClick(f.cards(), Qt::Key_Home);
    QTest::keyClick(f.cards(), Qt::Key_Right);
    check(f.cards()->currentIndex().row() == 1, "Right moves to the next card");
    QTest::keyClick(f.cards(), Qt::Key_Down);
    check(f.cards()->currentIndex().row() == 3,
          "Down moves a row in the two-column grid (row " +
              std::to_string(f.cards()->currentIndex().row()) + ", list " +
              std::to_string(f.cards()->width()) + " px, grid " +
              std::to_string(f.cards()->gridSize().width()) + " x " +
              std::to_string(f.cards()->gridSize().height()) + ", second card at " +
              std::to_string(f.cards()->visualRect(f.cards()->model()->index(1, 0)).x()) + "," +
              std::to_string(f.cards()->visualRect(f.cards()->model()->index(1, 0)).y()) + ")");
    QTest::keyClick(f.cards(), Qt::Key_Tab);
    check(QTest::qWaitFor([&] { return !f.cards()->hasFocus(); }),
          "The cards are one Tab stop");
    // All components adds the (empty) user library: still the six assemblies.
    filter->setCurrentIndex(1);
    check(f.cards()->model()->rowCount() == 6, "All components includes the assemblies");
    filter->setCurrentIndex(0);
}
void valuesForm(Fixture &f) {
    const auto revision = f.doc.revision();
    f.startByKeyboard("Interior door");
    check(f.tab->valuesTitle()->text() == "Interior door · values for this placement",
          "The values form names the assembly");
    check(f.tab->fields().size() == 4 && f.field("width") && f.field("height") &&
              f.field("hinge") && f.field("leaf_open"),
          "One row per input parameter");
    check(f.field("width")->caption().contains("600–1200 mm") &&
              f.field("height")->caption().contains("1900–2400 mm"),
          "The form shows the bounds as captions, in the model's units");
    check(f.status.startsWith("Placing Interior door · Click a wall face to place · the "
                              "opening is cut in the same step · Esc cancels"),
          "The status text names the placement: " + f.status.toStdString());
    // Live validation against the bundle's definition, before any placement.
    auto *width = qobject_cast<QLineEdit *>(f.field("width")->control());
    f.focus(width);
    width->selectAll();
    QTest::keyClick(width, Qt::Key_Backspace);
    QTest::keyClicks(width, "2000");
    check(QTest::qWaitFor([&] { return !f.field("width")->error().isEmpty(); }, 2000),
          "A width above the maximum shows an error at the field");
    check(f.field("width")->error().contains("maximum"),
          "The error names the maximum: " + f.field("width")->error().toStdString());
    check(f.tab->overrides().isEmpty(), "An invalid value is not a placement value");
    QTest::keyClick(width, Qt::Key_Escape);
    check(f.field("width")->error().isEmpty(), "Esc in the field restores the default");
    f.type("width", "1000");
    check(f.field("width")->error().isEmpty() &&
              f.tab->overrides() == QJsonObject{{"width", 1.0}},
          "1000 mm is this placement's width");
    f.type("height", "2100");
    check(!f.tab->overrides().contains("height"),
          "A value equal to the default is not stored as an override");
    f.tab->useDefaultsButton()->click();
    check(f.tab->overrides().isEmpty(), "Use defaults resets the form");
    // Esc in the tab ends placement.
    f.focus(f.tab->useDefaultsButton());
    QTest::keyClick(f.tab->useDefaultsButton(), Qt::Key_Escape);
    check(QTest::qWaitFor([&] { return !f.view.assemblyPlacing(); }) &&
              !f.tab->valuesSection()->isVisible(),
          "Esc ends placement and hides the form");
    f.startByKeyboard("Interior door");
    f.tab->cancelButton()->click();
    check(!f.view.assemblyPlacing() && !f.tab->valuesSection()->isVisible(),
          "Cancel ends placement");
    check(f.doc.revision() == revision, "Choosing values changes nothing in the model");
}
void doorOnWall(Fixture &f) {
    auto &view = f.view;
    view.standardView(2);
    view.fit();
    QTest::qWait(50);
    const auto history = f.doc.history().total;
    f.startByKeyboard("Interior door");
    f.type("width", "1000");
    // The ghost follows the pointer onto the wall face and shows the opening outline.
    const auto point = f.onWall(1.4, 1.2);
    check(f.hover(point), "The ghost appears on the hovered wall face: " + f.status.toStdString() +
                              " (viewport " + std::to_string(view.width()) + "x" +
                              std::to_string(view.height()) + ", point " +
                              std::to_string(point.x()) + "," + std::to_string(point.y()) +
                              ", pick " + std::to_string(view.pick(point).first) + "/" +
                              std::to_string(view.pick(point).second) + ", wall " +
                              std::to_string(f.wall) + "/" + std::to_string(f.face) + ", " +
                              view.assemblyPlacementError().toStdString() + ")");
    check(view.assemblyOpeningOutline().size() == 4, "The opening outline is drawn");
    const auto ghost = *view.assemblyGhostTransform();
    check(std::abs(ghost.point({.45, 0, 1.05}).x - 1.4) < .05 &&
              std::abs(ghost.point({.45, 0, 1.05}).y) < 1e-6,
          "The ghost's opening centre is at the pointer on the wall");
    QTest::qWait(250); // the dry run of the full placement after the pointer rests
    check(f.doc.history().total == history, "The ghost and its dry run change nothing");
    QTest::mouseClick(&view, Qt::LeftButton, {}, point);
    check(QTest::qWaitFor([&] { return !view.assemblyPlacing(); }), "A click places the door");
    check(f.doc.history().total == history + 1 && latestLabel(f.doc) == "Place Interior door",
          "Placing the door is one History entry, " + latestLabel(f.doc));
    Id door = 0;
    for (const auto &[root, attachment] : f.doc.hostedComponents().attachments)
        door = root;
    check(door && std::abs(f.openingWidth(door) - 1.0) < 1e-9,
          "The door is attached and cuts a 1000 mm opening in the same entry");
    check(view.selectionState().entities() == SelectionSet{{door, SelectionKind::Body, 0}},
          "The placed door is selected");
    check(QTest::qWaitFor([&] { return f.tabs->currentIndex() == 2; }),
          "Entity info shows the placement's parameters");
    check(!f.tab->valuesSection()->isVisible(), "The values form closes after placing");
    // Changing the width in Entity info re-cuts the opening in one entry.
    auto *section =
        f.window.findChild<ComponentParametersSection *>("componentParametersSection");
    check(QTest::qWaitFor([&] { return section->isVisible() && section->field("width"); }),
          "Entity info lists the door's parameters");
    check(section->field("width")->storedDisplay() == "1000 mm",
          "Entity info shows the placed width");
    auto *edit = qobject_cast<QLineEdit *>(section->field("width")->control());
    f.focus(edit);
    edit->selectAll();
    QTest::keyClick(edit, Qt::Key_Backspace);
    QTest::keyClicks(edit, "1100");
    QTest::keyClick(edit, Qt::Key_Return);
    check(QTest::qWaitFor([&] { return f.doc.history().total == history + 2; }),
          "Changing the width is one entry (field error: " +
              section->field("width")->error().toStdString() + "; text " +
              edit->text().toStdString() + "; history " +
              std::to_string(f.doc.history().total - history) + "; status " +
              f.status.toStdString() + "; anchor " + [&] {
                  const auto a = f.doc.worldTransform(door).point({.45, 0, 1.05});
                  char text[128];
                  std::snprintf(text, sizeof text, "%.17g %.17g %.17g", a.x, a.y, a.z);
                  return std::string(text);
              }() + ")");
    check(latestLabel(f.doc) == "Change Width" && std::abs(f.openingWidth(door) - 1.1) < 1e-9,
          "Changing the width in Entity info re-cuts the opening");
}
void keyboardOnFace(Fixture &f) {
    auto &view = f.view;
    const auto history = f.doc.history().total;
    const auto definitions = f.doc.definitions().size();
    check(f.caption("Interior door").endsWith("In this model"),
          "The door's card says In this model");
    f.startByKeyboard("Interior door");
    // With the wall face selected, Enter in the viewport places on it at its centre.
    view.setSelection(f.wall, f.face);
    f.focus(&view);
    QTest::keyClick(&view, Qt::Key_Return);
    check(QTest::qWaitFor([&] { return !view.assemblyPlacing(); }),
          "Enter places on the selected face");
    check(f.doc.history().total == history + 1 && f.doors() == 2 &&
              f.doc.definitions().size() == definitions,
          "A second door reuses the in-model definition in one entry");
    auto second = view.selectionState().entities().begin()->body;
    check(std::abs(f.openingWidth(second) - .9) < 1e-9,
          "The second door at its defaults cuts a 900 mm opening");
    const auto centre = f.doc.worldTransform(second).point({.45, 0, 1.05});
    check(std::abs(centre.x - 3) < 1e-6 && std::abs(centre.z - 1.35) < 1e-6,
          "Enter places the door at the face's centre");
    // The Measurements box re-entry amends: an anchor moves the door in the same entry.
    auto *box = f.window.findChild<QLineEdit *>("measurements");
    f.focus(box);
    box->setText("[4500,0,1200]");
    QTest::keyClick(box, Qt::Key_Return);
    check(QTest::qWaitFor([&] { return !box->property("invalid").toBool(); }) &&
              f.doc.history().total == history + 1,
          "Re-entry amends the placement entry");
    second = view.selectionState().entities().begin()->body;
    const auto moved = f.doc.worldTransform(second).point({.45, 0, 1.05});
    check(std::abs(moved.x - 4.5) < 1e-6 && std::abs(moved.z - 1.2) < 1e-6,
          "The amended door sits at the typed anchor");
}
void freeAssemblies(Fixture &f) {
    auto &view = f.view;
    view.standardView(0);
    view.fit();
    QTest::qWait(50);
    auto *box = f.window.findChild<QLineEdit *>("measurements");
    // The table by the Measurements box: [x,y,z] places it, re-entry moves it.
    auto history = f.doc.history().total;
    f.startByKeyboard("Dining table");
    f.focus(box);
    box->setText("[1000,3000,0]");
    QTest::keyClick(box, Qt::Key_Return);
    check(QTest::qWaitFor([&] { return !f.view.assemblyPlacing(); }) &&
              f.doc.history().total == history + 1 && latestLabel(f.doc) == "Place Dining table",
          "[x,y,z] places the table in one entry");
    auto table = view.selectionState().entities().begin()->body;
    f.focus(box);
    box->setText("[2000,3000,0]");
    QTest::keyClick(box, Qt::Key_Return);
    table = view.selectionState().entities().begin()->body;
    check(f.doc.history().total == history + 1 &&
              std::abs(f.doc.worldTransform(table).point({}).x - 2) < 1e-9,
          "Re-entry moves the table in the same entry");
    // The window on the wall, and the bookshelf, cabinet and stair by clicks on the ground,
    // each at its defaults.
    view.standardView(2);
    view.fit();
    QTest::qWait(50);
    history = f.doc.history().total;
    f.startByKeyboard("Casement window");
    check(f.hover(f.onWall(3.0, 1.6)), "The window's ghost is on the wall: " + f.status.toStdString());
    QTest::mouseClick(&view, Qt::LeftButton, {}, f.onWall(3.0, 1.6));
    check(QTest::qWaitFor([&] { return !view.assemblyPlacing(); }) &&
              f.doc.history().total == history + 1 && latestLabel(f.doc) == "Place Casement window",
          "The window is placed in one entry: " + f.status.toStdString());
    const auto window = view.selectionState().entities().begin()->body;
    check(f.doc.hostedComponents().attachments.contains(window) &&
              std::abs(f.openingWidth(window) - 1.2) < 1e-9,
          "The window cuts its 1200 mm opening");
    view.standardView(0);
    view.frameBounds({-8, -8, 0}, {8, 8, 3});
    QTest::qWait(50);
    // A ground point in view that no geometry covers, away from the earlier placements.
    std::vector<QPoint> used;
    auto clearGround = [&] {
        for (double x = -6; x <= 6; x += 1.5)
            for (double y = -6; y <= 6; y += 1.5) {
                const auto point = view.project({x, y, 0}).toPoint();
                bool clear = view.rect().adjusted(20, 20, -20, -20).contains(point) &&
                             view.pick(point).first == 0;
                for (const auto &other : used)
                    clear = clear && (other - point).manhattanLength() > 200;
                if (clear) {
                    used.push_back(point);
                    return point;
                }
            }
        throw std::runtime_error("No clear ground point is in view");
    };
    // The bookshelf by a click on the ground; the cabinet and the stair by [x,y,z], which
    // keeps the sanitized run inside its deadline.
    double x = -4;
    for (const char *name : {"Bookshelf", "Base cabinet", "Straight stair"}) {
        history = f.doc.history().total;
        f.startByKeyboard(name);
        const bool pointer = std::string(name) == "Bookshelf";
        if (pointer) {
            const auto point = clearGround();
            check(f.hover(point), std::string("The ") + name + " ghost follows the pointer: " +
                                      f.status.toStdString());
            QTest::mouseClick(&view, Qt::LeftButton, {}, point);
        } else {
            f.focus(box);
            box->setText(QString("[%1,-4000,0]").arg(int(x * 1000)));
            QTest::keyClick(box, Qt::Key_Return);
            x += 2.5;
        }
        check(QTest::qWaitFor([&] { return !view.assemblyPlacing(); }) &&
                  f.doc.history().total == history + 1 &&
                  latestLabel(f.doc) == std::string("Place ") + name,
              std::string("The ") + name + " is placed in one entry: " +
                  f.status.toStdString());
        const auto placed = view.selectionState().entities().begin()->body;
        check(f.doc.instances().at(placed)->overrides.empty() &&
                  std::abs(f.doc.worldTransform(placed).point({}).z) < 1e-6,
              std::string("The ") + name + " stands on the ground at its defaults");
    }
    // Undo removes the last placement whole.
    const auto total = f.doc.instances().size();
    f.window.findChild<QAction *>("edit.undo")->trigger();
    check(f.doc.instances().size() < total, "Undo removes the stair");
}
} // namespace
int main(int argc, char **argv) {
    QTemporaryDir files;
    if (!files.isValid())
        return 2;
    qputenv("XDG_CONFIG_HOME", files.path().toUtf8());
    qputenv("XDG_DATA_HOME", files.path().toUtf8());
    QSurfaceFormat format;
    format.setDepthBufferSize(24);
    sketchy::setDefaultViewportFormat(format);
    QApplication app(argc, argv);
    app.setOrganizationName("SketchyUp");
    app.setApplicationName("SketchyUp");
    QSettings().setValue("recoverySeconds", 0);
    // An empty user library: All components adds nothing to the assemblies.
    QSettings("SketchyUp", "SketchyUp").setValue("libraryFolder", files.filePath("library"));
    Window window;
    window.resize(1280, 900);
    try {
        window.show();
        check(QTest::qWaitForWindowExposed(&window), "Window exposed");
        window.activateWindow();
        check(QTest::qWaitForWindowActive(&window), "Window active");
        Fixture f(window);
        QElapsedTimer clock;
        clock.start();
        tabAndCards(f);
        std::cout << "Components tab, filter, cards and accessible text passed (" << clock.elapsed() << " ms)\n";
        valuesForm(f);
        std::cout << "Values form, live validation, Use defaults, Cancel and Esc passed (" << clock.elapsed() << " ms)\n";
        doorOnWall(f);
        std::cout << "Door on a wall with the opening cut in one entry, re-cut from Entity "
                     "info passed (" << clock.elapsed() << " ms)\n";
        keyboardOnFace(f);
        std::cout << "Keyboard placement on a selected face, reuse and amend passed (" << clock.elapsed() << " ms)\n";
        freeAssemblies(f);
        std::cout << "Window, table, bookshelf, cabinet and stair at their defaults passed (" << clock.elapsed() << " ms)\n";
        if (argc == 2) {
            f.startByKeyboard("Interior door");
            f.hover(f.onWall(5.2, 1.3));
            QTest::qWait(300);
            check(window.grab().save(QString::fromLocal8Bit(argv[1])), "Screenshot saved");
            f.tab->cancelButton()->click();
        }
        window.document().markSaved();
        std::cout << "assembly_library_input_tests: " << checks
                  << " checks passed; DPR=" << window.devicePixelRatioF() << '\n';
        return 0;
    } catch (const std::exception &error) {
        window.document().markSaved();
        std::cerr << error.what() << '\n';
        return 1;
    }
}
