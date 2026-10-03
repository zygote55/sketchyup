#include "io/document_io.hpp"
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <limits>
namespace sketchy {
namespace {
constexpr qint64 fileLimit = 32 * 1024 * 1024;
QString sid(Id id) { return QString::number(id); }
Id readId(const QJsonValue &v, bool allowZero = false) {
    bool ok = false;
    auto s = v.toString();
    auto id = s.toULongLong(&ok);
    if (!v.isString() || !ok || (!allowZero && id == 0) || QString::number(id) != s)
        throw std::runtime_error("Invalid stable ID");
    return id;
}
double number(const QJsonValue &v) {
    if (!v.isDouble() || !std::isfinite(v.toDouble()))
        throw std::runtime_error("Expected finite number");
    return v.toDouble();
}
QJsonArray array(const QJsonValue &v) {
    if (!v.isArray())
        throw std::runtime_error("Expected array");
    return v.toArray();
}
QJsonObject object(const QJsonValue &v) {
    if (!v.isObject())
        throw std::runtime_error("Expected object");
    return v.toObject();
}
} // namespace
QByteArray encodeDocument(const Document &doc) {
    QJsonArray bodies;
    for (const auto &[id, b] : doc.bodies()) {
        QJsonArray vertices, faces, wires;
        for (auto [vid, p] : b->surface.vertices)
            vertices.append(QJsonArray{sid(vid), p.x, p.y, p.z});
        for (const auto &[fid, f] : b->surface.faces) {
            QJsonArray loops;
            for (const auto &l : f.loops) {
                QJsonArray loop;
                for (Id v : l)
                    loop.append(sid(v));
                loops.append(loop);
            }
            faces.append(QJsonObject{{"id", sid(fid)}, {"loops", loops}});
        }
        for (auto w : b->surface.wires)
            wires.append(QJsonArray{sid(w[0]), sid(w[1])});
        QJsonArray transform;
        for (auto value : b->transform.m)
            transform.append(value);
        QJsonObject properties;
        for (const auto &[key, value] : b->properties)
            std::visit(
                [&](const auto &v) {
                    using T = std::decay_t<decltype(v)>;
                    if constexpr (std::is_same_v<T, std::string>)
                        properties[QString::fromStdString(key)] = QString::fromStdString(v);
                    else
                        properties[QString::fromStdString(key)] = v;
                },
                value);
        bodies.append(QJsonObject{{"id", sid(id)},
                                  {"parent", sid(b->parent)},
                                  {"transform", transform},
                                  {"properties", properties},
                                  {"name", QString::fromStdString(b->name)},
                                  {"color", QJsonArray{b->color[0], b->color[1], b->color[2]}},
                                  {"nextId", sid(b->surface.nextId)},
                                  {"vertices", vertices},
                                  {"faces", faces},
                                  {"wires", wires}});
    }
    auto bytes = QJsonDocument(QJsonObject{{"format", "sketchyup"},
                                           {"version", 2},
                                           {"revision", sid(doc.revision())},
                                           {"units", "m"},
                                           {"up", "Z"},
                                           {"documentId", QString::fromStdString(doc.identity())},
                                           {"nextId", sid(doc.nextId())},
                                           {"bodies", bodies}})
                     .toJson(QJsonDocument::Compact);
    if (bytes.size() > fileLimit)
        throw std::runtime_error("Document exceeds the 32 MiB file limit");
    return bytes;
}
Document decodeDocument(const QByteArray &bytes) {
    if (bytes.size() > fileLimit)
        throw std::runtime_error("Document exceeds the 32 MiB file limit");
    QJsonParseError error;
    auto json = QJsonDocument::fromJson(bytes, &error);
    if (error.error != QJsonParseError::NoError || !json.isObject())
        throw std::runtime_error("Invalid JSON document");
    auto root = json.object();
    if (root["format"] != "sketchyup" || !root["version"].isDouble() ||
        (root["version"].toDouble() != 1 && root["version"].toDouble() != 2) ||
        root["units"] != "m" || root["up"] != "Z")
        throw std::runtime_error(
            "Unsupported document format, version, units or coordinate system");
    auto records = array(root["bodies"]);
    if (records.size() > 10000)
        throw std::runtime_error("Too many bodies");
    std::map<Id, BodyPtr> bodies;
    size_t totalVertices = 0, totalFaces = 0;
    for (auto record : records) {
        auto o = object(record);
        auto b = std::make_shared<Body>();
        b->id = readId(o["id"]);
        if (!o["name"].isString())
            throw std::runtime_error("Invalid body name");
        b->name = o["name"].toString().toStdString();
        auto color = array(o["color"]);
        if (color.size() != 3)
            throw std::runtime_error("Invalid color");
        for (int i = 0; i < 3; ++i) {
            const auto component = number(color[i]);
            if (component < 0 || component > 1)
                throw std::runtime_error("Color component outside range");
            b->color[i] = component;
        }
        if (root["version"].toInt() == 2) {
            b->parent = readId(o["parent"], true);
            auto transform = array(o["transform"]);
            if (transform.size() != 16)
                throw std::runtime_error("Invalid affine matrix size");
            for (int i = 0; i < 16; ++i)
                b->transform.m[i] = number(transform[i]);
            auto properties = object(o["properties"]);
            for (auto it = properties.begin(); it != properties.end(); ++it) {
                auto key = it.key().toStdString();
                if (it->isBool())
                    b->properties[key] = it->toBool();
                else if (it->isDouble())
                    b->properties[key] = number(*it);
                else if (it->isString())
                    b->properties[key] = it->toString().toStdString();
                else
                    throw std::runtime_error("Unsupported entity property value");
            }
        }
        b->surface.nextId = readId(o["nextId"]);
        auto vertices = array(o["vertices"]), faces = array(o["faces"]), wires = array(o["wires"]);
        totalVertices += vertices.size();
        totalFaces += faces.size();
        if (totalVertices > 100000 || totalFaces > 100000 || wires.size() > 100000)
            throw std::runtime_error("Document complexity exceeds editing limits");
        for (auto vertex : vertices) {
            auto a = array(vertex);
            if (a.size() != 4)
                throw std::runtime_error("Invalid vertex record");
            if (!b->surface.vertices
                     .emplace(readId(a[0]), Vec3{number(a[1]), number(a[2]), number(a[3])})
                     .second)
                throw std::runtime_error("Duplicate vertex ID");
        }
        for (auto face : faces) {
            auto f = object(face);
            Face out;
            out.id = readId(f["id"]);
            for (auto loop : array(f["loops"])) {
                out.loops.emplace_back();
                for (auto id : array(loop))
                    out.loops.back().push_back(readId(id));
            }
            if (!b->surface.faces.emplace(out.id, std::move(out)).second)
                throw std::runtime_error("Duplicate face ID");
        }
        for (auto wire : wires) {
            auto a = array(wire);
            if (a.size() != 2)
                throw std::runtime_error("Invalid wire record");
            b->surface.wires.push_back({readId(a[0]), readId(a[1])});
        }
        if (!bodies.emplace(b->id, b).second)
            throw std::runtime_error("Duplicate body ID");
    }
    if (!root["documentId"].isString())
        throw std::runtime_error("Missing document ID");
    Document doc;
    doc.restore(root["documentId"].toString().toStdString(), readId(root["nextId"]),
                std::move(bodies),
                root["version"].toInt() == 2 ? readId(root["revision"], true) : 0);
    return doc;
}
void saveDocument(Document &doc, const QString &path) {
    const auto bytes = encodeDocument(doc);
    QSaveFile file(path);
    file.setDirectWriteFallback(false);
    if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size() || !file.commit())
        throw std::runtime_error(("Could not save: " + file.errorString()).toStdString());
    doc.markSaved();
}
Document loadDocument(const QString &path) {
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly))
        throw std::runtime_error(f.errorString().toStdString());
    if (f.size() > fileLimit)
        throw std::runtime_error("Document exceeds the 32 MiB file limit");
    auto bytes = f.read(fileLimit + 1);
    if (f.error() != QFileDevice::NoError)
        throw std::runtime_error(f.errorString().toStdString());
    return decodeDocument(bytes);
}
} // namespace sketchy
