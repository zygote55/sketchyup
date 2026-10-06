#pragma once
#include "text/text_geometry.hpp"
#include <QJsonObject>
namespace sketchy {
inline constexpr int textWorkerRequestLimit = 32768;
inline constexpr int textWorkerResponseLimit = 32 * 1024 * 1024;
struct TextWorkerOptions {
    QString executable; // Empty discovers only adjacent or installed application helper.
    int timeoutMs{30000};
    int responseBytes{textWorkerResponseLimit};
};
QJsonObject encodeTextSettings(const TextGeometrySettings &settings);
TextGeometrySettings decodeTextSettings(const QJsonObject &value);
QJsonObject encodeTextGeometry(const TextGeometry &geometry);
TextGeometry decodeTextGeometry(const QJsonObject &value);
// No model is modified. Complete validated geometry is returned or an exception
// is raised after terminating the bounded worker process.
TextGeometry runTextWorker(const TextGeometrySettings &settings,
                           const TextWorkerOptions &options = {});
} // namespace sketchy
