// R094.d: definition authoring, drivers and preview (ADR 0166 §2, §3): the parametric context
// and its banner; "Parameters · definition" (tree, editor, + Parameter, F2, Alt+Up/Down,
// Delete); "Drivers" (tree with state, form, Rebind); Add driver with Shift+D for each member
// kind and the kinds offered; the stretch plane by drag, snapping, Alt and typed value;
// formula fields checked as you type, with the caret and the one-click unit fix; the Preview
// strip's ghost without a revision change; context, selection and materials kept across
// regeneration. Defining the ADR 0165 table fixture entirely through the interface gives
// byte-identical records to the command fixture. Real key events to the focused widget; run on
// X11 and Wayland at 1× and 2×.
#include "app/component_definition_section.hpp"
#include "app/component_drivers_section.hpp"
#include "app/formula_field.hpp"
#include "app/surface_format.hpp"
#include "app/window.hpp"
#include "component_parameter_fixture.hpp"
#include "io/component_parameters_io.hpp"
#include "io/document_io.hpp"
#include <QAction>
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QJsonArray>
#include <QJsonDocument>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QLineEdit>
#include <QPushButton>
#include <QSettings>
#include <QSpinBox>
#include <QTabWidget>
#include <QTemporaryDir>
#include <QTest>
#include <QToolBar>
#include <QToolButton>
#include <QTreeWidget>
#include <cmath>
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
std::string latestLabel(const Document &doc) {
    const auto page = doc.history();
    for (auto it = page.entries.rbegin(); it != page.entries.rend(); ++it)
        if (it->applied)
            return it->label;
    return {};
}
std::size_t applied(const Document &doc) { return doc.history(0, 1).position; }
QByteArray records(const Document &doc) {
    return QJsonDocument(encodeComponentParameters(doc.definitions())).toJson(QJsonDocument::Compact);
}
QByteArray overrides(const Document &doc) {
    return QJsonDocument(encodeComponentParameterOverrides(doc.instances()))
        .toJson(QJsonDocument::Compact);
}
std::string difference(const QByteArray &a, const QByteArray &b) {
    qsizetype at = 0;
    while (at < a.size() && at < b.size() && a[at] == b[at])
        ++at;
    return " (differs at " + std::to_string(at) + ": " +
           a.mid(std::max<qsizetype>(0, at - 100), 220).toStdString() + " | " +
           b.mid(std::max<qsizetype>(0, at - 100), 220).toStdString() + ")";
}
// The ADR 0165 table before any parameter or driver: the geometry, grouped and made a
// component, exactly as parameter_fixture::makeTable() builds it.
struct Raw {
    Document doc;
    Id top{}, legLeftFront{}, legLeftBack{}, legRightFront{}, legRightBack{}, shelf{};
    Id definition{}, first{};
};
Raw rawTable() {
    Raw r;
    auto &doc = r.doc;
    r.top = box(doc, {0, 0, .72}, {.9, .6, .75}, "Top");
    r.legLeftFront = box(doc, {0, 0, 0}, {.05, .05, .72}, "Leg left front");
    r.legLeftBack = box(doc, {0, .55, 0}, {.05, .6, .72}, "Leg left back");
    r.legRightFront = box(doc, {.84, 0, 0}, {.89, .05, .72}, "Leg right front");
    r.legRightBack = box(doc, {.84, .55, 0}, {.89, .6, .72}, "Leg right back");
    r.shelf = box(doc, {.05, .05, .2}, {.84, .55, .22}, "Shelf");
    const auto group = createGroup(
        doc, {r.top, r.legLeftFront, r.legLeftBack, r.legRightFront, r.legRightBack, r.shelf},
        "Table");
    const auto created = createComponent(doc, group, "Table");
    r.definition = created.definition;
    r.first = created.instance;
    return r;
}
struct Fixture {
    Window &window;
    QTemporaryDir &files;
    Document &doc() { return window.document(); }
    Viewport *view() { return window.viewport(); }
    ComponentDefinitionSection *definition() {
        return window.findChild<ComponentDefinitionSection *>("componentDefinitionSection");
    }
    ComponentDriversSection *drivers() {
        return window.findChild<ComponentDriversSection *>("componentDriversSection");
    }
    QLabel *status() { return window.findChild<QLabel *>("statusMessage"); }
    void open(Document &source, const QString &name) {
        const auto path = files.filePath(name);
        saveDocument(source, path);
        window.document().markSaved();
        window.openPath(path);
        window.findChild<QTabWidget *>("organizationTabs")->setCurrentIndex(2);
        QTest::qWait(30);
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
    void choose(QComboBox *combo, const QString &data) {
        const int index = combo->findData(data);
        check(index >= 0, "The " + text(combo->objectName()) + " list offers " + text(data));
        combo->setCurrentIndex(index);
    }
    void select(Id body) {
        view()->setSelection(body);
        QTest::qWait(10);
    }
    // Opens a placement's context the way the keyboard does: select it, then Enter.
    void openContext(Id placement) {
        view()->enterContext(0);
        select(placement);
        focus(view());
        QTest::keyClick(view(), Qt::Key_Return);
        check(view()->componentScope() == placement, "Enter opens the placement's context");
        QTest::qWait(10);
    }
    void closeContexts() {
        view()->enterContext(0);
        QTest::qWait(10);
    }
    // Shift+D on the viewport with `body` selected.
    void shiftD(Id body) {
        select(body);
        focus(view());
        QTest::keyClick(view(), Qt::Key_D, Qt::ShiftModifier);
        QTest::qWait(20);
    }
    void undo() { window.findChild<QAction *>("edit.undo")->trigger(); }
    // + Parameter, the editor's fields and Enter in the default or formula field.
    void addParameter(const QString &label, const QString &name, const QString &type,
                      const QString &mode, const QString &expression, const QString &min = {},
                      const QString &max = {}, const QString &step = {},
                      const QString &options = {}) {
        auto *section = definition();
        const auto before = applied(doc());
        focus(section->addButton());
        QTest::keyClick(section->addButton(), Qt::Key_Space);
        check(section->creating() && section->labelEdit()->hasFocus(),
              "+ Parameter opens the editor in create mode with focus on Label");
        this->type(section->labelEdit(), label);
        this->type(section->nameEdit(), name);
        choose(section->typeChoice(), type);
        choose(section->modeChoice(), mode);
        if (!min.isEmpty())
            this->type(section->minimumEdit(), min);
        if (!max.isEmpty())
            this->type(section->maximumEdit(), max);
        if (!step.isEmpty())
            this->type(section->stepEdit(), step);
        if (!options.isEmpty())
            this->type(section->optionsEdit(), options);
        this->type(section->expressionField()->edit(), expression);
        enter(section->expressionField()->edit());
        check(applied(doc()) == before + 1 && latestLabel(doc()) == "Add parameter " + text(label),
              "Enter adds " + text(name) + " as one entry labelled Add parameter, got " +
                  latestLabel(doc()) + "; editor: " + text(section->editorError()->text()) +
                  " / " + text(section->expressionField()->error()));
        check(!section->creating() && section->current() == name,
              "The new parameter is selected in the tree");
    }
    // Shift+D on a member, the form, and Enter in the formula field.
    void addDriver(Id body, const QString &kind, const QString &axis, const QString &formula,
                   const QString &spacing = {}, int maxCount = 0, const QString &child = {}) {
        auto *section = drivers();
        const auto before = applied(doc());
        shiftD(body);
        check(section->creating() && !section->editor()->isHidden(),
              "Shift+D opens the driver form for the selected part");
        check(QTest::qWaitFor([&] { return section->kindChoice()->hasFocus(); }),
              "Focus moves to the form's Kind");
        choose(section->kindChoice(), kind);
        if (!axis.isEmpty())
            choose(section->axisChoice(), axis);
        if (!child.isEmpty())
            choose(section->childChoice(), child);
        if (maxCount)
            section->maxCountSpin()->setValue(maxCount);
        if (!spacing.isEmpty())
            type(section->spacingField()->edit(), spacing);
        type(section->formulaField()->edit(), formula);
        enter(section->formulaField()->edit());
        const auto title = ComponentDriversSection::kindTitle(kind);
        check(applied(doc()) == before + 1 && latestLabel(doc()) == "Add " + text(title) + " driver",
              "Enter adds the " + text(kind) + " driver as one labelled entry, got " +
                  latestLabel(doc()) + "; form: " + text(section->editorError()->text()) + " / " +
                  text(section->formulaField()->error()));
    }
};
QTreeWidgetItem *driverRow(QTreeWidget *tree, const QString &kind, const QString &target) {
    for (int i = 0; i < tree->topLevelItemCount(); ++i) {
        auto *item = tree->topLevelItem(i);
        if (item->text(0) == kind && item->text(1).startsWith(target))
            return item;
    }
    return nullptr;
}

// ADR 0166 Verify: the table of ADR 0165 §1 defined entirely through the interface gives the
// same records, byte for byte, as the command fixture.
void fixtureThroughInterface(Fixture &f) {
    auto raw = rawTable();
    raw.doc.setDisplayUnits(DisplayUnit::Meters);
    f.open(raw.doc, "raw-table.sketchyup");
    auto &doc = f.doc();
    check(doc.instances().contains(raw.first), "The raw table opened");
    f.openContext(raw.first);
    auto *section = f.definition();
    check(section->isVisible() && section->scope().definition == raw.definition,
          "Parameters · definition shows in the definition context");
    check(section->findChild<QLabel *>("definitionParametersEmpty")->isVisible(),
          "A definition without parameters says how to add one");
    f.addParameter("Width", "width", "length", "input", "900 mm", "0.4", "2.4");
    // A parametric context reads "Model › Table (definition)" with the definition banner.
    auto *breadcrumb = f.window.findChild<QLabel *>("contextBreadcrumb");
    auto *banner = f.window.findChild<QLabel *>("componentScopeBanner");
    check(breadcrumb->text().contains("Table (definition)"),
          "The breadcrumb names the definition, got " + text(breadcrumb->text()));
    check(banner->isVisible() && banner->text().contains("default values") &&
              banner->text().contains("Changes apply to all 1 placement;") &&
              banner->text().contains("Close definition"),
          "The banner is the definition strip, got " + text(banner->text()));
    f.addParameter("Shelves", "shelves", "count", "input", "1", "0", "4", "1");
    f.addParameter("Finish", "finish", "choice", "input", "\"oak\"", {}, {}, {},
                   "oak = Oak; ash = Ash \"natural\"");
    f.addParameter("Open shelf", "open", "boolean", "input", "true");
    f.addParameter("Leg tilt", "tilt", "angle", "input", "0 deg", "-0.5 rad", "0.5 rad");
    f.addParameter("Leg position", "leg_x", "length", "derived", "width-60 mm");
    f.addParameter("Top area", "top_area", "area", "derived", "width * 600 mm");
    f.addParameter("Width ratio", "ratio", "number", "derived", "width / 1 m");
    check(section->tree()->topLevelItemCount() == 8, "The tree lists eight parameters");
    // Drivers: Shift+D on each driven member, in the fixture's order.
    auto *drivers = f.drivers();
    f.shiftD(raw.top);
    check(drivers->planeActive() && f.view()->stretchPlane() &&
              std::abs(f.view()->stretchPlane()->at - .45) < 1e-15 &&
              drivers->axisChoice()->currentData().toString() == "x",
          "Stretch starts on the longest axis with the plane at the middle of the bounds");
    QTest::keyClick(drivers->kindChoice(), Qt::Key_Escape);
    check(drivers->editor()->isHidden() && !f.view()->stretchPlane(),
          "Esc closes the form and the plane");
    f.addDriver(raw.top, "stretch", "x", "width");
    f.addDriver(raw.legRightFront, "position", "x", "leg_x");
    f.addDriver(raw.legRightBack, "position", "x", "leg_x");
    f.addDriver(raw.legLeftFront, "rotation", "z", "tilt");
    f.addDriver(raw.legLeftBack, "visibility", {}, "open");
    // The retired driver: Delete on its row removes it and its ID is never reused.
    auto *row = driverRow(drivers->tree(), "Visibility", "Leg left back");
    check(row, "The visibility driver is listed");
    f.focus(drivers->tree());
    drivers->tree()->setCurrentItem(row);
    check(f.view()->selectionState().entities().size() == 1 &&
              f.view()->selectionState().entities().begin()->body == raw.legLeftBack,
          "Selecting a driver row selects its member in the viewport");
    f.focus(drivers->tree());
    QTest::keyClick(drivers->tree(), Qt::Key_Delete);
    check(latestLabel(doc) == "Remove Visibility driver" &&
              !driverRow(drivers->tree(), "Visibility", "Leg left back"),
          "Delete removes the driver as one labelled entry, got " + latestLabel(doc));
    f.addDriver(raw.shelf, "repeat", "z", "max(shelves,1)", "300 mm", 4);
    f.addDriver(raw.shelf, "visibility", {}, "shelves > 0");
    check(drivers->tree()->topLevelItemCount() == 6, "Six drivers are listed");
    auto *stretchRow = driverRow(drivers->tree(), "Stretch", "Top");
    check(stretchRow && stretchRow->text(1) == "Top · x past 0.45 m" &&
              stretchRow->data(0, Qt::AccessibleTextRole).toString() ==
                  "Stretch, Top · x past 0.45 m, width, Bound",
          "A driver row reads kind, target, formula and state, got " +
              text(stretchRow ? stretchRow->data(0, Qt::AccessibleTextRole).toString() : ""));
    // The room nests the table and drives its width through childParameter.
    f.closeContexts();
    const auto floor = box(doc, {-1, -1, -.1}, {3, 3, 0}, "Floor");
    const auto roomGroup = createGroup(doc, {raw.first, floor}, "Room");
    const auto room = createComponent(doc, roomGroup, "Room");
    f.view()->refresh();
    emit f.view()->changed();
    f.openContext(room.instance);
    f.addParameter("Table width", "table_width", "length", "input", "1500 mm");
    f.shiftD(raw.first);
    QStringList kinds;
    for (int i = 0; i < drivers->kindChoice()->count(); ++i)
        kinds << drivers->kindChoice()->itemData(i).toString();
    check(kinds == QStringList{"position", "rotation", "visibility", "repeat", "childParameter"},
          "A nested placement offers its kinds and Child parameter, got " +
              text(kinds.join(",")));
    QTest::keyClick(drivers->kindChoice(), Qt::Key_Escape);
    f.addDriver(raw.first, "childParameter", {}, "table_width", {}, 0, "width");
    f.closeContexts();
    overrideComponentParameters(doc, room.instance, {{"table_width", metres(1.8)}});
    setComponentAssembly(doc, room.definition, ComponentAssembly{"sketchyup-tests/room", 1});
    const auto defaults =
        placeComponent(doc, raw.definition, Transform::translation({0, 4, 0})).instance;
    const auto wide = placeComponent(doc, raw.definition, Transform::translation({4, 0, 0})).instance;
    overrideComponentParameters(
        doc, wide,
        {{"width", metres(1.2)}, {"shelves", count(3)}, {"finish", ParameterChoice{"ash"}}});
    f.view()->refresh();
    emit f.view()->changed();
    const auto reference = makeTable();
    check(room.definition == reference.room && defaults == reference.defaults &&
              wide == reference.wide,
          "The interface built the same records under the same IDs");
    check(records(doc) == records(reference.doc),
          "Parameter records are byte-identical to the command fixture" +
              difference(records(doc), records(reference.doc)));
    check(overrides(doc) == overrides(reference.doc),
          "Placement overrides are byte-identical to the command fixture");
    check(doc.nextComponentDriverId() == reference.doc.nextComponentDriverId(),
          "The driver allocator matches, so the retired ID stays retired");
}

// Parameters · definition: editing, F2, Alt+Up/Down, Delete and their labels (§3.2).
void parameterEditing(Fixture &f, const Table &t) {
    f.openContext(t.defaults);
    auto &doc = f.doc();
    auto *section = f.definition();
    auto *tree = section->tree();
    check(tree->topLevelItemCount() == 8 && tree->topLevelItem(0)->text(0) == "Width\nwidth" &&
              tree->topLevelItem(0)->text(1) == "Length\n400–2400 mm" &&
              tree->topLevelItem(0)->text(2) == "900 mm" &&
              tree->topLevelItem(5)->text(2) == "= width - 60 mm" &&
              tree->topLevelItem(5)->text(1) == "Length\nderived",
          "Rows show the label over the name, the type with bounds or derived, and the default "
          "or formula; got " + text(tree->topLevelItem(0)->text(1)));
    f.focus(tree);
    tree->setCurrentItem(tree->topLevelItem(0));
    check(section->current() == "width" && !section->editor()->isHidden() &&
              section->usedByLabel()->text().contains("Leg position") &&
              section->usedByLabel()->text().contains("Stretch Top"),
          "Selecting a row opens its editor with Used by, got " +
              text(section->usedByLabel()->text()));
    check(section->findChild<QLabel *>("parameterEditorNote")->isVisibleTo(section) &&
              section->findChild<QLabel *>("parameterEditorNote")->text() ==
                  "Changing the default re-baselines the shape and keeps each placement's own "
                  "values.",
          "The editor notes what changing a default does");
    // Enter moves into the editor; editing the default is one "Edit parameter" entry.
    f.focus(tree);
    QTest::keyClick(tree, Qt::Key_Return);
    check(section->labelEdit()->hasFocus(), "Enter on the tree moves into the editor");
    auto before = applied(doc);
    f.type(section->expressionField()->edit(), "1000 mm");
    f.enter(section->expressionField()->edit());
    check(applied(doc) == before + 1 && latestLabel(doc) == "Edit parameter Width" &&
              tree->topLevelItem(0)->text(2) == "1000 mm",
          "Enter commits the default as Edit parameter Width, got " + latestLabel(doc));
    f.undo();
    // Esc restores the edited fields; nothing is committed.
    before = applied(doc);
    f.type(section->labelEdit(), "Breadth");
    QTest::keyClick(section->labelEdit(), Qt::Key_Escape);
    check(section->labelEdit()->text() == "Width" && applied(doc) == before,
          "Esc restores the editor without an entry");
    // F2 renames: focus goes to Name; the entry is "Rename parameter width".
    f.focus(tree);
    QTest::keyClick(tree, Qt::Key_F2);
    check(section->nameEdit()->hasFocus(), "F2 on the tree focuses the Name field");
    QTest::keyClicks(section->nameEdit(), "span");
    f.enter(section->nameEdit());
    check(latestLabel(doc) == "Rename parameter width" && section->current() == "span" &&
              doc.definitions().at(t.definition)->parameters->parameters[0].name == "span" &&
              doc.definitions().at(t.definition)->parameters->parameters[5].expression ==
                  "span - 60 mm",
          "F2 then Enter renames the parameter and its references, got " + latestLabel(doc));
    f.undo();
    f.closeContexts();
    f.openContext(t.defaults);
    // A malformed or taken name is refused at the field.
    f.focus(tree);
    tree->setCurrentItem(tree->topLevelItem(0));
    f.type(section->nameEdit(), "Width");
    f.enter(section->nameEdit());
    check(section->editorError()->isVisibleTo(section) &&
              section->editorError()->text() ==
                  "Use lowercase letters, digits and _, starting with a letter.",
          "A malformed name is refused, got " + text(section->editorError()->text()));
    f.type(section->nameEdit(), "shelves");
    f.enter(section->nameEdit());
    check(section->editorError()->text() == "shelves is already a parameter.",
          "A taken name is refused, got " + text(section->editorError()->text()));
    QTest::keyClick(section->nameEdit(), Qt::Key_Escape);
    // Alt+Down and Alt+Up move the row (position) as "Move parameter Width".
    f.focus(tree);
    tree->setCurrentItem(tree->topLevelItem(0));
    f.focus(tree);
    before = applied(doc);
    QTest::keyClick(tree, Qt::Key_Down, Qt::AltModifier);
    check(applied(doc) == before + 1 && latestLabel(doc) == "Move parameter Width" &&
              tree->topLevelItem(1)->data(0, Qt::UserRole).toString() == "width" &&
              tree->currentItem() == tree->topLevelItem(1),
          "Alt+Down moves the parameter down one row, got " + latestLabel(doc));
    QTest::keyClick(tree, Qt::Key_Up, Qt::AltModifier);
    check(tree->topLevelItem(0)->data(0, Qt::UserRole).toString() == "width",
          "Alt+Up moves it back");
    // Delete is refused while the parameter is referenced, listing what uses it.
    before = applied(doc);
    QTest::keyClick(tree, Qt::Key_Delete);
    check(applied(doc) == before && section->editorError()->isVisibleTo(section) &&
              section->editorError()->text().startsWith("Still used by Leg position"),
          "Delete is refused while referenced, got " + text(section->editorError()->text()));
    // An unreferenced parameter is removed as one entry.
    for (int i = 0; i < tree->topLevelItemCount(); ++i)
        if (tree->topLevelItem(i)->data(0, Qt::UserRole).toString() == "ratio")
            tree->setCurrentItem(tree->topLevelItem(i));
    f.focus(tree);
    QTest::keyClick(tree, Qt::Key_Delete);
    check(latestLabel(doc) == "Remove parameter Width ratio" && tree->topLevelItemCount() == 7,
          "Delete removes an unreferenced parameter, got " + latestLabel(doc));
    f.undo();
    // Esc in create mode closes the editor.
    f.focus(section->addButton());
    QTest::keyClick(section->addButton(), Qt::Key_Space);
    QTest::keyClick(section->labelEdit(), Qt::Key_Escape);
    check(!section->creating() && section->editor()->isHidden(), "Esc cancels create mode");
    f.closeContexts();
}

// Formula fields: checked as you type, caret, unit fix, cycle (§3.5, §5).
void formulaFields(Fixture &f, const Table &t) {
    f.openContext(t.defaults);
    auto &doc = f.doc();
    auto *section = f.definition();
    auto *tree = section->tree();
    for (int i = 0; i < tree->topLevelItemCount(); ++i)
        if (tree->topLevelItem(i)->data(0, Qt::UserRole).toString() == "leg_x")
            tree->setCurrentItem(tree->topLevelItem(i));
    auto *field = section->expressionField();
    check(field->edit()->accessibleName() == "Formula", "A derived row edits its Formula");
    // Syntax: a missing closing parenthesis, with the caret one past the end.
    const auto before = applied(doc);
    f.type(field->edit(), "width - (120 mm");
    check(field->error() == "A closing \")\" is missing at character 16." &&
              field->caretIndex() == 15 && field->caret()->isVisibleTo(field) &&
              field->edit()->property("invalid").toBool() &&
              field->edit()->accessibleDescription() ==
                  "Invalid: A closing \")\" is missing at character 16.",
          "A syntax error is checked as you type with its caret, got " + text(field->error()));
    // The caret marker sits under the end of the text, by the field's own font metrics.
    const auto metrics = field->edit()->fontMetrics();
    const int caretX = field->caret()->geometry().center().x();
    const int textEnd = field->edit()->x() +
                        field->edit()->inputMethodQuery(Qt::ImCursorRectangle).toRect().center().x();
    check(std::abs(caretX - textEnd) <= metrics.averageCharWidth() + 2,
          "The caret marks the character the error names");
    f.enter(field->edit());
    check(applied(doc) == before && doc.definitions().at(t.definition)->parameters->parameters[5]
                                            .expression == "width - 60 mm",
          "An invalid formula is never committed; the document keeps the last valid one");
    // Units: a bare number offers "Use 120 mm", shown only because the fixed text checks clean.
    f.type(field->edit(), "width - 120");
    check(field->error() ==
              "Can't subtract a number from a length. Add a unit, for example 120 mm." &&
              field->fixButton()->isVisibleTo(field) && field->fixButton()->text() == "Use 120 mm",
          "A bare number offers the unit fix, got " + text(field->error()));
    QTest::mouseClick(field->fixButton(), Qt::LeftButton);
    check(field->edit()->text() == "width - 120 mm" && field->error().isEmpty() &&
              !field->fixButton()->isVisibleTo(field) && field->edit()->hasFocus(),
          "The fix edits the text, which then checks clean");
    f.enter(field->edit());
    check(applied(doc) == before + 1 && latestLabel(doc) == "Edit parameter Leg position" &&
              field->edit()->text() == "width - 120 mm",
          "Enter still commits; the field shows the engine's canonical text, got " +
              latestLabel(doc) + " / " + text(field->edit()->text()) + " / " +
              text(field->error()) + " / " + text(section->editorError()->text()) + " / " +
              text(section->current()));
    f.undo();
    // Esc restores the stored formula.
    f.type(field->edit(), "width +");
    check(!field->error().isEmpty(), "An incomplete formula shows an error");
    QTest::keyClick(field->edit(), Qt::Key_Escape);
    check(field->edit()->text() == "width - 60 mm" && field->error().isEmpty(),
          "Esc restores the last valid formula");
    // Names: "No parameter is called depth."
    f.type(field->edit(), "depth - 10 mm");
    check(field->error() == "No parameter is called depth." && field->caretIndex() == 0,
          "An unknown name is named, got " + text(field->error()));
    QTest::keyClick(field->edit(), Qt::Key_Escape);
    // A cycle is found by the dry run of the real command, 300 ms after typing stops.
    for (int i = 0; i < tree->topLevelItemCount(); ++i)
        if (tree->topLevelItem(i)->data(0, Qt::UserRole).toString() == "top_area")
            tree->setCurrentItem(tree->topLevelItem(i));
    f.type(field->edit(), "leg_x * 600 mm");
    f.enter(field->edit());
    check(latestLabel(doc) == "Edit parameter Top area", "Top area now reads leg_x");
    for (int i = 0; i < tree->topLevelItemCount(); ++i)
        if (tree->topLevelItem(i)->data(0, Qt::UserRole).toString() == "leg_x")
            tree->setCurrentItem(tree->topLevelItem(i));
    f.type(field->edit(), "top_area / 600 mm");
    check(field->error().isEmpty(), "The cheap check passes: names and units are fine");
    check(QTest::qWaitFor([&] { return !field->error().isEmpty(); }, 2000) &&
              field->error() == "These depend on each other: Leg position → Top area → Leg "
                                "position. One of them needs a value instead of a formula.",
          "The dry run reports the cycle by labels, got " + text(field->error()));
    QTest::keyClick(field->edit(), Qt::Key_Escape);
    f.undo();
    // Longer than 1,024 characters or not ASCII: the field refuses the input with the reason.
    f.type(field->edit(), "width");
    QKeyEvent accented(QEvent::KeyPress, Qt::Key_Eacute, Qt::NoModifier, QString::fromUtf8("é"));
    QApplication::sendEvent(field->edit(), &accented);
    check(field->edit()->text() == "width" && field->error().startsWith("Formulas use ASCII"),
          "Non-ASCII input is refused, got " + text(field->error()));
    QTest::keyClick(field->edit(), Qt::Key_Escape);
    f.closeContexts();
    // The sentences of the formula rows of §5.
    const auto label = [](const QString &name) { return name == "width" ? QString("Width") : name; };
    check(formulaErrorSentence({{"code", "PARAMETER_DOMAIN"},
                                {"message", "PARAMETER_DOMAIN at byte 9: division by zero"},
                                {"details", QJsonObject{{"offset", 9}}}},
                               "width / 0", label) == "Division by zero at character 9.",
          "Division by zero names its character");
    check(formulaErrorSentence({{"code", "PARAMETER_DOMAIN"},
                                {"message", "count is not a whole number; use round()"}},
                               "width / 3 mm", label) ==
              "The value must be a whole number; use round().",
          "A count asks for a whole number");
    check(formulaErrorSentence({{"code", "PARAMETER_DRIVER_TARGET"},
                                {"message", "driver #3: curve #2 crosses the stretch plane"}},
                               "width", label) ==
              "The arc crosses the stretch plane; move the plane.",
          "A curve crossing the plane asks to move it");
}

// Add driver: the kinds each member accepts, Shift+D's refusals and the rail action (§3.3).
void memberKinds(Fixture &f) {
    Document doc;
    const auto a = box(doc, {0, 0, 0}, {1, .1, 1}, "Panel");
    const auto b = box(doc, {2, 0, 0}, {2.5, .5, .5}, "Block");
    const auto g = createGroup(doc, {b}, "Group");
    const auto c = box(doc, {4, 0, 0}, {4.5, .5, .5}, "Knob");
    const auto knob = createComponent(doc, createGroup(doc, {c}, "Knob group"), "Knob");
    defineComponentParameter(doc, knob.definition,
                             parameter("size", "Size", ParameterType::Length,
                                       ComponentParameterMode::Input, "500 mm"));
    const auto kit = createComponent(doc, createGroup(doc, {a, g, knob.instance}, "Kit"), "Kit");
    // The panel's face is the glue face: no rotation, and no position along its normal.
    const auto &panel = *doc.bodies().at(a);
    Id face = 0;
    for (const auto &[id, record] : panel.surface.faces)
        if (panel.surface.normal(id).y < -.9)
            face = id;
    setComponentGlue(doc, kit.definition, ComponentGlue{a, face, {}, {1, 0, 0}, false});
    doc.setDisplayUnits(DisplayUnit::Millimeters);
    f.open(doc, "kinds.sketchyup");
    auto *drivers = f.drivers();
    auto *add = f.window.findChild<QAction *>("component.add_driver");
    auto *rail = f.window.findChild<QToolBar *>("toolRail");
    auto *railAction = f.window.findChild<QAction *>("railAddDriver");
    check(!add->isEnabled() && !railAction->isVisible(),
          "Add driver is off, and not on the rail, outside a definition context");
    f.openContext(kit.instance);
    // The rail overflows into its extension at this window size, as Orbit and Pan already do;
    // the action is the rail's last, after a separator, and shown only in the context.
    const auto railActions = rail->actions();
    check(add->isEnabled() && railAction->isVisible() && railActions.last() == railAction &&
              railActions[railActions.size() - 2]->isSeparator() &&
              railActions[railActions.size() - 2]->isVisible(),
          "In a definition context the rail shows Add driver after a separator");
    const auto offered = [&] {
        QStringList kinds;
        for (int i = 0; i < drivers->kindChoice()->count(); ++i)
            kinds << drivers->kindChoice()->itemData(i).toString();
        return kinds.join(",");
    };
    // Nothing selected.
    f.view()->selectEntities({});
    f.focus(f.view());
    QTest::keyClick(f.view(), Qt::Key_D, Qt::ShiftModifier);
    check(f.status()->text() == "Select a part inside the definition, then Shift+D" &&
              drivers->editor()->isHidden(),
          "With nothing selected Shift+D says what to select, got " + text(f.status()->text()));
    f.shiftD(g);
    check(offered() == "position,rotation,visibility,repeat",
          "A group offers position, rotation, visibility and repeat, got " + text(offered()));
    QTest::keyClick(drivers->kindChoice(), Qt::Key_Escape);
    f.shiftD(knob.instance);
    check(offered() == "position,rotation,visibility,repeat,childParameter",
          "A nested placement adds Child parameter, got " + text(offered()));
    f.choose(drivers->kindChoice(), "childParameter");
    check(drivers->childChoice()->count() == 1 &&
              drivers->childChoice()->currentData().toString() == "size",
          "Child parameter lists the child definition's input parameters");
    QTest::keyClick(drivers->kindChoice(), Qt::Key_Escape);
    f.shiftD(a);
    check(offered() == "stretch,position,visibility,repeat",
          "The glue member offers no rotation, got " + text(offered()));
    f.choose(drivers->kindChoice(), "position");
    check(drivers->axisChoice()->count() == 2 && drivers->axisChoice()->findData("y") < 0,
          "Position on the glue member leaves out the glue normal's axis");
    check(drivers->axisChoice()->itemData(0, Qt::AccessibleTextRole).toString() ==
              "x axis (red), the part's parent frame",
          "Each axis is named with its letter and colour, got " +
              text(drivers->axisChoice()->itemData(0, Qt::AccessibleTextRole).toString()));
    QTest::keyClick(drivers->kindChoice(), Qt::Key_Escape);
    // The rail action and + Driver do what Shift+D does.
    f.select(g);
    railAction->trigger();
    check(drivers->creating() && offered() == "position,rotation,visibility,repeat",
          "The rail's Add driver opens the form");
    QTest::keyClick(drivers->kindChoice(), Qt::Key_Escape);
    f.select(g);
    drivers->addButton()->click();
    check(drivers->creating(), "+ Driver opens the form");
    QTest::keyClick(drivers->kindChoice(), Qt::Key_Escape);
    // The root and a missing member have none.
    QString why;
    check(ComponentDriversSection::kindsFor(f.doc(), kit.definition,
                                            f.doc().definitions().at(kit.definition)->root, &why)
                  .isEmpty() &&
              why.startsWith("The definition itself can't be driven"),
          "The root offers nothing and says why");
    f.closeContexts();
    check(!add->isEnabled() && !railAction->isVisible(), "Closing the context hides the rail");
}
void repeatCopies(Fixture &f, const Table &t) {
    // The wide placement has shelf copies; a copy is generated, so it cannot be driven. Its
    // context is opened directly: Enter on a placement with its own values asks first (R094.e).
    f.closeContexts();
    f.view()->enterContext(t.wide);
    QTest::qWait(10);
    const auto &instance = *f.doc().instances().at(t.wide);
    const auto &definition = *f.doc().definitions().at(t.definition);
    Id copy = 0;
    for (const auto &[member, scene] : instance.members)
        if (!definition.members.contains(member))
            copy = scene;
    check(copy != 0, "The wide placement shows repeat copies");
    f.shiftD(copy);
    check(f.status()->text().startsWith("A repeat copy can't be driven") &&
              f.drivers()->editor()->isHidden(),
          "A repeat copy is refused with the reason, got " + text(f.status()->text()));
    f.closeContexts();
}

// The stretch plane by drag, snapping, Alt and typed value (§3.4).
void stretchPlane(Fixture &f, const Table &t) {
    f.openContext(t.defaults);
    auto *drivers = f.drivers();
    auto *view = f.view();
    view->standardView(2);
    view->fit();
    QTest::qWait(30);
    const auto topScene = authoring::sceneOf(f.doc(), t.defaults, t.top);
    f.shiftD(topScene);
    check(drivers->kindChoice()->currentData().toString() == "stretch" && drivers->planeActive(),
          "A geometry member's form starts on Stretch with its plane");
    auto *moving = drivers->movingLabel();
    check(moving->text() == "4 of 8 points move",
          "The form states how many points move, got " + text(moving->text()));
    const auto revision = f.doc().revision();
    // Drag the plane towards the member's end: it snaps within 8 px to the end coordinate.
    const auto snaps = view->stretchSnaps();
    check(snaps.size() == 3 && std::abs(snaps[0]) < 1e-12 && std::abs(snaps[1] - .45) < 1e-12 &&
              std::abs(snaps[2] - .9) < 1e-12,
          "The plane snaps to the distinct coordinates and the midpoints between them");
    const auto start = view->stretchPlanePoint(.45);
    const auto end = view->stretchPlanePoint(.9);
    const auto near = view->stretchPlanePoint(.88);
    check(std::hypot(near.x() - end.x(), near.y() - end.y()) < 8,
          "The test point lies within the snap radius");
    QTest::mousePress(view, Qt::LeftButton, {}, start.toPoint());
    QTest::mouseMove(view, ((start + near) / 2).toPoint());
    QTest::mouseMove(view, near.toPoint());
    QTest::mouseRelease(view, Qt::LeftButton, {}, near.toPoint());
    check(view->stretchPlane() && std::abs(view->stretchPlane()->at - .9) < 1e-12 &&
              drivers->planeEdit()->text() == "900 mm" && moving->text() == "0 of 8 points move",
          "Dragging moves the plane and snaps it to the member's end, got " +
              std::to_string(view->stretchPlane() ? view->stretchPlane()->at : -1));
    // With Alt the plane does not snap.
    const auto from = view->stretchPlanePoint(.9);
    QTest::mousePress(view, Qt::LeftButton, Qt::AltModifier, from.toPoint());
    // QTest moves carry no modifiers: the move with Alt held is sent as the platform sends it.
    QMouseEvent altMove(QEvent::MouseMove, near, view->mapToGlobal(near), Qt::NoButton,
                        Qt::LeftButton, Qt::AltModifier);
    QApplication::sendEvent(view, &altMove);
    QTest::mouseRelease(view, Qt::LeftButton, Qt::AltModifier, near.toPoint());
    check(view->stretchPlane() && std::abs(view->stretchPlane()->at - .9) > 1e-4 &&
              std::abs(view->stretchPlane()->at - .88) < .02,
          "Alt drags without snapping, got " + std::to_string(view->stretchPlane()->at));
    // Typed in the form's Plane at field.
    auto *plane = drivers->planeEdit();
    f.type(plane, "300");
    check(std::abs(view->stretchPlane()->at - .3) < 1e-12 && moving->text() == "4 of 8 points move",
          "The Plane at field moves the plane");
    // The Measurements box is labelled "Plane"; Enter moves the plane and commits nothing.
    auto *box = f.window.findChild<QLineEdit *>("measurements");
    auto *step = f.window.findChild<QLabel *>("measurementStep");
    check(step->isVisible() && step->text() == "Plane" &&
              box->accessibleName().startsWith("Plane, Measurements"),
          "The Measurements box names the plane, got " + text(step->text()));
    f.focus(box);
    box->clear();
    QTest::keyClicks(box, "600");
    QTest::keyClick(box, Qt::Key_Return);
    check(std::abs(view->stretchPlane()->at - .6) < 1e-12 && f.doc().revision() == revision,
          "Enter in the box moves the plane without committing the driver");
    check(drivers->checkForm().isEmpty(), "The plane's dry run passes");
    QTest::keyClick(drivers->formulaField()->edit(), Qt::Key_Escape);
    check(!view->stretchPlane() && !step->isVisible(), "Closing the form removes the plane");
    f.closeContexts();
}

// The Preview strip and its ghost (§2, §3.1): never a revision, History or dirty change.
void previewGhost(Fixture &f, const Table &t) {
    f.doc().markSaved();
    f.openContext(t.defaults);
    auto &doc = f.doc();
    auto *view = f.view();
    view->standardView(2);
    view->fit();
    auto *strip = f.window.findChild<QFrame *>("parameterPreviewStrip");
    auto *banner = f.window.findChild<QLabel *>("componentScopeBanner");
    check(strip && strip->isVisible() && strip->geometry().top() >= banner->geometry().bottom(),
          "The Preview strip shows under the banner");
    auto *width = strip->findChild<QLineEdit *>("parameterPreview_width");
    auto *shelves = strip->findChild<QLineEdit *>("parameterPreview_shelves");
    check(width && shelves && width->text() == "900 mm" && shelves->text() == "1" &&
              width->accessibleName() == "Width" && !strip->findChild<QWidget *>("parameterPreview_leg_x"),
          "The strip has one field per input parameter, prefilled with the defaults");
    check(strip->findChild<QLabel *>("parameterPreviewNote")->text() ==
              "Shows the regenerated shape as a ghost. Nothing is changed until you apply.",
          "The strip says nothing changes");
    const auto revision = doc.revision();
    const auto history = doc.history().total;
    const auto position = applied(doc);
    const auto dirty = doc.dirty();
    const auto bytes = encodeDocument(doc);
    f.type(width, "1500");
    f.type(shelves, "3");
    check(QTest::qWaitFor([&] { return view->parameterGhostShown(); }, 2000),
          "The ghost appears after the debounce");
    check(view->parameterGhostEdges() > 0, "The ghost draws the regenerated members");
    check(doc.revision() == revision && doc.history().total == history &&
              applied(doc) == position && doc.dirty() == dirty && encodeDocument(doc) == bytes,
          "The ghost never changes the revision, History, dirty state or records");
    // A value outside its bounds is refused in the strip with the §5 sentence.
    f.type(width, "2600");
    auto *error = strip->findChild<QLabel *>("parameterPreviewError");
    check(QTest::qWaitFor([&] { return error->isVisible(); }, 2000) &&
              error->text() == "2600 mm is above the maximum, 2400 mm. Nothing was changed." &&
              !view->parameterGhostShown(),
          "A bounds error shows in the strip, got " + text(error->text()));
    // Esc in the strip goes back to the defaults; the context stays open.
    QTest::keyClick(width, Qt::Key_Escape);
    check(width->text() == "900 mm" && !error->isVisible() && !view->parameterGhostShown() &&
              view->componentScope() == t.defaults,
          "Esc in the strip goes back to the defaults and keeps the context");
    f.type(width, "1200");
    check(QTest::qWaitFor([&] { return view->parameterGhostShown(); }, 2000), "Ghost again");
    strip->findChild<QPushButton *>("parameterPreviewDefaults")->click();
    check(!view->parameterGhostShown() && width->text() == "900 mm",
          "Back to defaults clears the ghost");
    // Any committed edit clears the ghost.
    f.type(width, "1200");
    check(QTest::qWaitFor([&] { return view->parameterGhostShown(); }, 2000), "Ghost again");
    auto *section = f.definition();
    section->tree()->setCurrentItem(section->tree()->topLevelItem(1));
    f.type(section->expressionField()->edit(), "2");
    f.enter(section->expressionField()->edit());
    check(latestLabel(doc) == "Edit parameter Shelves" && !view->parameterGhostShown() &&
              strip->findChild<QLineEdit *>("parameterPreview_width")->text() == "900 mm",
          "A committed edit clears the ghost: " + latestLabel(doc) + " ghost " +
              std::to_string(view->parameterGhostShown()) + " width " +
              text(strip->findChild<QLineEdit *>("parameterPreview_width")->text()) + " / " +
              text(section->current()) + " / " + text(section->editorError()->text()) + " / " +
              text(section->expressionField()->error()));
    f.undo();
    // Closing the definition (the banner's link) closes the strip.
    f.focus(banner);
    QMetaObject::invokeMethod(banner, "linkActivated", Q_ARG(QString, "close"));
    check(view->componentScope() == 0 && !strip->isVisible() && !view->parameterGhostShown(),
          "Close definition closes the context and the strip");
}

// Regeneration keeps the open context, the selection and materials (§3.1, R094 Verify).
void regeneration(Fixture &f, const Table &t) {
    auto &doc = f.doc();
    const auto shelfScene = authoring::sceneOf(doc, t.defaults, t.shelf);
    const auto topScene = authoring::sceneOf(doc, t.defaults, t.top);
    f.openContext(t.defaults);
    // A colour on the top, painted inside the definition.
    const auto unpainted = std::pair(doc.bodies().at(topScene)->color,
                                     doc.bodies().at(topScene)->faceColors);
    f.select(topScene);
    f.view()->paintSelection({.4f, .25f, .1f});
    const auto painted = std::pair(doc.bodies().at(topScene)->color,
                                   doc.bodies().at(topScene)->faceColors);
    check(painted != unpainted, "The top is painted");
    f.select(shelfScene);
    auto *section = f.definition();
    section->select("width");
    f.type(section->expressionField()->edit(), "1200 mm");
    f.enter(section->expressionField()->edit());
    check(latestLabel(doc) == "Edit parameter Width", "The default changed");
    check(f.view()->componentScope() == t.defaults, "The context stays open across regeneration");
    check(f.view()->selectionState().entities().size() == 1 &&
              f.view()->selectionState().entities().begin()->body == shelfScene,
          "The selection persists by ID");
    const auto &top = *doc.bodies().at(topScene);
    double maxX = -1;
    for (const auto &[id, point] : top.surface.vertices)
        maxX = std::max(maxX, point.x);
    check(std::abs(maxX - 1.2) < 1e-9, "The placement regenerated to the new width");
    check(std::pair(top.color, top.faceColors) == painted, "The member keeps its colour");
    f.undo();
    f.undo();
    f.closeContexts();
}

// Rebind: a hand edit makes a driver Changed; Rebind is one labelled entry (§3.3).
void rebind(Fixture &f, const Table &t) {
    auto &doc = f.doc();
    f.openContext(t.defaults);
    auto *drivers = f.drivers();
    // Moving the shelf by hand inside the definition changes what its drivers act on.
    const auto shelfScene = authoring::sceneOf(doc, t.defaults, t.shelf);
    f.select(shelfScene);
    f.view()->setTool(Viewport::Tool::Move);
    check(f.view()->measurements("[0.1,0.1,0.21]") && f.view()->measurements("0,0,0.05"),
          "The shelf moves inside the definition");
    f.view()->setTool(Viewport::Tool::Select);
    QTest::qWait(20);
    auto *row = driverRow(drivers->tree(), "Repeat", "Shelf");
    check(row && row->data(0, Qt::AccessibleTextRole).toString().endsWith(", Changed") &&
              drivers->tree()->itemWidget(row, 3) &&
              drivers->tree()->itemWidget(row, 3)->findChild<QPushButton *>("driverRowRebind")
                      ->focusPolicy() == Qt::NoFocus,
          "The driver reads Changed and its in-row Rebind takes no focus");
    check(drivers->changedNote()->isVisible() && drivers->rebindAllButton()->isVisible(),
          "The note explains Changed; Rebind all shows for several");
    drivers->tree()->setCurrentItem(row);
    check(drivers->rebindButton()->isEnabled(), "Rebind is enabled for a Changed selection");
    auto before = applied(doc);
    f.focus(drivers->rebindButton());
    QTest::keyClick(drivers->rebindButton(), Qt::Key_Space);
    check(applied(doc) == before + 1 && latestLabel(doc) == "Rebind driver" &&
              !drivers->rebindAllButton()->isVisible() && drivers->changedNote()->isVisible(),
          "Rebind is one entry for the selected driver, got " + latestLabel(doc));
    f.undo();
    before = applied(doc);
    f.focus(drivers->rebindAllButton());
    QTest::keyClick(drivers->rebindAllButton(), Qt::Key_Space);
    check(applied(doc) == before + 1 && latestLabel(doc) == "Rebind 2 drivers" &&
              !drivers->changedNote()->isVisible() && !drivers->rebindButton()->isVisible(),
          "Rebind all is one entry, got " + latestLabel(doc));
    f.undo();
    f.undo();
    f.closeContexts();
}

// Keyboard reachability and names (§8).
void keyboard(Fixture &f, const Table &t) {
    f.openContext(t.defaults);
    auto *section = f.definition();
    auto *drivers = f.drivers();
    check(section->header()->accessibleName() == "Parameters · definition" &&
              drivers->header()->accessibleName() == "Drivers",
          "Section headers are named");
    check(section->tree()->accessibleName() == "Definition parameters" &&
              drivers->tree()->accessibleName() == "Drivers" &&
              section->addButton()->text() == "+ Parameter" &&
              drivers->addButton()->text() == "+ Driver",
          "Trees and header buttons are named");
    // Tab: header → + Parameter → tree → the editor.
    section->tree()->setCurrentItem(section->tree()->topLevelItem(0));
    f.focus(section->header());
    QTest::keyClick(section->header(), Qt::Key_Tab);
    check(section->addButton()->hasFocus(), "Tab from the header reaches + Parameter");
    QTest::keyClick(section->addButton(), Qt::Key_Tab);
    check(section->tree()->hasFocus(), "Tab reaches the parameter tree");
    QTest::keyClick(section->tree(), Qt::Key_Tab);
    check(section->labelEdit()->hasFocus(), "Tab reaches the editor");
    for (QWidget *control : std::initializer_list<QWidget *>{
             section->labelEdit(), section->nameEdit(), section->expressionField()->edit(),
             section->minimumEdit(), section->maximumEdit(), section->stepEdit(),
             drivers->tree(), drivers->addButton(), drivers->kindChoice(), drivers->axisChoice(),
             drivers->formulaField()->edit(), drivers->planeEdit(), drivers->spacingField()->edit(),
             drivers->maxCountSpin(), drivers->childChoice(), drivers->commitButton()})
        check((control->focusPolicy() & Qt::TabFocus) && !control->accessibleName().isEmpty(),
              "Every control is a named Tab stop: " + text(control->objectName()));
    // The banner, then the strip in the Tab chain.
    auto *banner = f.window.findChild<QLabel *>("componentScopeBanner");
    auto *width = f.window.findChild<QLineEdit *>("parameterPreview_width");
    check(banner->nextInFocusChain() == width || banner->focusPolicy() & Qt::TabFocus,
          "The strip follows the banner");
    QWidget *after = banner;
    for (int i = 0; i < 8 && after != width; ++i)
        after = after->nextInFocusChain();
    check(after == width, "Tab from the banner reaches the strip's first field");
    f.closeContexts();
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
    Window window;
    window.resize(1280, 900);
    try {
        window.show();
        check(QTest::qWaitForWindowExposed(&window), "Window exposed");
        window.activateWindow();
        check(QTest::qWaitForWindowActive(&window), "Window active");
        Fixture f{window, files};
        fixtureThroughInterface(f);
        std::cout << "The ADR 0165 table through the interface matches the command fixture\n";
        auto table = makeTable();
        table.doc.setDisplayUnits(DisplayUnit::Millimeters);
        f.open(table.doc, "table.sketchyup");
        parameterEditing(f, table);
        std::cout << "Parameters · definition: tree, editor, F2, Alt+Up/Down, Delete passed\n";
        formulaFields(f, table);
        std::cout << "Formula fields: checks, caret, unit fix, cycle passed\n";
        memberKinds(f);
        std::cout << "Add driver: kinds by member, refusals, rail and + Driver passed\n";
        f.open(table.doc, "table-2.sketchyup");
        repeatCopies(f, table);
        stretchPlane(f, table);
        std::cout << "Stretch plane by drag, snapping, Alt and typed value passed\n";
        previewGhost(f, table);
        std::cout << "Preview strip ghost without a revision change passed\n";
        regeneration(f, table);
        std::cout << "Context, selection and materials kept across regeneration passed\n";
        rebind(f, table);
        std::cout << "Changed drivers and Rebind passed\n";
        keyboard(f, table);
        std::cout << "Keyboard order and names passed\n";
        if (argc == 2) {
            f.openContext(table.defaults);
            dynamic_cast<CollapsibleSection *>(window.findChild<QWidget *>("entityInfoSection"))
                ->setExpanded(false);
            f.definition()->select("width");
            f.drivers()->selectDriver(table.stretch);
            f.view()->standardView(0);
            f.view()->fit();
            f.type(window.findChild<QLineEdit *>("parameterPreview_width"), "1500");
            check(QTest::qWaitFor([&] { return f.view()->parameterGhostShown(); }, 2000),
                  "The parameter ghost is shown before the capture");
            QTest::qWait(60);
            std::cout << "screenshot: ghost " << f.view()->parameterGhostShown() << " edges "
                      << f.view()->parameterGhostEdges() << " plane "
                      << f.view()->stretchPlaneOutline().size() << " "
                      << f.view()->stretchPlanePoint(.45).x() << ","
                      << f.view()->stretchPlanePoint(.45).y() << '\n';
            check(window.grab().save(QString::fromLocal8Bit(argv[1])), "Screenshot saved");
            f.view()->grabFramebuffer().save(QString::fromLocal8Bit(argv[1]) + ".viewport.png");
        }
        window.document().markSaved();
        std::cout << "component_authoring_input_tests: " << checks
                  << " checks passed; DPR=" << window.devicePixelRatioF() << '\n';
    } catch (const std::exception &error) {
        window.document().markSaved();
        std::cerr << error.what() << '\n';
        return 1;
    }
}
