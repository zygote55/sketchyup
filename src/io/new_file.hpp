#pragma once
#include <QByteArray>
#include <QString>
namespace sketchy {
// Atomically publishes a new file without replacing any existing destination.
// A directory-sync failure after publication leaves the complete file in place.
void publishNewFile(const QString &path, const QByteArray &bytes);
} // namespace sketchy
