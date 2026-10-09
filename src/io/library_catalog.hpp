#pragma once
#include "io/library_bundle.hpp"
namespace sketchy {
enum class LibraryKind { Template, Component };
struct LibraryEntry {
    QString path;
    LibraryKind kind{LibraryKind::Template};
    TemplateMetadata metadata;
    QByteArray thumbnailPng;
    QByteArray contentSha256;
    QString error;
};
struct LibraryCatalog {
    std::vector<LibraryEntry> entries;
    QStringList notices;
};
// Flat, local directory; full validation before an entry becomes selectable.
LibraryCatalog scanLibraryDirectory(const QString &directory);
// Reject stale selections rather than loading different bytes under a cached name.
QByteArray readLibraryEntry(const LibraryEntry &entry);
bool matchesLibrarySearch(const LibraryEntry &entry, const QString &query);
} // namespace sketchy
