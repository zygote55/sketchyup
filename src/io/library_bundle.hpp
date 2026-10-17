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
struct ComponentBundle {
    TemplateMetadata metadata; // Component bundles require defaultScene == 0.
    Document document;
    Id definition{};
    QByteArray thumbnailPng;
};
// Capture only the selected definition, its recursive references and used resources.
// `identity`, when not empty, is the captured document's identity (32 lowercase hex
// digits) in place of a fresh random one, so a build-time bundle is byte-identical
// across builds (ADR 0166 §6.1, R094.f).
Document captureLibraryComponent(const Document &source, Id definition,
                                 const std::string &identity = {});
QByteArray encodeComponentBundle(const Document &source, Id definition,
                                 const TemplateMetadata &metadata, const QByteArray &thumbnailPng,
                                 const std::string &identity = {});
ComponentBundle decodeComponentBundle(const QByteArray &bytes);
ComponentBundle loadComponentBundle(const QString &path);
void writeComponentBundle(const QByteArray &bytes, const QString &newPath);
inline constexpr qsizetype libraryBundleLimit = 130 * 1024 * 1024;
QByteArray encodeTemplateBundle(const Document &source, const TemplateMetadata &metadata,
                                const QByteArray &thumbnailPng);
TemplateBundle decodeTemplateBundle(const QByteArray &bytes);
TemplateBundle loadTemplateBundle(const QString &path);
void writeTemplateBundle(const QByteArray &bytes, const QString &newPath);
// Fresh identity/session, retained defaults and records, no history, explicitly unsaved.
Document instantiateTemplate(const TemplateBundle &bundle);
} // namespace sketchy
