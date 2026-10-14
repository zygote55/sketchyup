#pragma once
#include <QJsonObject>
#include <QString>
namespace sketchy {
class Document;
// Verify and atomically publish a new native container; never replace an existing path.
QJsonObject createNativeFile(const Document &document, const QString &output);
// The newest schema this build reads and can write.
inline constexpr int nativeDocumentVersion = 27, nativeContainerVersion = 2;
// The newest schema SketchyUp 1.0 reads (sketchyup-document-v26).
inline constexpr int nativeSheetFreeDocumentVersion = 26;
// The writer emits the lowest schema the content needs (ADR 0163 decision 2): a
// document with no drawing sheets and no drawing views stays schema 26, without the
// drawing-sheets-v1 feature, so SketchyUp 1.0 can still open it. Anything else is
// schema 27. Drawing-sheet allocator floors alone do not raise the schema.
int nativeDocumentVersionFor(const Document &document);
// Fully decode and validate before reporting any file as valid. Never rewrite input.
QJsonObject inspectNativeFile(const QString &path);
// Publishes a verified current container to a new path without replacing any file.
QJsonObject migrateNativeFile(const QString &input, const QString &output);
QJsonObject nativeFormatCapabilities();
} // namespace sketchy
