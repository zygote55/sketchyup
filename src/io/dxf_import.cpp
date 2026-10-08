#include "io/dxf_import.hpp"
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <numbers>
#include <set>
#include <tuple>
namespace sketchy {
namespace {
void require(bool ok, const char *message) {
    if (!ok)
        throw std::runtime_error(message);
}
} // namespace
DxfImport importDxf(const DxfSource &source, unsigned segmentsPerCircle) {
    require(segmentsPerCircle >= 12 && segmentsPerCircle <= 256,
            "DXF curve resolution must be 12..256 segments per circle");
    require(!source.entities.empty() && source.entities.size() <= 10000 &&
                source.layers.size() <= 1024,
            "DXF has no supported entities or exceeds native body/layer limits");
    DxfImport result;
    Edit edit{"Import planar DXF", {}};
    std::map<QString, Id> tags;
    Id nextTag = 1, nextBody = 1;
    for (const auto &[name, layer] : source.layers) {
        require(name == layer.name && !name.isEmpty() && name.toUtf8().size() <= 256,
                "Invalid DXF source layer");
        auto tag = std::make_shared<TagRecord>();
        tag->id = nextTag++;
        tag->name = name.toStdString();
        tag->visible = !layer.hidden;
        tags[name] = tag->id;
        edit.tags.push_back({tag->id, nullptr, tag});
    }
    size_t vertices{}, wires{}, curves{};
    double maximumSag{};
    for (const auto &entity : source.entities) {
        require(tags.contains(entity.layer) && !entity.segments.empty() &&
                    entity.segments.size() <= 100000,
                "DXF entity has invalid layer or segment count");
        auto body = std::make_shared<Body>();
        body->id = nextBody++;
        body->tag = tags.at(entity.layer);
        body->locked = source.layers.at(entity.layer).locked;
        body->name = (entity.type + " " + QString::number(body->id)).toStdString();
        body->properties["dxf.layer"] = entity.layer.toStdString();
        body->properties["dxf.entity"] = entity.type.toStdString();
        body->properties["dxf.closed"] = entity.closed;
        auto &surface = body->surface;
        std::map<std::tuple<double, double, double>, Id> identities;
        std::set<std::pair<Id, Id>> edges;
        auto vertex = [&](Vec3 p) {
            checkPoint(p);
            require(p.z == 0, "DXF conversion supports world XY only");
            const auto key = std::make_tuple(p.x, p.y, p.z);
            if (!identities.contains(key)) {
                require(++vertices <= 100000, "DXF expansion exceeds 100000 native vertices");
                const auto id = surface.nextId++;
                identities[key] = id;
                surface.vertices[id] = p;
            }
            return identities.at(key);
        };
        auto wire = [&](Vec3 a, Vec3 b) {
            require(++wires <= 100000, "DXF expansion exceeds 100000 wire segments");
            require(length(b - a) >= tolerance, "DXF wire is below modeling tolerance");
            const auto x = vertex(a), y = vertex(b);
            const auto key = std::minmax(x, y);
            require(edges.emplace(key.first, key.second).second,
                    "DXF entity contains a duplicate segment");
            surface.wires.push_back({x, y});
        };
        for (const auto &segment : entity.segments) {
            if (segment.sweep == 0) {
                wire(segment.start, segment.end);
                continue;
            }
            require(++curves <= 10000 && body->curves.size() < 1024,
                    "DXF expansion exceeds curve record limit");
            checkPoint(segment.center);
            const auto radius = length(segment.start - segment.center);
            const bool circle = segment.sweep == 2 * std::numbers::pi;
            const auto start =
                std::atan2(segment.start.y - segment.center.y, segment.start.x - segment.center.x);
            require(std::isfinite(segment.sweep) && std::abs(segment.sweep) <= 2 * std::numbers::pi,
                    "DXF arc sweep invalid");
            const auto count =
                std::max(1u, unsigned(std::ceil(std::abs(segment.sweep) / (2 * std::numbers::pi) *
                                                segmentsPerCircle)));
            Curve curve{circle ? CurveKind::Circle : CurveKind::Arc,
                        segment.center,
                        {1, 0, 0},
                        {0, 1, 0},
                        radius,
                        start,
                        segment.sweep,
                        count,
                        {}};
            auto chords = curve.chords();
            require(length(chords.front()[0] - segment.start) < tolerance &&
                        length(chords.back()[1] - segment.end) < tolerance,
                    "DXF analytic arc disagrees with endpoints");
            chords.front()[0] = segment.start;
            chords.back()[1] = circle ? segment.start : segment.end;
            for (const auto &chord : chords)
                wire(chord[0], chord[1]);
            maximumSag =
                std::max(maximumSag, radius * (1 - std::cos(std::abs(segment.sweep) / count / 2)));
            body->curves.emplace(surface.nextId++, std::move(curve));
        }
        surface.validate();
        body->topology = Topology::rebuild(surface, {});
        size_t budget = 1000000;
        for (auto &[id, curve] : body->curves) {
            (void)id;
            require(bindCurve(curve, surface, body->topology, budget),
                    "DXF curve could not bind to editable wire geometry");
        }
        edit.changes.push_back({body->id, nullptr, body});
    }
    edit.nextIdFloor = nextBody;
    edit.nextTagFloor = nextTag;
    result.document.apply(std::move(edit), result.document.revision());
    (void)decodeDocument(encodeDocument(result.document));
    result.report = {
        {"apiVersion", 1},
        {"source", source.report},
        {"bodies", qint64(result.document.bodies().size())},
        {"vertices", qint64(vertices)},
        {"wireSegments", qint64(wires)},
        {"curves", qint64(curves)},
        {"layers", qint64(tags.size())},
        {"segmentsPerCircle", int(segmentsPerCircle)},
        {"maximumChordDeviationMetres", maximumSag},
        {"notices", QJsonArray{"Supported model-space XY entities become separate editable wire "
                               "bodies; closed outlines do not automatically create faces.",
                               "Layers become native tags; off/frozen layers remain hidden and "
                               "locked-layer entities remain locked.",
                               "Analytic arc and circle records are retained over chord geometry. "
                               "The maximum chord deviation is reported.",
                               "Appearance, blocks, layouts and unsupported CAD entities are not "
                               "fully preserved; review source omission counts."}}};
    return result;
}
DxfImport loadDxf(const QString &path, DxfOptions options, unsigned segmentsPerCircle) {
    QFile file(path);
    const QFileInfo info(path);
    require(info.isFile() && info.size() <= 64 * 1024 * 1024 && file.open(QIODevice::ReadOnly),
            "DXF requires a regular file of at most 64 MiB");
    const auto bytes = file.read(64 * 1024 * 1024 + 1);
    require(file.error() == QFileDevice::NoError, "Cannot capture DXF source");
    return importDxf(parseDxf(bytes, options), segmentsPerCircle);
}
} // namespace sketchy
