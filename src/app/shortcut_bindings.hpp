#pragma once
#include <QByteArray>
#include <QJsonObject>
#include <QKeySequence>
#include <QMap>
#include <QStringList>
#include <functional>
namespace sketchy {
// Stable action IDs, never translated labels or action ordering.
using ShortcutMap = QMap<QString, QKeySequence>;
class ShortcutBindings {
  public:
    using Guard = std::function<QString(const QString &, const QKeySequence &)>;
    explicit ShortcutBindings(ShortcutMap defaults, const QByteArray &saved = {}, Guard guard = {});
    const ShortcutMap &effective() const { return effective_; }
    const QStringList &notices() const { return notices_; }
    QStringList conflicts(const QString &id, const QKeySequence &sequence) const;
    // Reassignment is explicit; all displaced actions become persistently unbound.
    void assign(const QString &id, const QKeySequence &sequence, bool replace = false);
    void resetKnown();
    QByteArray encode() const;
    static void validate(const QKeySequence &sequence);

  private:
    ShortcutMap defaults_, effective_;
    Guard guard_;
    QJsonObject envelope_, overrides_;
    QStringList notices_;
    void resolve();
};
} // namespace sketchy
