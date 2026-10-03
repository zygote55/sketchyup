#include "io/document_io.hpp"
#include <QCryptographicHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QtEndian>
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
QJsonObject floors(const Document &doc) {
    QJsonObject surfaces;
    for (const auto &[id, body] : doc.bodies())
        surfaces[QString::number(id)] = QString::number(body->surface.nextId);
    return {{"body", QString::number(doc.nextId())}, {"surfaces", surfaces}};
}
} // namespace
QByteArray encodeContainer(const Document &doc) {
    const auto payload = encodeDocument(doc);
    const auto manifest =
        QJsonDocument(QJsonObject{{"documentId", QString::fromStdString(doc.identity())},
                                  {"epoch", "1"},
                                  {"revision", QString::number(doc.revision())},
                                  {"writer", "SketchyUp/0.1.0"},
                                  {"units", "m"},
                                  {"up", "Z"},
                                  {"requiredFeatures", QJsonArray{"scene-v2"}},
                                  {"allocatorFloors", floors(doc)},
                                  {"chunks", QJsonArray{QJsonObject{
                                                 {"kind", "document"},
                                                 {"encoding", "json-v2"},
                                                 {"offset", "0"},
                                                 {"bytes", QString::number(payload.size())},
                                                 {"sha256", hash(payload)}}}}})
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
        if (bytes.size() > documentLimit)
            throw std::runtime_error("Legacy document exceeds 32 MiB");
        return decodeDocument(bytes);
    }
    if (bytes.size() < 16 || bytes.size() > 16 + manifestLimit + documentLimit)
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
    fields(manifest, {"documentId", "epoch", "revision", "writer", "units", "up",
                      "requiredFeatures", "allocatorFloors", "chunks"});
    // Epoch rotation/recovery and assets are later capabilities. Never discard their data.
    if (manifest["epoch"] != "1" || manifest["units"] != "m" || manifest["up"] != "Z" ||
        !manifest["writer"].isString() || manifest["writer"].toString().size() > 256 ||
        manifest["requiredFeatures"] != QJsonArray{"scene-v2"})
        throw std::runtime_error("Unsupported required document features");
    if (!manifest["chunks"].isArray() || manifest["chunks"].toArray().size() != 1)
        throw std::runtime_error("This reader requires exactly one document chunk");
    const auto chunk = manifest["chunks"].toArray().first().toObject();
    fields(chunk, {"kind", "encoding", "offset", "bytes", "sha256"});
    if (chunk["kind"] != "document" || chunk["encoding"] != "json-v2" ||
        integer(chunk["offset"]) != 0 || integer(chunk["bytes"]) > documentLimit ||
        integer(chunk["bytes"]) != quint64(bytes.size() - 16 - length))
        throw std::runtime_error("Invalid, unsupported or truncated chunk range");
    const auto payload = bytes.mid(16 + length);
    if (chunk["sha256"] != hash(payload))
        throw std::runtime_error("Document checksum mismatch");
    const auto payloadTree = QJsonDocument::fromJson(payload).object();
    if (payloadTree["version"] != 2)
        throw std::runtime_error("Document chunk encoding mismatch");
    auto doc = decodeDocument(payload);
    if (manifest["documentId"] != QString::fromStdString(doc.identity()) ||
        integer(manifest["revision"]) != doc.revision() ||
        manifest["allocatorFloors"] != floors(doc))
        throw std::runtime_error("Container metadata disagrees with document");
    return doc;
}
} // namespace sketchy
