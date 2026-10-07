#include "app/viewport.hpp"
#include "core/camera_motion.hpp"
#include <QApplication>
#include <QKeyEvent>
#include <QTimer>
#include <algorithm>
namespace sketchy {
namespace {
bool movementKey(int key) {
    return key == Qt::Key_W || key == Qt::Key_A || key == Qt::Key_S || key == Qt::Key_D ||
           key == Qt::Key_Q || key == Qt::Key_E || key == Qt::Key_Up || key == Qt::Key_Down ||
           key == Qt::Key_Left || key == Qt::Key_Right || key == Qt::Key_PageUp ||
           key == Qt::Key_PageDown;
}
} // namespace
void Viewport::initializeWalkNavigation() {
    walkTimer_ = new QTimer(this);
    walkTimer_->setInterval(16);
    connect(walkTimer_, &QTimer::timeout, this, [this] {
        if (tool_ != Tool::Walk || !hasFocus() || walkKeys_.empty()) {
            stopWalking();
            return;
        }
        const auto seconds = std::clamp(walkElapsed_.restart() / 1000., 0., .1);
        auto key = [&](int a, int b) { return walkKeys_.contains(a) || walkKeys_.contains(b); };
        double forward = int(key(Qt::Key_W, Qt::Key_Up)) - int(key(Qt::Key_S, Qt::Key_Down));
        double right = int(key(Qt::Key_D, Qt::Key_Right)) - int(key(Qt::Key_A, Qt::Key_Left));
        double vertical =
            int(key(Qt::Key_E, Qt::Key_PageUp)) - int(key(Qt::Key_Q, Qt::Key_PageDown));
        const auto norm = std::sqrt(forward * forward + right * right + vertical * vertical);
        if (norm == 0)
            return;
        const auto speed =
            walkSpeed_ * (QApplication::keyboardModifiers().testFlag(Qt::ShiftModifier) ? 3 : 1);
        const auto distance = seconds * speed / norm;
        try {
            applySceneCamera(walkCamera({target_, yaw_, pitch_, distance_, fov_, ortho_},
                                        forward * distance, right * distance, vertical * distance));
        } catch (const std::exception &error) {
            stopWalking();
            emit message(error.what());
        }
    });
}
void Viewport::stopWalking() {
    walkKeys_.clear();
    if (walkTimer_)
        walkTimer_->stop();
}
bool Viewport::walkNavigation(QEvent *event) {
    if (event->type() == QEvent::FocusOut || event->type() == QEvent::WindowDeactivate ||
        event->type() == QEvent::Hide || event->type() == QEvent::UngrabKeyboard)
        stopWalking();
    if (tool_ != Tool::Walk && tool_ != Tool::LookAround)
        return false;
    if (event->type() != QEvent::ShortcutOverride && event->type() != QEvent::KeyPress &&
        event->type() != QEvent::KeyRelease)
        return false;
    auto *key = static_cast<QKeyEvent *>(event);
    const auto code = key->key();
    if (event->type() == QEvent::KeyRelease && tool_ == Tool::Walk && movementKey(code)) {
        if (!key->isAutoRepeat())
            walkKeys_.erase(code);
        if (walkKeys_.empty())
            stopWalking();
        event->accept();
        return true;
    }
    if (key->modifiers() & (Qt::ControlModifier | Qt::AltModifier | Qt::MetaModifier)) {
        stopWalking();
        return false;
    }
    const auto arrow =
        code == Qt::Key_Up || code == Qt::Key_Down || code == Qt::Key_Left || code == Qt::Key_Right;
    if (code != Qt::Key_Escape && !(tool_ == Tool::Walk ? movementKey(code) : arrow))
        return false;
    if (event->type() == QEvent::ShortcutOverride) {
        event->accept();
        return true;
    }
    if (event->type() != QEvent::KeyPress)
        return false;
    if (code == Qt::Key_Escape) {
        setTool(Tool::Select);
        event->accept();
        return true;
    }
    if (tool_ == Tool::LookAround) {
        try {
            applySceneCamera(lookAround({target_, yaw_, pitch_, distance_, fov_, ortho_},
                                        code == Qt::Key_Left    ? 5
                                        : code == Qt::Key_Right ? -5
                                                                : 0,
                                        code == Qt::Key_Up     ? -5
                                        : code == Qt::Key_Down ? 5
                                                               : 0));
        } catch (const std::exception &error) {
            emit message(error.what());
        }
    } else if (!key->isAutoRepeat()) {
        walkKeys_.insert(code);
        if (!walkTimer_->isActive()) {
            walkElapsed_.start();
            walkTimer_->start();
        }
    }
    event->accept();
    return true;
}
void Viewport::setSceneTransitionDuration(int milliseconds) {
    if (milliseconds < 0 || milliseconds > 10000)
        throw std::runtime_error("Scene transition must be from zero to ten seconds");
    stopSceneTransition();
    sceneTransitionMs_ = milliseconds;
}
} // namespace sketchy
