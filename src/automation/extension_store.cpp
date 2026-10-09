#include "automation/extension_store.hpp"
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QLockFile>
#include <QRegularExpression>
#include <QSaveFile>
namespace sketchy {
namespace {
constexpr qsizetype storeLimit = 24 * 1024 * 1024;
void require(bool value, const char *message) {
    if (!value)
        throw std::runtime_error(message);
}
QString digest(const QByteArray &bytes) {
    return QString::fromLatin1(QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex());
}
void fields(const QJsonObject &object, const QStringList &names) {
    require(object.size() == names.size(), "Extension registry has missing or extra fields");
    for (const auto &name : names)
        require(object.contains(name), "Extension registry field missing");
}
QString path(const QString &directory) { return QDir(directory).filePath("extensions.json"); }
void regularOrAbsent(const QString &file) {
    const QFileInfo info(file);
    require(!info.isSymLink() && (!info.exists() || info.isFile()),
            "Extension registry must be a regular file, not a link or folder");
}
} // namespace
ExtensionStore::ExtensionStore(QString directory) : directory_(QDir(directory).absolutePath()) {
    const auto filePath = path(directory_);
    regularOrAbsent(filePath);
    if (!QFileInfo::exists(filePath))
        return;
    QFile file(filePath);
    require(file.open(QIODevice::ReadOnly) && file.size() <= storeLimit,
            "Cannot read bounded extension registry");
    const auto bytes = file.read(storeLimit + 1);
    require(file.error() == QFileDevice::NoError && bytes.size() <= storeLimit,
            "Extension registry read failed or exceeded 24 MiB");
    registryDigest_ = QCryptographicHash::hash(bytes, QCryptographicHash::Sha256);
    QJsonParseError error;
    const auto json = QJsonDocument::fromJson(bytes, &error);
    require(error.error == QJsonParseError::NoError && json.isObject(),
            "Invalid extension registry JSON");
    const auto object = json.object();
    fields(object, {"registryVersion", "entries"});
    require(object["registryVersion"] == 1 && object["entries"].isArray() &&
                object["entries"].toArray().size() <= 64,
            "Unsupported extension registry version or entry count");
    for (const auto &value : object["entries"].toArray()) {
        require(value.isObject(), "Invalid extension registry entry");
        const auto entry = value.toObject();
        fields(entry, {"id", "manifest", "sha256", "enabled", "error"});
        InstalledExtension installed;
        require(entry["id"].isString() && entry["id"].toString().size() <= 128 &&
                    QRegularExpression("^[a-z][a-z0-9_-]*(\\.[a-z][a-z0-9_-]*)+$")
                        .match(entry["id"].toString())
                        .hasMatch(),
                "Invalid installed extension identity");
        installed.id = entry["id"].toString();
        require(entry["manifest"].isString() &&
                    entry["manifest"].toString().size() <= ((extensionManifestLimit + 2) / 3) * 4,
                "Installed extension source exceeds limits");
        const auto encoded = entry["manifest"].toString().toLatin1();
        installed.source = QByteArray::fromBase64(encoded, QByteArray::AbortOnBase64DecodingErrors);
        require(!installed.source.isEmpty() && installed.source.size() <= extensionManifestLimit &&
                    installed.source.toBase64() == encoded &&
                    entry["sha256"] == digest(installed.source),
                "Installed extension source encoding or hash mismatch");
        const auto sourceJson = QJsonDocument::fromJson(installed.source).object();
        require(sourceJson["id"] == installed.id,
                "Installed extension identity differs from its source");
        require(entry["enabled"].isBool() && entry["error"].isString() &&
                    entry["error"].toString().toUtf8().size() <= 1024,
                "Invalid installed extension state");
        installed.enabled = entry["enabled"].toBool();
        installed.error = entry["error"].toString();
        require(!installed.enabled || installed.error.isEmpty(),
                "Enabled extensions cannot retain an error state");
        try {
            installed.manifest = parseExtensionManifest(installed.source);
        } catch (const std::exception &failure) {
            installed.enabled = false;
            installed.error = QString::fromUtf8(failure.what()).left(256);
        }
        require(entries_.emplace(installed.id, std::move(installed)).second,
                "Duplicate installed extension identity");
    }
}
void ExtensionStore::commit(std::map<QString, InstalledExtension> replacement) {
    require(replacement.size() <= 64, "Extension registry supports at most 64 packages");
    QJsonArray entries;
    for (const auto &[id, entry] : replacement) {
        entries.append(QJsonObject{{"id", id},
                                   {"manifest", QString::fromLatin1(entry.source.toBase64())},
                                   {"sha256", digest(entry.source)},
                                   {"enabled", entry.enabled},
                                   {"error", entry.error}});
    }
    const auto bytes = QJsonDocument(QJsonObject{{"registryVersion", 1}, {"entries", entries}})
                           .toJson(QJsonDocument::Compact);
    require(bytes.size() <= storeLimit, "Extension registry exceeds 24 MiB");
    require(QDir().mkpath(directory_), "Cannot create extension registry folder");
    QLockFile lock(QDir(directory_).filePath("extensions.lock"));
    require(lock.tryLock(0), "Another extension registry update is in progress");
    const auto filePath = path(directory_);
    regularOrAbsent(filePath);
    if (QFileInfo::exists(filePath)) {
        QFile current(filePath);
        require(current.open(QIODevice::ReadOnly) && current.size() <= storeLimit,
                "Cannot verify current extension registry");
        const auto previous = current.read(storeLimit + 1);
        require(current.error() == QFileDevice::NoError && previous.size() <= storeLimit &&
                    QCryptographicHash::hash(previous, QCryptographicHash::Sha256) ==
                        registryDigest_,
                "Extension registry changed. Reload it before making changes");
    } else
        require(registryDigest_.isEmpty(),
                "Extension registry was removed. Reload it before making changes");
    QSaveFile file(filePath);
    file.setDirectWriteFallback(false);
    require(file.open(QIODevice::WriteOnly), "Cannot create atomic extension registry update");
    require(file.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner),
            "Cannot protect extension registry permissions");
    require(file.write(bytes) == bytes.size() && file.commit(), "Extension registry update failed");
    registryDigest_ = QCryptographicHash::hash(bytes, QCryptographicHash::Sha256);
    entries_ = std::move(replacement);
}
void ExtensionStore::install(const QByteArray &source) {
    auto manifest = parseExtensionManifest(source);
    require(!entries_.contains(manifest.id), "This extension identity is already installed; remove "
                                             "it before installing another version");
    auto replacement = entries_;
    InstalledExtension entry{manifest.id, source, manifest, false, {}};
    replacement.emplace(entry.id, std::move(entry));
    commit(std::move(replacement));
}
void ExtensionStore::setEnabled(const QString &id, bool enabled) {
    require(entries_.contains(id), "Extension is not installed");
    auto replacement = entries_;
    auto &entry = replacement.at(id);
    if (enabled) {
        entry.manifest = parseExtensionManifest(entry.source);
        entry.error.clear();
    }
    entry.enabled = enabled;
    commit(std::move(replacement));
}
void ExtensionStore::recordFailure(const QString &id, const QString &error) {
    require(entries_.contains(id), "Extension is not installed");
    auto replacement = entries_;
    auto &entry = replacement.at(id);
    entry.enabled = false;
    entry.error = error.isEmpty() ? "Extension action failed" : error.left(256);
    commit(std::move(replacement));
}
void ExtensionStore::remove(const QString &id) {
    require(entries_.contains(id), "Extension is not installed");
    auto replacement = entries_;
    replacement.erase(id);
    commit(std::move(replacement));
}
const ExtensionManifest &ExtensionStore::enabledManifest(const QString &id) const {
    require(entries_.contains(id), "Extension is not installed");
    const auto &entry = entries_.at(id);
    require(entry.enabled && entry.manifest.has_value(),
            "Enable a compatible extension before running an action");
    return *entry.manifest;
}
} // namespace sketchy
