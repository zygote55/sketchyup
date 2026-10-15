#pragma once
#include <QDialog>
#include <QElapsedTimer>
#include <QGuiApplication>
#include <QInputDialog>
#include <QTimer>
#include <QWindow>
#include <memory>
namespace sketchy {
// A prompt that closes only once the platform has finished activating it (R091.h).
//
// On Wayland, Qt applies a keyboard focus change after a wl_display.sync round trip and then
// compares the result with QGuiApplication::focusWindow(). A prompt closed while the sync for
// its own activation is still in flight queues "no focus window", but the main window's
// keyboard enter, handled before that queued change is delivered, compares equal to the stale
// focus window and is dropped: the application is left with no focus window although the
// compositor focused the main window. Waiting (at most 300 ms) until the prompt is the focus
// window before closing it keeps the two changes in order. People never close a prompt that
// fast; automation and scripted input do. Settled<QDialog> is the same rule for a dialog
// that closes from its own controls (the R092.b export dialog).
template <class Base> class Settled : public Base {
  public:
    using Base::Base;
    void done(int result) override {
        if (settling_)
            return;
        if (!QGuiApplication::platformName().startsWith(QLatin1String("wayland")) ||
            !this->isVisible() || !this->windowHandle() ||
            QGuiApplication::focusWindow() == this->windowHandle()) {
            Base::done(result);
            return;
        }
        settling_ = true;
        auto *timer = new QTimer(this);
        auto clock = std::make_shared<QElapsedTimer>();
        clock->start();
        QObject::connect(timer, &QTimer::timeout, this, [this, timer, clock, result] {
            if (QGuiApplication::focusWindow() != this->windowHandle() && clock->elapsed() < 300)
                return;
            timer->deleteLater();
            settling_ = false;
            Base::done(result);
        });
        timer->start(10);
    }

  private:
    bool settling_{};
};
using SettledInputDialog = Settled<QInputDialog>;
} // namespace sketchy
