#pragma once
#include "io/library_bundle.hpp"
namespace sketchy {
enum class LibraryKind { Template, Component };
struct LibraryEntry {
    QString path;
    LibraryKind kind{LibraryKind::Template};
    TemplateMetadata metadata;
    QByteArray thumbnailPng;
    QString error;
};
struct LibraryCatalog {
    std::vector<LibraryEntry> entries;
    QStringList notices;
};
// Flat, local directory; full validation before an entry becomes selectable.
LibraryCatalog scanLibraryDirectory(const QString &directory);
bool matchesLibrarySearch(const LibraryEntry &entry, const QString &query);
} // namespace sketchy
