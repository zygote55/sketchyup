#pragma once
// The numeric keypad's decimal key is labelled by the keyboard layout, not by the
// application locale: it types ',' on a German layout and '.' on a French one (Qt has
// no separate key code; it reports Key_Comma or Key_Period with KeypadModifier). In a
// numeric field it always means "the locale's decimal separator", as in a spreadsheet.
// Only the keypad key is affected; '.' and ',' on the main keyboard are left alone.
#include <QAbstractSpinBox>
#include <QCoreApplication>
#include <QEvent>
#include <QKeyEvent>
#include <QLineEdit>
#include <QLocale>
#include <QObject>
#include <QString>
namespace sketchy {
inline bool isKeypadDecimal(const QKeyEvent &event) {
    return (event.modifiers() & Qt::KeypadModifier) &&
           (event.key() == Qt::Key_Period || event.key() == Qt::Key_Comma);
}
// The text a key press types into a numeric entry field.
inline QString numericKeyText(const QKeyEvent &event) {
    return isKeypadDecimal(event) ? QLocale().decimalPoint() : event.text();
}
// Re-delivers a keypad decimal press as the locale's separator key. The replacement
// drops the keypad modifier, so the filter does not see it again.
class KeypadDecimalFilter final : public QObject {
  public:
    explicit KeypadDecimalFilter(QObject *parent) : QObject(parent) {}
    bool eventFilter(QObject *object, QEvent *event) override {
        if (event->type() != QEvent::KeyPress && event->type() != QEvent::KeyRelease)
            return false;
        const auto &key = static_cast<QKeyEvent &>(*event);
        if (!isKeypadDecimal(key))
            return false;
        const auto point = QLocale().decimalPoint();
        QKeyEvent replacement(key.type(), point == "," ? Qt::Key_Comma : Qt::Key_Period,
                              key.modifiers() & ~Qt::KeypadModifier, key.nativeScanCode(),
                              key.nativeVirtualKey(), key.nativeModifiers(), point,
                              key.isAutoRepeat(), key.count());
        QCoreApplication::sendEvent(object, &replacement);
        return true;
    }
};
// Marks a line edit, or a spin box's embedded line edit, as numeric entry.
inline void acceptKeypadDecimal(QWidget *field) {
    auto *edit = qobject_cast<QLineEdit *>(field);
    if (!edit && qobject_cast<QAbstractSpinBox *>(field))
        edit = field->findChild<QLineEdit *>();
    if (edit)
        edit->installEventFilter(new KeypadDecimalFilter(edit));
}
} // namespace sketchy
