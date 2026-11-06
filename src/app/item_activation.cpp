#include "app/item_activation.hpp"
#include <QAbstractItemView>
#include <QEvent>
#include <QKeyEvent>
namespace sketchy {
namespace {
class ReturnActivation final : public QObject {
  public:
    explicit ReturnActivation(QAbstractItemView *view) : QObject(view), view_(view) {}

  protected:
    bool eventFilter(QObject *watched, QEvent *event) override {
        if (watched != view_ || event->type() != QEvent::KeyPress)
            return false;
        const auto *key = static_cast<QKeyEvent *>(event);
        const auto modifiers = key->modifiers() & ~Qt::KeypadModifier;
        // The view receives the key only when no editor has focus, so no row is being edited.
        if ((key->key() == Qt::Key_Return || key->key() == Qt::Key_Enter) &&
            modifiers == Qt::NoModifier && view_->currentIndex().isValid())
            QMetaObject::invokeMethod(view_, "activated", Qt::DirectConnection,
                                      Q_ARG(QModelIndex, view_->currentIndex()));
        return false;
    }

  private:
    QAbstractItemView *view_;
};
} // namespace
void activateOnReturn([[maybe_unused]] QAbstractItemView *view) {
#ifdef Q_OS_MACOS
    view->installEventFilter(new ReturnActivation(view));
#endif
}
} // namespace sketchy
