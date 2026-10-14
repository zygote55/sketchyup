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
// ADR 0164 §4: the workspace an action's shortcut is live in. Global actions work in every
// workspace; Model and Sheets actions only in their own, so the same key may bind once in
// each. Scope comes from the code, never from saved preferences.
enum class ShortcutScope { Global, Model, Sheets };
using ShortcutScopes = QMap<QString, ShortcutScope>;
// Two scopes can conflict when both can be live at once: Global with every scope, and a
// workspace scope with itself.
constexpr bool shortcutScopesOverlap(ShortcutScope a, ShortcutScope b) {
    return a == ShortcutScope::Global || b == ShortcutScope::Global || a == b;
}
class ShortcutBindings {
  public:
    using Guard = std::function<QString(const QString &, const QKeySequence &)>;
    // Actions absent from `scopes` are Global, which keeps the single-scope behaviour.
    explicit ShortcutBindings(ShortcutMap defaults, const QByteArray &saved = {}, Guard guard = {},
                              ShortcutScopes scopes = {});
    ShortcutScope scope(const QString &id) const {
        return scopes_.value(id, ShortcutScope::Global);
    }
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
    ShortcutScopes scopes_;
    Guard guard_;
    QJsonObject envelope_, overrides_;
    QStringList notices_;
    void resolve();
};
} // namespace sketchy
