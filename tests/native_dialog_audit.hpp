#pragma once
// R084.u: keyboard-only audit of every dialog the desktop application can show.
//
// Each case starts with keyboard focus in the viewport, opens its dialog through a
// keyboard path (the Ctrl+K command palette, a shortcut, or Tab/Space on a panel or
// parent-dialog button), and then checks, with key events only:
//   - every visible, enabled, Tab-focusable control has an accessible name;
//   - Tab and Shift+Tab each reach every such control, in mirrored order, without
//     leaving the dialog, and the Tab order follows reading order;
//   - Escape closes the dialog with no document or preference change, and the owning
//     window's keyboard focus returns to the opener (or the viewport);
//   - the expected button is the default, and in a second opening Enter triggers it (where
//     that would act on external files or a provider, only the default is asserted).
// Preparation (selection, fixture models, files to import) uses public APIs; it is
// not part of the keyboard claim. Findings are collected rather than thrown, so one
// run reports every failing dialog of its group.
#include "app/window.hpp"
#include "integrations/glb_export.hpp"
#include "integrations/render_snapshot.hpp"
#include "io/document_io.hpp"
#include <QAbstractButton>
#include <QAbstractItemView>
#include <QAbstractSpinBox>
#include <QAccessible>
#include <QAction>
#include <QApplication>
#include <QButtonGroup>
#include <QCheckBox>
#include <QColorDialog>
#include <QComboBox>
#include <QDialog>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileDialog>
#include <QInputDialog>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QKeySequenceEdit>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMenu>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPointer>
#include <QProgressDialog>
#include <QPushButton>
#include <QRadioButton>
#include <QRegularExpression>
#include <QScrollArea>
#include <QSettings>
#include <QTabWidget>
#include <QTest>
#include <QTextEdit>
#include <QTimer>
#include <QTreeWidget>
#include <QWindow>
#include <functional>
#include <iostream>
#include <memory>
#include <set>
namespace dialog_audit {
using namespace sketchy;
using Match = std::function<bool(QWidget *)>;

struct Failure : std::runtime_error {
    using std::runtime_error::runtime_error;
};
inline void require(bool value, const QString &message) {
    if (!value)
        throw Failure(message.toStdString());
}
inline QString identity(const QObject *object) {
    if (!object)
        return {};
    return QString::fromLatin1(object->metaObject()->className()) + ':' + object->objectName();
}
inline QWidget *focused() { return QApplication::focusWidget(); }
// The widget that holds `window`'s keyboard focus: the application focus widget while the
// window is active, otherwise the focus child it restores when reactivated. Reactivation after
// a dialog closes belongs to the window manager (Xvfb runs none; Weston reactivates).
inline QWidget *returnFocus(QWidget *window) {
    if (!window)
        return nullptr;
    return window->isActiveWindow() ? QApplication::focusWidget() : window->focusWidget();
}

// ----- Matching top-level dialogs ------------------------------------------------------
inline QWidget *shown(const Match &match) {
    for (auto *widget : QApplication::topLevelWidgets())
        if (widget->isVisible() && match(widget))
            return widget;
    return nullptr;
}
inline Match named(const QString &name) {
    return [name](QWidget *w) { return w->objectName() == name; };
}
inline Match namedTitle(const QString &name, const QString &title) {
    return
        [name, title](QWidget *w) { return w->objectName() == name && w->windowTitle() == title; };
}
template <class T> Match kind(const QString &title) {
    return [title](QWidget *w) {
        return qobject_cast<T *>(w) && (title.isEmpty() || w->windowTitle() == title);
    };
}

// ----- Watching for a dialog while a blocking trigger runs ------------------------------
// Each watch owns its timer: Qt never re-enters a timer whose slot is still running, and a
// watch body usually runs inside the watched dialog's nested event loop.
class Watch {
  public:
    Watch(Match match, std::function<void(QWidget &)> body)
        : match_(std::move(match)), body_(std::move(body)) {
        QObject::connect(&timer_, &QTimer::timeout, [this] { poll(); });
        timer_.start(10);
    }
    bool fired() const { return fired_; }
    void rethrow() const {
        if (failure_)
            std::rethrow_exception(failure_);
    }

  private:
    void poll() {
        if (fired_)
            return;
        auto *widget = shown(match_);
        if (!widget)
            return;
        fired_ = true;
        timer_.stop();
        try {
            body_(*widget);
        } catch (...) {
            failure_ = std::current_exception();
            if (auto *dialog = qobject_cast<QDialog *>(widget); dialog && dialog->isVisible())
                dialog->reject();
        }
    }
    Match match_;
    std::function<void(QWidget &)> body_;
    QTimer timer_;
    bool fired_{};
    std::exception_ptr failure_;
};
// Closes every visible dialog while a case is stuck past its budget, so the run continues.
class Rescue {
  public:
    explicit Rescue(int milliseconds) {
        timer_.setSingleShot(true);
        QObject::connect(&timer_, &QTimer::timeout, [this] {
            triggered_ = true;
            sweep_.start(100);
        });
        QObject::connect(&sweep_, &QTimer::timeout, [] {
            if (auto *popup = QApplication::activePopupWidget())
                popup->close();
            for (auto *widget : QApplication::topLevelWidgets())
                if (auto *dialog = qobject_cast<QDialog *>(widget); dialog && dialog->isVisible())
                    dialog->reject();
        });
        timer_.start(milliseconds);
    }
    bool triggered() const { return triggered_; }

  private:
    QTimer timer_, sweep_;
    bool triggered_{};
};

// ----- Keyboard primitives -----------------------------------------------------------
inline void waitDialogFocus(QWidget &dialog) {
    dialog.activateWindow();
    require(QTest::qWaitForWindowFocused(&dialog), "Dialog receives native keyboard focus");
    const bool transferred = QTest::qWaitFor([&] {
        auto *f = focused();
        return f && (f == &dialog || dialog.isAncestorOf(f));
    });
    require(transferred, "Dialog focus transfer completes (focus " + identity(focused()) +
                             ", active " + identity(QApplication::activeWindow()) + ")");
}
inline bool inside(QWidget &dialog, QWidget *widget) {
    return widget && (widget == &dialog || dialog.isAncestorOf(widget));
}
// Presses Tab inside `scope` until `name` has focus; fails if focus leaves the scope.
inline QWidget *tabTo(QWidget &scope, const QString &name, bool backward = false, int limit = 90) {
    for (int i = 0; i < limit; ++i) {
        auto *f = focused();
        require(f && (f == &scope || scope.isAncestorOf(f)),
                "Tab search for " + name + " stays inside " + identity(&scope));
        // "text:Label" finds an unnamed button by its visible text.
        if (f->objectName() == name ||
            (name.startsWith("text:") && qobject_cast<QAbstractButton *>(f) &&
             static_cast<QAbstractButton *>(f)->text() == name.mid(5)))
            return f;
        if (backward)
            QTest::keyClick(f, Qt::Key_Backtab, Qt::ShiftModifier);
        else
            QTest::keyClick(f, Qt::Key_Tab);
    }
    throw Failure(("Tab reaches " + name).toStdString());
}
// Activates a push button by keyboard (Space). Blocks while a modal opened by it runs.
inline void press(QWidget *button) {
    require(button && button->isEnabled(), "Opener button is enabled: " + identity(button));
    QTest::keyClick(button, Qt::Key_Space);
}

// ----- Inspection --------------------------------------------------------------------
inline bool tabFocusable(QWidget *widget, QWidget &dialog) {
    return widget->isVisibleTo(&dialog) && widget->isEnabled() &&
           (widget->focusPolicy() & Qt::TabFocus);
}
inline QWidget *publicControl(QWidget *widget, QWidget &dialog) {
    // Composite editors expose internal line edits; a visit to either is the same control.
    for (auto *parent = widget->parentWidget(); parent && parent != &dialog;
         parent = parent->parentWidget())
        if (qobject_cast<QAbstractSpinBox *>(parent) || qobject_cast<QKeySequenceEdit *>(parent) ||
            qobject_cast<QComboBox *>(parent))
            return parent;
    return widget;
}
inline QString accessibleName(QWidget *widget) {
    auto *accessible = QAccessible::queryAccessibleInterface(widget);
    return accessible ? accessible->text(QAccessible::Name) : QString{};
}
// Where a control is, for reading order: in the dialog when it had focus (a scroll area
// scrolls it into view), and in its scroll area's content, if any.
struct Place {
    QRect dialog;
    QWidget *content{};
    QRect inContent;
};
struct Traversal {
    std::vector<QWidget *> cycle;
    std::vector<Place> places;
    std::set<QWidget *> visited;
    QString escape;
};
inline Place placeOf(QWidget *control, QWidget &dialog) {
    Place place{QRect(control->mapTo(&dialog, QPoint()), control->size())};
    for (auto *parent = control->parentWidget(); parent && parent != &dialog;
         parent = parent->parentWidget())
        if (auto *scroll = qobject_cast<QScrollArea *>(parent); scroll && scroll->widget()) {
            place.content = scroll->widget();
            place.inContent = QRect(control->mapTo(place.content, QPoint()), control->size());
            break;
        }
    return place;
}
// A radio group is one Tab stop, the platform convention Qt follows: Tab enters its checked
// button and the arrow keys choose within the group (R092.b's page choice). An unchecked member
// of a group with a checked, Tab-focusable button is reached that way, not by Tab. With no button
// checked (a choice the person has not made), Tab enters at the group's first Tab-focusable
// button, and the rest are reached by the arrow keys in the same way.
inline bool arrowOnlyRadio(QWidget *widget, QWidget &dialog) {
    auto *radio = qobject_cast<QRadioButton *>(widget);
    if (!radio || radio->isChecked() || !radio->autoExclusive())
        return false;
    QList<QAbstractButton *> group;
    if (radio->group())
        group = radio->group()->buttons();
    else if (radio->parentWidget())
        for (auto *sibling :
             radio->parentWidget()->findChildren<QRadioButton *>(Qt::FindDirectChildrenOnly))
            if (sibling->autoExclusive())
                group << sibling;
    if (std::any_of(group.begin(), group.end(), [&](QAbstractButton *member) {
            return member != radio && member->isChecked() && tabFocusable(member, dialog);
        }))
        return true;
    for (auto *member : group)
        if (tabFocusable(member, dialog))
            return member != radio;
    return false;
}
inline std::set<QWidget *> expectedControls(QWidget &dialog) {
    std::set<QWidget *> result;
    for (auto *widget : dialog.findChildren<QWidget *>())
        if (tabFocusable(widget, dialog) && !arrowOnlyRadio(widget, dialog))
            result.insert(publicControl(widget, dialog));
    return result;
}
inline Traversal traverse(QWidget &dialog, size_t expected, bool backward) {
    Traversal result;
    const auto steps = expected * 2 + 2;
    for (size_t step = 0; step < steps; ++step) {
        auto *f = focused();
        if (!inside(dialog, f)) {
            result.escape = identity(f);
            break;
        }
        auto *control = publicControl(f, dialog);
        // The Tab chain is deterministic: back at the first control, the cycle is complete.
        if (step > 0 && !result.cycle.empty() && control == result.cycle.front())
            break;
        if (result.visited.insert(control).second) {
            result.cycle.push_back(control);
            result.places.push_back(placeOf(control, dialog));
        }
        if (backward)
            QTest::keyClick(f, Qt::Key_Backtab, Qt::ShiftModifier);
        else
            QTest::keyClick(f, Qt::Key_Tab);
    }
    return result;
}
inline QJsonArray identities(const std::vector<QWidget *> &widgets) {
    QJsonArray result;
    for (auto *widget : widgets)
        result.append(identity(widget));
    return result;
}
// Reading order: the next control must not start clearly above the previous one, except
// for the single wrap from the last control back to the first.
// Reading order around the Tab cycle: a step to the top of a column further right is reading
// order; a step up and to the left is a wrap. A cycle in reading order wraps exactly once.
inline QJsonArray backwardJumps(const Traversal &traversal) {
    QJsonArray jumps;
    const auto &cycle = traversal.cycle;
    for (size_t i = 0; cycle.size() > 1 && i < cycle.size(); ++i) {
        const auto next = (i + 1) % cycle.size();
        const auto &from = traversal.places[i], &to = traversal.places[next];
        // Within one scroll area compare content positions; otherwise where each was shown.
        const bool shared = from.content && from.content == to.content;
        const auto &a = shared ? from.inContent : from.dialog;
        const auto &b = shared ? to.inContent : to.dialog;
        if (b.bottom() < a.top() - 2 && b.left() <= a.left())
            jumps.append(identity(cycle[i]) + " -> " + identity(cycle[next]));
    }
    return jumps;
}
inline QPushButton *defaultButton(QWidget &dialog) {
    for (auto *button : dialog.findChildren<QPushButton *>())
        if (button->isDefault() && button->isVisibleTo(&dialog))
            return button;
    return nullptr;
}
inline QJsonObject settingsSnapshot() {
    QSettings settings("SketchyUp", "SketchyUp");
    settings.sync();
    QJsonObject result;
    for (const auto &k : settings.allKeys())
        result[k] = settings.value(k).toString() + '|' +
                    QString::fromLatin1(settings.value(k).toByteArray().toHex());
    return result;
}
// A text field that passes Enter on to its dialog's default button. Enter on a closed combo
// box opens its list, multi-line editors insert a line break, and check boxes keep it; a
// dialog without a single-line field is tested with Enter on its focused default button.
inline bool passesReturn(QWidget *widget) {
    if (auto *edit = qobject_cast<QLineEdit *>(widget))
        return !edit->isReadOnly() && !qobject_cast<QComboBox *>(edit->parentWidget());
    return qobject_cast<QAbstractSpinBox *>(widget) != nullptr;
}

// ----- Cases -------------------------------------------------------------------------
enum class Enter {
    Accept,      // Enter on the default button applies and closes the dialog (Accepted).
    Closes,      // The default button only closes (Close/OK on reports, Cancel on a confirmation).
    Child,       // The default button opens a nested chooser or confirmation, which is cancelled.
    Next,        // The default button accepts and the flow continues to the next dialog.
    Disabled,    // The default (apply) button is disabled until a choice exists; Enter is inert.
    DefaultOnly, // Default asserted only; pressing Enter would act on external state.
};
enum class Return { Viewport, Opener, OpenerPanel };
struct Context;
struct Case {
    QString id, group, title, opener;
    std::function<void(Context &)> prepare;
    std::function<void(Context &)> trigger;
    Match target;
    Enter enter;
    QString defaultButton; // Expected default button text (accessible name); empty = none.
    Return returnTo = Return::Viewport;
    Match next;                               // Dialog Enter opens (Child/Next).
    std::function<void(QWidget &)> configure; // Optional state inside the dialog.
    std::vector<std::pair<Match, std::function<void(QWidget &)>>> hops; // Flow before target.
    QString note;
};
struct Context {
    Window &window;
    QString files; // Isolated scratch directory.
    QPointer<QWidget> opener;
    QString openerId, afterChildId;
    bool afterChildIsOpener{};
    QJsonObject phase;
    std::vector<QWidget *> parentOpeners;
};

// Opens the command palette from the current focus with Ctrl+K and runs `label`.
inline void palette(Context &context, const QString &label) {
    std::exception_ptr failure;
    bool handled{};
    QElapsedTimer shown;
    shown.start();
    Watch watch(named("commandPalette"), [&](QWidget &dialog) {
        handled = true;
        context.phase["paletteShownMs"] = shown.elapsed();
        auto *query = dialog.findChild<QLineEdit *>("paletteQuery");
        auto *list = dialog.findChild<QListWidget *>("paletteResults");
        require(query && list && QTest::qWaitFor([&] { return query->hasFocus(); }),
                "Command palette focuses its query");
        // Type until the label is the first match (QTest types ASCII only), then use arrows.
        const auto chosen = [&] {
            auto *item = list->currentItem();
            return item && item->text().startsWith(label + "    ");
        };
        for (const auto ch : label) {
            if (chosen())
                break;
            if (ch.unicode() < 0x80)
                QTest::keyClick(query, ch.toLatin1());
        }
        for (int i = 0; i < 60; ++i) {
            auto *item = list->currentItem();
            if (item && item->text().startsWith(label + "    "))
                break;
            QTest::keyClick(query, Qt::Key_Down);
        }
        require(list->currentItem() && list->currentItem()->text().startsWith(label + "    "),
                "Command palette lists " + label);
        context.phase["paletteTypedMs"] = shown.elapsed();
        QTest::keyClick(query, Qt::Key_Return);
    });
    context.opener = focused();
    context.openerId = identity(focused());
    QElapsedTimer clock;
    clock.start();
    QTest::keyClick(focused(), Qt::Key_K, Qt::ControlModifier);
    context.phase["paletteReturnedMs"] = clock.elapsed();
    require(QTest::qWaitFor([&] { return watch.fired(); }), "Ctrl+K opens the command palette");
    watch.rethrow();
    require(handled, "Palette chose " + label);
}
// Palette to a Model-panel page, then Tab to a button inside it and press Space.
inline void panelButton(Context &context, const QString &view, const QString &button) {
    QElapsedTimer clock;
    clock.start();
    palette(context, view);
    context.phase["paletteMs"] = clock.elapsed();
    auto *panel = context.window.findChild<QWidget *>("organizationPanel");
    require(QTest::qWaitFor([&] { return inside(*panel, focused()); }),
            view + " moves focus into the Model panel");
    context.phase["panelFocusMs"] = clock.elapsed();
    auto *target = tabTo(*panel, button);
    context.phase["tabToMs"] = clock.elapsed();
    context.opener = target;
    context.openerId = identity(target);
    press(target);
}
// A hop that, inside an open parent dialog, Tabs to `button` and presses Space. The child it
// opens is the case target. Afterwards it records where focus returned, then Escapes the parent.
inline std::pair<Match, std::function<void(QWidget &)>>
childOf(Context &context, Match parent, const QString &button,
        std::function<void(QWidget &)> configure = {}) {
    return {parent, [&context, button, configure](QWidget &dialog) {
                waitDialogFocus(dialog);
                if (configure)
                    configure(dialog);
                auto *target = tabTo(dialog, button);
                context.opener = target;
                context.openerId = identity(target);
                press(target);
                (void)QTest::qWaitFor([&] { return returnFocus(&dialog) == target; }, 2000);
                context.afterChildId = identity(returnFocus(&dialog));
                context.afterChildIsOpener = returnFocus(&dialog) == target;
                if (!dialog.isActiveWindow()) {
                    dialog.activateWindow();
                    (void)QTest::qWaitForWindowFocused(&dialog);
                }
                if (dialog.isVisible()) {
                    // Return to the dialog's first control before Escape (keyboard only).
                    QTest::keyClick(focused(), Qt::Key_Escape);
                    (void)QTest::qWaitFor([&] { return !dialog.isVisible(); }, 3000);
                    require(!dialog.isVisible(), "Escape closes the parent dialog");
                }
            }};
}
// A hop that accepts an intermediate dialog with Enter (after optional configuration).
inline std::pair<Match, std::function<void(QWidget &)>>
enterThrough(Match dialogMatch, std::function<void(QWidget &)> configure = {}) {
    return {dialogMatch, [configure](QWidget &dialog) {
                waitDialogFocus(dialog);
                if (configure)
                    configure(dialog);
                QTest::keyClick(focused(), Qt::Key_Return);
                (void)QTest::qWaitFor([&] { return !dialog.isVisible(); }, 5000);
                require(!dialog.isVisible(), "Enter accepts " + dialog.windowTitle());
            }};
}
// A hop through a Qt file dialog: the file name is set directly (preparation), then Enter.
inline std::pair<Match, std::function<void(QWidget &)>> chooseFile(const QString &title,
                                                                   std::function<QString()> path) {
    return enterThrough(kind<QFileDialog>(title), [path](QWidget &dialog) {
        auto *name = dialog.findChild<QLineEdit *>("fileNameEdit");
        require(name, "File dialog has a file name field");
        name->setText(path());
        name->setFocus();
        (void)QTest::qWaitFor([&] { return name->hasFocus(); });
    });
}

inline std::pair<Match, std::function<void(QWidget &)>> chooseFile(const QString &title,
                                                                   const QString &path) {
    return chooseFile(title, [path] { return path; });
}

// ----- Running one case --------------------------------------------------------------
struct Result {
    QJsonObject json;
    QStringList findings;
};
// Activates `window`. Synthetic keys carry no Wayland input serial, so after a nonmodal
// window hides itself Qt may have no serial to request activation with, and Weston leaves
// no surface focused. A newly mapped window receives keyboard focus from the compositor, so
// the fallback maps the model window again (as a user would re-select it). Returns whether
// that fallback was needed.
inline bool activate(QWidget &window, bool settle = false) {
    // After windows closed, activation must also survive their late focus events.
    const auto active = [&] {
        if (!QTest::qWaitForWindowActive(&window, 2000))
            return false;
        if (settle)
            QTest::qWait(400);
        return window.isActiveWindow();
    };
    window.activateWindow();
    if (active())
        return false;
    for (int attempt = 0; attempt < 3; ++attempt) {
        settle = true;
        window.hide();
        window.show();
        const bool exposed = QTest::qWaitForWindowExposed(&window, 5000);
        window.activateWindow();
        const bool reactivated = active();
        std::cerr << "[audit] activation fallback " << attempt << ": exposed " << exposed
                  << ", active " << reactivated << '\n';
        if (reactivated)
            break;
    }
    return true;
}
// Leaves no dialog open and keyboard focus in the viewport.
inline void focusViewport(Window &window) {
    bool hid{};
    for (auto *widget : QApplication::topLevelWidgets())
        if (auto *dialog = qobject_cast<QDialog *>(widget);
            dialog && dialog->isVisible() && dialog->objectName() != "fileOperationDialog") {
            dialog->hide();
            hid = true;
        }
    if (window.workspace() != Window::Workspace::Model)
        window.setWorkspace(Window::Workspace::Model); // A Sheets case leaves the canvas.
    if (auto *tabs = window.findChild<QTabWidget *>("modelTabs"))
        tabs->setCurrentWidget(window.viewport()); // A render result tab may be in front.
    activate(window, hid);
    window.viewport()->setFocus(Qt::OtherFocusReason);
    (void)QTest::qWaitFor([&] { return focused() == window.viewport(); });
}
inline QJsonObject auditOpen(QWidget &dialog, QStringList &findings) {
    QJsonObject json{{"objectName", dialog.objectName()},
                     {"windowTitle", dialog.windowTitle()},
                     {"class", QString::fromLatin1(dialog.metaObject()->className())},
                     {"modal", dialog.isModal() || QApplication::activeModalWidget() == &dialog}};
    waitDialogFocus(dialog);
    auto *initial = focused();
    json["initialFocus"] = identity(initial);
    // Accessible names.
    QJsonArray controls, unnamed;
    for (auto *widget : dialog.findChildren<QWidget *>()) {
        if (!tabFocusable(widget, dialog))
            continue;
        auto name = accessibleName(widget);
        // On Linux Qt reports a closed combo box's current value as its name and exposes its
        // form label through the "labelled by" relation instead.
        if (qobject_cast<QComboBox *>(widget)) {
            auto *accessible = QAccessible::queryAccessibleInterface(widget);
            const auto labels = accessible ? accessible->relations(QAccessible::Label)
                                           : decltype(accessible->relations()){};
            if (!widget->accessibleName().isEmpty())
                name = widget->accessibleName();
            else if (!labels.isEmpty() && labels.front().first)
                name = labels.front().first->text(QAccessible::Name);
            else
                name.clear();
        }
        controls.append(QJsonObject{{"identity", identity(widget)}, {"accessibleName", name}});
        // A name must describe the control: a bare colour value or number does not.
        static const QRegularExpression valueOnly("^#[0-9A-Fa-f]{3,8}$|^[^\\p{L}]*$");
        if (name.trimmed().isEmpty() || valueOnly.match(name.trimmed()).hasMatch())
            unnamed.append(identity(widget) + (name.isEmpty() ? QString{} : " = " + name));
    }
    json["controls"] = controls;
    json["unnamedControls"] = unnamed;
    if (!unnamed.isEmpty())
        findings << "Unnamed or non-descriptive controls: " +
                        QString::fromUtf8(QJsonDocument(unnamed).toJson(QJsonDocument::Compact));
    // Tab and Shift+Tab traversal.
    const auto expected = expectedControls(dialog);
    json["expectedControls"] = int(expected.size());
    const auto forward = traverse(dialog, expected.size(), false);
    const auto backward = traverse(dialog, expected.size(), true);
    json["tabOrder"] = identities(forward.cycle);
    json["shiftTabOrder"] = identities(backward.cycle);
    for (const auto &[traversal, label] :
         {std::pair{&forward, QString("Tab")}, std::pair{&backward, QString("Shift+Tab")}}) {
        if (!traversal->escape.isEmpty())
            findings << label + " moved focus out of the dialog to " + traversal->escape;
        QJsonArray missing;
        for (auto *control : expected)
            if (!traversal->visited.contains(control))
                missing.append(identity(control));
        json[label == "Tab" ? "unreachableByTab" : "unreachableByShiftTab"] = missing;
        if (!missing.isEmpty())
            findings << label + " does not reach: " +
                            QString::fromUtf8(
                                QJsonDocument(missing).toJson(QJsonDocument::Compact));
    }
    // Shift+Tab must visit the same cycle in reverse.
    bool mirrored = forward.cycle.size() == backward.cycle.size();
    if (mirrored && !forward.cycle.empty()) {
        const auto n = forward.cycle.size();
        const auto start =
            size_t(std::find(forward.cycle.begin(), forward.cycle.end(), backward.cycle.front()) -
                   forward.cycle.begin());
        for (size_t i = 0; mirrored && i < n; ++i)
            mirrored = start < n && backward.cycle[i] == forward.cycle[(start + n - i) % n];
    }
    json["shiftTabMirrorsTab"] = mirrored;
    if (!mirrored)
        findings << "Shift+Tab order is not the reverse of Tab order";
    const auto jumps = backwardJumps(forward);
    json["readingOrderBackwardJumps"] = jumps;
    if (jumps.size() > 1)
        findings << "Tab order moves up and left more than once per cycle: " +
                        QString::fromUtf8(QJsonDocument(jumps).toJson(QJsonDocument::Compact));
    auto *button = defaultButton(dialog);
    json["defaultButton"] = button ? accessibleName(button) : QString{};
    // Return to the initial control by Tab, so Escape is pressed where a user starts.
    for (size_t i = 0;
         i < expected.size() * 2 + 2 && focused() != initial && inside(dialog, focused()); ++i)
        QTest::keyClick(focused(), Qt::Key_Tab);
    return json;
}
inline void runCase(Context &context, const Case &c, Result &result, bool enterPhase) {
    auto &window = context.window;
    QElapsedTimer prepareClock;
    prepareClock.start();
    try {
        if (c.prepare)
            c.prepare(context);
        context.phase["prepareMs"] = prepareClock.elapsed();
        focusViewport(window);
        context.phase["focusViewportMs"] = prepareClock.elapsed();
        if (focused() != window.viewport()) {
            QStringList visible;
            for (auto *widget : QApplication::topLevelWidgets())
                if (widget->isVisible())
                    visible << identity(widget);
            throw Failure(
                ("Case starts with focus in the viewport (focus " + identity(focused()) +
                 ", active " + identity(QApplication::activeWindow()) + ", focus window " +
                 (QGuiApplication::focusWindow() ? QGuiApplication::focusWindow()->objectName()
                                                 : QString("none")) +
                 ", visible " + visible.join(", ") + ")")
                    .toStdString());
        }
    } catch (const std::exception &error) {
        result.findings << (enterPhase ? "[Enter] " : "[Escape] ") +
                               QString("Preparation failed: ") + error.what();
        focusViewport(window);
        return;
    }
    context.opener = nullptr;
    context.openerId.clear();
    context.afterChildId.clear();
    context.afterChildIsOpener = false;
    auto documentBefore = encodeContainer(window.document());
    auto settingsBefore = settingsSnapshot();
    QJsonObject phase;
    QStringList findings;
    bool closed{}, childSeen{};
    int finished = -1;
    std::vector<std::unique_ptr<Watch>> hops;
    for (const auto &[match, act] : c.hops)
        hops.push_back(std::make_unique<Watch>(match, act));
    std::unique_ptr<Watch> next;
    if (enterPhase && c.enter == Enter::Next)
        next = std::make_unique<Watch>(c.next, [&](QWidget &dialog) {
            childSeen = true;
            waitDialogFocus(dialog);
            QTest::keyClick(focused(), Qt::Key_Escape);
            (void)QTest::qWaitFor([&] { return !dialog.isVisible(); }, 3000);
        });
    QElapsedTimer clock;
    clock.start();
    qint64 openedAt = -1, auditedAt = -1;
    Watch target(c.target, [&](QWidget &widget) {
        openedAt = clock.elapsed();
        auto &dialog = widget;
        // Side effects are measured from the moment the dialog is open: a flow such as an
        // import legitimately changes the model before its report appears.
        documentBefore = encodeContainer(window.document());
        if (c.configure)
            c.configure(dialog);
        auto *asDialog = qobject_cast<QDialog *>(&dialog);
        if (asDialog)
            QObject::connect(asDialog, &QDialog::finished, asDialog,
                             [&](int code) { finished = code; });
        if (!enterPhase) {
            phase = auditOpen(dialog, findings);
            auditedAt = clock.elapsed();
            settingsBefore = settingsSnapshot();
            QTest::keyClick(focused(), Qt::Key_Escape);
            closed = QTest::qWaitFor([&] { return !dialog.isVisible(); }, 3000);
            return;
        }
        waitDialogFocus(dialog);
        auto *button = defaultButton(dialog);
        phase["defaultButton"] = button ? accessibleName(button) : QString{};
        if ((button ? accessibleName(button) : QString{}) != c.defaultButton)
            findings << "Default button is '" + (button ? accessibleName(button) : QString{}) +
                            "', expected '" + c.defaultButton + "'";
        // Enter from the first control that hands Enter to its dialog (or the default button).
        const auto limit = expectedControls(dialog).size() * 2 + 2;
        for (size_t i = 0; i < limit && !passesReturn(focused()) && focused() != button &&
                           inside(dialog, focused());
             ++i)
            QTest::keyClick(focused(), Qt::Key_Tab);
        phase["enterFrom"] = identity(focused());
        std::unique_ptr<Watch> child;
        if (c.enter == Enter::Child)
            child = std::make_unique<Watch>(c.next, [&](QWidget &nested) {
                childSeen = true;
                waitDialogFocus(nested);
                QTest::keyClick(focused(), Qt::Key_Escape);
                (void)QTest::qWaitFor([&] { return !nested.isVisible(); }, 3000);
            });
        settingsBefore = settingsSnapshot();
        QTest::keyClick(focused(), Qt::Key_Return);
        if (c.enter == Enter::Child) {
            (void)QTest::qWaitFor([&] { return childSeen; }, 3000);
            child->rethrow();
            if (!childSeen)
                findings << "Enter did not open the expected nested dialog";
            QTest::qWait(20);
            if (dialog.isVisible()) {
                dialog.activateWindow();
                (void)QTest::qWaitForWindowFocused(&dialog);
                QTest::keyClick(focused(), Qt::Key_Escape);
                closed = QTest::qWaitFor([&] { return !dialog.isVisible(); }, 3000);
            }
            return;
        }
        if (c.enter == Enter::Disabled) {
            QTest::qWait(50);
            if (!dialog.isVisible())
                findings << "Enter closed the dialog although its default action is unavailable";
            QTest::keyClick(focused(), Qt::Key_Escape);
            closed = QTest::qWaitFor([&] { return !dialog.isVisible(); }, 3000);
            return;
        }
        closed = QTest::qWaitFor([&] { return !dialog.isVisible(); }, 5000);
        if (!closed) {
            QStringList labels;
            for (auto *label : dialog.findChildren<QLabel *>())
                if (!label->text().isEmpty())
                    labels << label->objectName() + "=" + label->text();
            phase["openAfterEnter"] =
                QJsonObject{{"popup", identity(QApplication::activePopupWidget())},
                            {"focus", identity(focused())},
                            {"labels", labels.join(" | ")}};
            QTest::keyClick(focused(), Qt::Key_Escape);
            (void)QTest::qWaitFor([&] { return !dialog.isVisible(); }, 3000);
        }
    });
    Rescue rescue(25000);
    try {
        c.trigger(context);
        const bool opened = QTest::qWaitFor([&] { return target.fired(); }, 15000);
        for (auto &hop : hops) // A failed step explains a dialog that never opened.
            hop->rethrow();
        require(opened, "Keyboard path opens the dialog");
        phase["triggerReturnedMs"] = clock.elapsed();
        (void)QTest::qWaitFor([&] { return closed; }, 5000);
        for (auto &hop : hops)
            hop->rethrow();
        target.rethrow();
        if (next) {
            (void)QTest::qWaitFor([&] { return childSeen; }, 5000);
            next->rethrow();
        }
    } catch (const std::exception &error) {
        findings << QString("Case aborted: ") + error.what();
    }
    if (rescue.triggered())
        findings << "Case exceeded its time budget; dialogs were closed by the rescue timer";
    phase["openedMs"] = openedAt;
    phase["auditedMs"] = auditedAt;
    phase["closedMs"] = clock.elapsed();
    phase["closed"] = closed;
    phase["finished"] = finished;
    if (!closed)
        findings << (enterPhase ? "Enter did not close the dialog" : "Escape did not close it");
    // Focus return.
    QWidget *expected = nullptr;
    QString returnedTo;
    if (!context.afterChildId.isEmpty() && c.returnTo == Return::Opener) {
        // Recorded by the parent hop right after the child closed.
        returnedTo = context.afterChildId;
        if (!context.afterChildIsOpener && !enterPhase)
            findings << "Focus returned to " + returnedTo + ", not the opener " + context.openerId;
    } else if (!enterPhase) {
        expected = c.returnTo == Return::Viewport ? window.viewport() : context.opener;
        auto acceptable = [&] {
            auto *f = returnFocus(&window);
            if (!f)
                return false;
            if (f == expected || f == window.viewport())
                return true;
            if (c.returnTo == Return::OpenerPanel && context.opener) {
                for (auto *page = context.opener->parentWidget(); page; page = page->parentWidget())
                    if (page->parentWidget() && page->parentWidget()->inherits("QStackedWidget"))
                        return page->isAncestorOf(f);
            }
            return false;
        };
        phase["focusCheckStartMs"] = clock.elapsed();
        (void)QTest::qWaitFor(acceptable, 3000);
        phase["focusCheckEndMs"] = clock.elapsed();
        returnedTo = identity(returnFocus(&window));
        phase["windowReactivated"] = window.isActiveWindow();
        if (!enterPhase && !acceptable())
            findings << "After Escape focus is on " + returnedTo + ", expected " +
                            (c.returnTo == Return::Viewport ? identity(window.viewport())
                                                            : context.openerId);
    }
    phase["focusAfterClose"] = returnedTo;
    for (auto it = context.phase.begin(); it != context.phase.end(); ++it)
        phase[it.key()] = it.value();
    context.phase = {};
    phase["opener"] = context.openerId;
    if (!enterPhase) {
        const bool documentSame = encodeContainer(window.document()) == documentBefore;
        const bool settingsSame = settingsSnapshot() == settingsBefore;
        phase["escapeDocumentUnchanged"] = documentSame;
        phase["escapePreferencesUnchanged"] = settingsSame;
        if (!documentSame)
            findings << "Escape changed the document";
        if (!settingsSame)
            findings << "Escape changed preferences";
    } else {
        phase["enterMode"] = int(c.enter);
        phase["nestedDialogSeen"] = childSeen;
        if (c.enter == Enter::Accept && finished != QDialog::Accepted)
            findings
                << QString("Enter finished the dialog with result %1, not Accepted").arg(finished);
        if (c.enter == Enter::Next && !childSeen)
            findings << "Enter did not continue to the next dialog";
        if ((c.enter == Enter::Child || c.enter == Enter::Closes || c.enter == Enter::Disabled ||
             c.enter == Enter::DefaultOnly) &&
            encodeContainer(window.document()) != documentBefore)
            findings << "Enter changed the document although its default does not apply";
    }
    result.json[enterPhase ? "enter" : "escape"] = phase;
    for (const auto &finding : findings)
        result.findings << (enterPhase ? "[Enter] " : "[Escape] ") + finding;
    focusViewport(window);
}
inline Result run(Context &context, const Case &c) {
    Result result;
    result.json = QJsonObject{{"id", c.id},
                              {"group", c.group},
                              {"title", c.title},
                              {"keyboardPath", c.opener},
                              {"note", c.note}};
    runCase(context, c, result, false);
    if (c.enter == Enter::DefaultOnly) {
        // Enter is not pressed; the default button recorded while open is the whole check.
        const auto actual = result.json["escape"].toObject()["defaultButton"].toString();
        result.json["enter"] = QJsonObject{{"enterMode", int(c.enter)}, {"defaultButton", actual}};
        if (actual != c.defaultButton)
            result.findings << "[Enter] Default button is '" + actual + "', expected '" +
                                   c.defaultButton + "'";
    } else
        runCase(context, c, result, true);
    result.json["findings"] = QJsonArray::fromStringList(result.findings);
    result.json["passed"] = result.findings.isEmpty();
    return result;
}
} // namespace dialog_audit
