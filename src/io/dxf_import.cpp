#include "io/dxf_import.hpp"
#include "core/document_limits.hpp"
#include "io/body_budget.hpp"
#include "io/format_limits.hpp"
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
    // Each entity becomes at least one body, so the entity count is bounded by the body limit.
    require(!source.entities.empty() && source.layers.size() <= 1024,
            "DXF has no supported entities or exceeds native layer limits");
    checkFormatCount("DXF import", "entities", source.entities.size(),
                     DocumentLimits::unique.bodies, "unique geometry");
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
    // The bodies of a split entity, in order (R113, D-102): an entity over the one-body limits
    // continues in the next body, named as the first with a number.
    struct Piece {
        std::shared_ptr<Body> body;
        std::map<std::tuple<double, double, double>, Id> identities;
        BodyBudget budget;
    };
    QJsonArray bodySplits;
    for (const auto &entity : source.entities) {
        require(tags.contains(entity.layer) && !entity.segments.empty() &&
                    entity.segments.size() <= DocumentLimits::unique.wires,
                "DXF entity has invalid layer or segment count");
        std::vector<Piece> pieces;
        const auto entityName = (entity.type + " " + QString::number(nextBody)).toStdString();
        std::set<std::pair<std::tuple<double, double, double>, std::tuple<double, double, double>>>
            edges;
        auto startPiece = [&]() -> Piece & {
            auto body = std::make_shared<Body>();
            body->id = nextBody++;
            checkFormatCount("DXF import", "bodies", body->id, DocumentLimits::unique.bodies,
                             "unique geometry");
            body->tag = tags.at(entity.layer);
            body->locked = source.layers.at(entity.layer).locked;
            body->name = continuedBodyName(entityName, pieces.size() + 1);
            body->properties["dxf.layer"] = entity.layer.toStdString();
            body->properties["dxf.entity"] = entity.type.toStdString();
            body->properties["dxf.closed"] = entity.closed;
            pieces.push_back({body, {}, {}});
            return pieces.back();
        };
        // The piece that takes these chords and curves: the last piece when it fits under the
        // one-body limits and its curve record limit, else a new piece.
        auto place = [&](const auto &chords, size_t curveCount) -> Piece & {
            if (!pieces.empty()) {
                auto &last = pieces.back();
                std::set<std::tuple<double, double, double>> fresh;
                for (const auto &chord : chords)
                    for (const auto &p : chord) {
                        const auto key = std::make_tuple(p.x, p.y, p.z);
                        if (!last.identities.contains(key))
                            fresh.insert(key);
                    }
                if (last.budget.admits(0, fresh.size(), chords.size()) &&
                    last.body->curves.size() + curveCount <= 1024)
                    return last;
            }
            return startPiece();
        };
        auto vertex = [&](Piece &piece, Vec3 p) {
            checkPoint(p);
            require(p.z == 0, "DXF conversion supports world XY only");
            const auto key = std::make_tuple(p.x, p.y, p.z);
            if (!piece.identities.contains(key)) {
                checkFormatCount("DXF import", "vertices", ++vertices,
                                 DocumentLimits::unique.vertices, "unique geometry");
                const auto id = piece.body->surface.nextId++;
                piece.identities[key] = id;
                piece.body->surface.vertices[id] = p;
            }
            return piece.identities.at(key);
        };
        auto wire = [&](Piece &piece, Vec3 a, Vec3 b) {
            checkFormatCount("DXF import", "wires", ++wires, DocumentLimits::unique.wires,
                             "unique geometry");
            require(length(b - a) >= tolerance, "DXF wire is below modeling tolerance");
            const auto x = vertex(piece, a), y = vertex(piece, b);
            const auto ka = std::make_tuple(a.x, a.y, a.z), kb = std::make_tuple(b.x, b.y, b.z);
            const auto key = std::minmax(ka, kb);
            require(edges.emplace(key.first, key.second).second,
                    "DXF entity contains a duplicate segment");
            piece.body->surface.wires.push_back({x, y});
            piece.budget.vertices = piece.body->surface.vertices.size();
            piece.budget.edges += 1;
        };
        for (const auto &segment : entity.segments) {
            if (segment.sweep == 0) {
                const std::vector<std::array<Vec3, 2>> chord{{segment.start, segment.end}};
                wire(place(chord, 0), segment.start, segment.end);
                continue;
            }
            checkFormatCount("DXF import", "curves", ++curves, DocumentLimits::unique.curves,
                             "unique geometry");
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
            // An arc and its chords stay in one body, so that its curve binds to them.
            auto &piece = place(chords, 1);
            for (const auto &chord : chords)
                wire(piece, chord[0], chord[1]);
            maximumSag =
                std::max(maximumSag, radius * (1 - std::cos(std::abs(segment.sweep) / count / 2)));
            piece.body->curves.emplace(piece.body->surface.nextId++, std::move(curve));
        }
        if (pieces.size() > 1)
            bodySplits.append(QJsonObject{{"name", QString::fromStdString(entityName)},
                                          {"bodies", qint64(pieces.size())}});
        for (auto &piece : pieces) {
            auto &body = *piece.body;
            body.surface.validate();
            body.topology = Topology::rebuild(body.surface, {});
            size_t budget = 1000000;
            for (auto &[id, curve] : body.curves) {
                (void)id;
                require(bindCurve(curve, body.surface, body.topology, budget),
                        "DXF curve could not bind to editable wire geometry");
            }
            edit.changes.push_back({body.id, nullptr, piece.body});
        }
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
        {"bodySplits", bodySplits},
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
    require(info.isFile() && info.size() <= dxfByteLimit && file.open(QIODevice::ReadOnly),
            "DXF requires a regular file of at most 256 MiB");
    const auto bytes = file.read(dxfByteLimit + 1);
    require(file.error() == QFileDevice::NoError, "Cannot capture DXF source");
    return importDxf(parseDxf(bytes, options), segmentsPerCircle);
}
} // namespace sketchy
