#pragma once
#include <QMutex>
#include <QMutexLocker>
#include <QString>
#include <QStringList>
#include <QtGlobal>
// R092.b: fails a suite on Qt's geometry warnings ("QWidget::setMinimumSize: … Negative sizes
// (0,-1) are not possible"), which CI now shows because Qt logs to stderr (D-033). Messages
// still reach the previous handler.
namespace qt_warning_guard {
inline QMutex &lock() {
    static QMutex mutex;
    return mutex;
}
inline QStringList &caught() {
    static QStringList messages;
    return messages;
}
inline QtMessageHandler &previous() {
    static QtMessageHandler handler{};
    return handler;
}
inline void handler(QtMsgType type, const QMessageLogContext &context, const QString &message) {
    if (message.contains(QLatin1String("Negative sizes"))) {
        QMutexLocker locked(&lock());
        caught() << message;
    }
    if (previous())
        previous()(type, context, message);
}
inline void install() { previous() = qInstallMessageHandler(handler); }
// The first caught warning, or an empty string.
inline QString first() {
    QMutexLocker locked(&lock());
    return caught().isEmpty() ? QString{} : caught().front();
}
} // namespace qt_warning_guard
