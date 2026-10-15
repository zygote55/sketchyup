#include "app/interface_preferences.hpp"
#include "app/sheet_workspace.hpp"
#include "app/shortcut_bindings.hpp"
#include "app/window.hpp"
#include <QAction>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QFileInfo>
#include <QHeaderView>
#include <QKeyEvent>
#include <QKeySequenceEdit>
#include <QLockFile>
#include <QPushButton>
#include <QSettings>
#include <QTreeWidget>
#include <QVBoxLayout>
#include <algorithm>
namespace sketchy {
namespace {
// QKeySequenceEdit lets Tab move focus on but records Shift+Tab as a key combination. Tab keys
// are reserved for focus (ShortcutBindings rejects them), so Shift+Tab moves focus back too.
class BacktabLeavesEditor final : public QObject {
  public:
    using QObject::QObject;

  protected:
    bool eventFilter(QObject *object, QEvent *event) override {
        if (event->type() != QEvent::KeyPress ||
            static_cast<QKeyEvent *>(event)->key() != Qt::Key_Backtab)
            return false;
        // The window moves focus to the previous control in the Tab chain.
        return QCoreApplication::sendEvent(static_cast<QWidget *>(object)->window(), event);
    }
};
constexpr auto settingsKey = "shortcuts/v1";
bool viewportShortcut(QAction *action, const QKeySequence &sequence) {
    const bool plain =
        !sequence.isEmpty() &&
        !(sequence[0].keyboardModifiers() & (Qt::ControlModifier | Qt::AltModifier)) &&
        !(sequence[0].key() >= Qt::Key_F1 && sequence[0].key() <= Qt::Key_F35);
    return plain || action->property("defaultShortcutContext").toInt() == int(Qt::WidgetShortcut);
}
ShortcutScope scopeOf(const QAction *action) {
    return ShortcutScope(action->property("shortcutScope").toInt());
}
// Model panels belong to the Model workspace (ADR 0164 §4): their local keys guard Global and
// Model bindings, never Sheets ones.
ShortcutBindings::Guard panelGuard(const std::vector<QAction *> &actions, QWidget *panels) {
    return [actions, panels](const QString &id, const QKeySequence &sequence) -> QString {
        const auto found = std::find_if(actions.begin(), actions.end(), [&](QAction *action) {
            return action->objectName() == id;
        });
        if (found == actions.end() || viewportShortcut(*found, sequence) ||
            scopeOf(*found) == ShortcutScope::Sheets)
            return {};
        for (auto *local : panels->findChildren<QAction *>()) {
            if (local->shortcutContext() != Qt::WidgetShortcut &&
                local->shortcutContext() != Qt::WidgetWithChildrenShortcut)
                continue;
            if (local->shortcuts().contains(sequence)) {
                auto name = local->text().isEmpty() ? local->objectName() : local->text();
                return "Shortcut belongs to a model panel (" + name.remove('&') +
                       "). Choose a different key combination.";
            }
        }
        return {};
    };
}
ShortcutMap defaults(const std::vector<QAction *> &actions) {
    ShortcutMap result;
    for (auto *action : actions)
        result[action->objectName()] = action->property("defaultShortcut").value<QKeySequence>();
    return result;
}
ShortcutScopes scopes(const std::vector<QAction *> &actions) {
    ShortcutScopes result;
    for (auto *action : actions)
        result[action->objectName()] = scopeOf(action);
    return result;
}
QString scopeName(ShortcutScope scope) {
    switch (scope) {
    case ShortcutScope::Model:
        return "Model";
    case ShortcutScope::Sheets:
        return "Sheets";
    case ShortcutScope::Global:
        break;
    }
    return "All";
}
} // namespace
void Window::applyShortcuts(const ShortcutBindings &bindings) {
    QWidget *canvas = sheets_ ? static_cast<QWidget *>(sheets_->canvas()) : nullptr;
    for (auto *action : publicActions_) {
        const auto sequence = bindings.effective().value(action->objectName());
        const bool viewportOnly = viewportShortcut(action, sequence);
        const auto scope = actionScope(action);
        removeAction(action);
        viewport_->removeAction(action);
        if (canvas)
            canvas->removeAction(action);
        action->setShortcut(sequence);
        action->setShortcutContext(viewportOnly ? Qt::WidgetShortcut : Qt::WindowShortcut);
        // Plain keys never reach text fields: Model ones work on the viewport, Sheets ones on
        // the page canvas, and Global ones on both. Workspace actions of the inactive
        // workspace are disabled (syncWorkspaceActions), so equal keys never compete.
        if (viewportOnly) {
            if (scope != ShortcutScope::Sheets)
                viewport_->addAction(action);
            if (canvas && scope != ShortcutScope::Model)
                canvas->addAction(action);
        } else
            addAction(action);
        const auto base = action->property("shortcutTooltip").toString();
        action->setToolTip(sequence.isEmpty()
                               ? base
                               : base + " (" + sequence.toString(QKeySequence::NativeText) + ")");
    }
    updateCommandSearchLabel(this);
    sheets_->setShortcutHints(findChild<QAction *>("sheet.view.fit")->shortcut(),
                              findChild<QAction *>("sheet.page.add")->shortcut());
}
void Window::initializeShortcuts() {
    for (auto *action : publicActions_) {
        action->setProperty("defaultShortcutContext", int(action->shortcutContext()));
        action->setProperty("shortcutTooltip", action->toolTip());
        // Viewport-only actions (Ctrl+A, Ctrl+G) are Model actions (ADR 0164 §4).
        if (!action->property("shortcutScopeExplicit").toBool() &&
            action->shortcutContext() == Qt::WidgetShortcut)
            action->setProperty("shortcutScope", int(ShortcutScope::Model));
    }
    try {
        QSettings settings("SketchyUp", "SketchyUp");
        ShortcutBindings bindings(
            defaults(publicActions_), settings.value(settingsKey).toByteArray(),
            panelGuard(publicActions_, organization_), scopes(publicActions_));
        applyShortcuts(bindings);
        if (!bindings.notices().isEmpty())
            QTimer::singleShot(0, this, [this] {
                status_->setText("Some saved shortcuts conflict. Open Edit → Keyboard shortcuts to "
                                 "resolve them.");
            });
    } catch (const std::exception &error) {
        const auto message = QString::fromUtf8(error.what());
        QTimer::singleShot(
            0, this, [this, message] { status_->setText("Default shortcuts active. " + message); });
    }
}
void Window::shortcutSettings() {
    QSettings settings("SketchyUp", "SketchyUp");
    settings.sync();
    if (settings.status() != QSettings::NoError)
        throw std::runtime_error("Cannot read shortcut preferences");
    const auto original = settings.value(settingsKey).toByteArray();
    ShortcutBindings draft(defaults(publicActions_), original,
                           panelGuard(publicActions_, organization_), scopes(publicActions_));
    QDialog dialog(this);
    dialog.setObjectName("shortcutDialog");
    dialog.setWindowTitle("Keyboard shortcuts");
    dialog.resize(600, 600);
    QVBoxLayout layout(&dialog);
    auto *hint = new QLabel(
        "Choose a command and a key combination, then Assign. Save applies your changes.");
    hint->setWordWrap(true);
    layout.addWidget(hint);
    auto *search = new QLineEdit;
    search->setObjectName("shortcutSearch");
    search->setAccessibleName("Find a command");
    search->setPlaceholderText("Find a command…");
    layout.addWidget(search);
    // Command, shortcut and the workspace it works in (ADR 0164 §4). The same key may be used
    // once in Model and once in Sheets; "All" commands conflict with both.
    auto *list = new QTreeWidget;
    list->setObjectName("shortcutActions");
    list->setAccessibleName("Command shortcuts");
    list->setRootIsDecorated(false);
    list->setUniformRowHeights(true);
    list->setColumnCount(3);
    list->setHeaderLabels({"Command", "Shortcut", "Workspace"});
    list->header()->setStretchLastSection(false);
    list->header()->setSectionResizeMode(0, QHeaderView::Stretch);
    list->header()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    list->header()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
    layout.addWidget(list);
    auto *editor = new QKeySequenceEdit;
    editor->setObjectName("shortcutSequence");
    editor->setAccessibleName("New key combination");
    if (auto *input = editor->findChild<QLineEdit *>())
        input->setAccessibleName("New key combination");
    auto *backtab = new BacktabLeavesEditor(editor);
    editor->installEventFilter(backtab);
    if (auto *input = editor->findChild<QLineEdit *>())
        input->installEventFilter(backtab);
    editor->setMaximumSequenceLength(1);
    layout.addWidget(editor);
    auto *error = new QLabel;
    error->setObjectName("shortcutError");
    error->setTextFormat(Qt::PlainText);
    error->setWordWrap(true);
    layout.addWidget(error);
    auto *notices = new QLabel;
    notices->setObjectName("shortcutNotices");
    notices->setTextFormat(Qt::PlainText);
    notices->setWordWrap(true);
    layout.addWidget(notices);
    auto *operations = new QDialogButtonBox;
    auto *assign = operations->addButton("Assign", QDialogButtonBox::ActionRole);
    assign->setObjectName("shortcutAssign");
    auto *replace = operations->addButton("Reassign", QDialogButtonBox::ActionRole);
    replace->setObjectName("shortcutReassign");
    replace->setEnabled(false);
    auto *clear = operations->addButton("Clear", QDialogButtonBox::ActionRole);
    clear->setObjectName("shortcutClear");
    auto *reset = operations->addButton("Reset defaults", QDialogButtonBox::ActionRole);
    reset->setObjectName("shortcutReset");
    layout.addWidget(operations);
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Cancel);
    layout.addWidget(buttons);
    auto refresh = [&] {
        for (int i = 0; i < list->topLevelItemCount(); ++i) {
            auto *item = list->topLevelItem(i);
            const auto id = item->data(0, Qt::UserRole).toString();
            const auto key = draft.effective().value(id).toString(QKeySequence::NativeText);
            item->setText(1, key.isEmpty() ? "Unassigned" : key);
        }
        notices->setText(draft.notices().join('\n'));
    };
    auto selectedId = [&] {
        return list->currentItem() && !list->currentItem()->isHidden()
                   ? list->currentItem()->data(0, Qt::UserRole).toString()
                   : QString{};
    };
    auto showConflict = [&] {
        replace->setEnabled(false);
        error->clear();
        if (selectedId().isEmpty())
            return;
        try {
            const auto conflicts = draft.conflicts(selectedId(), editor->keySequence());
            if (!conflicts.isEmpty()) {
                QStringList names;
                for (auto *action : publicActions_)
                    if (conflicts.contains(action->objectName()))
                        names << action->text().remove('&') +
                                     (sheets_
                                          ? " (" + scopeName(draft.scope(action->objectName())) +
                                                ")"
                                          : QString{});
                error->setText("Already used by " + names.join(", ") +
                               ". Reassign clears those bindings; Save applies the changes.");
                replace->setEnabled(true);
            }
        } catch (const std::exception &failure) {
            error->setText(QString::fromUtf8(failure.what()));
        }
    };
    connect(list, &QTreeWidget::currentItemChanged, &dialog, [&] {
        editor->setKeySequence(draft.effective().value(selectedId()));
        showConflict();
    });
    auto filterCommands = [&](const QString &text) {
        const auto previous = selectedId();
        list->clear();
        QTreeWidgetItem *selected{};
        for (auto *action : publicActions_) {
            const auto name = action->text().remove('&');
            const auto key =
                draft.effective().value(action->objectName()).toString(QKeySequence::NativeText);
            const auto workspace = scopeName(draft.scope(action->objectName()));
            // The workspace is a search term too ("sheets", "model").
            const auto label = name + "  —  " + (key.isEmpty() ? "Unassigned" : key) +
                               (sheets_ ? "  —  " + workspace : QString{});
            if (!label.contains(text, Qt::CaseInsensitive))
                continue;
            auto *item =
                new QTreeWidgetItem(list, {name, key.isEmpty() ? "Unassigned" : key, workspace});
            item->setData(0, Qt::UserRole, action->objectName());
            if (action->objectName() == previous || !selected)
                selected = item;
        }
        list->setCurrentItem(selected);
    };
    connect(search, &QLineEdit::textChanged, &dialog, filterCommands);
    connect(editor, &QKeySequenceEdit::keySequenceChanged, &dialog, showConflict);
    auto change = [&](bool reassign) {
        try {
            draft.assign(selectedId(), editor->keySequence(), reassign);
            refresh();
            showConflict();
        } catch (const std::exception &failure) {
            error->setText(QString::fromUtf8(failure.what()));
        }
    };
    connect(assign, &QPushButton::clicked, &dialog, [&] { change(false); });
    connect(replace, &QPushButton::clicked, &dialog, [&] { change(true); });
    connect(clear, &QPushButton::clicked, &dialog, [&] {
        editor->clear();
        change(false);
    });
    connect(reset, &QPushButton::clicked, &dialog, [&] {
        draft.resetKnown();
        refresh();
        editor->setKeySequence(draft.effective().value(selectedId()));
        showConflict();
    });
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, [&] {
        try {
            if (!selectedId().isEmpty() &&
                editor->keySequence() != draft.effective().value(selectedId()))
                throw std::runtime_error(
                    "Assign this key combination before saving, or cancel the change");
            // Coordinate our own editors and detect changes made since this dialog opened.
            const auto folder = QFileInfo(settings.fileName()).absolutePath();
            if (!QDir().mkpath(folder))
                throw std::runtime_error("Cannot create the preference directory");
            QLockFile lock(settings.fileName() + ".shortcuts-lock");
            if (!lock.tryLock(0))
                throw std::runtime_error("Another shortcut editor is saving; try again");
            settings.sync();
            if (settings.status() != QSettings::NoError)
                throw std::runtime_error("Cannot read shortcut preferences");
            if (settings.value(settingsKey).toByteArray() != original)
                throw std::runtime_error(
                    "Shortcuts changed elsewhere. Cancel and reopen this editor.");
            settings.setValue(settingsKey, draft.encode());
            settings.sync();
            if (settings.status() != QSettings::NoError)
                throw std::runtime_error(
                    "Could not save shortcuts; current window bindings are unchanged");
            applyShortcuts(draft);
            dialog.accept();
        } catch (const std::exception &failure) {
            error->setText(QString::fromUtf8(failure.what()));
        }
    });
    filterCommands({});
    refresh();
    dialog.exec();
    activateWindow();
    focusWorkspace();
}
} // namespace sketchy
