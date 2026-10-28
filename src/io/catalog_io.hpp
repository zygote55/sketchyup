#pragma once
#include "core/asset_records.hpp"
#include "core/material_records.hpp"
#include "core/model_style.hpp"
#include "io/document_io.hpp"
#include <QJsonObject>
#include <QStringList>
#include <array>
#include <cstdint>
#include <map>
#include <optional>
#include <vector>
namespace sketchy {
// `.sketchycat` (ADR 0174 section 4): an authored, self-contained catalog on the `.sketchylib`
// envelope with `kind = "catalog"`. The envelope's payload region holds every shared asset, then
// every entry payload, in id order and contiguous; the manifest addresses each by an internal
// offset. Nothing is stored by absolute path, so a moved catalog still resolves.
struct CatalogLimits {
    static constexpr std::size_t manifestBytes = 32768;            // the envelope manifest limit
    static constexpr std::size_t payloadBytes = 128 * 1024 * 1024; // the envelope model limit
    static constexpr std::size_t recordBytes = 16 * 1024;          // one style or material payload
    static constexpr std::size_t entries = 128;
    static constexpr std::size_t assets = 256;
    static constexpr std::size_t dependencies = 64; // per entry
    static constexpr std::size_t labels = 32;
    static constexpr std::size_t nameBytes = 256; // catalog, entry and author names
    static constexpr std::size_t descriptionBytes = 4096;
    static constexpr std::size_t labelBytes = 64;
    static constexpr std::size_t authorBytes = 256;
    static constexpr std::size_t catalogIdBytes = 64;
    static constexpr std::size_t versionBytes = 64;
    static constexpr std::size_t licenseTextBytes = 4096;
    static constexpr std::size_t licenseIdentifierBytes = 64;
    static constexpr std::size_t assetNameBytes = 1024;
    static constexpr std::size_t mediaTypeBytes = 128;
    static constexpr std::size_t materialNameBytes = 1024;
    static constexpr std::size_t assetBytes = AssetPayload::limit;  // one asset, 16 MiB
    static constexpr std::size_t assetTotalBytes = assetTotalLimit; // all assets, 64 MiB
};
enum class CatalogEntryKind { Component, Template, Style, Material };
struct CatalogDependency {
    enum class Kind { Asset, Entry };
    Kind kind{Kind::Asset};
    std::uint32_t id{};
    bool operator==(const CatalogDependency &) const = default;
};
struct CatalogMetadata {
    QString catalogId, name, description, author, licenseText, licenseIdentifier, version;
    QStringList labels;
};
// A material preset record: the ADR 0010 fields, with the texture as a catalog asset id (0 for
// none). The asset is a declared dependency of the entry that carries it. The optional texture
// size (R100.b2) is written only when present.
struct CatalogMaterial {
    std::string name;
    std::array<float, 3> color{1, 1, 1};
    float opacity{1};
    std::uint32_t asset{};
    std::optional<TextureSize> textureSize;
};
QString catalogKindName(CatalogEntryKind kind);
// Writer. Equal media type and bytes share one stored asset (ADR 0131: a hash is only an index,
// so the bytes are compared too). Dependencies must name assets and entries added earlier.
class CatalogBuilder {
  public:
    explicit CatalogBuilder(CatalogMetadata metadata);
    std::uint32_t addAsset(const QString &name, const QString &mediaType, const QByteArray &bytes);
    std::uint32_t addStyle(const QString &name, const QString &description, const ModelStyle &style,
                           const std::vector<CatalogDependency> &dependencies = {});
    std::uint32_t addMaterial(const QString &name, const QString &description,
                              const CatalogMaterial &material);
    std::uint32_t addTemplate(const QString &name, const QString &description,
                              const Document &source);
    // `identity`, when not empty, is the captured document's identity (see
    // captureLibraryComponent).
    std::uint32_t addComponent(const QString &name, const QString &description,
                               const Document &source, Id definition,
                               const std::vector<CatalogDependency> &dependencies = {},
                               const std::string &identity = {});
    // Encodes, then decodes the result and refuses it unless every entry is importable.
    QByteArray encode() const;

  private:
    struct AssetDraft {
        QString name, mediaType;
        QByteArray bytes, sha256;
    };
    struct EntryDraft {
        CatalogEntryKind kind{};
        QString name, description, definition;
        std::vector<CatalogDependency> dependencies;
        QByteArray payload;
    };
    std::uint32_t addEntry(EntryDraft entry);
    CatalogMetadata metadata_;
    std::vector<AssetDraft> assets_;
    std::map<QByteArray, std::vector<std::uint32_t>> assetsByHash_;
    std::vector<EntryDraft> entries_;
};
// Reader. An entry is listed as importable only when its payload hash, its declared dependencies
// and its payload decode all check out. An envelope, manifest, limit or layout fault refuses the
// whole file instead.
struct CatalogAssetReport {
    std::uint32_t id{};
    QString name, mediaType, sha256;
    qsizetype offset{}, bytes{};
    bool verified{}; // the stored SHA-256 matches the stored bytes
};
struct CatalogEntryReport {
    std::uint32_t id{};
    CatalogEntryKind kind{};
    QString name, description;
    QString definition; // decimal document identity of the component definition, else empty
    std::vector<CatalogDependency> dependencies;
    QString sha256;
    qsizetype offset{}, bytes{};
    bool importable{};
    QString reason;                           // empty when importable
    std::optional<CatalogDependency> missing; // the dependency the reason names, if any
};
struct CatalogPackage {
    CatalogMetadata metadata;
    std::vector<CatalogAssetReport> assets;
    std::vector<CatalogEntryReport> entries;
    QByteArray file; // the validated envelope; implicitly shared with the bytes decoded
};
CatalogPackage decodeCatalog(const QByteArray &bytes);
CatalogPackage loadCatalog(const QString &path);
// Decodes first, then publishes a new file only (never overwriting, ADR 0133).
void writeCatalog(const QByteArray &bytes, const QString &newPath);
QByteArray catalogAssetBytes(const CatalogPackage &package, const CatalogAssetReport &asset);
QByteArray catalogEntryPayload(const CatalogPackage &package, const CatalogEntryReport &entry);
// The read-only listing shape of `catalog.list`: kind, name and importable state per entry.
QJsonObject encodeCatalogListing(const CatalogPackage &package);

// The catalog library (ADR 0174 section 4, the R100.d reference decision): a user-level folder of
// `.sketchycat` files, beside the style folder of R100.a. A catalog is named, never given as a
// path: MCP and the session pass `{"library": "<name>"}`, and the name is the file's base name.
// The folder is `catalogLibraryFolder()`; its files are bounded like the style folder scan.
QString catalogLibraryFolder();
// A name is 1 to 64 letters, digits, spaces, dots, underscores or hyphens. It starts and ends with
// a letter, digit, underscore or hyphen, so no separator, dot-prefix or link can be named.
bool isCatalogLibraryName(const QString &name);
// Loads `<folder>/<name>.sketchycat`. A missing file, a link or a non-file is refused by name.
CatalogPackage loadCatalogLibraryEntry(const QString &folder, const QString &name);

struct CatalogLibraryEntry {
    QString name, catalogId, version;
    qsizetype entryCount{}, importableCount{};
    QString error; // empty when the file decodes; the name then cannot be referenced if refused
};
struct CatalogLibrary {
    std::vector<CatalogLibraryEntry> entries;
    QStringList notices;
};
// Scans a flat folder: at most 4096 folder entries and 128 catalogs, each fully decoded, within a
// 256 MiB read budget. A folder that does not exist is an empty library.
CatalogLibrary scanCatalogLibrary(const QString &folder);
QJsonObject encodeCatalogLibrary(const CatalogLibrary &library);
} // namespace sketchy
