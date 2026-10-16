// R094.c: Entity info's "Parameters · this placement" (ADR 0166 §1): rows and captions from
// component_parameters.values, typed entry in document units and feet-and-inches, live
// validation, commit on Enter and focus loss, Esc, per-row reset and Ctrl+Backspace (and its
// word-delete meaning while editing), Reset all, one labelled History entry per change,
// Measurements box routing and amend, refusals that leave the document unchanged, nested and
// several placements, keyboard order and accessible names. Real key events to the focused
// widget; run on X11 and Wayland at 1×, 1.5× and 2×.
#include "app/component_parameters_section.hpp"
#include "app/parameter_value_field.hpp"
#include "app/surface_format.hpp"
#include "app/window.hpp"
#include "component_parameter_fixture.hpp"
#include "io/document_io.hpp"
#include <QAction>
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QLineEdit>
#include <QPushButton>
#include <QRegularExpression>
#include <QSettings>
#include <QSpinBox>
#include <QTabWidget>
#include <QTemporaryDir>
#include <QTest>
#include <QToolButton>
#include <cmath>
#include <iostream>
#include <numbers>
using namespace sketchy;
using namespace sketchy::parameter_fixture;
namespace {
std::size_t checks = 0;
void check(bool value, const std::string &message) {
    ++checks;
    if (!value)
        throw std::runtime_error(message);
}
std::string latestLabel(const Document &doc) {
    const auto page = doc.history();
    for (auto it = page.entries.rbegin(); it != page.entries.rend(); ++it)
        if (it->applied)
            return it->label;
    return {};
}
std::size_t applied(const Document &doc) { return doc.history(0, 1).position; }
// The saved records without the revision counter and the allocator high-water marks, which
// undo keeps by design (ADR 0005, 0007): undo restores every record exactly.
QByteArray content(const Document &doc) {
    return QString::fromUtf8(encodeDocument(doc))
        .replace(QRegularExpression(R"re("(revision|next\w*)":"?\d+"?)re"), {})
        .toUtf8();
}
// The document's content, revision and History, compared before and after a refusal.
struct State {
    QByteArray bytes;
    std::uint64_t revision{};
    std::size_t history{}, position{};
    explicit State(const Document &doc)
        : bytes(content(doc)), revision(doc.revision()), history(doc.history().total),
          position(applied(doc)) {}
    bool operator==(const State &) const = default;
};
// Where two encodings first differ, for failure messages.
std::string difference(const QByteArray &a, const QByteArray &b) {
    qsizetype at = 0;
    while (at < a.size() && at < b.size() && a[at] == b[at])
        ++at;
    return " (differs at " + std::to_string(at) + ": " +
           a.mid(std::max<qsizetype>(0, at - 80), 160).toStdString() + " | " +
           b.mid(std::max<qsizetype>(0, at - 80), 160).toStdString() + ")";
}
std::optional<double> override(const Document &doc, Id placement, const std::string &name) {
    const auto &values = doc.instances().at(placement)->overrides;
    const auto found = values.find(name);
    if (found == values.end())
        return std::nullopt;
    if (const auto *quantity = std::get_if<ParameterQuantity>(&found->second))
        return quantity->value;
    return std::nullopt;
}
bool near(std::optional<double> value, double expected) {
    return value && std::abs(*value - expected) < 1e-9;
}
struct Fixture {
    Window &window;
    Table &t;
    Document &doc() { return window.document(); }
    ComponentParametersSection *section() {
        return window.findChild<ComponentParametersSection *>("componentParametersSection");
    }
    ParameterValueField *field(const char *name) {
        auto *field = section()->field(name);
        check(field, std::string("Row for ") + name);
        return field;
    }
    QLineEdit *edit(const char *name) {
        auto *edit = qobject_cast<QLineEdit *>(field(name)->control());
        check(edit, std::string(name) + " is a line edit");
        return edit;
    }
    void select(Id body) {
        window.viewport()->setSelection(body);
        QTest::qWait(20);
    }
    void focus(QWidget *widget) {
        widget->setFocus(Qt::TabFocusReason);
        check(QTest::qWaitFor([&] { return widget->hasFocus(); }),
              "Focus reaches " + widget->objectName().toStdString());
    }
    void type(QLineEdit *edit, const QString &text) {
        focus(edit);
        edit->selectAll();
        QTest::keyClick(edit, Qt::Key_Backspace);
        QTest::keyClicks(edit, text);
    }
    void enter(QWidget *widget) { QTest::keyClick(widget, Qt::Key_Return); }
    void undo() { window.findChild<QAction *>("edit.undo")->trigger(); }
};
// Rows, captions, the header count and accessible names (ADR 0166 §1.2, §8).
void rows(Fixture &f) {
    f.select(f.t.wide);
    auto *section = f.section();
    check(section->isVisible() && section->placement() == f.t.wide,
          "The section shows for one top-level parametric placement");
    check(section->detail() == "3 overridden",
          "Header counts the overrides, got " + section->detail().toStdString());
    check(section->header()->accessibleDescription() == "Expanded, 3 overridden",
          "The header reads its state and count");
    std::vector<std::string> order;
    for (auto *field : section->fields())
        order.push_back(field->name().toStdString());
    std::string listed;
    for (const auto &name : order)
        listed += " " + name;
    check(order.size() == 8 && order.front() == "width",
          "One row per parameter in order, got" + listed);
    auto *width = f.field("width");
    check(width->caption() == "Override · default 900 mm · 400–2400 mm",
          "Override caption, got " + width->caption().toStdString());
    check(f.edit("width")->text() == "1200 mm", "Width shows its override in millimetres");
    check(width->control()->accessibleName() == "Width" &&
              width->control()->accessibleDescription() == "Override, default 900 mm, 400–2400 mm",
          "The field's accessible name is the label and its description the caption, got " +
              width->control()->accessibleDescription().toStdString());
    check(width->resetButton()->isVisible() &&
              width->resetButton()->accessibleName() == "Reset Width to the default, 900 mm",
          "Override rows have a named reset button");
    check(f.field("open")->caption() == "Default" && !f.field("open")->resetButton()->isVisible(),
          "Default rows have no reset button");
    check(f.field("tilt")->caption().startsWith("Default · -28.") &&
              f.field("tilt")->caption().contains("°–28."),
          "Bounded angle caption in degrees, got " + f.field("tilt")->caption().toStdString());
    auto *legs = f.field("leg_x");
    check(legs->derived() && legs->caption() == "Derived · width - 60 mm" &&
              f.edit("leg_x")->isReadOnly() && (f.edit("leg_x")->focusPolicy() & Qt::TabFocus),
          "Derived rows are read-only, focusable and captioned with the formula, got " +
              legs->caption().toStdString());
    auto *shelves = qobject_cast<QSpinBox *>(f.field("shelves")->control());
    check(shelves && shelves->value() == 3, "Count rows use a spin box");
    auto *finish = qobject_cast<QComboBox *>(f.field("finish")->control());
    check(finish && finish->currentText() == "Ash \"natural\"", "Choice rows list option labels");
    auto *open = qobject_cast<QCheckBox *>(f.field("open")->control());
    check(open && open->isChecked() && open->text() == "Shown", "Boolean rows are check boxes");
    for (auto *button : section->findChildren<QToolButton *>())
        check(button->focusPolicy() == Qt::NoFocus, "Spin box − and + take no focus");
}
// Typed entry in document units and feet-and-inches gives the same SI override (§1.3).
void typedEntry(Fixture &f) {
    f.select(f.t.defaults);
    auto &doc = f.doc();
    const State before(doc);
    auto *width = f.edit("width");
    f.type(width, "1193.8");
    f.enter(width);
    check(near(override(doc, f.t.defaults, "width"), 1.1938),
          "A bare number is read in the document's millimetres");
    check(latestLabel(doc) == "Change Width" && applied(doc) == before.position + 1,
          "Enter commits one entry labelled Change Width, got " + latestLabel(doc));
    check(f.edit("width")->text() == "1193.8 mm", "The field shows the canonical text");
    const auto metric = override(doc, f.t.defaults, "width");
    f.undo();
    check(State(doc).bytes == before.bytes,
          "Undo restores the placement exactly" + difference(State(doc).bytes, before.bytes));
    for (const auto *text : {"3'11\"", "47\"", "1.1938m", "119.38cm"}) {
        f.type(f.edit("width"), text);
        f.enter(f.edit("width"));
        check(override(doc, f.t.defaults, "width") == metric,
              std::string("Typed ") + text + " gives the same SI override as 1193.8 mm");
        f.undo();
    }
    check(State(doc).bytes == before.bytes, "Every typed entry undoes exactly");
    // An angle in degrees by default and radians on request.
    f.type(f.edit("tilt"), "10");
    f.enter(f.edit("tilt"));
    const auto degrees = override(doc, f.t.defaults, "tilt");
    check(degrees && std::abs(*degrees - 10 * std::numbers::pi / 180) < 1e-9,
          "Angles default to degrees");
    f.undo();
    f.type(f.edit("tilt"), "0.2 rad");
    f.enter(f.edit("tilt"));
    check(near(override(doc, f.t.defaults, "tilt"), .2), "Angles take rad");
    f.undo();
    check(State(doc).bytes == before.bytes, "Angle entries undo exactly");
}
// Live validation, Esc, focus-loss commit and refusals without clamping (§1.3, §5).
void validation(Fixture &f) {
    f.select(f.t.defaults);
    auto &doc = f.doc();
    const State before(doc);
    auto *width = f.edit("width");
    auto *row = f.field("width");
    f.type(width, "abc");
    check(row->error() == "Type a length, such as 1200 mm or 3'11\".",
          "A parse failure shows at once, got " + row->error().toStdString());
    check(width->property("invalid").toBool() &&
              width->accessibleDescription().startsWith("Invalid: Type a length"),
          "An invalid field is marked and described as Invalid");
    f.type(width, "2.6m");
    check(row->error().isEmpty(), "A readable value waits for the check");
    check(QTest::qWaitFor([&] { return !row->error().isEmpty(); }, 2000),
          "The bounds check runs after the pause");
    check(row->error() == "2.6m is above the maximum, 2400 mm. Nothing was changed.",
          "Bounds sentence, got " + row->error().toStdString());
    check(State(doc) == before, "Checking never changes the document");
    f.enter(width);
    check(State(doc) == before && width->hasFocus() && width->text() == "2.6m" &&
              !row->error().isEmpty(),
          "Enter on an invalid value keeps the error, the text and focus; nothing changes");
    // Focus loss keeps the text and error too.
    f.focus(f.edit("leg_x"));
    check(State(doc) == before && width->text() == "2.6m" && !row->error().isEmpty(),
          "Focus loss with an invalid value commits nothing");
    f.focus(width);
    QTest::keyClick(width, Qt::Key_Escape);
    check(width->text() == "900 mm" && row->error().isEmpty() &&
              !width->property("invalid").toBool(),
          "Esc restores the stored value and clears the error");
    f.type(width, "0.3 m");
    QTest::qWait(300);
    check(row->error() == "0.3 m is below the minimum, 400 mm. Nothing was changed.",
          "Below the minimum, got " + row->error().toStdString());
    QTest::keyClick(width, Qt::Key_Escape);
    // A count step outside the bounds is refused, never clamped.
    f.select(f.t.wide);
    auto *shelves = qobject_cast<QSpinBox *>(f.field("shelves")->control());
    f.focus(shelves);
    const State wide(doc);
    QTest::keyClick(shelves, Qt::Key_Up);
    check(near(override(doc, f.t.wide, "shelves"), 4) && latestLabel(doc) == "Change Shelves" &&
              applied(doc) == wide.position + 1,
          "Up steps the count and commits one entry");
    const State four(doc);
    QTest::keyClick(shelves, Qt::Key_Up);
    check(State(doc) == four && shelves->value() == 5 &&
              f.field("shelves")->error() == "5 is above the maximum, 4. Nothing was changed.",
          "A step past the maximum is refused at the field, got " +
              f.field("shelves")->error().toStdString());
    QTest::keyClick(shelves, Qt::Key_Escape);
    check(shelves->value() == 4 && f.field("shelves")->error().isEmpty(), "Esc restores the count");
    f.undo();
    check(State(doc).bytes == wide.bytes,
          "Undo restores the count" + difference(State(doc).bytes, wide.bytes));
    // Focus loss commits a valid value.
    f.select(f.t.defaults);
    f.type(f.edit("width"), "1000");
    f.focus(f.edit("leg_x"));
    check(near(override(doc, f.t.defaults, "width"), 1) && latestLabel(doc) == "Change Width",
          "Focus leaving the field commits");
    f.undo();
    check(State(doc).bytes == before.bytes, "Undo after a focus-loss commit");
}
// A value the definition cannot regenerate is refused at the field (§5 PARAMETER_REGENERATION):
// an unbounded size whose stretch would turn a slab inside out.
void regeneration(Fixture &f) {
    auto &doc = f.doc();
    const auto slab = box(doc, {10, 0, 0}, {11, 1, .1}, "Slab");
    const auto group = createGroup(doc, {slab}, "Slab");
    const auto created = createComponent(doc, group, "Slab");
    defineComponentParameter(
        doc, created.definition,
        parameter("size", "Size", ParameterType::Length, ComponentParameterMode::Input, "1 m"));
    auto stretch = driver(ComponentDriverKind::Stretch, slab, "size");
    stretch.at = 10.5; // Definition coordinates are the slab's model coordinates.
    driveComponent(doc, created.definition, 0, stretch);
    emit f.window.viewport()->changed();
    f.select(created.instance);
    const State before(doc);
    auto *size = f.edit("size");
    f.type(size, "-200");
    QTest::qWait(300);
    check(f.field("size")->error() ==
              "At -200 mm the shape can't be regenerated. Nothing was changed.",
          "The check refuses an unregenerable value, got " +
              f.field("size")->error().toStdString());
    f.enter(size);
    check(State(doc) == before && f.field("size")->error().startsWith("At -200 mm"),
          "Enter on an unregenerable value changes nothing");
    QTest::keyClick(size, Qt::Key_Escape);
    f.type(size, "1500");
    f.enter(size);
    check(near(override(doc, created.instance, "size"), 1.5), "A regenerable value commits");
    f.undo();
    check(State(doc).bytes == before.bytes, "Undo restores the slab");
}
// Per-row reset, Ctrl+Backspace and its word-delete meaning, Reset all (§1.2, §1.4).
void resets(Fixture &f) {
    f.select(f.t.wide);
    auto &doc = f.doc();
    const State before(doc);
    auto *width = f.edit("width");
    // While the text is edited, Ctrl+Backspace deletes the previous word.
    f.type(width, "1300 mm");
    QTest::keyClick(width, Qt::Key_Backspace, Qt::ControlModifier);
    check(width->text() == "1300 " && State(doc) == before,
          "Ctrl+Backspace deletes a word while editing, got \"" + width->text().toStdString() +
              "\"");
    QTest::keyClick(width, Qt::Key_Escape);
    check(width->text() == "1200 mm", "Esc restores before the reset");
    QTest::keyClick(width, Qt::Key_Backspace, Qt::ControlModifier);
    check(!override(doc, f.t.wide, "width") && latestLabel(doc) == "Reset Width" &&
              applied(doc) == before.position + 1 && f.edit("width")->text() == "900 mm",
          "Ctrl+Backspace on an unchanged override resets it as one entry");
    check(f.edit("width")->hasFocus() && !f.field("width")->resetButton()->isVisible(),
          "Focus stays on the reset row");
    f.undo();
    check(State(doc).bytes == before.bytes, "Undo restores the override");
    // The reset button.
    auto *reset = f.field("width")->resetButton();
    f.focus(reset);
    QTest::keyClick(reset, Qt::Key_Space);
    check(!override(doc, f.t.wide, "width") && latestLabel(doc) == "Reset Width",
          "The reset button resets the row");
    check(QTest::qWaitFor([&] { return f.edit("width")->hasFocus(); }),
          "Focus moves from the vanished reset button to its field");
    f.undo();
    // Ctrl+Backspace on a choice row.
    auto *finish = f.field("finish")->control();
    f.focus(finish);
    QTest::keyClick(finish, Qt::Key_Backspace, Qt::ControlModifier);
    check(doc.instances().at(f.t.wide)->overrides.count("finish") == 0 &&
              latestLabel(doc) == "Reset Finish",
          "Ctrl+Backspace resets a choice row");
    f.undo();
    // Reset all.
    auto *all = f.section()->resetAllButton();
    check(all->isEnabled(), "Reset all is available with overrides");
    f.focus(all);
    QTest::keyClick(all, Qt::Key_Space);
    check(doc.instances().at(f.t.wide)->overrides.empty() &&
              latestLabel(doc) == "Reset all parameters" && applied(doc) == before.position + 1,
          "Reset all is one entry");
    check(!all->isEnabled() && f.section()->detail().isEmpty(), "Nothing left to reset");
    f.undo();
    check(State(doc).bytes == before.bytes, "Undo restores every override");
}
// Check boxes and choices commit on change (§1.3).
void choices(Fixture &f) {
    f.select(f.t.defaults);
    auto &doc = f.doc();
    const State before(doc);
    auto *open = qobject_cast<QCheckBox *>(f.field("open")->control());
    f.focus(open);
    QTest::keyClick(open, Qt::Key_Space);
    const auto &values = doc.instances().at(f.t.defaults)->overrides;
    check(values.count("open") && std::get<bool>(values.at("open")) == false &&
              latestLabel(doc) == "Change Open shelf" && open->text() == "Hidden",
          "Space on a check box commits one entry");
    f.undo();
    auto *finish = qobject_cast<QComboBox *>(f.field("finish")->control());
    f.focus(finish);
    QTest::keyClick(finish, Qt::Key_Down);
    check(doc.instances().at(f.t.defaults)->overrides.count("finish") &&
              latestLabel(doc) == "Change Finish",
          "Choosing an option commits one entry");
    f.undo();
    check(State(doc).bytes == before.bytes, "Check and choice entries undo exactly");
}
// The Measurements box names a parameter and amends on re-entry (§1.3, §1.4).
void measurements(Fixture &f) {
    auto &window = f.window;
    auto &doc = f.doc();
    window.findChild<QAction *>("tool." + QString::number(int(Viewport::Tool::Select)))->trigger();
    f.select(f.t.defaults);
    auto *step = window.findChild<QLabel *>("measurementStep");
    auto *box = window.findChild<QLineEdit *>("measurements");
    check(step->isVisible() && step->text() == "Width" && box->text() == "900 mm",
          "The box names the first length parameter, got " + step->text().toStdString());
    check(box->accessibleName().startsWith("Width, Measurements in"), "The box's name says Width");
    // The parameter last focused in the section.
    f.focus(f.edit("tilt"));
    check(step->text() == "Leg tilt", "The box follows the last focused parameter");
    f.focus(f.edit("width"));
    check(step->text() == "Width", "Back to Width");
    const State before(doc);
    f.focus(box);
    box->clear();
    QTest::keyClicks(box, "1300");
    QTest::keyClick(box, Qt::Key_Return);
    check(near(override(doc, f.t.defaults, "width"), 1.3) && latestLabel(doc) == "Change Width" &&
              applied(doc) == before.position + 1,
          "Enter in the box sets the parameter as one entry");
    check(QTest::qWaitFor([&] { return window.viewport()->hasFocus(); }) &&
              box->text() == "1300 mm",
          "The canvas has focus again and the box shows the new value");
    // Re-entry amends the entry.
    f.focus(box);
    box->clear();
    QTest::keyClicks(box, "4'");
    QTest::keyClick(box, Qt::Key_Return);
    check(near(override(doc, f.t.defaults, "width"), 1.2192) &&
              applied(doc) == before.position + 1 && latestLabel(doc) == "Change Width",
          "Re-entry amends the one entry");
    // A refused value leaves the document and the entry unchanged.
    const State amended(doc);
    f.focus(box);
    box->clear();
    QTest::keyClicks(box, "3m");
    QTest::keyClick(box, Qt::Key_Return);
    check(State(doc) == amended && box->property("invalid").toBool(),
          "A refused entry changes nothing and marks the box");
    QTest::keyClick(box, Qt::Key_Escape);
    f.undo();
    check(State(doc).bytes == before.bytes, "One undo removes the amended entry");
    // Another tool leaves the box unchanged.
    window.findChild<QAction *>("tool." + QString::number(int(Viewport::Tool::Line)))->trigger();
    check(!step->isVisible() && box->text().isEmpty(), "Other tools keep their box");
    window.findChild<QAction *>("tool." + QString::number(int(Viewport::Tool::Select)))->trigger();
    check(step->isVisible() && step->text() == "Width", "Select routes the box again");
    f.select(0);
    check(!step->isVisible(), "Without a parametric selection the box is unchanged");
}
// Keyboard order, the status hint and Edit definition (§1.2, §8).
void keyboard(Fixture &f) {
    auto &window = f.window;
    f.select(f.t.wide);
    auto *section = f.section();
    auto *header = section->header();
    f.focus(header);
    std::vector<QWidget *> expected;
    for (auto *field : section->fields()) {
        if (field->control()->isEnabled())
            expected.push_back(field->control());
        if (field->resetButton()->isVisible())
            expected.push_back(field->resetButton());
    }
    expected.push_back(section->resetAllButton());
    expected.push_back(section->editDefinitionButton());
    for (auto *widget : expected) {
        QTest::keyClick(QApplication::focusWidget(), Qt::Key_Tab);
        check(QTest::qWaitFor([&] { return widget->hasFocus(); }),
              "Tab reaches " + widget->objectName().toStdString() + " (focus " +
                  (QApplication::focusWidget()
                       ? QApplication::focusWidget()->objectName().toStdString()
                       : std::string("none")) +
                  ")");
        auto *button = qobject_cast<QPushButton *>(widget);
        check(!widget->accessibleName().isEmpty() || (button && !button->text().isEmpty()),
              "Every control is named: " + widget->objectName().toStdString());
    }
    const auto hint = ComponentParametersSection::hint();
    bool shown = false;
    for (auto *label : window.findChildren<QLabel *>())
        shown |= label->isVisible() && label->text() == hint;
    check(shown, "The status bar shows the section's keys while focus is in it");
    window.viewport()->setFocus();
    QTest::qWait(20);
    bool kept = false;
    for (auto *label : window.findChildren<QLabel *>())
        kept |= label->isVisible() && label->text() == hint;
    check(!kept, "The status text returns when focus leaves");
    // Edit definition opens the context as Enter does.
    f.select(f.t.defaults);
    auto *edit = section->editDefinitionButton();
    f.focus(edit);
    QTest::keyClick(edit, Qt::Key_Space);
    check(window.viewport()->selectionState().context() == f.t.defaults,
          "Edit definition… opens the placement's context");
    window.viewport()->enterContext(0);
}
// Nested placements read-only; several placements ask for one (§1.1).
void nestedAndSeveral(Fixture &f) {
    auto *view = f.window.viewport();
    view->enterContext(f.t.roomPlacement);
    f.select(f.t.nested);
    auto *section = f.section();
    check(section->isVisible() && section->nested(), "A nested placement shows its values");
    auto *width = f.field("width");
    check(width->caption() == "From Room" && f.edit("width")->isReadOnly() &&
              !width->resetButton()->isVisible() && f.edit("width")->text() == "1800 mm",
          "Parent-supplied values read From Room, got " + width->caption().toStdString());
    check(!section->resetAllButton()->isEnabled(), "Nothing to reset on a nested placement");
    const State before(f.doc());
    f.type(f.edit("width"), "5");
    f.enter(f.edit("width"));
    check(State(f.doc()) == before, "A nested value cannot be committed");
    view->enterContext(0);
    view->selectEntities(
        {{f.t.wide, SelectionKind::Body, 0}, {f.t.defaults, SelectionKind::Body, 0}});
    QTest::qWait(20);
    auto *message = section->findChild<QLabel *>("parameterMessage");
    check(section->isVisible() && message->isVisible() &&
              message->text() == "Select one placement to see its parameters" &&
              !section->resetAllButton()->isVisible(),
          "Several placements ask for one");
    f.select(f.t.top);
    check(!section->isVisible(), "Nothing to show for other geometry");
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
        auto table = makeTable();
        table.doc.setDisplayUnits(DisplayUnit::Millimeters);
        const auto path = files.filePath("table.sketchyup");
        saveDocument(table.doc, path);
        window.show();
        check(QTest::qWaitForWindowExposed(&window), "Window exposed");
        window.activateWindow();
        check(QTest::qWaitForWindowActive(&window), "Window active");
        window.openPath(path);
        check(window.document().instances().contains(table.wide), "Fixture opened");
        window.findChild<QTabWidget *>("organizationTabs")->setCurrentIndex(2);
        QTest::qWait(30);
        Fixture f{window, table};
        rows(f);
        std::cout << "Rows, captions, header count and accessible names passed\n";
        typedEntry(f);
        std::cout << "Typed entry in millimetres, feet-and-inches and angles passed\n";
        validation(f);
        std::cout << "Live validation, Esc, focus loss and bounds refusals passed\n";
        regeneration(f);
        std::cout << "Unregenerable value refused at the field passed\n";
        resets(f);
        std::cout << "Reset, Ctrl+Backspace, word delete and Reset all passed\n";
        choices(f);
        std::cout << "Check box and choice commits passed\n";
        measurements(f);
        std::cout << "Measurements box routing and amend passed\n";
        keyboard(f);
        std::cout << "Keyboard order, status hint and Edit definition passed\n";
        nestedAndSeveral(f);
        std::cout << "Nested and several placements passed\n";
        if (argc == 2) {
            f.select(table.wide);
            QTest::qWait(40);
            check(window.grab().save(QString::fromLocal8Bit(argv[1])), "Screenshot saved");
        }
        window.document().markSaved();
        std::cout << "component_parameter_input_tests: " << checks
                  << " checks passed; DPR=" << window.devicePixelRatioF() << '\n';
    } catch (const std::exception &error) {
        window.document().markSaved();
        std::cerr << error.what() << '\n';
        return 1;
    }
}
