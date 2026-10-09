#include "io/library_catalog.hpp"
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QRegularExpression>
#include <QtEndian>
#include <algorithm>
namespace sketchy {
namespace {
constexpr qsizetype scanByteLimit = 256 * 1024 * 1024, retainedThumbnailLimit = 16 * 1024 * 1024;
LibraryKind bundleKind(const QByteArray &bytes) {
    if (bytes.size() < 24 || !bytes.startsWith(QByteArray("SKYLIB\0\1", 8)))
        throw std::runtime_error("Invalid library bundle header");
    const auto length = qFromLittleEndian<quint32>(bytes.constData() + 8);
    if (!length || length > 32768 || bytes.size() < 24 + qsizetype(length))
        throw std::runtime_error("Invalid library manifest length");
    const auto manifest = QJsonDocument::fromJson(bytes.mid(24, length)).object();
    if (manifest["kind"] == "template")
        return LibraryKind::Template;
    if (manifest["kind"] == "component")
        return LibraryKind::Component;
    throw std::runtime_error("Unsupported library bundle kind");
}
} // namespace
LibraryCatalog scanLibraryDirectory(const QString &directory) {
    const QFileInfo info(directory);
    if (!info.isDir() || !info.isReadable())
        throw std::runtime_error("Choose a readable local library folder");
    LibraryCatalog result;
    qsizetype readBytes{}, thumbnails{};
    size_t visited{};
    QDirIterator files(info.absoluteFilePath(), QDir::AllEntries | QDir::NoDotAndDotDot,
                       QDirIterator::NoIteratorFlags);
    while (files.hasNext()) {
        files.next();
        if (++visited > 4096) {
            result.notices.append(
                "Stopped after 4096 folder entries. Use a smaller library folder.");
            break;
        }
        const auto fileInfo = files.fileInfo();
        if (fileInfo.suffix().compare("sketchylib", Qt::CaseInsensitive) != 0)
            continue;
        if (result.entries.size() == 128) {
            result.notices.append(
                "Showing at most 128 bundles. Split this library into smaller folders.");
            break;
        }
        LibraryEntry entry;
        entry.path = fileInfo.absoluteFilePath();
        entry.metadata.name = fileInfo.completeBaseName().left(256);
        try {
            if (fileInfo.isSymLink() || !fileInfo.isFile())
                throw std::runtime_error(
                    "Library entries must be regular files, not links or folders");
            if (fileInfo.size() < 24 || fileInfo.size() > libraryBundleLimit)
                throw std::runtime_error("Library file exceeds bundle size limits");
            if (fileInfo.size() > scanByteLimit - readBytes)
                throw std::runtime_error(
                    "Library scan reached its 256 MiB read budget. Use a smaller folder.");
            QFile file(entry.path);
            if (!file.open(QIODevice::ReadOnly))
                throw std::runtime_error("Cannot read library file");
            const auto budget = std::min(libraryBundleLimit, scanByteLimit - readBytes);
            const auto bytes = file.read(budget + 1);
            readBytes += bytes.size();
            if (file.error() != QFileDevice::NoError || bytes.size() > budget || !file.atEnd())
                throw std::runtime_error("Library file changed or exceeded the read budget");
            entry.kind = bundleKind(bytes);
            if (entry.kind == LibraryKind::Template) {
                auto bundle = decodeTemplateBundle(bytes);
                entry.metadata = std::move(bundle.metadata);
                entry.thumbnailPng = std::move(bundle.thumbnailPng);
            } else {
                auto bundle = decodeComponentBundle(bytes);
                entry.metadata = std::move(bundle.metadata);
                entry.thumbnailPng = std::move(bundle.thumbnailPng);
            }
            if (entry.thumbnailPng.size() > retainedThumbnailLimit - thumbnails)
                throw std::runtime_error(
                    "Library thumbnails reached the 16 MiB cache budget. Use a smaller folder.");
            thumbnails += entry.thumbnailPng.size();
        } catch (const std::exception &error) {
            entry.error = QString::fromUtf8(error.what()).left(512);
            entry.thumbnailPng.clear();
        }
        result.entries.push_back(std::move(entry));
    }
    std::sort(result.entries.begin(), result.entries.end(), [](const auto &a, const auto &b) {
        const auto comparison =
            QString::compare(a.metadata.name, b.metadata.name, Qt::CaseInsensitive);
        return comparison ? comparison < 0 : a.path < b.path;
    });
    return result;
}
bool matchesLibrarySearch(const LibraryEntry &entry, const QString &query) {
    const auto haystack = (entry.metadata.name + " " + entry.metadata.description + " " +
                           entry.metadata.labels.join(' ') + " " + QFileInfo(entry.path).fileName())
                              .toCaseFolded();
    const auto words =
        query.left(1024).toCaseFolded().split(QRegularExpression("\\s+"), Qt::SkipEmptyParts);
    return std::all_of(words.begin(), words.end(),
                       [&](const auto &word) { return haystack.contains(word); });
}
} // namespace sketchy
