// R094.h: the ADR 0166 §10 acceptance script, "overrides and shared defaults behave as specified
// across undo, redo, save and reopen; regeneration keeps the edit scope and materials; a hand
// edit inside the definition shows Changed with Rebind".
//
// The session opens a model holding the ADR 0165 §1 table (three placements: one at its
// defaults, one with its own values and one nested in a room that supplies its width) and a
// plain wall, then, through the real interface:
// 1. places the Dining table assembly from the Components tab with a width of 1800 mm;
// 2. overrides Width and Shelves on the table at its defaults in Entity info, and resets Shelves;
// 3. opens that table (the dialog → Edit definition at defaults), paints the top, changes the
//    default Width (the context, the selection and the colour survive regeneration),
//    adds a Visibility driver with Shift+D, moves the shelf by hand (its drivers show Changed,
//    and the strip offers Rebind), presses Rebind all and closes the definition (the table's
//    values are restored);
// 4. opens the table with its own values and makes it unique (promotion), then edits the copy;
// 5. places an interior door and a casement window on the wall (each cuts its opening in the
//    same entry) and changes the door's width in Entity info (the opening is re-cut);
// 6. checks every History entry's label, undoes to the start and redoes to the end;
// 7. saves, reopens and saves again: the model and the file round-trip byte for byte.
// Keys go to the focused widget; pointer input is sent to the canvas as real mouse events.
// Run on X11 and Wayland at 1× and 2×.
#include "app/component_definition_section.hpp"
#include "app/component_drivers_section.hpp"
#include "app/component_parameters_section.hpp"
#include "app/components_tab.hpp"
#include "app/formula_field.hpp"
#include "app/parameter_value_field.hpp"
#include "app/parameters_scope_dialog.hpp"
#include "app/surface_format.hpp"
#include "app/window.hpp"
#include "component_parameter_fixture.hpp"
#include "core/hosted_components.hpp"
#include "io/document_io.hpp"
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
#include <QListView>
#include <QMessageBox>
#include <QMouseEvent>
#include <QPushButton>
#include <QRegularExpression>
#include <QSettings>
#include <QSpinBox>
#include <QTabWidget>
#include <QTemporaryDir>
#include <QTest>
#include <QTimer>
#include <cmath>
#include <functional>
#include <iostream>
using namespace sketchy;
using namespace sketchy::parameter_fixture;
namespace {
std::size_t checks = 0;
void check(bool value, const std::string &message) {
    ++checks;
    if (!value)
        throw std::runtime_error(message);
}
std::string text(const QString &value) { return value.toStdString(); }
std::size_t applied(const Document &doc) { return doc.history(0, 1).position; }
std::vector<std::string> labels(const Document &doc) {
    std::vector<std::string> result;
    for (const auto &entry : doc.history(0, 1000).entries)
        result.push_back(entry.label);
    return result;
}
std::string latestLabel(const Document &doc) {
    const auto all = labels(doc);
    const auto position = applied(doc);
    return position ? all.at(position - 1) : std::string{};
}
// The saved records without the revision counter and the allocator high-water marks, which
// undo keeps by design (ADR 0005, 0007): undo restores every record exactly.
const QRegularExpression kStripped(R"re("(revision|next\w*)":"?\d+"?)re");
QByteArray content(const Document &doc) {
    return QString::fromUtf8(encodeDocument(doc)).replace(kStripped, {}).toUtf8();
}
QString sha256(const QByteArray &bytes) {
    return QString::fromLatin1(QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex());
}
QByteArray read(const QString &path) {
    QFile file(path);
    check(file.open(QIODevice::ReadOnly), "Read " + text(path));
    return file.readAll();
}
std::optional<double> quantity(const Document &doc, Id placement, const std::string &name) {
    const auto &values = doc.instances().at(placement)->overrides;
    const auto found = values.find(name);
    if (found == values.end())
        return std::nullopt;
    if (const auto *value = std::get_if<ParameterQuantity>(&found->second))
        return value->value;
    return std::nullopt;
}
bool near(std::optional<double> value, double expected) {
    return value && std::abs(*value - expected) < 1e-9;
}
// The stored default (or formula) of a parameter of `placement`'s definition.
std::string defaultOf(const Document &doc, Id placement, const std::string &name) {
    const auto &definition = doc.definitions().at(doc.instances().at(placement)->definition);
    if (definition->parameters)
        for (const auto &parameter : definition->parameters->parameters)
            if (parameter.name == name)
                return parameter.expression;
    throw std::runtime_error("No parameter " + name);
}

// The wall's front face (normal -y) is at y = -3.2, in front of the tables.
constexpr double kWallFront = -3.2;

struct Fixture {
    Window &window;
    Table &t;
    Id wall{}, face{};
    QString status;
    Document &doc() { return window.document(); }
    Viewport &view() { return *window.viewport(); }
    QTabWidget *tabs() { return window.findChild<QTabWidget *>("organizationTabs"); }
    ComponentsTab *components() { return window.findChild<ComponentsTab *>("componentsTab"); }
    ComponentParametersSection *section() {
        return window.findChild<ComponentParametersSection *>("componentParametersSection");
    }
    ComponentDefinitionSection *definition() {
        return window.findChild<ComponentDefinitionSection *>("componentDefinitionSection");
    }
    ComponentDriversSection *drivers() {
        return window.findChild<ComponentDriversSection *>("componentDriversSection");
    }
    QLabel *banner() { return window.findChild<QLabel *>("componentScopeBanner"); }
    QLabel *statusBar() { return window.findChild<QLabel *>("statusMessage"); }
    QLineEdit *measurementsBox() { return window.findChild<QLineEdit *>("measurements"); }
    QAction *action(const char *name) {
        auto *found = window.findChild<QAction *>(name);
        check(found, std::string("Action ") + name);
        return found;
    }
    void focus(QWidget *widget) {
        widget->setFocus(Qt::TabFocusReason);
        check(QTest::qWaitFor([&] { return widget->hasFocus(); }),
              "Focus reaches " + text(widget->objectName()));
    }
    void type(QLineEdit *edit, const QString &value) {
        focus(edit);
        edit->selectAll();
        QTest::keyClick(edit, Qt::Key_Backspace);
        QTest::keyClicks(edit, value);
    }
    void enter(QWidget *widget) { QTest::keyClick(widget, Qt::Key_Return); }
    void press(QWidget *button) {
        focus(button);
        QTest::keyClick(button, Qt::Key_Space);
    }
    void select(Id body) {
        view().setSelection(body);
        QTest::qWait(20);
    }
    void showInfo() {
        tabs()->setCurrentIndex(2);
        QTest::qWait(20);
    }
    // Entity info's row for a parameter of the selected placement.
    ParameterValueField *field(const char *name) {
        check(QTest::qWaitFor([&] { return section()->isVisible() && section()->field(name); }),
              std::string("Entity info lists ") + name);
        return section()->field(name);
    }
    QLineEdit *edit(const char *name) {
        auto *edit = qobject_cast<QLineEdit *>(field(name)->control());
        check(edit, std::string(name) + " is a line edit");
        return edit;
    }
    // Esc in the viewport until the context is closed, as the user closes it.
    void closeByEscape() {
        focus(&view());
        for (int i = 0; i < 4 && view().selectionState().context(); ++i) {
            QTest::keyClick(&view(), Qt::Key_Escape);
            QTest::qWait(10);
        }
        check(!view().selectionState().context(), "Esc closes the context");
    }
    // Enter on `placement` selected in the viewport; `act` runs on the dialog once it has
    // keyboard focus. Returns whether the dialog opened.
    bool openWithDialog(Id placement, const std::function<void(ParametersScopeDialog &)> &act) {
        bool seen = false;
        std::string failure;
        QTimer timer;
        timer.setInterval(10);
        QObject::connect(&timer, &QTimer::timeout, [&] {
            auto *dialog = dynamic_cast<ParametersScopeDialog *>(QApplication::activeModalWidget());
            if (!dialog || seen)
                return;
            seen = true;
            timer.stop();
            try {
                check(QTest::qWaitForWindowExposed(dialog), "The dialog is exposed");
                dialog->activateWindow();
                check(QTest::qWaitForWindowActive(dialog), "The dialog is active");
                check(QTest::qWaitFor([&] {
                          auto *focused = QApplication::focusWidget();
                          return focused && dialog->isAncestorOf(focused);
                      }),
                      "The dialog owns keyboard focus");
                act(*dialog);
            } catch (const std::exception &error) {
                failure = error.what();
                dialog->reject();
            }
        });
        timer.start();
        view().enterContext(0);
        select(placement);
        focus(&view());
        QTest::keyClick(&view(), Qt::Key_Return);
        timer.stop();
        if (!failure.empty())
            throw std::runtime_error(failure);
        return seen;
    }
    // The Components tab: focus the cards, move to `name`, Enter.
    void startPlacing(const QString &name) {
        auto *tab = components();
        tabs()->setCurrentWidget(tab);
        auto *cards = tab->cards();
        focus(cards);
        int target = -1;
        for (int i = 0; i < cards->model()->rowCount(); ++i)
            if (cards->model()->index(i, 0).data().toString() == name)
                target = i;
        check(target >= 0, "A card for " + text(name));
        QTest::keyClick(cards, Qt::Key_Home);
        for (int i = 0; i < 20 && cards->currentIndex().row() != target; ++i)
            QTest::keyClick(cards, Qt::Key_Right);
        check(cards->currentIndex().row() == target, "Arrows reach the " + text(name));
        QTest::keyClick(cards, Qt::Key_Return);
        check(QTest::qWaitFor(
                  [&] { return view().assemblyPlacing() && tab->valuesSection()->isVisible(); }),
              "Enter on the " + text(name) + " card starts placing it");
    }
    void typeValue(const char *name, const QString &value) {
        auto *row = components()->field(name);
        check(row, std::string("Values form row for ") + name);
        auto *edit = qobject_cast<QLineEdit *>(row->control());
        check(edit, std::string(name) + " is a line edit");
        type(edit, value);
        enter(edit);
        check(row->error().isEmpty(),
              std::string("The form accepts ") + name + ": " + text(row->error()));
    }
    // Moves the pointer to `point` until the ghost shows (the first move can come before the
    // viewport has drawn its pick buffer, so it is repeated with a one-pixel wobble).
    bool hover(QPoint point) {
        auto move = [&](QPoint at) {
            QMouseEvent event(QEvent::MouseMove, QPointF(at), view().mapToGlobal(QPointF(at)),
                              Qt::NoButton, Qt::NoButton, Qt::NoModifier);
            QApplication::sendEvent(&view(), &event);
        };
        for (int i = 0; i < 100; ++i) {
            move(point + QPoint(i % 2, 0));
            QTest::qWait(50);
            if (view().assemblyGhostEdges() > 0) {
                move(point);
                QTest::qWait(20);
                return view().assemblyGhostEdges() > 0;
            }
        }
        return false;
    }
    QPoint onWall(double x, double z) { return view().project({x, kWallFront, z}).toPoint(); }
    double openingWidth(Id placement) {
        const auto &opening = doc().hostedComponents().hosts.at(wall)->openings.at(placement);
        double low = 1e9, high = -1e9;
        for (const auto &corner : opening.profile.corners) {
            low = std::min(low, corner.point.x);
            high = std::max(high, corner.point.x);
        }
        return high - low;
    }
    Id selected() {
        const auto &entities = view().selectionState().entities();
        check(entities.size() == 1, "One placement is selected");
        return entities.begin()->body;
    }
};

// 1. The Dining table from the Components tab with a value, placed by the Measurements box.
QJsonObject placeTable(Fixture &f) {
    auto &doc = f.doc();
    const auto before = applied(doc);
    f.startPlacing("Dining table");
    f.typeValue("width", "1800");
    check(f.components()->overrides() == QJsonObject{{"width", 1.8}},
          "1800 mm is the table's value");
    auto *box = f.measurementsBox();
    f.focus(box);
    box->setText("[7000,4000,0]");
    QTest::keyClick(box, Qt::Key_Return);
    check(QTest::qWaitFor([&] { return !f.view().assemblyPlacing(); }) &&
              applied(doc) == before + 1 && latestLabel(doc) == "Place Dining table",
          "Placing the Dining table is one entry: " + text(f.status));
    const auto table = f.selected();
    check(near(quantity(doc, table, "width"), 1.8) &&
              doc.instances().at(table)->overrides.size() == 1,
          "The placed table holds its width and nothing else");
    check(std::abs(doc.worldTransform(table).point({}).x - 7) < 1e-9,
          "The table stands at the typed point");
    check(QTest::qWaitFor([&] { return f.tabs()->currentIndex() == 2; }),
          "After placing, the Tray shows Entity info");
    check(f.field("width")->storedDisplay() == "1800 mm", "Entity info shows the placed width");
    return {{"placement", QString::number(table)}, {"width", "1800 mm"}};
}

// 2. Width and Shelves on the table at its defaults; Shelves reset.
QJsonObject overrideAndReset(Fixture &f) {
    auto &doc = f.doc();
    const auto placement = f.t.defaults;
    f.view().enterContext(0);
    f.showInfo();
    f.select(placement);
    check(QTest::qWaitFor([&] { return f.section()->placement() == placement; }),
          "Entity info shows the table at its defaults");
    const auto before = applied(doc);
    auto *width = f.edit("width");
    f.type(width, "1500");
    f.enter(width);
    check(applied(doc) == before + 1 && latestLabel(doc) == "Change Width" &&
              near(quantity(doc, placement, "width"), 1.5),
          "Width 1500 mm is one entry, got " + latestLabel(doc));
    auto *shelves = qobject_cast<QSpinBox *>(f.field("shelves")->control());
    check(shelves && shelves->value() == 1, "Shelves shows its default, 1");
    f.focus(shelves);
    QTest::keyClick(shelves, Qt::Key_Up);
    check(QTest::qWaitFor([&] { return applied(doc) == before + 2; }) &&
              latestLabel(doc) == "Change Shelves" && near(quantity(doc, placement, "shelves"), 2),
          "One step up sets Shelves to 2 as one entry, got " + latestLabel(doc));
    // The other placements are untouched: the shared definition keeps its defaults.
    check(near(quantity(doc, f.t.wide, "width"), 1.2) &&
              near(quantity(doc, f.t.wide, "shelves"), 3) &&
              doc.instances().at(f.t.defaults)->definition == f.t.definition &&
              doc.instances().at(f.t.wide)->definition == f.t.definition,
          "One placement's values leave the others and the definition alone");
    auto *reset = f.field("shelves")->resetButton();
    check(reset->isVisible(), "Shelves offers its reset");
    f.press(reset);
    check(applied(doc) == before + 3 && latestLabel(doc) == "Reset Shelves" &&
              !quantity(doc, placement, "shelves") && near(quantity(doc, placement, "width"), 1.5),
          "Reset Shelves is one entry and keeps Width, got " + latestLabel(doc));
    return {{"width", "1500 mm"}, {"shelvesReset", true}};
}

// 3. Edit definition at defaults: default Width, a driver, a hand edit with Rebind, close.
QJsonObject editAtDefaults(Fixture &f) {
    auto &doc = f.doc();
    const auto placement = f.t.defaults;
    const auto before = applied(doc);
    const bool opened = f.openWithDialog(placement, [&](ParametersScopeDialog &dialog) {
        check(dialog.text().startsWith("Width 1500 mm differs from the defaults."),
              "The dialog names the differing value, got " + text(dialog.text()));
        check(dialog.defaultsButton()->hasFocus() && dialog.defaultsButton()->isDefault(),
              "Edit definition at defaults is the focused default");
        QTest::keyClick(dialog.defaultsButton(), Qt::Key_Return);
    });
    check(opened, "Enter on a placement with its own values opens the dialog");
    check(QTest::qWaitFor([&] { return f.view().componentScope() == placement; }, 3000),
          "Edit definition at defaults opens the context");
    check(applied(doc) == before + 1 && latestLabel(doc) == "Edit Table at defaults" &&
              doc.instances().at(placement)->overrides.empty(),
          "One labelled entry suspends the values, got " + latestLabel(doc));
    check(f.banner()->text().contains("own values return when you close the definition"),
          "The banner says the values return");
    f.showInfo();
    // A colour on the top, painted inside the definition, survives regeneration.
    const auto top = authoring::sceneOf(doc, placement, f.t.top);
    f.select(top);
    f.view().paintSelection({.4f, .25f, .1f});
    const auto paintLabel = latestLabel(doc);
    const auto painted = std::pair(doc.bodies().at(top)->color, doc.bodies().at(top)->faceColors);
    check(applied(doc) == before + 2 && paintLabel == "Edit shared component definition",
          "Painting the top is one entry, got " + paintLabel);
    // The Stretch driver's digest covers the top's whole record (ADR 0165 §4), colour included,
    // so the paint makes it Changed and the strip names it.
    auto *strip = f.window.findChild<QWidget *>("parameterStatusStrip");
    auto *stripText = strip->findChild<QLabel *>("parameterStatusText");
    check(QTest::qWaitFor([&] { return strip->isVisible(); }) &&
              stripText->text().contains("<b>Stretch Top</b>"),
          "The strip names Stretch Top after the paint, got " + text(stripText->text()));
    // The default Width: the shared definition changes, every default placement follows.
    auto *definition = f.definition();
    check(QTest::qWaitFor([&] { return definition->isVisible(); }),
          "Parameters · definition shows in the context");
    definition->select("width");
    auto *expression = definition->expressionField();
    f.type(expression->edit(), "1000 mm");
    f.enter(expression->edit());
    check(applied(doc) == before + 3 && latestLabel(doc) == "Edit parameter Width",
          "The default changes as one entry, got " + latestLabel(doc));
    const auto widthDefault = defaultOf(doc, placement, "width");
    check(widthDefault != "900mm" && f.view().componentScope() == placement,
          "The definition's default is 1000 mm and the context stays open, got " + widthDefault);
    double maxX = -1;
    for (const auto &[id, point] : doc.bodies().at(top)->surface.vertices)
        maxX = std::max(maxX, point.x);
    check(std::abs(maxX - 1.0) < 1e-9, "The top regenerated to the new width");
    check(std::pair(doc.bodies().at(top)->color, doc.bodies().at(top)->faceColors) == painted,
          "Regeneration keeps the top's colour");
    check(f.view().selectionState().entities().size() == 1 &&
              f.view().selectionState().entities().begin()->body == top,
          "Regeneration keeps the selection");
    // Add driver with Shift+D: a Visibility driver on the right back leg.
    auto *drivers = f.drivers();
    const auto leg = authoring::sceneOf(doc, placement, f.t.legRightBack);
    f.select(leg);
    f.focus(&f.view());
    QTest::keyClick(&f.view(), Qt::Key_D, Qt::ShiftModifier);
    check(QTest::qWaitFor([&] { return drivers->creating() && drivers->kindChoice()->hasFocus(); }),
          "Shift+D opens the driver form with focus on Kind");
    const int visibility = drivers->kindChoice()->findData("visibility");
    check(visibility >= 0, "The leg accepts a Visibility driver");
    drivers->kindChoice()->setCurrentIndex(visibility);
    f.type(drivers->formulaField()->edit(), "open");
    f.enter(drivers->formulaField()->edit());
    check(applied(doc) == before + 4 && latestLabel(doc) == "Add Visibility driver",
          "The driver is one entry, got " + latestLabel(doc) + " / " +
              text(drivers->editorError()->text()) + " / " +
              text(drivers->formulaField()->error()));
    // A hand edit: the shelf moves inside the definition; its drivers start from a changed
    // shape, so they read Changed and the strip offers Rebind.
    const auto shelf = authoring::sceneOf(doc, placement, f.t.shelf);
    f.select(shelf);
    f.view().setTool(Viewport::Tool::Move);
    check(f.view().measurements("[0.1,0.1,0.21]") && f.view().measurements("0,0,0.05"),
          "The shelf moves inside the definition");
    f.view().setTool(Viewport::Tool::Select);
    QTest::qWait(20);
    const auto moveLabel = latestLabel(doc);
    check(applied(doc) == before + 5 && moveLabel == "Edit shared component definition",
          "The move is one entry, got " + moveLabel);
    check(strip->isVisible() && stripText->text().contains("<b>Repeat Shelf</b>") &&
              stripText->text().contains("<b>Visibility Shelf</b>") &&
              !stripText->text().contains("Stretch Top") &&
              stripText->text().contains("Placements may not look as expected until you rebind."),
          "The strip names the Changed drivers, got " + text(stripText->text()));
    int changed = 0;
    QStringList rows;
    for (int i = 0; i < drivers->tree()->topLevelItemCount(); ++i) {
        const auto row =
            drivers->tree()->topLevelItem(i)->data(0, Qt::AccessibleTextRole).toString();
        rows << row;
        changed += row.endsWith(", Changed");
    }
    check(changed == 3 && rows.join("|").contains("Stretch, Top · x past 450 mm, width, Changed") &&
              rows.join("|").contains("Repeat, Shelf") &&
              rows.join("|").contains("Visibility, Shelf, shelves > 0, Changed"),
          "Stretch Top, Repeat Shelf and Visibility Shelf read Changed in Drivers, got " +
              text(rows.join(" | ")));
    check(f.view().componentScope() == placement, "Regeneration keeps the edit scope");
    check(strip->findChild<QPushButton *>("parameterStatusRebind")->isVisible() &&
              drivers->rebindAllButton()->isVisible(),
          "The strip offers Rebind, and Drivers offers Rebind all");
    f.press(drivers->rebindAllButton());
    check(applied(doc) == before + 6 && latestLabel(doc) == "Rebind 3 drivers" &&
              !strip->isVisible() && !drivers->rebindAllButton()->isVisible(),
          "Rebind all is one entry and clears Changed, got " + latestLabel(doc));
    // Close: the placement's own values are restored in one entry.
    f.closeByEscape();
    check(applied(doc) == before + 7 && latestLabel(doc) == "Restore Table values",
          "Closing restores the values as one entry, got " + latestLabel(doc));
    check(near(quantity(doc, placement, "width"), 1.5) &&
              doc.instances().at(placement)->overrides.size() == 1,
          "The placement's Width is back and nothing else");
    check(near(quantity(doc, f.t.wide, "width"), 1.2) &&
              doc.instances().at(f.t.wide)->definition == f.t.definition,
          "The other placement keeps its values on the shared definition");
    return {{"paintLabel", QString::fromStdString(paintLabel)},
            {"moveLabel", QString::fromStdString(moveLabel)},
            {"changedDrivers", changed},
            {"restored", "Width 1500 mm"}};
}

// 4. Make unique and edit: the placement's values become the copy's defaults.
QJsonObject promote(Fixture &f) {
    auto &doc = f.doc();
    const auto placement = f.t.wide;
    const auto before = applied(doc);
    const bool opened = f.openWithDialog(placement, [&](ParametersScopeDialog &dialog) {
        QTest::keyClick(QApplication::focusWidget(), Qt::Key_Backtab);
        check(dialog.uniqueButton()->hasFocus(), "Shift+Tab reaches Make unique and edit");
        QTest::keyClick(dialog.uniqueButton(), Qt::Key_Space);
    });
    check(opened, "The dialog opened");
    check(QTest::qWaitFor([&] { return f.view().componentScope() == placement; }, 3000),
          "Make unique and edit opens the context");
    const auto copy = doc.instances().at(placement)->definition;
    check(copy != f.t.definition && doc.instances().at(placement)->overrides.empty() &&
              applied(doc) == before + 1 && latestLabel(doc) == "Make Table unique",
          "One entry makes the placement unique without values, got " + latestLabel(doc));
    const auto width = defaultOf(doc, placement, "width");
    check(width.find("1.2") != std::string::npos,
          "The copy's default Width is the placement's value, got " + width);
    f.showInfo();
    auto *definition = f.definition();
    check(QTest::qWaitFor([&] { return definition->isVisible(); }), "The copy's parameters show");
    definition->select("shelves");
    f.type(definition->expressionField()->edit(), "2");
    f.enter(definition->expressionField()->edit());
    check(applied(doc) == before + 2 && latestLabel(doc) == "Edit parameter Shelves",
          "Editing the copy is one entry, got " + latestLabel(doc));
    f.closeByEscape();
    check(applied(doc) == before + 2, "Closing a unique copy adds nothing");
    check(doc.instances().at(f.t.defaults)->definition == f.t.definition &&
              defaultOf(doc, f.t.defaults, "shelves") == "1",
          "The shared definition is unaffected by the copy's edit");
    return {{"copy", QString::number(copy)}};
}

// 5. A door and a window cut their openings in the wall; the door's width re-cuts.
QJsonObject openings(Fixture &f) {
    auto &doc = f.doc();
    auto &view = f.view();
    view.enterContext(0);
    view.standardView(2);
    view.frameBounds({-2.5, kWallFront, 0}, {6.5, kWallFront, 2.8});
    QTest::qWait(50);
    auto before = applied(doc);
    f.startPlacing("Interior door");
    f.typeValue("width", "1000");
    const auto doorPoint = f.onWall(0.8, 1.2);
    check(f.hover(doorPoint), "The door's ghost is on the wall: " + text(f.status) + " / " +
                                  text(view.assemblyPlacementError()));
    QTest::mouseClick(&view, Qt::LeftButton, {}, doorPoint);
    check(QTest::qWaitFor([&] { return !view.assemblyPlacing(); }) && applied(doc) == before + 1 &&
              latestLabel(doc) == "Place Interior door",
          "The door is placed in one entry: " + text(f.status));
    const auto door = f.selected();
    check(doc.hostedComponents().attachments.contains(door) &&
              std::abs(f.openingWidth(door) - 1.0) < 1e-9,
          "The door cuts its 1000 mm opening in the same entry");
    before = applied(doc);
    f.startPlacing("Casement window");
    const auto windowPoint = f.onWall(3.6, 1.5);
    check(f.hover(windowPoint), "The window's ghost is on the wall: " + text(f.status));
    QTest::mouseClick(&view, Qt::LeftButton, {}, windowPoint);
    check(QTest::qWaitFor([&] { return !view.assemblyPlacing(); }) && applied(doc) == before + 1 &&
              latestLabel(doc) == "Place Casement window",
          "The window is placed in one entry: " + text(f.status));
    const auto window = f.selected();
    check(doc.hostedComponents().attachments.contains(window) &&
              std::abs(f.openingWidth(window) - 1.2) < 1e-9,
          "The window cuts its 1200 mm opening in the same entry");
    // The door's width in Entity info re-cuts its opening in one entry.
    f.showInfo();
    f.select(door);
    check(QTest::qWaitFor([&] { return f.section()->placement() == door; }),
          "Entity info shows the door");
    before = applied(doc);
    auto *width = f.edit("width");
    f.type(width, "1100");
    f.enter(width);
    check(QTest::qWaitFor([&] { return applied(doc) == before + 1; }) &&
              latestLabel(doc) == "Change Width" && std::abs(f.openingWidth(door) - 1.1) < 1e-9,
          "Changing the door's width re-cuts the opening in one entry, got " + latestLabel(doc) +
              " / " + text(f.field("width")->error()));
    check(std::abs(f.openingWidth(window) - 1.2) < 1e-9, "The window's opening is unchanged");
    const auto hosts = doc.hostedComponents().hosts.at(f.wall)->openings.size();
    check(hosts == 2, "The wall holds two openings");
    return {{"doorOpeningMm", 1100}, {"windowOpeningMm", 1200}, {"openings", int(hosts)}};
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
    QApplication::setAttribute(Qt::AA_DontUseNativeDialogs);
    QApplication app(argc, argv);
    app.setOrganizationName("SketchyUp");
    app.setApplicationName("SketchyUp");
    QSettings().setValue("recoverySeconds", 0);
    QSettings("SketchyUp", "SketchyUp").setValue("libraryFolder", files.filePath("library"));
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
    window.resize(1280, 900);
    try {
        // The ADR 0165 table fixture and a 8 m wall, 200 mm thick and 2.7 m high.
        auto table = makeTable();
        const auto wall = box(table.doc, {-2, kWallFront, 0}, {6, kWallFront + .2, 2.7}, "Wall");
        table.doc.setDisplayUnits(DisplayUnit::Millimeters);
        const auto path = files.filePath("acceptance.sketchyup");
        saveDocument(table.doc, path);
        window.show();
        check(QTest::qWaitForWindowExposed(&window), "Window exposed");
        check(QTest::qWaitFor([&] { return window.viewport()->rendererReady(); }, 10000),
              "Viewport renderer initialized");
        window.activateWindow();
        check(QTest::qWaitForWindowActive(&window), "Window active");
        window.openPath(path);
        Fixture f{window, table};
        QObject::connect(window.viewport(), &Viewport::message,
                         [&f](const QString &value) { f.status = value; });
        auto &doc = f.doc();
        check(doc.instances().contains(table.wide) && doc.bodies().contains(wall),
              "The fixture opened");
        f.wall = wall;
        for (const auto &[id, record] : doc.bodies().at(wall)->surface.faces) {
            (void)record;
            if (doc.bodies().at(wall)->surface.normal(id).y < -.99)
                f.face = id;
        }
        check(f.face != 0, "The wall has a front face");
        const auto start = applied(doc);
        const auto startContent = content(doc);
        QJsonObject summary{{"suite", "component_acceptance_input_tests"}};

        std::cerr << "[acceptance] 1. Place the Dining table with values" << std::endl;
        summary["place"] = placeTable(f);
        std::cerr << "[acceptance] 2. Override Width and Shelves; reset Shelves" << std::endl;
        summary["override"] = overrideAndReset(f);
        std::cerr << "[acceptance] 3. Edit definition at defaults" << std::endl;
        summary["editAtDefaults"] = editAtDefaults(f);
        std::cerr << "[acceptance] 4. Make unique and edit" << std::endl;
        summary["promotion"] = promote(f);
        std::cerr << "[acceptance] 5. Door and window openings" << std::endl;
        summary["openings"] = openings(f);

        // 6. Every History entry's label; undo to the start and redo to the end.
        std::cerr << "[acceptance] 6. History labels, undo and redo" << std::endl;
        const std::vector<std::string> expected{"Place Dining table",
                                                "Change Width",
                                                "Change Shelves",
                                                "Reset Shelves",
                                                "Edit Table at defaults",
                                                "Edit shared component definition",
                                                "Edit parameter Width",
                                                "Add Visibility driver",
                                                "Edit shared component definition",
                                                "Rebind 3 drivers",
                                                "Restore Table values",
                                                "Make Table unique",
                                                "Edit parameter Shelves",
                                                "Place Interior door",
                                                "Place Casement window",
                                                "Change Width"};
        const auto all = labels(doc);
        check(applied(doc) == start + expected.size() && all.size() == start + expected.size(),
              "The session is " + std::to_string(expected.size()) + " entries, got " +
                  std::to_string(applied(doc) - start));
        for (std::size_t i = 0; i < expected.size(); ++i)
            check(all.at(start + i) == expected.at(i), "Entry " + std::to_string(i + 1) +
                                                           " reads " + expected.at(i) + ", got " +
                                                           all.at(start + i));
        f.view().enterContext(0);
        const auto endContent = content(doc);
        for (std::size_t i = 0; i < expected.size(); ++i)
            f.action("edit.undo")->trigger();
        check(applied(doc) == start && content(doc) == startContent,
              "Undo to the start restores every record");
        for (std::size_t i = 0; i < expected.size(); ++i)
            f.action("edit.redo")->trigger();
        check(applied(doc) == start + expected.size() && content(doc) == endContent,
              "Redo to the end restores every record");
        check(near(quantity(doc, table.defaults, "width"), 1.5) &&
                  doc.instances().at(table.wide)->definition != table.definition &&
                  doc.hostedComponents().hosts.at(wall)->openings.size() == 2,
              "After redo: the values, the unique copy and both openings");
        QJsonArray history;
        for (const auto &label : expected)
            history.append(QString::fromStdString(label));
        summary["history"] = history;

        // 7. Save, reopen and save again.
        std::cerr << "[acceptance] 7. Save, reopen and compare bytes" << std::endl;
        f.action("file.save")->trigger();
        check(!doc.dirty(), "Saved");
        const auto first = read(path);
        window.openPath(path);
        window.activateWindow();
        check(QTest::qWaitForWindowActive(&window), "Window active after reopening");
        check(content(window.document()) == endContent, "The model reopens byte-identical");
        check(near(quantity(window.document(), table.defaults, "width"), 1.5) &&
                  window.document().hostedComponents().hosts.at(wall)->openings.size() == 2,
              "Values and openings reopen");
        check(QFile::remove(path), "Remove the first save");
        f.action("file.save")->trigger();
        const auto second = read(path);
        check(second == first, "Saving the reopened model writes the same bytes");
        summary["roundTrip"] = QJsonObject{{"fileBytes", qint64(first.size())},
                                           {"fileSha256", sha256(first)},
                                           {"secondSaveIdentical", true}};
        check(unexpected.empty(), "No unexpected message box: " + unexpected);
        summary["passed"] = true;
        summary["platform"] = QGuiApplication::platformName();
        summary["scale"] = window.devicePixelRatioF();
        summary["checks"] = qint64(checks);
        window.document().markSaved();
        std::cout << QJsonDocument(summary).toJson(QJsonDocument::Compact).constData() << '\n';
        std::cout << "component_acceptance_input_tests: " << checks
                  << " checks passed; DPR=" << window.devicePixelRatioF() << '\n';
        return 0;
    } catch (const std::exception &error) {
        window.document().markSaved();
        std::cerr << error.what() << '\n';
        return 1;
    }
}
