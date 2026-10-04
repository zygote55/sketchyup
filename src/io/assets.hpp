#pragma once
#include "core/assets.hpp"
#include <QByteArray>
#include <QJsonArray>
#include <QString>
namespace sketchy {
QByteArray assetByteArray(const AssetPayloadPtr &payload);
AssetPayloadPtr assetPayload(const QByteArray &bytes);
AssetPayloadPtr decodeAssetPayload(const QString &base64);
QJsonArray assetManifest(const Document &doc);
struct AssetFile {
    std::string name, mediaType;
    AssetPayloadPtr payload;
};
// Bounded explicit import. No path is retained or subsequently resolved by a document.
AssetFile readAssetFile(const QString &path);
} // namespace sketchy
