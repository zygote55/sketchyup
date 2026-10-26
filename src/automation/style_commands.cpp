#include "automation/style_commands.hpp"
#include "io/style_bundle.hpp"
#include <QJsonArray>
#include <stdexcept>

namespace sketchy {
namespace {

// Canonical base64 of one whole style file, bounded before decoding.
QByteArray styleData(const QJsonValue &value) {
    if (!value.isString() || value.toString().size() > qsizetype(4 * ((styleFileLimit + 2) / 3)))
        throw std::runtime_error("Style data must be canonical base64 of at most 16 MiB");
    const auto encoded = value.toString().toLatin1();
    if (QString::fromLatin1(encoded) != value.toString())
        throw std::runtime_error("Style data must be canonical base64");
    const auto result =
        QByteArray::fromBase64Encoding(encoded, QByteArray::AbortOnBase64DecodingErrors);
    if (!result || result.decoded.toBase64() != encoded)
        throw std::runtime_error("Style data must be canonical base64");
    return result.decoded;
}

QJsonObject textureReport(const AssetRecord &texture, StyleTextureStatus status) {
    return QJsonObject{{"id", QString::number(texture.id)},
                       {"name", QString::fromStdString(texture.name)},
                       {"mediaType", QString::fromStdString(texture.mediaType)},
                       {"status", QString::fromLatin1(styleTextureStatusCode(status).data())}};
}

// A missing or mismatched texture is refused by name and id; the document is not touched.
QJsonObject applyStyle(Document &doc, const StyleBundle &bundle) {
    if (bundle.textureStatus == StyleTextureStatus::Missing ||
        bundle.textureStatus == StyleTextureStatus::HashMismatch)
        throw std::runtime_error(
            "Style edge texture '" + bundle.texture.name + "' (asset " +
            std::to_string(bundle.texture.id) + ") is " +
            (bundle.textureStatus == StyleTextureStatus::Missing ? "missing" : "hash-mismatched") +
            "; the style was not applied and no texture was substituted");
    ModelStyle next = bundle.style;
    Edit edit{"Apply style", {}};
    bool created = false;
    if (bundle.textureStatus == StyleTextureStatus::Present) {
        // Reuse only an asset with the same media type and byte-for-byte equal payload (ADR 0131).
        Id target{};
        for (const auto &[id, asset] : doc.assets())
            if (asset->payload && asset->mediaType == bundle.texture.mediaType &&
                asset->payload->bytes() == bundle.texture.payload->bytes()) {
                target = id;
                break;
            }
        if (!target) {
            target = doc.nextAssetId();
            auto record = std::make_shared<AssetRecord>(AssetRecord{
                target, bundle.texture.name, bundle.texture.mediaType, bundle.texture.payload});
            edit.assets.push_back({target, nullptr, record});
            created = true;
        }
        next.edgeStroke->texture = target;
    }
    QJsonObject report{{"command", "style.apply"},
                       {"name", bundle.metadata.name},
                       {"status", "unchanged"},
                       {"assetCreated", created}};
    if (bundle.textureStatus == StyleTextureStatus::Present) {
        auto texture = textureReport(bundle.texture, bundle.textureStatus);
        texture["id"] = QString::number(next.edgeStroke->texture);
        report["texture"] = texture;
    }
    if (next == doc.style() && edit.assets.empty())
        return report;
    edit.style = std::pair{doc.style(), next};
    doc.apply(std::move(edit), doc.revision());
    report["status"] = "applied";
    return report;
}

} // namespace

QJsonObject styleExport(const Document &doc, const QJsonObject &request) {
    StyleMetadata metadata;
    if (!request["name"].isString())
        throw std::runtime_error("A style needs a name");
    metadata.name = request["name"].toString();
    if (request.contains("description")) {
        if (!request["description"].isString())
            throw std::runtime_error("A style description must be text");
        metadata.description = request["description"].toString();
    }
    if (request.contains("labels")) {
        if (!request["labels"].isArray())
            throw std::runtime_error("Style labels must be an array of text");
        for (const auto &label : request["labels"].toArray()) {
            if (!label.isString())
                throw std::runtime_error("Style labels must be an array of text");
            metadata.labels.append(label.toString());
        }
    }
    const auto bytes = exportStyleBundle(doc, metadata);
    const auto style = doc.style();
    QJsonObject report{{"command", "style.export"},
                       {"name", metadata.name},
                       {"bytes", double(bytes.size())},
                       {"data", QString::fromLatin1(bytes.toBase64())}};
    if (style.edgeStroke && style.edgeStroke->texture) {
        const auto &asset = doc.assets().at(style.edgeStroke->texture);
        report["texture"] = textureReport(*asset, StyleTextureStatus::Present);
    }
    return report;
}

bool isStyleCommand(const QString &name) { return name == "style.apply"; }

QJsonObject executeStyleCommand(Document &doc, const QJsonObject &command) {
    if (command["command"].toString() != "style.apply")
        throw std::runtime_error("Unknown style command");
    return applyStyle(doc, decodeStyleBundle(styleData(command["data"])));
}

} // namespace sketchy
