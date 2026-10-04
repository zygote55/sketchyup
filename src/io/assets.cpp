#include "io/assets.hpp"
#include <QCryptographicHash>
#include <QFile>
#include <QFileInfo>
#include <QJsonObject>
#include <QMimeDatabase>
namespace sketchy {
QByteArray assetByteArray(const AssetPayloadPtr &payload) {
    if (!payload)
        throw std::runtime_error("Asset payload is missing");
    const auto &bytes = payload->bytes();
    return QByteArray(reinterpret_cast<const char *>(bytes.data()), qsizetype(bytes.size()));
}
AssetPayloadPtr assetPayload(const QByteArray &bytes) {
    if (bytes.isEmpty() || bytes.size() > qsizetype(AssetPayload::limit))
        throw std::runtime_error("Asset payload must contain 1 byte to 16 MiB");
    return std::make_shared<AssetPayload>(std::vector<std::uint8_t>(bytes.begin(), bytes.end()));
}
AssetPayloadPtr decodeAssetPayload(const QString &base64) {
    if (base64.size() > qsizetype(4 * ((AssetPayload::limit + 2) / 3)))
        throw std::runtime_error("Encoded asset exceeds 16 MiB");
    const auto encoded = base64.toLatin1();
    if (QString::fromLatin1(encoded) != base64)
        throw std::runtime_error("Asset data must be canonical base64");
    const auto result =
        QByteArray::fromBase64Encoding(encoded, QByteArray::AbortOnBase64DecodingErrors);
    if (!result || result.decoded.toBase64() != encoded)
        throw std::runtime_error("Asset data must be canonical base64");
    return assetPayload(result.decoded);
}
QJsonArray assetManifest(const Document &doc) {
    QJsonArray records;
    for (const auto &[id, asset] : doc.assets()) {
        const auto bytes = asset->payload ? assetByteArray(asset->payload) : QByteArray{};
        records.append(QJsonObject{
            {"id", QString::number(id)},
            {"name", QString::fromStdString(asset->name)},
            {"mediaType", QString::fromStdString(asset->mediaType)},
            {"missing", !asset->payload},
            {"bytes", QString::number(bytes.size())},
            {"path", "assets/" + QString::number(id) + ".bin"},
            {"sha256",
             asset->payload
                 ? QJsonValue(QString::fromLatin1(
                       QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex()))
                 : QJsonValue::Null}});
    }
    return records;
}
AssetFile readAssetFile(const QString &path) {
    if (!QFileInfo(path).isFile())
        throw std::runtime_error("Choose a regular asset file");
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly) || file.isSequential() ||
        file.size() > qsizetype(AssetPayload::limit))
        throw std::runtime_error("Cannot read asset or file exceeds 16 MiB");
    const auto bytes = file.read(AssetPayload::limit + 1);
    if (file.error() != QFileDevice::NoError)
        throw std::runtime_error("Asset read failed");
    const auto payload = assetPayload(bytes);
    return {QFileInfo(path).fileName().toStdString(),
            QMimeDatabase().mimeTypeForData(bytes).name().toStdString(), payload};
}
} // namespace sketchy
