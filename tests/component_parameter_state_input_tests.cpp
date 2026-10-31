// R094.e: states, refusals and dialogs (ADR 0166 §§4, 5). The "has its own values" dialog and
// its three choices: Cancel; Make unique and edit (promotion); Edit definition at defaults
// (suspend and restore, undo when nothing changed, partial restore, undoing the reset); nested
// placements; the §5 rows produced and shown at their controls with their actions (Show the
// driver, Select the locked placement, Use 120 mm); the changed-since-bound strip with Rebind
// and Undo; Rebind and Rebind all. Every refusal leaves the revision, History and records
// unchanged. Real key events to the focused widget; run on X11 and Wayland at 1× and 2×.
#include "app/component_definition_section.hpp"
#include "app/component_drivers_section.hpp"
#include "app/component_parameters_section.hpp"
#include "app/formula_field.hpp"
#include "app/parameter_messages.hpp"
#include "app/parameter_value_field.hpp"
#include "app/parameters_scope_dialog.hpp"
#include "app/surface_format.hpp"
#include "app/window.hpp"
#include "automation/commands.hpp"
#include "component_parameter_fixture.hpp"
#include "io/document_io.hpp"
#include <QAccessible>
#include <QAction>
#include <QApplication>
#include <QComboBox>
#include <QJsonArray>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QRegularExpression>
#include <QSettings>
#include <QTabWidget>
#include <QTemporaryDir>
#include <QTest>
#include <QTimer>
#include <QTreeWidget>
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
std::string labelAt(const Document &doc, std::size_t position) {
    const auto page = doc.history(position, 1);
    return page.entries.empty() ? std::string{} : page.entries.front().label;
}
std::size_t applied(const Document &doc) { return doc.history(0, 1).position; }
std::string latestLabel(const Document &doc) {
    const auto position = applied(doc);
    return position ? labelAt(doc, position - 1) : std::string{};
}
// The saved records without the revision counter and the allocator high-water marks, which
// undo keeps by design (ADR 0005, 0007): undo restores every record exactly.
QByteArray content(const Document &doc) {
    return QString::fromUtf8(encodeDocument(doc))
        .replace(QRegularExpression(R"re("(revision|next\w*)":"?\d+"?)re"), {})
        .toUtf8();
}
// The document's records, revision and History, compared before and after a refusal.
struct State {
    QByteArray bytes;
    std::uint64_t revision{};
    std::size_t history{}, position{};
    explicit State(const Document &doc)
        : bytes(content(doc)), revision(doc.revision()), history(doc.history().total),
          position(applied(doc)) {}
    bool operator==(const State &) const = default;
};
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
QString batch(Document &doc, const QJsonArray &commands, const QString &label = {}) {
    QJsonObject request{{"apiVersion", 1},
                        {"documentId", QString::fromStdString(doc.identity())},
                        {"expectedRevision", QString::number(doc.revision())},
                        {"commands", commands}};
    if (!label.isEmpty())
        request["history"] = QJsonObject{{"label", label}};
    try {
        executeBatch(doc, request);
    } catch (const std::exception &error) {
        return QString::fromUtf8(error.what());
    }
    return {};
}
struct Fixture {
    Window &window;
    Table &t;
    Document &doc() { return window.document(); }
    Viewport *view() { return window.viewport(); }
    ComponentParametersSection *section() {
        return window.findChild<ComponentParametersSection *>("componentParametersSection");
    }
    ComponentDefinitionSection *definition() {
        return window.findChild<ComponentDefinitionSection *>();
    }
    ComponentDriversSection *drivers() { return window.findChild<ComponentDriversSection *>(); }
    QLabel *status() { return window.findChild<QLabel *>("statusMessage"); }
    QLabel *banner() { return window.findChild<QLabel *>("componentScopeBanner"); }
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
        view()->setSelection(body);
        QTest::qWait(20);
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
    void undo() { window.findChild<QAction *>("edit.undo")->trigger(); }
    void redo() { window.findChild<QAction *>("edit.redo")->trigger(); }
    void changed() {
        view()->refresh();
        emit view() -> changed();
        QTest::qWait(10);
    }
    // Esc in the viewport until the context is closed, as the user closes it.
    void closeByEscape() {
        focus(view());
        for (int i = 0; i < 4 && view()->selectionState().context(); ++i) {
            QTest::keyClick(view(), Qt::Key_Escape);
            QTest::qWait(10);
        }
        check(!view()->selectionState().context(), "Esc closes the context");
    }
    // Opens the dialog with `open` and runs `act` on it once it has keyboard focus; returns
    // whether the dialog opened.
    bool withDialog(const std::function<void()> &open,
                    const std::function<void(ParametersScopeDialog &)> &act) {
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
                check(QTest::qWaitForWindowFocused(dialog), "The dialog has keyboard focus");
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
        open();
        timer.stop();
        if (!failure.empty())
            throw std::runtime_error(failure);
        return seen;
    }
    // Enter on the selected placement in the viewport.
    std::function<void()> enterOn(Id placement) {
        return [this, placement] {
            view()->enterContext(0);
            select(placement);
            focus(view());
            QTest::keyClick(view(), Qt::Key_Return);
        };
    }
    void press(QWidget *button) {
        focus(button);
        QTest::keyClick(button, Qt::Key_Space);
    }
};
QString nameOf(const Document &doc, Id body) {
    return QString::fromStdString(doc.bodies().at(body)->name);
}

// The dialog: title, text, buttons, default, accessible names; Esc is Cancel (§4.1).
void cancel(Fixture &f) {
    auto &doc = f.doc();
    const State before(doc);
    const auto name = nameOf(doc, f.t.wide);
    const bool opened = f.withDialog(f.enterOn(f.t.wide), [&](ParametersScopeDialog &dialog) {
        check(dialog.windowTitle() == name + " has its own values" &&
                  dialog.accessibleName() == dialog.windowTitle(),
              "The dialog's title names the placement, got " + text(dialog.windowTitle()));
        check(dialog.text() ==
                  "Width 1200 mm, Shelves 3 and Finish Ash \"natural\" differ from the defaults. "
                  "Shape edits are made on the shared definition at its default values, or on a "
                  "unique copy.",
              "The text names the differing values, got " + text(dialog.text()));
        check(dialog.accessibleDescription() == dialog.text(), "The text is the description");
        check(dialog.cancelButton()->isVisible() && dialog.uniqueButton()->isVisible() &&
                  dialog.defaultsButton()->isVisible() && dialog.defaultsButton()->isDefault() &&
                  dialog.defaultsButton()->property("accentFill").toBool() &&
                  dialog.defaultsButton()->hasFocus(),
              "Three choices; Edit definition at defaults is the focused, accent-filled default");
        check(dialog.uniqueButton()->text() == "Make unique and edit" &&
                  dialog.defaultsButton()->text() == "Edit definition at defaults" &&
                  dialog.cancelButton()->text() == "Cancel",
              "The design's button texts");
        // Tab reaches each button.
        std::set<QWidget *> reached;
        for (int i = 0; i < 6; ++i) {
            reached.insert(QApplication::focusWidget());
            QTest::keyClick(QApplication::focusWidget(), Qt::Key_Tab);
        }
        check(reached.contains(dialog.cancelButton()) && reached.contains(dialog.uniqueButton()) &&
                  reached.contains(dialog.defaultsButton()),
              "Tab reaches every button");
        QTest::keyClick(QApplication::focusWidget(), Qt::Key_Escape);
    });
    check(opened, "Enter on a placement with its own values opens the dialog");
    check(QTest::qWaitFor([] { return !QApplication::activeModalWidget(); }, 3000),
          "Esc closes the dialog");
    check(State(doc) == before && !f.view()->selectionState().context(),
          "Cancel changes nothing and opens nothing");
    check(QTest::qWaitFor([&] { return f.window.isActiveWindow(); }, 3000),
          "Activation returns to the window");
    // A placement at its defaults opens directly.
    f.view()->enterContext(0);
    f.select(f.t.defaults);
    f.focus(f.view());
    QTest::keyClick(f.view(), Qt::Key_Return);
    check(f.view()->componentScope() == f.t.defaults && !QApplication::activeModalWidget(),
          "A placement at its defaults opens without the dialog");
    f.view()->enterContext(0);
    QTest::qWait(10);
}

// Edit definition at defaults, closed without an edit: the reset is undone (§4.3).
// R094.f: Enter (and double-click, which activates the row) on a placement with its own values
// in the Outliner opens the same dialog as the viewport; Cancel leaves focus in the Outliner.
void outlinerOpen(Fixture &f) {
    auto &doc = f.doc();
    const State before(doc);
    auto *tabs = f.window.findChild<QTabWidget *>("organizationTabs");
    auto *outliner = f.window.findChild<QTreeWidget *>("outlinerTree");
    f.view()->enterContext(0);
    f.select(f.t.wide);
    tabs->setCurrentIndex(0);
    check(QTest::qWaitFor([&] { return outliner->isVisible() && outliner->currentItem(); }) &&
              outliner->currentItem()->data(0, Qt::UserRole).toULongLong() == f.t.wide,
          "The Outliner shows the selected placement");
    f.focus(outliner);
    const bool opened = f.withDialog(
        [&] { QTest::keyClick(outliner, Qt::Key_Return); },
        [&](ParametersScopeDialog &dialog) {
            check(dialog.windowTitle() == nameOf(doc, f.t.wide) + " has its own values",
                  "Enter in the Outliner opens the placement's dialog");
            QTest::keyClick(QApplication::focusWidget(), Qt::Key_Escape);
        });
    check(opened, "Enter in the Outliner offers the dialog before opening the placement");
    check(QTest::qWaitFor([&] { return outliner->hasFocus(); }) &&
              !f.view()->selectionState().context() && State(doc) == before,
          "Cancel opens nothing, changes nothing and leaves focus in the Outliner");
    tabs->setCurrentIndex(2);
    QTest::qWait(20);
}
void defaultsUnchanged(Fixture &f) {
    auto &doc = f.doc();
    const State before(doc);
    const bool dirty = doc.dirty();
    const auto encoded = encodeDocument(doc);
    const bool opened = f.withDialog(f.enterOn(f.t.wide), [&](ParametersScopeDialog &dialog) {
        QTest::keyClick(dialog.defaultsButton(), Qt::Key_Return);
    });
    check(opened, "The dialog opened");
    check(QTest::qWaitFor([&] { return f.view()->componentScope() == f.t.wide; }, 3000),
          "Edit definition at defaults opens the context");
    const auto name = nameOf(doc, f.t.wide);
    check(doc.instances().at(f.t.wide)->overrides.empty() && applied(doc) == before.position + 1 &&
              latestLabel(doc) == "Edit Table at defaults",
          "One labelled entry suspends the overrides, got " + latestLabel(doc));
    check(f.banner()->text().contains(name + "'s own values return when you close the definition"),
          "The banner says the values return, got " + text(f.banner()->text()));
    check(QTest::qWaitFor([&] { return f.view()->hasFocus(); }, 3000),
          "Focus returns to the viewport");
    f.closeByEscape();
    check(State(doc).bytes == before.bytes && applied(doc) == before.position &&
              doc.dirty() == dirty && doc.canRedo() && encodeDocument(doc).size() == encoded.size(),
          "Opening and closing without an edit leaves the records and the saved state");
    check(near(override(doc, f.t.wide, "width"), 1.2), "The values are back");
    // The breadcrumb and a click outside close it the same way; here the Close definition link.
    f.withDialog(f.enterOn(f.t.wide), [&](ParametersScopeDialog &dialog) {
        QTest::keyClick(dialog.defaultsButton(), Qt::Key_Return);
    });
    check(QTest::qWaitFor([&] { return f.view()->componentScope() == f.t.wide; }, 3000),
          "Reopened at defaults");
    emit f.banner()->linkActivated("close");
    QTest::qWait(20);
    check(!f.view()->componentScope() && State(doc).bytes == before.bytes &&
              applied(doc) == before.position,
          "Close definition restores by undoing the reset");
}

// Edit definition at defaults with an edit: exactly two extra entries, values restored (§4.3).
void defaultsEdited(Fixture &f) {
    auto &doc = f.doc();
    const State before(doc);
    f.withDialog(f.enterOn(f.t.wide), [&](ParametersScopeDialog &dialog) {
        QTest::keyClick(dialog.defaultsButton(), Qt::Key_Return);
    });
    check(QTest::qWaitFor([&] { return f.view()->componentScope() == f.t.wide; }, 3000),
          "The context is open at defaults");
    auto *section = f.definition();
    section->select("width");
    auto *expression = section->expressionField();
    f.type(expression->edit(), "1000 mm");
    f.enter(expression->edit());
    check(applied(doc) == before.position + 2 && latestLabel(doc) == "Edit parameter Width",
          "The default changes as one entry, got " + latestLabel(doc));
    f.closeByEscape();
    check(applied(doc) == before.position + 3 &&
              labelAt(doc, before.position) == "Edit Table at defaults" &&
              labelAt(doc, before.position + 1) == "Edit parameter Width" &&
              labelAt(doc, before.position + 2) == "Restore Table values",
          "Two extra labelled entries: suspend and restore, got " + latestLabel(doc));
    const auto &values = doc.instances().at(f.t.wide)->overrides;
    check(near(override(doc, f.t.wide, "width"), 1.2) &&
              near(override(doc, f.t.wide, "shelves"), 3) && values.contains("finish") &&
              std::get<ParameterChoice>(values.at("finish")).value == "ash",
          "The placement's values are restored");
    for (int i = 0; i < 3; ++i)
        f.undo();
    check(State(doc).bytes == before.bytes, "Undo returns to the start");
}

// Values the new definition refuses are left out of the restore (§4.3).
void partialRestore(Fixture &f) {
    auto &doc = f.doc();
    const State before(doc);
    f.withDialog(f.enterOn(f.t.wide), [&](ParametersScopeDialog &dialog) {
        QTest::keyClick(dialog.defaultsButton(), Qt::Key_Return);
    });
    check(QTest::qWaitFor([&] { return f.view()->componentScope() == f.t.wide; }, 3000),
          "The context is open at defaults");
    // Finish is removed while the definition is open: its value has nowhere to go.
    auto *section = f.definition();
    section->select("finish");
    const auto refused = section->removeCurrent();
    check(refused.isEmpty() && applied(doc) == before.position + 2 &&
              latestLabel(doc) == "Remove parameter Finish",
          "Finish is removed, got " + latestLabel(doc) + " / " + text(refused));
    f.closeByEscape();
    check(applied(doc) == before.position + 3 && latestLabel(doc) == "Restore Table values",
          "One restore entry, got " + latestLabel(doc));
    check(near(override(doc, f.t.wide, "width"), 1.2) &&
              near(override(doc, f.t.wide, "shelves"), 3) &&
              !doc.instances().at(f.t.wide)->overrides.contains("finish"),
          "Finish is left out; Width and Shelves are restored");
    check(f.status()->text() ==
              "Table's values are restored except Finish, which the definition no longer accepts",
          "The status bar names what was left out, got " + text(f.status()->text()));
    for (int i = 0; i < 3; ++i)
        f.undo();
    check(State(doc).bytes == before.bytes, "Undo returns to the start");
}

// Undoing the reset inside the context ends it and forgets the restore (§4.3).
void undoReset(Fixture &f) {
    auto &doc = f.doc();
    const State before(doc);
    f.withDialog(f.enterOn(f.t.wide), [&](ParametersScopeDialog &dialog) {
        QTest::keyClick(dialog.defaultsButton(), Qt::Key_Return);
    });
    check(QTest::qWaitFor([&] { return f.view()->componentScope() == f.t.wide; }, 3000),
          "The context is open at defaults");
    f.undo();
    QTest::qWait(20);
    check(!f.view()->componentScope() && near(override(doc, f.t.wide, "width"), 1.2) &&
              State(doc).bytes == before.bytes && applied(doc) == before.position,
          "Undoing the reset closes the context with the values back");
    check(f.status()->text() == "Table has its own values again; the definition was closed",
          "The status bar says why, got " + text(f.status()->text()));
    // Nothing is restored later.
    f.select(f.t.defaults);
    check(applied(doc) == before.position, "The restore is forgotten");
}

// Make unique and edit: one entry promotes the values to the copy's defaults (§4.2).
void promotion(Fixture &f) {
    auto &doc = f.doc();
    const State before(doc);
    const bool opened = f.withDialog(f.enterOn(f.t.wide), [&](ParametersScopeDialog &dialog) {
        // Back-tab from the default reaches Make unique and edit; Space presses it.
        QTest::keyClick(QApplication::focusWidget(), Qt::Key_Backtab);
        check(dialog.uniqueButton()->hasFocus(), "Shift+Tab reaches Make unique and edit");
        QTest::keyClick(dialog.uniqueButton(), Qt::Key_Space);
    });
    check(opened, "The dialog opened");
    check(QTest::qWaitFor([&] { return f.view()->componentScope() == f.t.wide; }, 3000),
          "Make unique and edit opens the context");
    const auto definition = doc.instances().at(f.t.wide)->definition;
    check(definition != f.t.definition && doc.instances().at(f.t.wide)->overrides.empty(),
          "The placement is unique and has no overrides");
    check(applied(doc) == before.position + 1 && latestLabel(doc) == "Make Table unique",
          "One labelled entry, got " + latestLabel(doc));
    const auto &record = *doc.definitions().at(definition)->parameters;
    const auto width = std::find_if(record.parameters.begin(), record.parameters.end(),
                                    [](const auto &p) { return p.name == "width"; });
    check(width != record.parameters.end() && width->expression.find("1.2") != std::string::npos,
          "The copy's default is the placement's value, got " +
              (width != record.parameters.end() ? width->expression : std::string{}));
    check(!f.banner()->text().contains("own values return"), "Nothing is suspended");
    f.closeByEscape();
    check(applied(doc) == before.position + 1, "Closing a unique copy adds nothing");
    f.undo();
    check(State(doc).bytes == before.bytes, "Undo returns to the start");
}

// A nested placement's values come from its parent: Cancel and Make unique and edit (§4.1).
void nested(Fixture &f) {
    auto &doc = f.doc();
    // The room has its own value: its context is opened directly here.
    f.view()->enterContext(f.t.roomPlacement);
    QTest::qWait(10);
    const State before(doc);
    const auto open = [&] {
        f.select(f.t.nested);
        f.focus(f.view());
        QTest::keyClick(f.view(), Qt::Key_Return);
    };
    bool opened = f.withDialog(open, [&](ParametersScopeDialog &dialog) {
        check(dialog.text() ==
                  "Width 1800 mm differs from the defaults. Its values come from Room.",
              "The nested text, got " + text(dialog.text()));
        check(!dialog.defaultsButton()->isVisible() && dialog.uniqueButton()->isDefault() &&
                  dialog.uniqueButton()->hasFocus() && dialog.cancelButton()->isVisible(),
              "Only Cancel and Make unique and edit, the latter the default");
        QTest::keyClick(QApplication::focusWidget(), Qt::Key_Escape);
    });
    check(opened && State(doc) == before &&
              f.view()->selectionState().context() == f.t.roomPlacement,
          "Cancel leaves the nested placement");
    opened = f.withDialog(open, [&](ParametersScopeDialog &dialog) {
        QTest::keyClick(dialog.uniqueButton(), Qt::Key_Return);
    });
    check(opened && QTest::qWaitFor([&] { return f.view()->componentScope() == f.t.nested; }, 3000),
          "Make unique and edit opens the nested placement");
    check(applied(doc) == before.position + 1 && latestLabel(doc) == "Make Table unique",
          "One labelled entry, got " + latestLabel(doc));
    check(doc.instances().at(f.t.nested)->definition != f.t.definition,
          "The nested placement is unique");
    // Its values come from the room still, and now they are its defaults: edits are allowed.
    try {
        requireComponentTemplateScope(doc, f.t.nested, {});
    } catch (const std::exception &error) {
        check(false, std::string("The copy shows its template: ") + error.what());
    }
    f.view()->enterContext(0);
    f.undo();
    check(State(doc).bytes == before.bytes, "Undo returns to the start");
}

// The rows of §5 produced at their controls; each refusal leaves the document unchanged.
void valueRefusals(Fixture &f) {
    auto &doc = f.doc();
    f.view()->enterContext(0);
    f.select(f.t.defaults);
    // PARAMETER_DOMAIN (bounds): the value and the bound in the document's units.
    {
        const State before(doc);
        auto *width = f.edit("width");
        f.type(width, "2.6 m");
        check(QTest::qWaitFor([&] { return !f.field("width")->error().isEmpty(); }, 2000),
              "The bounds check runs");
        f.enter(width);
        check(f.field("width")->error() ==
                      "2600 mm is above the maximum, 2400 mm. Nothing was changed." &&
                  width->accessibleDescription().startsWith("Invalid: 2600 mm is above"),
              "Bounds at the field, got " + text(f.field("width")->error()));
        check(State(doc) == before, "The bounds refusal changes nothing");
        QTest::keyClick(width, Qt::Key_Escape);
    }
    // PARAMETER_REGENERATION with Show the driver: a slab that collapses past its plane.
    {
        const auto slab = box(doc, {10, 0, 0}, {11, 1, .1}, "Slab");
        const auto group = createGroup(doc, {slab}, "Slab");
        const auto created = createComponent(doc, group, "Slab");
        defineComponentParameter(
            doc, created.definition,
            parameter("size", "Size", ParameterType::Length, ComponentParameterMode::Input, "1 m"));
        auto stretch = driver(ComponentDriverKind::Stretch, slab, "size");
        stretch.at = 10.5;
        const auto driverId =
            driveComponent(doc, created.definition, 0, stretch).createdDrivers.at(0);
        f.changed();
        f.select(created.instance);
        const State before(doc);
        auto *size = f.edit("size");
        f.type(size, "-200");
        f.enter(size);
        auto *row = f.field("size");
        check(
            row->error() ==
                "At -200 mm Slab would pass its stretch plane (Stretch Slab). Nothing was changed.",
            "Regeneration at the field, got " + text(row->error()));
        check(row->actionButton()->isVisible() && row->actionButton()->text() == "Show the driver",
              "The field offers Show the driver");
        check(State(doc) == before, "The regeneration refusal changes nothing");
        QTest::keyClick(size, Qt::Key_Escape);
        // The error is shown again and its action pressed from the keyboard.
        f.type(size, "-200");
        f.enter(size);
        f.press(row->actionButton());
        check(QTest::qWaitFor([&] { return f.view()->componentScope() == created.instance; }, 3000),
              "Show the driver opens the definition");
        check(f.drivers()->currentDriver() == driverId && f.drivers()->tree()->hasFocus(),
              "Show the driver selects the driver in the Drivers list");
        f.view()->enterContext(0);
        QTest::qWait(10);
    }
}

// LOCKED_ENTITY at a value field and in the definition editor, with Select the placement.
void lockedRefusals(Fixture &f) {
    auto &doc = f.doc();
    f.view()->enterContext(0);
    check(batch(doc, {QJsonObject{{"command", "scene.state"},
                                  {"body", QString::number(f.t.defaults)},
                                  {"locked", true}}})
              .isEmpty(),
          "The defaults placement is locked");
    f.changed();
    const auto lockedAt = applied(doc);
    // A locked placement cannot be selected, so its own fields never show; the value-field
    // wording is checked through the table (messageTable).
    f.select(f.t.defaults);
    check(!f.section()->isVisible() || f.section()->placement() != f.t.defaults,
          "A locked placement is not selectable");
    // Changing a default would change the locked placement: the editor names it and offers
    // to select it, which closes the context (and restores the edited placement).
    f.withDialog(f.enterOn(f.t.wide), [&](ParametersScopeDialog &dialog) {
        QTest::keyClick(dialog.defaultsButton(), Qt::Key_Return);
    });
    check(QTest::qWaitFor([&] { return f.view()->componentScope() == f.t.wide; }, 3000),
          "The context is open at defaults");
    const State open(doc);
    auto *section = f.definition();
    section->select("width");
    f.type(section->expressionField()->edit(), "1000 mm");
    f.enter(section->expressionField()->edit());
    const auto sentence = section->editorError()->text();
    check(section->editorError()->isVisible() &&
              sentence == "Changing the default Width would change Table, which is locked. "
                          "Unlock it or keep the default." &&
              section->editorError()->property("tone").toString() == "warning",
          "The editor names the locked placement, got " + text(sentence));
    check(section->editorAction()->isVisible() && section->editorAction()->text() == "Select Table",
          "The editor offers to select it, got " + text(section->editorAction()->text()));
    check(State(doc) == open, "The locked refusal changes nothing");
    f.press(section->editorAction());
    QTest::qWait(20);
    auto *outliner = f.window.findChild<QTreeWidget *>("outlinerTree");
    check(!f.view()->selectionState().context() && outliner && outliner->isVisible() &&
              outliner->currentItem() &&
              outliner->currentItem()->data(0, Qt::UserRole).toULongLong() == f.t.defaults &&
              outliner->hasFocus(),
          "Select closes the context and shows the locked placement in the Outliner");
    check(near(override(doc, f.t.wide, "width"), 1.2) && applied(doc) == lockedAt,
          "The edited placement's values are back");
    f.undo();
    check(!doc.bodies().at(f.t.defaults)->locked, "The lock is undone");
}

// The formula rows (syntax, type with Use 120 mm, name, domain, cycle) and LIMIT_EXCEEDED.
void formulaRefusals(Fixture &f) {
    auto &doc = f.doc();
    // Select #1 showed the Outliner: back to Entity info.
    f.window.findChild<QTabWidget *>("organizationTabs")->setCurrentIndex(2);
    f.view()->enterContext(0);
    f.view()->enterContext(f.t.defaults);
    QTest::qWait(10);
    auto *section = f.definition();
    section->select("leg_x");
    auto *field = section->expressionField();
    const State before(doc);
    const std::pair<const char *, const char *> rows[] = {
        {"width - (120 mm", "A closing \")\" is missing at character 16."},
        {"width - 120", "Can't subtract a number from a length. Add a unit, for example 120 mm."},
        {"depth - 120 mm", "No parameter is called depth."},
        {"width / 0", "Division by zero at character 7."},
    };
    for (const auto &[formula, expected] : rows) {
        f.type(field->edit(), formula);
        check(QTest::qWaitFor([&] { return !field->error().isEmpty(); }, 2000) &&
                  field->error() == expected,
              std::string("The formula field says ") + expected + ", got " + text(field->error()));
        f.enter(field->edit());
        check(State(doc) == before, std::string("Refused: ") + formula);
    }
    // Use 120 mm fixes the text in one press.
    f.type(field->edit(), "width - 120");
    check(QTest::qWaitFor([&] { return field->fixButton()->isVisible(); }, 2000) &&
              field->fixButton()->text() == "Use 120 mm",
          "The unit fix is offered: " + text(field->error()) + " / " +
              text(field->fixButton()->text()) + " / " +
              std::to_string(field->fixButton()->isVisibleTo(field)) + " / " +
              std::to_string(field->isVisible()) + " / " + text(field->text()));
    f.press(field->fixButton());
    check(field->text() == "width - 120 mm" && field->error().isEmpty(), "Use 120 mm fixes it");
    QTest::keyClick(field->edit(), Qt::Key_Escape);
    // A cycle names the labels.
    section->select("ratio");
    f.type(section->expressionField()->edit(), "leg_x / 1 m");
    f.enter(section->expressionField()->edit());
    check(latestLabel(doc) == "Edit parameter Width ratio", "Width ratio follows Leg position");
    const State cycleBefore(doc);
    section->select("leg_x");
    f.type(section->expressionField()->edit(), "ratio * 1 m");
    check(QTest::qWaitFor([&] { return !section->expressionField()->error().isEmpty(); }, 2000) &&
              section->expressionField()->error() ==
                  "These depend on each other: Leg position → Width ratio → Leg position. One of "
                  "them needs a value instead of a formula.",
          "The cycle by labels, got " + text(section->expressionField()->error()));
    f.enter(section->expressionField()->edit());
    check(State(doc) == cycleBefore, "The cycle is refused");
    QTest::keyClick(section->expressionField()->edit(), Qt::Key_Escape);
    f.undo();
    // LIMIT_EXCEEDED: a choice with 33 options, refused in the editor with the core's message.
    const State limitBefore(doc);
    section->beginCreate();
    f.type(section->labelEdit(), "Many");
    f.type(section->nameEdit(), "many");
    const int choice = section->typeChoice()->findData("choice");
    check(choice >= 0, "The editor offers choice");
    section->typeChoice()->setCurrentIndex(choice);
    QStringList options;
    for (int i = 0; i < 33; ++i)
        options << QString("o%1 = O%1").arg(i);
    f.type(section->optionsEdit(), options.join("; "));
    f.type(section->expressionField()->edit(), "\"o0\"");
    f.enter(section->labelEdit());
    check(section->editorError()->isVisible() &&
              section->editorError()->text().contains("32 options") &&
              section->editorError()->text().endsWith("Nothing was changed."),
          "LIMIT_EXCEEDED names the limit, got " + text(section->editorError()->text()));
    check(State(doc) == limitBefore, "The limit refusal changes nothing");
    section->cancel();
    f.view()->enterContext(0);
    QTest::qWait(10);
}

// PARAMETER_TEMPLATE_SCOPE: another client gives the open placement values (§5).
void templateScope(Fixture &f) {
    auto &doc = f.doc();
    f.view()->enterContext(0);
    f.view()->enterContext(f.t.defaults);
    QTest::qWait(10);
    check(batch(doc, {QJsonObject{{"command", "component_parameter.override"},
                                  {"body", QString::number(f.t.defaults)},
                                  {"values", QJsonObject{{"width", 1.0}}}}})
              .isEmpty(),
          "Another client overrides the open placement");
    f.changed();
    check(!f.view()->componentScope() &&
              f.status()->text() ==
                  "Table now has its own values. Close the definition and open it again.",
          "The context closes and says why, got " + text(f.status()->text()));
    f.undo();
}

// STALE_REVISION in the dialog that issued the command: the model changed while it was open.
void staleRevision(Fixture &f) {
    auto &doc = f.doc();
    const bool opened = f.withDialog(f.enterOn(f.t.wide), [&](ParametersScopeDialog &dialog) {
        check(batch(doc, {QJsonObject{{"command", "component_parameter.override"},
                                      {"body", QString::number(f.t.defaults)},
                                      {"values", QJsonObject{{"width", 1.0}}}}})
                  .isEmpty(),
              "The model changes while the dialog is open");
        const State changed(doc);
        QTest::keyClick(dialog.defaultsButton(), Qt::Key_Return);
        QTest::qWait(20);
        check(dialog.isVisible() && dialog.errorLabel()->isVisible() &&
                  dialog.errorLabel()->text() == "The model changed. Try again." &&
                  dialog.accessibleDescription().endsWith("The model changed. Try again."),
              "The dialog says the model changed and stays open");
        check(State(doc) == changed, "Nothing was committed");
        QTest::keyClick(QApplication::focusWidget(), Qt::Key_Escape);
    });
    check(opened, "The dialog opened");
    f.undo();
    f.view()->enterContext(0);
}

// The table rows that the desktop cannot produce from its own thread (§5).
void messageTable(Fixture &f) {
    f.view()->enterContext(0);
    f.select(f.t.wide);
    ParameterMessageContext context;
    context.document = &f.doc();
    context.units = DisplayUnit::Meters;
    context.precision = 1;
    context.parameter = {{"type", "length"}, {"label", "Width"}};
    context.typed = "2.6m";
    const QJsonObject bounds{
        {"code", "PARAMETER_DOMAIN"},
        {"message", "width is above its maximum"},
        {"details", QJsonObject{{"bound", "max"}, {"limit", 2.4}, {"value", 2.6}}}};
    check(parameterMessage(bounds, context).sentence ==
              "2.6 m is above the maximum, 2.4 m. Nothing was changed.",
          "The design's sentence in metres, got " +
              text(parameterMessage(bounds, context).sentence));
    check(parameterMessage(parameterRefusal(std::runtime_error("STALE_REVISION")), context)
                  .sentence == "The model changed. Try again.",
          "STALE_REVISION");
    {
        auto locked = context;
        locked.placement = f.t.defaults;
        const auto message =
            parameterMessage({{"code", "LOCKED_ENTITY"},
                              {"message", "locked"},
                              {"details", QJsonObject{{"body", QString::number(f.t.defaults)}}}},
                             locked);
        check(message.sentence == "Table is locked. Unlock it to change its values." &&
                  message.tone == ParameterMessageTone::Warning &&
                  message.action == ParameterMessageAction::None,
              "A locked placement at its own value field, got " + text(message.sentence));
        auto *row = f.window.findChild<ComponentParametersSection *>()->fields().front();
        row->setMessage(message);
        check(row->warning() && !row->control()->property("invalid").toBool() &&
                  row->control()->accessibleDescription().startsWith("Warning: Table is locked."),
              "A warning is amber, not invalid, and described as a warning");
        row->clearError();
    }
    check(parameterMessage({{"code", "NOT_FOUND"}, {"message", "x"}}, context).sentence ==
                  "The placement was removed." &&
              parameterMessage({{"code", "INVALID_TARGET"}, {"message", "x"}}, context).sentence ==
                  "This placement can't hold values.",
          "NOT_FOUND and INVALID_TARGET at a value field");
    context.place = ParameterMessagePlace::Driver;
    context.member = "Shelf";
    context.textMember = true;
    check(parameterMessage({{"code", "PARAMETER_DRIVER_TARGET"},
                            {"message", "driver #4: stretch drives Geometry members only"}},
                           context)
                  .sentence == "Text can't be stretched.",
          "A text member can't be stretched");
    context.childParameter = "depth";
    check(parameterMessage({{"code", "PARAMETER_DRIVER_TARGET"},
                            {"message", "driver #4: Shelf has no input parameter 'depth'"}},
                           context)
                  .sentence == "Shelf has no parameter depth.",
          "A child without the parameter");
    check(parameterMessage(
              {{"code", "PARAMETER_DRIVER_TARGET"},
               {"message", "driver #4: a rotation would move the glue face off its plane"}},
              context)
                  .sentence == "This would move the glue face off its wall.",
          "The glue face");
    const auto limit = parameterMessage(
        {{"code", "LIMIT_EXCEEDED"}, {"message", "Component parameters exceed 128 per definition"}},
        context);
    check(limit.sentence == "Component parameters exceed 128 per definition. Nothing was changed.",
          "LIMIT_EXCEEDED quotes the core");
}

// The changed-since-bound strip, Rebind and Undo; Rebind and Rebind all in Drivers (§5).
void changedSinceBound(Fixture &f) {
    auto &doc = f.doc();
    f.view()->enterContext(0);
    f.view()->enterContext(f.t.defaults);
    QTest::qWait(10);
    auto *strip = f.window.findChild<QWidget *>("parameterStatusStrip");
    check(strip && !strip->isVisible(), "No strip while every driver is bound");
    // D-038: a paint changes no shape. Painting the shelf, which Repeat and Visibility
    // act on, leaves both bound: no strip, and Drivers reads no row Changed.
    const auto unpainted = applied(doc);
    f.select(authoring::sceneOf(doc, f.t.defaults, f.t.shelf));
    f.view()->paintSelection({.4f, .25f, .1f});
    QTest::qWait(20);
    check(applied(doc) == unpainted + 1 && latestLabel(doc) == "Edit shared component definition",
          "Painting the shelf is one entry, got " + latestLabel(doc));
    check(!strip->isVisible(), "Painting a driven member shows no strip");
    {
        auto *drivers = f.drivers();
        QStringList rows;
        for (int i = 0; i < drivers->tree()->topLevelItemCount(); ++i)
            rows << drivers->tree()->topLevelItem(i)->data(0, Qt::AccessibleTextRole).toString();
        check(!rows.isEmpty() && !rows.join("|").contains(", Changed") &&
                  !drivers->rebindAllButton()->isVisible(),
              "Painting leaves every driver bound in Drivers, got " + text(rows.join(" | ")));
    }
    f.undo();
    QTest::qWait(10);
    check(applied(doc) == unpainted && !strip->isVisible(), "Undo removes the paint");
    const auto before = applied(doc);
    const auto shelf = authoring::sceneOf(doc, f.t.defaults, f.t.shelf);
    f.select(shelf);
    f.view()->setTool(Viewport::Tool::Move);
    check(f.view()->measurements("[0.1,0.1,0.21]") && f.view()->measurements("0,0,0.05"),
          "The shelf moves inside the definition");
    f.view()->setTool(Viewport::Tool::Select);
    QTest::qWait(20);
    auto *label = strip->findChild<QLabel *>("parameterStatusText");
    const auto edit = QString::fromStdString(latestLabel(doc));
    check(strip->isVisible() && label->text().startsWith(edit.toHtmlEscaped() + ". ") &&
              label->text().contains("<b>Repeat Shelf</b>") &&
              label->text().contains("<b>Visibility Shelf</b>") &&
              label->text().contains(
                  "now start from a changed shape. Placements may not look as expected until you "
                  "rebind."),
          "The strip names the edit and the changed drivers, got " + text(label->text()));
    check(strip->accessibleDescription().contains("Repeat Shelf") &&
              !strip->accessibleDescription().contains("<b>"),
          "The strip's description is its plain sentence");
    auto *rebind = strip->findChild<QPushButton *>("parameterStatusRebind");
    auto *undo = strip->findChild<QPushButton *>("parameterStatusUndo");
    check(rebind->isVisible() && undo->isVisible() && undo->accessibleName() == "Undo" &&
              strip->geometry().top() > f.banner()->geometry().bottom(),
          "Rebind and Undo follow the sentence under the banner");
    // Tab from the banner's chain reaches Rebind and Undo.
    f.press(rebind);
    check(applied(doc) == before + 2 && latestLabel(doc) == "Rebind 2 drivers" &&
              !strip->isVisible(),
          "Rebind is one entry and the strip goes, got " + latestLabel(doc));
    f.undo();
    QTest::qWait(10);
    check(!strip->isVisible(), "Undoing the rebind does not show the strip again");
    f.undo();
    check(applied(doc) == before, "Back before the move");
    // Undo on the strip undoes the edit.
    f.select(authoring::sceneOf(doc, f.t.defaults, f.t.shelf));
    f.view()->setTool(Viewport::Tool::Move);
    check(f.view()->measurements("[0.1,0.1,0.21]") && f.view()->measurements("0,0,0.05"),
          "The shelf moves again");
    f.view()->setTool(Viewport::Tool::Select);
    QTest::qWait(20);
    check(strip->isVisible(), "The strip shows again");
    f.press(undo);
    QTest::qWait(10);
    check(applied(doc) == before && !strip->isVisible(),
          "Undo undoes the edit and hides the strip");
    // The Drivers list keeps Rebind and Rebind all.
    f.select(authoring::sceneOf(doc, f.t.defaults, f.t.shelf));
    f.view()->setTool(Viewport::Tool::Move);
    check(f.view()->measurements("[0.1,0.1,0.21]") && f.view()->measurements("0,0,0.05"),
          "The shelf moves a third time");
    f.view()->setTool(Viewport::Tool::Select);
    QTest::qWait(20);
    auto *drivers = f.drivers();
    check(drivers->rebindAllButton()->isVisible(), "Rebind all shows for two Changed drivers");
    f.press(drivers->rebindAllButton());
    check(latestLabel(doc) == "Rebind 2 drivers" && !strip->isVisible(),
          "Rebind all is one entry, got " + latestLabel(doc));
    f.undo();
    f.undo();
    // Closing the context hides the strip.
    f.view()->enterContext(0);
    QTest::qWait(10);
    check(!strip->isVisible() && applied(doc) == before, "Closing the context hides the strip");
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
        cancel(f);
        std::cout << "The dialog, Cancel and Esc passed\n";
        outlinerOpen(f);
        std::cout << "Enter in the Outliner offers the dialog passed\n";
        defaultsUnchanged(f);
        std::cout << "Edit definition at defaults without an edit leaves nothing passed\n";
        defaultsEdited(f);
        std::cout << "Edit definition at defaults with an edit: two entries, values back passed\n";
        partialRestore(f);
        std::cout << "Partial restore passed\n";
        undoReset(f);
        std::cout << "Undoing the reset closes the context passed\n";
        promotion(f);
        std::cout << "Make unique and edit promotes the values passed\n";
        nested(f);
        std::cout << "Nested placements passed\n";
        valueRefusals(f);
        std::cout << "Bounds and regeneration with Show the driver passed\n";
        lockedRefusals(f);
        std::cout << "Locked placement at the field and in the editor with Select passed\n";
        formulaRefusals(f);
        std::cout << "Formula rows, Use 120 mm, cycle and LIMIT_EXCEEDED passed\n";
        templateScope(f);
        std::cout << "PARAMETER_TEMPLATE_SCOPE closes the context passed\n";
        staleRevision(f);
        std::cout << "STALE_REVISION in the dialog passed\n";
        messageTable(f);
        std::cout << "Message table rows passed\n";
        changedSinceBound(f);
        std::cout << "Changed-since-bound strip, Rebind, Undo and Rebind all passed\n";
        if (argc == 2) {
            check(window.grab().save(QString::fromLocal8Bit(argv[1])), "Screenshot saved");
        }
        window.document().markSaved();
        std::cout << "component_parameter_state_input_tests: " << checks
                  << " checks passed; DPR=" << window.devicePixelRatioF() << '\n';
    } catch (const std::exception &error) {
        window.document().markSaved();
        std::cerr << error.what() << '\n';
        return 1;
    }
}
