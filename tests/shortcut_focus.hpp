#pragma once
#include <QApplication>
#include <QTest>
#include <QWidget>

// A window shortcut (F2, Delete, Alt+Down) fires only in a widget that has focus, in a window that
// holds keyboard focus. Right after a modal closes, the widget can already report focus, because Qt
// counts a window as active while its dialog is open, while the compositor has not yet given the
// window the keyboard back (Wayland). A key sent in that gap goes nowhere, and the shortcut does
// not fire. Waits for the window, then for the widget, and returns false if either never comes.
inline bool waitForShortcutFocus(QWidget *widget) {
    if (!QTest::qWaitForWindowFocused(widget->window()))
        return false;
    widget->setFocus(Qt::OtherFocusReason);
    return QTest::qWaitFor([widget] { return widget->hasFocus(); }, 1000);
}
