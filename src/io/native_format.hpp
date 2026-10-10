#pragma once
#include <QJsonObject>
#include <QString>
namespace sketchy {
class Document;
// Verify and atomically publish a new native container; never replace an existing path.
QJsonObject createNativeFile(const Document &document, const QString &output);
inline constexpr int nativeDocumentVersion = 25, nativeContainerVersion = 2;
// Fully decode and validate before reporting any file as valid. Never rewrite input.
QJsonObject inspectNativeFile(const QString &path);
// Publishes a verified current container to a new path without replacing any file.
QJsonObject migrateNativeFile(const QString &input, const QString &output);
QJsonObject nativeFormatCapabilities();
} // namespace sketchy
