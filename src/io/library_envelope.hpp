#pragma once
#include <QByteArray>
#include <QCryptographicHash>
#include <QJsonObject>
#include <QJsonValue>
#include <QString>
#include <QStringList>
#include <stdexcept>
// The 24-byte `.sketchylib` envelope shared by template, component and catalog bundles (ADR 0129,
// ADR 0130 and ADR 0174 section 4): the header layout, the manifest and thumbnail limits, and the
// hash, field-set and metadata checks. Each kind keeps its own manifest and payload rules.
namespace sketchy::library_envelope {
inline const QByteArray magic("SKYLIB\0\1", 8);
inline constexpr qsizetype headerSize = 24, manifestLimit = 32768, thumbnailLimit = 4 * 1024 * 1024;
inline void require(bool ok, const char *message) {
    if (!ok)
        throw std::runtime_error(message);
}
inline QString hash(const QByteArray &bytes) {
    return QString::fromLatin1(QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex());
}
inline void fields(const QJsonObject &object, const QStringList &names) {
    require(object.size() == names.size(), "Library manifest has missing or extra fields");
    for (const auto &name : names)
        require(object.contains(name), "Library manifest field missing");
}
inline QString text(const QJsonValue &value, qsizetype maximum, bool multiline = false) {
    require(value.isString(), "Library metadata must be text");
    const auto s = value.toString();
    require(s.toUtf8().size() <= maximum && QString::fromUtf8(s.toUtf8()) == s,
            "Library metadata exceeds UTF-8 limits");
    for (auto c : s)
        require(c.unicode() >= 32 || (multiline && (c == '\n' || c == '\t')),
                "Library metadata contains control characters");
    return s;
}
} // namespace sketchy::library_envelope
