#pragma once
#include "io/document_io.hpp"
namespace sketchy {
struct TemplateMetadata {
    QString name, description;
    QStringList labels;
    Id defaultScene{};
};
struct TemplateBundle {
    TemplateMetadata metadata;
    Document document;
    QByteArray thumbnailPng;
};
inline constexpr qsizetype libraryBundleLimit = 130 * 1024 * 1024;
QByteArray encodeTemplateBundle(const Document &source, const TemplateMetadata &metadata,
                                const QByteArray &thumbnailPng);
TemplateBundle decodeTemplateBundle(const QByteArray &bytes);
TemplateBundle loadTemplateBundle(const QString &path);
void writeTemplateBundle(const QByteArray &bytes, const QString &newPath);
// Fresh identity/session, retained defaults and records, no history, explicitly unsaved.
Document instantiateTemplate(const TemplateBundle &bundle);
} // namespace sketchy
