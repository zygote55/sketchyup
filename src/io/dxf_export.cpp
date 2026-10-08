#include "io/dxf_export.hpp"
#include "io/new_file.hpp"
#include <QCryptographicHash>
#include <QJsonArray>

#include <numbers>
#include <set>
namespace sketchy {
namespace {
void require(bool ok, const char *message) {
    if (!ok)
        throw std::runtime_error(message);
}
QString hash(const QByteArray &bytes) {
    return QString::fromLatin1(QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex());
}
struct Writer {
    const Document &doc;
    double scale;
    DxfExport result;
    QByteArray &bytes;
    size_t entityCount{}, vertexCount{}, arcCount{}, polylineCount{}, lineCount{}, edgeCount{};
    QJsonObject losses;
    QJsonArray layerNames;
    std::map<Id, QString> layers;
    Writer(const Document &d, double s) : doc(d), scale(s), bytes(result.bytes) {}
    void loss(const QString &name, int n = 1) { losses[name] = losses[name].toInt() + n; }
    void field(int code, const QByteArray &value) {
        bytes += QByteArray::number(code) + '\n' + value + '\n';
        require(bytes.size() <= 64 * 1024 * 1024, "DXF export exceeds 64 MiB");
    }
    void text(int code, const QString &value) {
        require(!value.contains('\n') && !value.contains('\r') && !value.contains(QChar(0)),
                "DXF text contains record separators");
        field(code, value.toUtf8());
    }
    void number(int code, double value) {
        require(std::isfinite(value), "Nonfinite DXF output");
        field(code, QByteArray::number(value == 0 ? 0 : value, 'g', 17));
    }
    void point(Vec3 p, int x = 10, int y = 20) {
        checkPoint(p);
        require(std::abs(p.z) <= tolerance,
                "DXF export requires world-XY geometry; use a projected view export for 3D models");
        number(x, p.x / scale);
        number(y, p.y / scale);
    }
    void entity(const QString &type, const QString &layer) {
        require(++entityCount <= 100000, "DXF export exceeds 100000 entities");
        text(0, type);
        text(100, "AcDbEntity");
        text(8, layer);
    }
    void curve(const Curve &c, const Transform &world, const QString &layer, std::set<Id> &used) {
        const auto x = world.vector(c.xAxis), y = world.vector(c.yAxis);
        const auto lx = length(x), ly = length(y);
        if (c.kind == CurveKind::Pie || std::abs(lx - ly) > 1e-8 * std::max(lx, ly) ||
            std::abs(dot(x, y)) > 1e-8 * lx * ly || std::abs(x.z) > 1e-10 ||
            std::abs(y.z) > 1e-10) {
            loss("curvesExportedAsChords");
            return;
        }
        for (const auto &edge : c.edges)
            if (used.contains(edge.edge)) {
                loss("overlappingCurveRecordsOmitted");
                return;
            }
        const auto center = world.point(c.center), start = world.point(c.point(c.startAngle)),
                   end = world.point(c.point(c.startAngle + c.sweepAngle));
        for (auto p : {center, start, end}) {
            checkPoint(p);
            require(std::abs(p.z) <= tolerance, "DXF analytic curves must lie in world XY");
        }
        const bool circle = c.kind == CurveKind::Circle;
        entity(circle ? "CIRCLE" : "ARC", layer);
        text(100, "AcDbCircle");
        point(center);
        number(30, 0);
        number(40, c.radius * lx / scale);
        if (!circle) {
            text(100, "AcDbArc");
            auto a = start, b = end;
            if (c.sweepAngle * cross(x, y).z < 0)
                std::swap(a, b);
            auto angle = [&](Vec3 p) {
                double degrees =
                    std::atan2(p.y - center.y, p.x - center.x) * 180 / std::numbers::pi;
                if (degrees < 0)
                    degrees += 360;
                return degrees;
            };
            number(50, angle(a));
            number(51, angle(b));
        }
        for (const auto &edge : c.edges)
            used.insert(edge.edge);
        ++arcCount;
    }
    void geometry(Id id, const Body &body) {
        require(body.topology.edges.size() <= 100000 - edgeCount,
                "DXF export exceeds 100000 source edges");
        edgeCount += body.topology.edges.size();
        const auto world = doc.worldTransform(id);
        const auto layer = layers.at(body.tag);
        std::map<Id, Vec3> points;
        for (const auto &[v, p] : body.surface.vertices) {
            const auto q = world.point(p);
            checkPoint(q);
            require(std::abs(q.z) <= tolerance, "DXF export supports world XY only");
            points[v] = q;
            require(++vertexCount <= 100000, "DXF export exceeds 100000 source vertices");
        }
        std::set<Id> used;
        for (const auto &[curveId, c] : body.curves) {
            (void)curveId;
            curve(c, world, layer, used);
        }
        std::map<Id, std::vector<Id>> adjacency;
        std::map<Id, std::array<Id, 2>> edges;
        for (const auto &[edgeId, edge] : body.topology.edges)
            if (!used.contains(edgeId)) {
                edges[edgeId] = {edge.a, edge.b};
                adjacency[edge.a].push_back(edgeId);
                adjacency[edge.b].push_back(edgeId);
            }
        auto chain = [&](Id start, Id first) {
            std::vector<Id> vertices{start};
            Id current = start, next = first;
            while (edges.contains(next)) {
                const auto edge = edges.at(next);
                edges.erase(next);
                current = edge[0] == current ? edge[1] : edge[0];
                vertices.push_back(current);
                require(vertices.size() <= 100001, "DXF polyline exceeds vertex budget");
                if (current == start || adjacency.at(current).size() != 2)
                    break;
                const auto &candidates = adjacency.at(current);
                next = edges.contains(candidates[0]) ? candidates[0] : candidates[1];
            }
            if (vertices.size() == 2) {
                entity("LINE", layer);
                text(100, "AcDbLine");
                point(points.at(vertices[0]));
                number(30, 0);
                point(points.at(vertices[1]), 11, 21);
                number(31, 0);
                ++lineCount;
            } else {
                const bool closed = vertices.front() == vertices.back();
                if (closed)
                    vertices.pop_back();
                entity("LWPOLYLINE", layer);
                text(100, "AcDbPolyline");
                number(90, vertices.size());
                number(70, closed ? 1 : 0);
                for (const auto v : vertices)
                    point(points.at(v));
                ++polylineCount;
            }
        };
        for (const auto &[v, incident] : adjacency)
            if (incident.size() != 2)
                for (const auto edge : incident)
                    if (edges.contains(edge))
                        chain(v, edge);
        while (!edges.empty()) {
            const auto edge = edges.begin()->first, start = edges.begin()->second[0];
            chain(start, edge);
        }
        if (!body.surface.faces.empty())
            loss("filledFacesExportedAsContours", int(body.surface.faces.size()));
        if (body.hidden || body.locked)
            loss("perBodyVisibilityOrLockOmitted");
        if (!body.guides.empty())
            loss("guidesOmitted", int(body.guides.size()));
        if (!body.properties.empty())
            loss("bodyMetadataOmitted");
    }
    DxfExport run() {
        const std::map<double, int> units{{.0254, 1}, {.3048, 2}, {.001, 4}, {.01, 5}, {1, 6}};
        require(units.contains(scale),
                "DXF export units must be inches, feet, millimetres, centimetres or metres");
        require(doc.readSnapshotBytes() <= 256 * 1024 * 1024,
                "DXF export exceeds native snapshot budget");
        layers[0] = "0";
        std::set<QString> names{"0"};
        for (const auto &[id, tag] : doc.tags())
            if (!tag->folder) {
                QString name = QString::fromStdString(tag->name);
                const QString forbidden = QStringLiteral("<>/\\\":;?*|=\r\n");
                const bool invalid = std::any_of(name.begin(), name.end(),
                                                 [&](QChar c) { return forbidden.contains(c); });
                if (name.toUtf8().size() > 255 || invalid || names.contains(name)) {
                    name = "Layer-" + QString::number(id);
                    while (names.contains(name))
                        name += "_";
                    loss("layerNamesMapped");
                }
                names.insert(name);
                layers[id] = name;
                layerNames.append(QJsonObject{{"nativeTag", QString::number(id)},
                                              {"nativeName", QString::fromStdString(tag->name)},
                                              {"dxfName", name}});
            }
        require(layers.size() <= 1024, "DXF export exceeds 1024 flat layers");
        text(0, "SECTION");
        text(2, "HEADER");
        text(9, "$ACADVER");
        text(1, "AC1032");
        text(9, "$INSUNITS");
        number(70, units.at(scale));
        text(0, "ENDSEC");
        text(0, "SECTION");
        text(2, "TABLES");
        text(0, "TABLE");
        text(2, "LAYER");
        number(70, layers.size());
        for (const auto &[id, name] : layers) {
            text(0, "LAYER");
            text(100, "AcDbSymbolTableRecord");
            text(100, "AcDbLayerTableRecord");
            text(2, name);
            number(70, 0);
            number(62, id && !tagVisible(doc.tags(), id) ? -7 : 7);
            text(6, "CONTINUOUS");
        }
        text(0, "ENDTAB");
        text(0, "ENDSEC");
        text(0, "SECTION");
        text(2, "ENTITIES");
        for (const auto &[id, body] : doc.bodies()) {
            if (body->kind == BodyKind::Group) {
                loss("hierarchyFlattened");
                continue;
            }
            if (body->kind == BodyKind::ReferenceImage) {
                loss("referenceImagesOmitted");
                continue;
            }
            if (!body->topology.edges.empty())
                geometry(id, *body);
        }
        require(entityCount > 0, "DXF export has no supported model edges");
        text(0, "ENDSEC");
        text(0, "EOF");
        loss("appearanceAndViewMetadataOmitted");
        if (!doc.instances().empty())
            loss("componentInstancesExpanded", int(doc.instances().size()));
        if (!doc.annotations().empty())
            loss("annotationsOmitted", int(doc.annotations().size()));
        result.report = {
            {"apiVersion", 1},
            {"version", "AC1032"},
            {"metresPerUnit", scale},
            {"entities", qint64(entityCount)},
            {"lines", qint64(lineCount)},
            {"polylines", qint64(polylineCount)},
            {"arcsAndCircles", qint64(arcCount)},
            {"layerMapping", layerNames},
            {"losses", losses},
            {"bytes", qint64(bytes.size())},
            {"sha256", hash(bytes)},
            {"notices",
             QJsonArray{"All model edges, including hidden geometry, export in world XY without "
                        "section clipping.",
                        "Filled faces export as contours; this is a documented 2D DXF subset, not "
                        "full CAD or DWG interchange.",
                        "Circular curves retain analytic arcs when their world transform preserves "
                        "circles; other curves export as chord edges.",
                        "Native hierarchy, per-body locking/visibility, materials, textures and "
                        "view metadata are not retained."}}};
        return std::move(result);
    }
};
} // namespace
DxfExport exportDxf(const Document &document, double metresPerUnit) {
    return Writer(document, metresPerUnit).run();
}
void writeDxfExport(const DxfExport &result, const QString &path) {
    require(!result.bytes.isEmpty() && result.bytes.size() <= 64 * 1024 * 1024 &&
                result.report["bytes"].toInteger() == result.bytes.size() &&
                result.report["sha256"] == hash(result.bytes),
            "DXF bytes do not match export report");
    publishNewFile(path, result.bytes);
}
} // namespace sketchy
