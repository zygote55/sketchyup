#include "app/surface_format.hpp"
#include "app/window.hpp"
#include <QAction>
#include <QApplication>
#include <QDialog>
#include <QLineEdit>
#include <QListWidget>
#include <QMenu>
#include <QMenuBar>
#include <QSettings>
#include <QTabBar>
#include <QTabWidget>
#include <QTemporaryDir>
#include <QTest>
#include <QTimer>
#include <QToolButton>
#include <iostream>
using namespace sketchy;
namespace {
struct PanelTab {
    const char *title, *action, *label;
};
// Every tab of the Model panel in display order, with the public View action that reveals it.
const PanelTab kTabs[] = {
    {"Outliner", "view.outliner", "Outliner"},
    {"Tags", "view.tags", "Tags and folders"},
    {"Info", "view.info", "Entity info"},
    {"Materials", "view.materials", "Materials"},
    {"Quantities", "view.quantities", "Quantities"},
    {"History", "view.history", "History"},
    {"Styles", "view.styles", "Model styles"},
    {"Scenes", "view.scenes", "Saved scenes"},
    {"Sections", "view.sections", "Section planes"},
    {"Annotations", "view.annotations", "Dimensions and labels"},
    {"3D Text", "view.text", "3D text"},
    {"Sun", "view.solar", "Sun and shadows"},
    {"Images", "view.reference_images", "Reference images"},
    {"Components", "view.components", "Components"},
};
constexpr int kTabCount = int(sizeof(kTabs) / sizeof(kTabs[0]));
void check(bool value, const std::string &message) {
    if (!value)
        throw std::runtime_error(message);
}
QWidget *focused() { return QApplication::focusWidget(); }
std::string describeKey(Qt::Key code, Qt::KeyboardModifiers modifiers) {
    return " (after key " +
           QKeySequence(QKeyCombination(modifiers, code)).toString().toStdString() + ")";
}
void key(Qt::Key code, Qt::KeyboardModifiers modifiers = Qt::NoModifier) {
    check(QTest::qWaitFor([] { return focused() != nullptr; }),
          "Keyboard workflow always has a focus owner");
    QTest::keyClick(focused(), code, modifiers);
    QCoreApplication::processEvents();
    // A modal command may return before the compositor re-activates the window.
    check(QTest::qWaitFor([] { return focused() != nullptr; }),
          "A key press never leaves the window without a focus owner" +
              describeKey(code, modifiers));
}
struct Fixture {
    Window &window;
    QTabWidget *tabs;
    QTabBar *bar;
    QWidget *viewport;
    bool inContent() const {
        auto *f = focused();
        return f && f != bar && tabs->currentWidget()->isAncestorOf(f);
    }
    std::string state() const {
        auto *f = focused();
        return std::string(" (tab ") + tabs->tabText(tabs->currentIndex()).toStdString() +
               ", focus " + (f ? f->metaObject()->className() : "none") + "/" +
               (f ? f->objectName().toStdString() : "") + ")";
    }
    void expectTab(int index, const char *via) const {
        check(tabs->currentIndex() == index && tabs->tabText(index) == kTabs[index].title,
              std::string(via) + " shows the " + kTabs[index].title + " tab" + state());
        check(tabs->currentWidget()->isVisible(),
              std::string(via) + " makes the " + kTabs[index].title + " page visible");
    }
    void expectContentFocus(int index, const char *via) const {
        check(QTest::qWaitFor([&] { return inContent(); }, 2000),
              std::string(via) + " leaves focus in the " + kTabs[index].title + " content" +
                  state());
    }
    // F6 cycles regions; the viewport must always be reachable again.
    void toViewport() const {
        for (int i = 0; !viewport->hasFocus() && i < 12; ++i)
            key(Qt::Key_F6);
        check(viewport->hasFocus(), "F6 returns keyboard focus to the viewport" + state());
    }
    void toPanel() const {
        for (int i = 0; !tabs->isAncestorOf(focused()) && i < 12; ++i)
            key(Qt::Key_F6);
        check(tabs->isAncestorOf(focused()), "F6 reaches the Model panel" + state());
    }
    // Shift+Tab walks back through the content (and any tab scroll buttons) to the tab bar.
    void toTabBar() const {
        for (int i = 0; !bar->hasFocus() && i < 8; ++i)
            key(Qt::Key_Tab, Qt::ShiftModifier);
        check(bar->hasFocus(), "Shift+Tab reaches the panel tab bar" + state());
    }
    // Tab leaves the bar; only its own scroll buttons may sit between it and the content.
    void barToContent(int index) const {
        for (int i = 0; i < 4 && !inContent(); ++i) {
            key(Qt::Key_Tab);
            auto *button = qobject_cast<QToolButton *>(focused());
            check(inContent() || (button && button->parentWidget() == bar),
                  std::string("Tab from the tab bar reaches ") + kTabs[index].title +
                      " content or a tab scroll button" + state());
        }
        expectContentFocus(index, "Tab from the tab bar");
    }
};
// Runs `action` (which opens the command palette) and chooses `label` using only key events.
void paletteChoose(Window &window, const QString &label, const std::function<void()> &action) {
    bool handled{};
    std::exception_ptr failure;
    QTimer timer;
    QObject::connect(&timer, &QTimer::timeout, &window, [&] {
        auto *dialog = window.findChild<QDialog *>("commandPalette");
        if (handled || !dialog || !dialog->isVisible())
            return;
        handled = true;
        try {
            auto *query = dialog->findChild<QLineEdit *>("paletteQuery");
            auto *list = dialog->findChild<QListWidget *>("paletteResults");
            check(query && list && QTest::qWaitFor([&] { return query->hasFocus(); }),
                  "Command palette focuses its query field");
            QTest::keyClicks(query, label);
            for (int i = 0; i < 40; ++i) {
                auto *item = list->currentItem();
                if (item && item->text().startsWith(label + "    "))
                    break;
                QTest::keyClick(query, Qt::Key_Down);
            }
            check(list->currentItem() && list->currentItem()->text().startsWith(label + "    "),
                  "Command palette lists " + label.toStdString());
            QTest::keyClick(query, Qt::Key_Return);
            check(!dialog->isVisible(), "Enter runs the chosen palette command");
        } catch (...) {
            failure = std::current_exception();
            dialog->reject();
        }
    });
    timer.start(10);
    action();
    timer.stop();
    if (failure)
        std::rethrow_exception(failure);
    check(handled && QTest::qWaitForWindowActive(&window),
          "Palette returns keyboard activation to the window");
}
// Opens the View menu with its mnemonic and chooses `id` using arrows and Enter only.
void menuChoose(Window &window, const char *id) {
    key(Qt::Key_V, Qt::AltModifier);
    check(QTest::qWaitFor([] { return QApplication::activePopupWidget() != nullptr; }),
          "Alt+V opens the View menu");
    auto *menu = qobject_cast<QMenu *>(QApplication::activePopupWidget());
    check(menu, "The View menu is the active popup");
    for (int i = 0; i < 80 && !(menu->activeAction() && menu->activeAction()->objectName() == id);
         ++i)
        QTest::keyClick(menu, Qt::Key_Down);
    check(menu->activeAction() && menu->activeAction()->objectName() == id,
          std::string("Arrow keys reach View > ") + id);
    QTest::keyClick(menu, Qt::Key_Return);
    QCoreApplication::processEvents();
    check(QTest::qWaitFor([] { return QApplication::activePopupWidget() == nullptr; }),
          "Enter closes the View menu");
    check(QTest::qWaitForWindowActive(&window), "View menu returns activation to the window");
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
    QSettings().setValue("recoverySeconds", 0);
    Window window;
    try {
        window.resize(1280, 900);
        window.show();
        check(QTest::qWaitForWindowExposed(&window), "Panel-tab window exposed");
        window.activateWindow();
        check(QTest::qWaitForWindowActive(&window), "Panel-tab window active");
        window.viewport()->setFocus();
        check(QTest::qWaitFor([&] { return window.viewport()->hasFocus(); }),
              "Initial viewport focus");
        auto *tabs = window.findChild<QTabWidget *>("organizationTabs");
        check(tabs && tabs->isVisible() && tabs->count() == kTabCount,
              "Model panel shows every expected tab");
        for (int i = 0; i < kTabCount; ++i) {
            check(tabs->tabText(i) == kTabs[i].title, "Tab order matches the keyboard fixture");
            auto *action = window.findChild<QAction *>(kTabs[i].action);
            check(action && action->text() == kTabs[i].label && action->shortcut().isEmpty(),
                  std::string("Public unbound View action exists for ") + kTabs[i].title);
        }
        Fixture f{window, tabs, tabs->tabBar(), window.viewport()};
        f.expectTab(0, "Initial state");

        // 1. F6, Shift+Tab to the tab bar, Right/Left between tabs, Tab into each page.
        f.toPanel();
        f.toTabBar();
        for (int i = 0; i < kTabCount; ++i) {
            f.expectTab(i, "Right arrow on the tab bar");
            f.barToContent(i);
            f.toTabBar();
            if (i + 1 < kTabCount)
                key(Qt::Key_Right);
        }
        key(Qt::Key_Right);
        f.expectTab(kTabCount - 1, "Right arrow at the last tab");
        for (int i = kTabCount - 2; i >= 0; --i) {
            key(Qt::Key_Left);
            f.expectTab(i, "Left arrow on the tab bar");
        }
        f.toViewport();

        // 2. Ctrl+Tab and Ctrl+Shift+Tab from inside the page content, wrapping at both ends.
        f.toPanel();
        for (int i = 1; i <= kTabCount; ++i) {
            key(Qt::Key_Tab, Qt::ControlModifier);
            f.expectTab(i % kTabCount, "Ctrl+Tab");
            f.expectContentFocus(i % kTabCount, "Ctrl+Tab");
        }
        for (int i = kTabCount - 1; i >= 0; --i) {
            key(Qt::Key_Backtab, Qt::ControlModifier | Qt::ShiftModifier);
            f.expectTab(i, "Ctrl+Shift+Tab");
            f.expectContentFocus(i, "Ctrl+Shift+Tab");
        }
        f.toViewport();

        // 3. Command palette: the four new commands plus two older ones whose focus handling
        // changed.
        for (const int i : {11, 6, 3, 2, 1, 0}) {
            paletteChoose(window, kTabs[i].label, [&] { key(Qt::Key_K, Qt::ControlModifier); });
            f.expectTab(i, "Command palette");
            f.expectContentFocus(i, "Command palette");
            f.toViewport();
        }

        // 4. View menu: every tab action is listed there; on X11 it is also driven by keys alone.
        // Qt's Wayland popups need a real compositor input serial that synthetic keys cannot
        // supply.
        QMenu *viewMenu{};
        for (auto *entry : window.menuBar()->actions())
            if (entry->text() == "&View")
                viewMenu = entry->menu();
        check(viewMenu, "View menu exists");
        for (const auto &tab : kTabs)
            check(viewMenu->actions().contains(window.findChild<QAction *>(tab.action)),
                  std::string("View menu lists ") + tab.label);
        const bool menuByKeys = QGuiApplication::platformName() == "xcb";
        if (menuByKeys)
            for (const int i : {2, 4}) {
                menuChoose(window, kTabs[i].action);
                f.expectTab(i, "View menu");
                f.expectContentFocus(i, "View menu");
                f.toViewport();
            }
        window.document().markSaved();
        std::cout << "Keyboard Model panel tabs: F6, Shift+Tab, arrows, Tab, Ctrl+Tab and command "
                     "palette"
                  << (menuByKeys ? " and View menu" : "") << " reached all " << kTabCount
                  << " tabs and returned to the viewport\n";
    } catch (const std::exception &error) {
        window.document().markSaved();
        std::cerr << error.what() << '\n';
        return 1;
    }
}
