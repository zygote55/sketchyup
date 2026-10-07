#include "app/shortcut_bindings.hpp"
#include <QJsonDocument>
#include <QJsonParseError>
#include <stdexcept>
namespace sketchy {
namespace {
constexpr qsizetype maxBytes = 128 * 1024;
void require(bool condition, const char *message) {
    if (!condition)
        throw std::runtime_error(message);
}
bool validId(const QString &id) {
    if (id.isEmpty() || id.size() > 128)
        return false;
    for (const auto ch : id)
        if (!(ch.isLetterOrNumber() || ch == '.' || ch == '_' || ch == '-'))
            return false;
    return true;
}
} // namespace
void ShortcutBindings::validate(const QKeySequence &sequence) {
    require(sequence.count() <= 1, "Choose one key combination, not a multi-step sequence");
    if (sequence.isEmpty())
        return;
    const auto key = sequence[0].key();
    const auto modifiers = sequence[0].keyboardModifiers();
    require(!(modifiers & Qt::MetaModifier), "Super/Meta belongs to the desktop compositor");
    require(!(modifiers & ~(Qt::ShiftModifier | Qt::ControlModifier | Qt::AltModifier)),
            "Unsupported shortcut modifier");
    require(key != Qt::Key_unknown && key != 0, "Shortcut key is not recognized");
    require(key != Qt::Key_Escape && key != Qt::Key_Tab && key != Qt::Key_Backtab &&
                key != Qt::Key_Return && key != Qt::Key_Enter && key != Qt::Key_Left &&
                key != Qt::Key_Right && key != Qt::Key_Up && key != Qt::Key_Down &&
                key != Qt::Key_Shift && key != Qt::Key_Control && key != Qt::Key_Alt &&
                key != Qt::Key_Meta && key != Qt::Key_AltGr,
            "This key is reserved for focus, tool completion or inference");
}
ShortcutBindings::ShortcutBindings(ShortcutMap defaults, const QByteArray &saved, Guard guard)
    : defaults_(std::move(defaults)), guard_(std::move(guard)) {
    require(defaults_.size() <= 1024, "Too many shortcut actions");
    for (auto it = defaults_.cbegin(); it != defaults_.cend(); ++it) {
        require(validId(it.key()), "Invalid shortcut action identity");
        validate(it.value());
        if (!it.value().isEmpty())
            for (auto other = defaults_.cbegin(); other != it; ++other)
                require(other.value() != it.value(), "Duplicate default shortcut");
    }
    envelope_ = {{"apiVersion", 1}, {"bindings", QJsonObject{}}};
    if (!saved.isEmpty()) {
        require(saved.size() <= maxBytes, "Saved shortcuts exceed the size limit");
        QJsonParseError error;
        const auto parsed = QJsonDocument::fromJson(saved, &error);
        require(error.error == QJsonParseError::NoError && parsed.isObject(),
                "Saved shortcuts are malformed; the original settings were not changed");
        envelope_ = parsed.object();
        require(
            envelope_["apiVersion"].isDouble() && envelope_["apiVersion"].toDouble() == 1,
            "Saved shortcuts use an unsupported version; the original settings were not changed");
        require(envelope_["bindings"].isObject(), "Saved shortcut bindings must be an object");
        overrides_ = envelope_["bindings"].toObject();
        require(overrides_.size() <= 1024, "Too many saved shortcut bindings");
        for (auto it = overrides_.constBegin(); it != overrides_.constEnd(); ++it) {
            require(validId(it.key()) && it.value().isString(), "Invalid saved shortcut entry");
            const auto text = it.value().toString();
            require(text.size() <= 128, "Saved shortcut is too long");
            // Unknown action entries remain opaque for a newer or temporarily absent command.
            if (!defaults_.contains(it.key()))
                continue;
            const auto sequence = QKeySequence::fromString(text, QKeySequence::PortableText);
            validate(sequence);
            require(text.isEmpty() || !sequence.isEmpty(), "Saved shortcut key is not recognized");
        }
    }
    resolve();
}
void ShortcutBindings::resolve() {
    effective_.clear();
    notices_.clear();
    QMap<QString, QString> owners;
    // Explicit choices win over new defaults after an upgrade. Conflicting saved choices
    // are preserved on disk but disabled as a group until the user resolves them.
    QMap<QString, QStringList> savedOwners;
    for (auto it = defaults_.cbegin(); it != defaults_.cend(); ++it)
        if (overrides_.contains(it.key())) {
            const auto sequence = QKeySequence::fromString(overrides_[it.key()].toString(),
                                                           QKeySequence::PortableText);
            effective_[it.key()] = sequence;
            if (!sequence.isEmpty())
                savedOwners[sequence.toString(QKeySequence::PortableText)].append(it.key());
        }
    for (auto it = savedOwners.cbegin(); it != savedOwners.cend(); ++it) {
        owners[it.key()] = it.value().join(", ");
        if (it.value().size() > 1) {
            for (const auto &id : it.value())
                effective_[id] = {};
            notices_ << QString(
                            "Saved shortcut %1 conflicts between %2; those bindings are inactive")
                            .arg(it.key(), owners[it.key()]);
        }
    }
    for (auto it = defaults_.cbegin(); it != defaults_.cend(); ++it) {
        if (effective_.contains(it.key()))
            continue;
        const auto portable = it.value().toString(QKeySequence::PortableText);
        if (!portable.isEmpty() && owners.contains(portable)) {
            effective_[it.key()] = {};
            notices_ << QString("Default shortcut for %1 is inactive because %2 uses %3")
                            .arg(it.key(), owners[portable], portable);
        } else {
            effective_[it.key()] = it.value();
            if (!portable.isEmpty())
                owners[portable] = it.key();
        }
    }
    if (guard_)
        for (auto it = effective_.begin(); it != effective_.end(); ++it) {
            if (it.value().isEmpty())
                continue;
            const auto error = guard_(it.key(), it.value());
            if (!error.isEmpty()) {
                notices_ << QString("Shortcut for %1 is inactive. %2").arg(it.key(), error);
                it.value() = {};
            }
        }
}
QStringList ShortcutBindings::conflicts(const QString &id, const QKeySequence &sequence) const {
    require(defaults_.contains(id), "Unknown shortcut action");
    validate(sequence);
    if (guard_ && !sequence.isEmpty()) {
        const auto error = guard_(id, sequence);
        if (!error.isEmpty())
            throw std::runtime_error(error.toStdString());
    }
    QStringList result;
    if (!sequence.isEmpty())
        for (auto it = effective_.cbegin(); it != effective_.cend(); ++it) {
            const auto requested = overrides_.contains(it.key())
                                       ? QKeySequence::fromString(overrides_[it.key()].toString(),
                                                                  QKeySequence::PortableText)
                                       : it.value();
            if (it.key() != id && requested == sequence)
                result << it.key();
        }
    return result;
}
void ShortcutBindings::assign(const QString &id, const QKeySequence &sequence, bool replace) {
    const auto displaced = conflicts(id, sequence);
    require(replace || displaced.isEmpty(),
            "Shortcut is already assigned; choose another or reassign it");
    auto candidate = *this;
    for (const auto &other : displaced)
        candidate.overrides_[other] = "";
    candidate.overrides_[id] = sequence.toString(QKeySequence::PortableText);
    require(candidate.overrides_.size() <= 1024, "Too many saved shortcut bindings");
    candidate.resolve();
    candidate.encode(); // Validate the complete candidate before changing this object.
    *this = std::move(candidate);
}
void ShortcutBindings::resetKnown() {
    for (auto it = defaults_.cbegin(); it != defaults_.cend(); ++it)
        overrides_.remove(it.key());
    resolve();
}
QByteArray ShortcutBindings::encode() const {
    auto object = envelope_;
    object["bindings"] = overrides_;
    const auto bytes = QJsonDocument(object).toJson(QJsonDocument::Compact);
    require(bytes.size() <= maxBytes, "Saved shortcuts exceed the size limit");
    return bytes;
}
} // namespace sketchy
