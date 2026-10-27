#pragma once
#include "core/model.hpp"
#include "core/model_style.hpp"
#include <QByteArray>
#include <QString>
#include <QStringList>
#include <string_view>
#include <vector>

namespace sketchy {

// The `.sketchystyle` format (ADR 0174 §1): a style record with its optional edge texture, in the
// `.sketchylib` envelope with kind "style". The 24-byte header keeps its layout: the manifest
// length, then the texture's byte count (zero when there is none), then a thumbnail length that is
// always zero, because a style has no model and no preview. The manifest holds the style and a
// texture descriptor; no path is stored, and no script or executable payload is possible.
inline constexpr qsizetype styleManifestLimit = 32 * 1024;
inline constexpr qsizetype styleFileLimit =
    24 + styleManifestLimit + qsizetype(AssetPayload::limit);

struct StyleMetadata {
    QString name, description;
    QStringList labels;
};

// Present: the texture bytes are embedded and match the recorded SHA-256. Missing: the file says
// the texture is missing and carries no bytes. HashMismatch: the bytes do not match their hash.
// None: the style has no edge texture. A missing or mismatched texture is reported by the caller
// and never replaced by a flat colour or a default texture (ADR 0174 §5).
enum class StyleTextureStatus { None, Present, Missing, HashMismatch };
std::string_view styleTextureStatusCode(StyleTextureStatus status);

struct StyleBundle {
    StyleMetadata metadata;
    ModelStyle style;
    // The edge texture's id, name and media type. Its payload is set only when Present.
    AssetRecord texture;
    StyleTextureStatus textureStatus{StyleTextureStatus::None};
};

// Encoding requires a Present texture whenever the style names one, and no texture otherwise.
QByteArray encodeStyleBundle(const StyleBundle &bundle);
// Validates the envelope, the manifest, the style record and the texture hash and decodability.
// A missing or mismatched texture is returned in textureStatus, not thrown.
StyleBundle decodeStyleBundle(const QByteArray &bytes);
StyleBundle loadStyleBundle(const QString &path);
void writeStyleBundle(const QByteArray &bytes, const QString &newPath);

// Captures the document's style and its edge texture. A missing texture refuses the export (the
// file would depend on a resource it does not hold). The result is decoded again before return.
QByteArray exportStyleBundle(const Document &doc, const StyleMetadata &metadata);

// The style library: a flat folder of `.sketchystyle` files, scanned with bounded limits (ADR 0174
// §1.3, the R079 pattern). Each entry is fully decoded before it reports its metadata.
struct StyleLibraryEntry {
    QString path;
    StyleMetadata metadata;
    StyleTextureStatus textureStatus{StyleTextureStatus::None};
    QString error;
};
struct StyleLibrary {
    std::vector<StyleLibraryEntry> entries;
    QStringList notices;
};
StyleLibrary scanStyleDirectory(const QString &directory);

} // namespace sketchy
