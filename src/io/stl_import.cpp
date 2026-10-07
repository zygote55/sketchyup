#include "io/stl_import.hpp"
#include "geometry/diagnostics.hpp"
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <algorithm>
#include <set>
#include <tuple>
namespace sketchy {
void StlRepairOptions::validate() const {
    if ((weld != StlWeld::None && weld != StlWeld::Exact && weld != StlWeld::Tolerance) ||
        !std::isfinite(toleranceMetres) || toleranceMetres < 1e-9 || toleranceMetres > 1e-3)
        throw std::runtime_error(
            "STL welding requires none, exact or a 1e-9..1e-3 metre tolerance");
}
namespace {
void require(bool ok, const char *message) {
    if (!ok)
        throw std::runtime_error(message);
}
struct Builder {
    const StlSource &source;
    StlRepairOptions repairs;
    StlImport result;
    Edit edit{"Import STL with selected weld/repair options", {}};
    using PointKey = std::tuple<double, double, double>;
    using GridKey = std::array<std::int64_t, 3>;
    struct Mesh {
        std::shared_ptr<Body> body;
        std::map<PointKey, Id> exact;
        std::map<GridKey, std::vector<Id>> grid;
    };
    std::map<size_t, Mesh> meshes;
    size_t vertices{}, welded{}, discarded{}, faces{}, moved{};
    Builder(const StlSource &input, StlRepairOptions options) : source(input), repairs(options) {}
    GridKey cell(Vec3 p) const {
        return {std::int64_t(std::floor(p.x / repairs.toleranceMetres)),
                std::int64_t(std::floor(p.y / repairs.toleranceMetres)),
                std::int64_t(std::floor(p.z / repairs.toleranceMetres))};
    }
    Id vertex(Mesh &mesh, Vec3 p) {
        checkPoint(p);
        const PointKey key{p.x, p.y, p.z};
        Id found{};
        if (repairs.weld == StlWeld::Exact && mesh.exact.contains(key))
            found = mesh.exact.at(key);
        if (repairs.weld == StlWeld::Tolerance) {
            const auto c = cell(p);
            double best = repairs.toleranceMetres;
            for (int x = -1; x <= 1; ++x)
                for (int y = -1; y <= 1; ++y)
                    for (int z = -1; z <= 1; ++z) {
                        const auto bucket = mesh.grid.find({c[0] + x, c[1] + y, c[2] + z});
                        if (bucket == mesh.grid.end())
                            continue;
                        for (const auto id : bucket->second) {
                            const auto distance = length(mesh.body->surface.vertices.at(id) - p);
                            if (distance < best || (distance == best && (!found || id < found))) {
                                best = distance;
                                found = id;
                            }
                        }
                    }
        }
        if (found) {
            ++welded;
            if (mesh.body->surface.vertices.at(found) != p)
                ++moved;
            return found;
        }
        require(++vertices <= 100000, "Expanded STL exceeds 100000 native vertices");
        const auto id = mesh.body->surface.nextId++;
        mesh.body->surface.vertices[id] = p;
        if (repairs.weld == StlWeld::Exact)
            mesh.exact[key] = id;
        if (repairs.weld == StlWeld::Tolerance)
            mesh.grid[cell(p)].push_back(id);
        return id;
    }
    bool degenerate(const std::array<Vec3, 3> &p) const {
        const std::array<double, 3> lengths{length(p[1] - p[0]), length(p[2] - p[1]),
                                            length(p[0] - p[2])};
        const auto longest = std::max({lengths[0], lengths[1], lengths[2]});
        return std::min({lengths[0], lengths[1], lengths[2]}) < tolerance ||
               length(cross(p[1] - p[0], p[2] - p[0])) <
                   std::max(2 * tolerance * tolerance, longest * tolerance);
    }
    bool discard(bool bad) {
        if (!bad)
            return false;
        require(repairs.discardDegenerate, "STL has a collapsed or sub-tolerance facet; enable "
                                           "explicit degenerate-facet removal to omit it");
        ++discarded;
        return true;
    }
    StlImport run() {
        repairs.validate();
        require(!source.facets.empty() && source.facets.size() <= 100000 &&
                    source.solids.size() > 0 && source.solids.size() <= 256,
                "STL conversion source exceeds bounds");
        for (const auto &facet : source.facets) {
            require(facet.solid < size_t(source.solids.size()), "STL facet has no source solid");
            for (auto p : facet.vertices)
                checkPoint(p);
            if (discard(degenerate(facet.vertices)))
                continue;
            if (!meshes.contains(facet.solid)) {
                auto body = std::make_shared<Body>();
                body->id = meshes.size() + 1;
                body->name = source.solids[qsizetype(facet.solid)].toStdString();
                meshes.emplace(facet.solid, Mesh{body, {}, {}});
            }
            auto &mesh = meshes.at(facet.solid);
            auto &surface = mesh.body->surface;
            const std::array<Id, 3> ids{vertex(mesh, facet.vertices[0]),
                                        vertex(mesh, facet.vertices[1]),
                                        vertex(mesh, facet.vertices[2])};
            const std::array<Vec3, 3> points{surface.vertices.at(ids[0]),
                                             surface.vertices.at(ids[1]),
                                             surface.vertices.at(ids[2])};
            if (discard(ids[0] == ids[1] || ids[1] == ids[2] || ids[2] == ids[0] ||
                        degenerate(points)))
                continue;
            surface.addFaceIds({{ids[0], ids[1], ids[2]}});
            ++faces;
        }
        require(faces > 0, "STL contains no retained native facets");
        QJsonArray geometry;
        size_t retainedVertices{};
        for (auto &[solid, mesh] : meshes) {
            (void)solid;
            auto &body = *mesh.body;
            auto &surface = body.surface;
            if (surface.faces.empty())
                continue;
            std::set<Id> used;
            for (const auto &[id, face] : surface.faces) {
                (void)id;
                for (const auto &loop : face.loops)
                    used.insert(loop.begin(), loop.end());
            }
            for (auto it = surface.vertices.begin(); it != surface.vertices.end();)
                if (!used.contains(it->first))
                    it = surface.vertices.erase(it);
                else
                    ++it;
            retainedVertices += surface.vertices.size();
            surface.validate();
            body.topology = Topology::rebuild(surface, {});
            const auto diagnostics = diagnoseGeometry(surface, body.topology);
            QJsonArray findings;
            for (const auto &f : diagnostics.findings)
                findings.append(QJsonObject{{"code", QString::fromStdString(f.code)},
                                            {"message", QString::fromStdString(f.message)},
                                            {"count", qint64(f.count)},
                                            {"countExact", f.countExact},
                                            {"sampleTruncated", f.truncated}});
            geometry.append(
                QJsonObject{{"body", QString::number(body.id)},
                            {"solidStatus", QString::fromStdString(diagnostics.solidStatus)},
                            {"analysisComplete", diagnostics.analysisComplete},
                            {"materialVolume", diagnostics.materialVolume
                                                   ? QJsonValue(*diagnostics.materialVolume)
                                                   : QJsonValue::Null},
                            {"findings", findings}});
            edit.changes.push_back({body.id, nullptr, mesh.body});
        }
        edit.nextIdFloor = meshes.size() + 1;
        result.document.apply(std::move(edit), result.document.revision());
        (void)decodeDocument(encodeDocument(result.document));
        QJsonArray notices{
            QJsonObject{{"code", "metadataUnavailable"},
                        {"message", "STL carries triangles only; native hierarchy, materials, "
                                    "textures and parametric metadata are unavailable."}},
            QJsonObject{
                {"code", "normalsRecomputed"},
                {"message",
                 "Facet winding is retained; native normals are recomputed from geometry."}}};
        if (welded)
            notices.append(QJsonObject{
                {"code", "verticesWelded"},
                {"count", qint64(welded)},
                {"message",
                 "Selected welding joins repeated corner references within each source solid."}});
        if (discarded)
            notices.append(QJsonObject{
                {"code", "degenerateFacetsRemoved"},
                {"count", qint64(discarded)},
                {"message", "Explicit repair omitted collapsed or sub-tolerance triangles."}});
        if (source.report["nonzeroAttributeWords"].toInteger())
            notices.append(
                QJsonObject{{"code", "vendorAttributesOmitted"},
                            {"count", source.report["nonzeroAttributeWords"]},
                            {"message", "Nonstandard STL attribute/color words are omitted."}});
        result.report = {{"apiVersion", 1},
                         {"source", source.report},
                         {"facets", qint64(faces)},
                         {"vertices", qint64(retainedVertices)},
                         {"bodies", qint64(result.document.bodies().size())},
                         {"weld", repairs.weld == StlWeld::None    ? "none"
                                  : repairs.weld == StlWeld::Exact ? "exact"
                                                                   : "tolerance"},
                         {"toleranceMetres", repairs.toleranceMetres},
                         {"discardDegenerate", repairs.discardDegenerate},
                         {"weldedCornerReferences", qint64(welded)},
                         {"movedCornerReferences", qint64(moved)},
                         {"discardedFacets", qint64(discarded)},
                         {"geometry", geometry},
                         {"notices", notices}};
        return std::move(result);
    }
};
} // namespace
StlImport importStl(const StlSource &source, StlRepairOptions repairs) {
    return Builder(source, repairs).run();
}
StlImport loadStl(const QString &path, StlCoordinateOptions coordinates, StlRepairOptions repairs) {
    coordinates.validate();
    repairs.validate();
    const QFileInfo info(path);
    QFile file(path);
    require(info.isFile() && info.size() <= 64 * 1024 * 1024 && file.open(QIODevice::ReadOnly),
            "STL requires a readable regular file of at most 64 MiB");
    const auto bytes = file.read(64 * 1024 * 1024 + 1);
    require(file.error() == QFileDevice::NoError, "Cannot capture STL source");
    return importStl(parseStl(bytes, coordinates), repairs);
}
} // namespace sketchy
