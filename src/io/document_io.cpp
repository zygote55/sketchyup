#include "io/document_io.hpp"
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
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
void supportedFields(const QJsonObject &record, const QStringList &allowed) {
    for (auto it = record.begin(); it != record.end(); ++it)
        if (!allowed.contains(it.key()))
            throw std::runtime_error("Unsupported document field: " + it.key().toStdString());
}
} // namespace
QJsonObject encodeCurve(Id id, const Curve &curve) {
    auto point = [](Vec3 p) { return QJsonArray{p.x, p.y, p.z}; };
    QJsonArray edges;
    for (auto edge : curve.edges)
        edges.append(QJsonArray{sid(edge.edge), edge.reversed});
    return {{"id", sid(id)},
            {"kind", curve.kind == CurveKind::Circle ? "circle"
                     : curve.kind == CurveKind::Arc  ? "arc"
                                                     : "pie"},
            {"center", point(curve.center)},
            {"xAxis", point(curve.xAxis)},
            {"yAxis", point(curve.yAxis)},
            {"radius", curve.radius},
            {"startAngle", curve.startAngle},
            {"sweepAngle", curve.sweepAngle},
            {"segments", int(curve.segments)},
            {"edges", edges}};
}
QJsonObject encodeGuide(Id id, const Guide &guide) {
    QJsonObject result{{"id", sid(id)},
                       {"kind", guide.kind == GuideKind::Point ? "point" : "line"},
                       {"origin", QJsonArray{guide.origin.x, guide.origin.y, guide.origin.z}}};
    if (guide.kind == GuideKind::Line)
        result["direction"] = QJsonArray{guide.direction.x, guide.direction.y, guide.direction.z};
    return result;
}
QByteArray encodeDocument(const Document &doc) {
    QJsonArray bodies;
    for (const auto &[id, b] : doc.bodies()) {
        QJsonArray vertices, faces, wires, edges, curves, guides;
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
        for (const auto &[edgeId, edge] : b->topology.edges)
            edges.append(QJsonArray{sid(edgeId), sid(edge.a), sid(edge.b), edge.wire});
        for (const auto &[curveId, curve] : b->curves)
            curves.append(encodeCurve(curveId, curve));
        for (const auto &[guideId, guide] : b->guides)
            guides.append(encodeGuide(guideId, guide));
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
                                  {"kind", b->kind == BodyKind::Group ? "group" : "geometry"},
                                  {"hidden", b->hidden},
                                  {"locked", b->locked},
                                  {"transform", transform},
                                  {"properties", properties},
                                  {"name", QString::fromStdString(b->name)},
                                  {"color", QJsonArray{b->color[0], b->color[1], b->color[2]}},
                                  {"nextId", sid(b->surface.nextId)},
                                  {"nextEdgeId", sid(b->topology.nextId)},
                                  {"edges", edges},
                                  {"curves", curves},
                                  {"guides", guides},
                                  {"vertices", vertices},
                                  {"faces", faces},
                                  {"wires", wires}});
    }
    auto bytes = QJsonDocument(QJsonObject{{"format", "sketchyup"},
                                           {"version", 6},
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
        (root["version"].toDouble() != 1 && root["version"].toDouble() != 2 &&
         root["version"].toDouble() != 3 && root["version"].toDouble() != 4 &&
         root["version"].toDouble() != 5 && root["version"].toDouble() != 6) ||
        root["units"] != "m" || root["up"] != "Z")
        throw std::runtime_error(
            "Unsupported document format, version, units or coordinate system");
    supportedFields(
        root, {"format", "version", "revision", "units", "up", "documentId", "nextId", "bodies"});
    auto records = array(root["bodies"]);
    if (records.size() > 10000)
        throw std::runtime_error("Too many bodies");
    std::map<Id, BodyPtr> bodies;
    size_t totalVertices = 0, totalFaces = 0, totalGuides = 0;
    for (auto record : records) {
        auto o = object(record);
        QStringList allowed{"id", "name", "color", "nextId", "vertices", "faces", "wires"};
        if (root["version"].toInt() >= 2)
            allowed += {"parent", "transform", "properties"};
        if (root["version"].toInt() >= 3)
            allowed += {"nextEdgeId", "edges"};
        if (root["version"].toInt() >= 4)
            allowed.append("curves");
        if (root["version"].toInt() >= 5)
            allowed.append("guides");
        if (root["version"].toInt() >= 6)
            allowed += {"kind", "hidden", "locked"};
        supportedFields(o, allowed);
        auto b = std::make_shared<Body>();
        b->id = readId(o["id"]);
        if (root["version"].toInt() >= 6) {
            if ((o["kind"] != "geometry" && o["kind"] != "group") || !o["hidden"].isBool() ||
                !o["locked"].isBool())
                throw std::runtime_error("Invalid entity kind, visibility or lock");
            b->kind = o["kind"] == "group" ? BodyKind::Group : BodyKind::Geometry;
            b->hidden = o["hidden"].toBool();
            b->locked = o["locked"].toBool();
        }
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
        if (root["version"].toInt() >= 2) {
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
            supportedFields(f, {"id", "loops"});
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
        if (root["version"].toInt() >= 3) {
            b->topology.nextId = readId(o["nextEdgeId"]);
            const auto edges = array(o["edges"]);
            if (size_t(edges.size()) > Topology::edgeLimit)
                throw std::runtime_error("Too many topology edges");
            for (auto edge : edges) {
                const auto record = array(edge);
                if (record.size() != 4 || !record[3].isBool())
                    throw std::runtime_error("Invalid edge record");
                if (!b->topology.edges
                         .emplace(
                             readId(record[0]),
                             EdgeRecord{readId(record[1]), readId(record[2]), record[3].toBool()})
                         .second)
                    throw std::runtime_error("Duplicate edge ID");
            }
            b->topology.validate(b->surface);
        } else
            b->topology = Topology::rebuild(b->surface, {});
        if (root["version"].toInt() >= 4) {
            const auto curves = array(o["curves"]);
            if (curves.size() > 1024)
                throw std::runtime_error("Too many curve records");
            auto point = [](const QJsonValue &value) {
                const auto p = array(value);
                if (p.size() != 3)
                    throw std::runtime_error("Invalid curve vector");
                return Vec3{number(p[0]), number(p[1]), number(p[2])};
            };
            for (const auto &value : curves) {
                const auto c = object(value);
                supportedFields(c, {"id", "kind", "center", "xAxis", "yAxis", "radius",
                                    "startAngle", "sweepAngle", "segments", "edges"});
                Curve curve;
                if (c["kind"] == "circle")
                    curve.kind = CurveKind::Circle;
                else if (c["kind"] == "arc")
                    curve.kind = CurveKind::Arc;
                else if (c["kind"] == "pie")
                    curve.kind = CurveKind::Pie;
                else
                    throw std::runtime_error("Unknown curve kind");
                curve.center = point(c["center"]);
                curve.xAxis = point(c["xAxis"]);
                curve.yAxis = point(c["yAxis"]);
                curve.radius = number(c["radius"]);
                curve.startAngle = number(c["startAngle"]);
                curve.sweepAngle = number(c["sweepAngle"]);
                const auto count = number(c["segments"]);
                if (count < 1 || count > 256 || count != std::floor(count))
                    throw std::runtime_error("Invalid curve segment count");
                curve.segments = unsigned(count);
                const auto edges = array(c["edges"]);
                if (size_t(edges.size()) > Topology::edgeLimit)
                    throw std::runtime_error("Too many curve edges");
                for (const auto &value : edges) {
                    const auto e = array(value);
                    if (e.size() != 2 || !e[1].isBool())
                        throw std::runtime_error("Invalid oriented curve edge");
                    curve.edges.push_back({readId(e[0]), e[1].toBool()});
                }
                if (!b->curves.emplace(readId(c["id"]), std::move(curve)).second)
                    throw std::runtime_error("Duplicate curve ID");
            }
        }
        if (root["version"].toInt() >= 5) {
            const auto guides = array(o["guides"]);
            totalGuides += guides.size();
            if (guides.size() > 1024 || totalGuides > 10000)
                throw std::runtime_error("Guide count exceeds editing limits");
            auto vector = [&](const QJsonValue &value) {
                const auto values = array(value);
                if (values.size() != 3)
                    throw std::runtime_error("Guide vector requires three coordinates");
                return Vec3{number(values[0]), number(values[1]), number(values[2])};
            };
            for (const auto &value : guides) {
                const auto record = object(value);
                Guide guide;
                if (record["kind"] == "point") {
                    supportedFields(record, {"id", "kind", "origin"});
                    guide.kind = GuideKind::Point;
                } else if (record["kind"] == "line") {
                    supportedFields(record, {"id", "kind", "origin", "direction"});
                    guide.kind = GuideKind::Line;
                    guide.direction = vector(record["direction"]);
                } else
                    throw std::runtime_error("Unsupported guide kind");
                guide.origin = vector(record["origin"]);
                guide.validate();
                if (!b->guides.emplace(readId(record["id"]), guide).second)
                    throw std::runtime_error("Duplicate guide identity");
            }
        }
        if (!bodies.emplace(b->id, b).second)
            throw std::runtime_error("Duplicate body ID");
    }
    if (!root["documentId"].isString())
        throw std::runtime_error("Missing document ID");
    Document doc;
    doc.restore(root["documentId"].toString().toStdString(), readId(root["nextId"]),
                std::move(bodies),
                root["version"].toInt() >= 2 ? readId(root["revision"], true) : 0);
    return doc;
}
} // namespace sketchy
