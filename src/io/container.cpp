#include "io/assets.hpp"
#include "io/document_io.hpp"
#include <QCryptographicHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QtEndian>
#include <set>
namespace sketchy {
namespace {
constexpr qsizetype manifestLimit = 1024 * 1024;
constexpr qsizetype documentLimit = 32 * 1024 * 1024;
const QByteArray magic("SKUPDOC\0", 8);
QString hash(const QByteArray &bytes) {
    return QString::fromLatin1(QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex());
}
quint64 integer(const QJsonValue &value) {
    bool ok = false;
    const auto text = value.toString();
    auto number = text.toULongLong(&ok);
    if (!value.isString() || !ok || QString::number(number) != text)
        throw std::runtime_error("Invalid container integer");
    return number;
}
void fields(const QJsonObject &object, const QStringList &expected) {
    for (auto it = object.begin(); it != object.end(); ++it)
        if (!expected.contains(it.key()))
            throw std::runtime_error("Unsupported container field");
    if (object.size() != expected.size())
        throw std::runtime_error("Missing container field");
}
QJsonObject floors(const Document &doc, bool topology = true, bool components = false,
                   bool tags = false, bool materials = false, bool assets = false) {
    QJsonObject surfaces, edges;
    for (const auto &[id, body] : doc.bodies()) {
        surfaces[QString::number(id)] = QString::number(body->surface.nextId);
        edges[QString::number(id)] = QString::number(body->topology.nextId);
    }
    QJsonObject result{{"body", QString::number(doc.nextId())}, {"surfaces", surfaces}};
    if (topology)
        result["edges"] = edges;
    if (assets)
        result["nextAssetId"] = QString::number(doc.nextAssetId());
    if (materials)
        result["nextMaterialId"] = QString::number(doc.nextMaterialId());
    if (tags)
        result["nextTagId"] = QString::number(doc.nextTagId());
    if (components) {
        QJsonObject definitions;
        for (const auto &[id, definition] : doc.definitions()) {
            QJsonObject members;
            for (const auto &[member, body] : definition->members)
                members[QString::number(member)] = QJsonArray{
                    QString::number(body->surface.nextId), QString::number(body->topology.nextId)};
            definitions[QString::number(id)] = QJsonObject{
                {"nextMemberId", QString::number(definition->nextMemberId)}, {"geometry", members}};
        }
        result["nextDefinitionId"] = QString::number(doc.nextDefinitionId());
        result["definitions"] = definitions;
    }
    return result;
}
} // namespace
QByteArray encodeContainer(const Document &doc) {
    const auto document = encodeDocument(doc, AssetStorage::External);
    QByteArray payload = document;
    QJsonArray chunks{QJsonObject{{"kind", "document"},
                                  {"encoding", "json-v14"},
                                  {"offset", "0"},
                                  {"bytes", QString::number(document.size())},
                                  {"sha256", hash(document)}}};
    const auto assets = assetManifest(doc);
    for (auto value : assets) {
        const auto asset = value.toObject();
        const auto id = integer(asset["id"]);
        if (asset["missing"].toBool())
            continue;
        const auto bytes = assetByteArray(doc.assets().at(id)->payload);
        chunks.append(QJsonObject{{"kind", "asset"},
                                  {"id", asset["id"]},
                                  {"path", asset["path"]},
                                  {"encoding", "raw"},
                                  {"offset", QString::number(payload.size())},
                                  {"bytes", QString::number(bytes.size())},
                                  {"sha256", asset["sha256"]}});
        payload.append(bytes);
    }
    const auto manifest =
        QJsonDocument(
            QJsonObject{{"documentId", QString::fromStdString(doc.identity())},
                        {"epoch", "1"},
                        {"revision", QString::number(doc.revision())},
                        {"writer", "SketchyUp/0.1.0"},
                        {"units", "m"},
                        {"up", "Z"},
                        {"requiredFeatures",
                         QJsonArray{"scene-v2", "topology-v1", "curves-v1", "guides-v1",
                                    "groups-v1", "face-colors-v1", "components-v1", "tags-v1",
                                    "materials-v1", "assets-v1", "display-units-v1",
                                    "edge-appearance-v1", "component-glue-v1"}},
                        {"allocatorFloors", floors(doc, true, true, true, true, true)},
                        {"assets", assets},
                        {"chunks", chunks}})
            .toJson(QJsonDocument::Compact);
    if (manifest.size() > manifestLimit)
        throw std::runtime_error("Container manifest exceeds 1 MiB");
    QByteArray header(16, '\0');
    header.replace(0, magic.size(), magic);
    qToLittleEndian<quint32>(2, header.data() + 8);
    qToLittleEndian<quint32>(manifest.size(), header.data() + 12);
    return header + manifest + payload;
}
Document decodeContainer(const QByteArray &bytes) {
    // Explicit legacy migration, without rewriting the source file.
    if (!bytes.startsWith(magic)) {
        if (bytes.size() > 128 * 1024 * 1024)
            throw std::runtime_error("Raw document exceeds 128 MiB");
        return decodeDocument(bytes);
    }
    if (bytes.size() < 16 ||
        bytes.size() > 16 + manifestLimit + documentLimit + qsizetype(assetTotalLimit))
        throw std::runtime_error("Invalid or oversized container");
    const auto version = qFromLittleEndian<quint32>(bytes.constData() + 8);
    const auto length = qFromLittleEndian<quint32>(bytes.constData() + 12);
    if (version != 2 || length > manifestLimit || length > bytes.size() - 16)
        throw std::runtime_error("Unsupported or truncated container header");
    QJsonParseError error;
    auto json = QJsonDocument::fromJson(bytes.mid(16, length), &error);
    if (error.error != QJsonParseError::NoError || !json.isObject())
        throw std::runtime_error("Invalid container manifest");
    const auto manifest = json.object();
    std::set<QString> features;
    if (!manifest["requiredFeatures"].isArray())
        throw std::runtime_error("Missing required features");
    for (const auto &value : manifest["requiredFeatures"].toArray())
        if (!value.isString() || !features.insert(value.toString()).second)
            throw std::runtime_error("Invalid required features");
    const bool componentGlue =
        features == std::set<QString>{"scene-v2",         "topology-v1",      "curves-v1",
                                      "guides-v1",        "groups-v1",        "face-colors-v1",
                                      "components-v1",    "tags-v1",          "materials-v1",
                                      "assets-v1",        "display-units-v1", "edge-appearance-v1",
                                      "component-glue-v1"};
    const bool edgeAppearance =
        componentGlue ||
        features == std::set<QString>{"scene-v2",      "topology-v1",      "curves-v1",
                                      "guides-v1",     "groups-v1",        "face-colors-v1",
                                      "components-v1", "tags-v1",          "materials-v1",
                                      "assets-v1",     "display-units-v1", "edge-appearance-v1"};
    const bool displayUnits =
        edgeAppearance ||
        features == std::set<QString>{"scene-v2",      "topology-v1",     "curves-v1",
                                      "guides-v1",     "groups-v1",       "face-colors-v1",
                                      "components-v1", "tags-v1",         "materials-v1",
                                      "assets-v1",     "display-units-v1"};
    const bool assets =
        displayUnits ||
        features == std::set<QString>{"scene-v2",      "topology-v1", "curves-v1",
                                      "guides-v1",     "groups-v1",   "face-colors-v1",
                                      "components-v1", "tags-v1",     "materials-v1",
                                      "assets-v1"};
    QStringList manifestFields{"documentId",       "epoch",           "revision",
                               "writer",           "units",           "up",
                               "requiredFeatures", "allocatorFloors", "chunks"};
    if (assets)
        manifestFields.append("assets");
    fields(manifest, manifestFields);
    const bool materials =
        assets || features == std::set<QString>{"scene-v2",      "topology-v1", "curves-v1",
                                                "guides-v1",     "groups-v1",   "face-colors-v1",
                                                "components-v1", "tags-v1",     "materials-v1"};
    const bool tags =
        materials ||
        features == std::set<QString>{"scene-v2",  "topology-v1",    "curves-v1",     "guides-v1",
                                      "groups-v1", "face-colors-v1", "components-v1", "tags-v1"};
    const bool components =
        tags ||
        features == std::set<QString>{"scene-v2",  "topology-v1",    "curves-v1",    "guides-v1",
                                      "groups-v1", "face-colors-v1", "components-v1"};
    const bool faceColors =
        components || features == std::set<QString>{"scene-v2",  "topology-v1", "curves-v1",
                                                    "guides-v1", "groups-v1",   "face-colors-v1"};
    const bool groups =
        faceColors || features == std::set<QString>{"scene-v2", "topology-v1", "curves-v1",
                                                    "guides-v1", "groups-v1"};
    const bool guides = groups || features == std::set<QString>{"scene-v2", "topology-v1",
                                                                "curves-v1", "guides-v1"};
    const bool curves =
        guides || features == std::set<QString>{"scene-v2", "topology-v1", "curves-v1"};
    const bool topology = curves || features == std::set<QString>{"scene-v2", "topology-v1"};
    // Epoch rotation/recovery is a later capability. Never discard its data.
    if (manifest["epoch"] != "1" || manifest["units"] != "m" || manifest["up"] != "Z" ||
        !manifest["writer"].isString() || manifest["writer"].toString().size() > 256 ||
        (!topology && manifest["requiredFeatures"] != QJsonArray{"scene-v2"}))
        throw std::runtime_error("Unsupported required document features");
    if (!manifest["chunks"].isArray())
        throw std::runtime_error("Missing container chunks");
    const auto chunks = manifest["chunks"].toArray();
    if (chunks.empty() || chunks.size() > 1025 || (!assets && chunks.size() != 1))
        throw std::runtime_error("Invalid container chunk count");
    if (assets && (!manifest["assets"].isArray() || manifest["assets"].toArray().size() > 1024))
        throw std::runtime_error("Invalid asset manifest");
    const auto chunk = chunks.first().toObject();
    fields(chunk, {"kind", "encoding", "offset", "bytes", "sha256"});
    const auto total = quint64(bytes.size() - 16 - length);
    const auto documentSize = integer(chunk["bytes"]);
    if (chunk["kind"] != "document" ||
        chunk["encoding"] != (componentGlue    ? "json-v14"
                              : edgeAppearance ? "json-v13"
                              : displayUnits   ? "json-v12"
                              : assets         ? "json-v11"
                              : materials      ? "json-v10"
                              : tags           ? "json-v9"
                              : components     ? "json-v8"
                              : faceColors     ? "json-v7"
                              : groups         ? "json-v6"
                              : guides         ? "json-v5"
                              : curves         ? "json-v4"
                              : topology       ? "json-v3"
                                               : "json-v2") ||
        integer(chunk["offset"]) != 0 || documentSize > documentLimit || documentSize > total)
        throw std::runtime_error("Invalid document chunk range or encoding");
    const auto payload = bytes.mid(16 + length, documentSize);
    if (chunk["sha256"] != hash(payload))
        throw std::runtime_error("Document checksum mismatch");
    AssetPayloads assetPayloads;
    quint64 cursor = documentSize, assetTotal = 0;
    for (qsizetype i = 1; i < chunks.size(); ++i) {
        const auto asset = chunks[i].toObject();
        fields(asset, {"kind", "id", "path", "encoding", "offset", "bytes", "sha256"});
        const auto id = integer(asset["id"]), size = integer(asset["bytes"]);
        // Paths are logical keys only. Never extract or resolve user-supplied paths.
        if (!id || assetPayloads.contains(id) || asset["kind"] != "asset" ||
            asset["encoding"] != "raw" ||
            asset["path"] != "assets/" + QString::number(id) + ".bin" ||
            integer(asset["offset"]) != cursor || !size || size > AssetPayload::limit ||
            size > total - cursor || size > assetTotalLimit - assetTotal)
            throw std::runtime_error("Invalid asset identity, path or chunk range");
        const auto data = bytes.mid(16 + length + cursor, size);
        if (asset["sha256"] != hash(data))
            throw std::runtime_error("Asset checksum mismatch");
        assetPayloads[id] = assetPayload(data);
        cursor += size;
        assetTotal += size;
    }
    if (cursor != total)
        throw std::runtime_error("Trailing or unreferenced container data");
    const auto payloadTree = QJsonDocument::fromJson(payload).object();
    if (payloadTree["version"] != (componentGlue    ? 14
                                   : edgeAppearance ? 13
                                   : displayUnits   ? 12
                                   : assets         ? 11
                                   : materials      ? 10
                                   : tags           ? 9
                                   : components     ? 8
                                   : faceColors     ? 7
                                   : groups         ? 6
                                   : guides         ? 5
                                   : curves         ? 4
                                   : topology       ? 3
                                                    : 2))
        throw std::runtime_error("Document chunk encoding mismatch");
    if (assets && payloadTree["assetStorage"] != "external")
        throw std::runtime_error("Packaged document requires external asset chunks");
    auto doc = decodeDocument(payload, assetPayloads);
    if (assets && manifest["assets"] != assetManifest(doc))
        throw std::runtime_error("Asset manifest disagrees with document");
    if (manifest["documentId"] != QString::fromStdString(doc.identity()) ||
        integer(manifest["revision"]) != doc.revision() ||
        manifest["allocatorFloors"] != floors(doc, topology, components, tags, materials, assets))
        throw std::runtime_error("Container metadata disagrees with document");
    return doc;
}
} // namespace sketchy
