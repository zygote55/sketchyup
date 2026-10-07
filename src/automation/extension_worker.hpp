#pragma once
#include "automation/extension.hpp"
namespace sketchy {
inline constexpr qsizetype extensionWorkerRequestLimit = 384 * 1024;
inline constexpr qsizetype extensionWorkerResponseLimit = 96 * 1024;
struct ExtensionWorkerOptions {
    QString executable; // Empty uses only the adjacent or installed application helper.
    int timeoutMs{10000};
};
QByteArray resolveExtensionWorkerRequest(const QByteArray &request);
// No document is passed to the process or modified by this call. QThread interruption cancels.
QJsonArray runExtensionWorker(const ExtensionManifest &manifest, const QString &action,
                              const QJsonObject &parameters,
                              const ExtensionWorkerOptions &options = {});
} // namespace sketchy
