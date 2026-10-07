#pragma once
#include "automation/commands.hpp"
#include <QByteArray>
namespace sketchy {
struct ExtensionManifest {
    QString id, name, version, description;
    QStringList commands;
    QJsonArray actions;
    QByteArray source;
};
inline constexpr qsizetype extensionManifestLimit = 256 * 1024;
// Declarative command-batch packages; no entrypoints, shell or embedded executables.
ExtensionManifest parseExtensionManifest(const QByteArray &bytes);
QJsonArray resolveExtensionAction(const ExtensionManifest &manifest, const QString &action,
                                  const QJsonObject &parameters);
QJsonObject extensionCapabilities();
} // namespace sketchy
