#include "io/stl_import.hpp"
#include "core/document_limits.hpp"
#include "geometry/diagnostics.hpp"
#include "io/body_budget.hpp"
#include "io/format_limits.hpp"
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
    // Every body in creation order. A solid over the one-body limit continues in its next body
    // (R113, D-102); `current` is the body that takes the solid's next facet.
    std::vector<Mesh> meshes;
    std::map<size_t, size_t> current;
    std::map<size_t, size_t> pieces;
    size_t vertices{}, welded{}, discarded{}, faces{}, moved{};
    Builder(const StlSource &input, StlRepairOptions options) : source(input), repairs(options) {}
    GridKey cell(Vec3 p) const {
        return {std::int64_t(std::floor(p.x / repairs.toleranceMetres)),
                std::int64_t(std::floor(p.y / repairs.toleranceMetres)),
                std::int64_t(std::floor(p.z / repairs.toleranceMetres))};
    }
    // The vertex that the weld joins p to, or 0 when p is a new vertex of the body.
    Id existing(const Mesh &mesh, Vec3 p) const {
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
        return found;
    }
    Id vertex(Mesh &mesh, Vec3 p) {
        checkPoint(p);
        const PointKey key{p.x, p.y, p.z};
        const auto found = existing(mesh, p);
        if (found) {
            ++welded;
            if (mesh.body->surface.vertices.at(found) != p)
                ++moved;
            return found;
        }
        checkFormatCount("STL import", "vertices", ++vertices, DocumentLimits::unique.vertices,
                         "unique geometry");
        const auto id = mesh.body->surface.nextId++;
        mesh.body->surface.vertices[id] = p;
        if (repairs.weld == StlWeld::Exact)
            mesh.exact[key] = id;
        if (repairs.weld == StlWeld::Tolerance)
            mesh.grid[cell(p)].push_back(id);
        return id;
    }
    // The body that takes a facet with these corners: the solid's open body when the facet fits
    // under the one-body limits (a corner that the weld does not join is a new vertex), or a new
    // body that continues the solid. Each triangle has three edges.
    Mesh &placement(size_t solid, const std::array<Vec3, 3> &corners) {
        const auto open = current.find(solid);
        if (open != current.end()) {
            auto &mesh = meshes.at(open->second);
            const auto &surface = mesh.body->surface;
            size_t fresh = 0;
            for (const auto &p : corners)
                fresh += existing(mesh, p) == 0;
            if (BodyBudget{surface.faces.size(), surface.vertices.size(),
                           3 * surface.faces.size()}
                    .admits(1, fresh, 3))
                return mesh;
        }
        const auto piece = ++pieces[solid];
        auto body = std::make_shared<Body>();
        body->id = meshes.size() + 1;
        body->name = continuedBodyName(source.solids[qsizetype(solid)].toStdString(), piece);
        meshes.push_back(Mesh{body, {}, {}});
        current[solid] = meshes.size() - 1;
        return meshes.back();
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
        require(!source.facets.empty() && source.facets.size() <= DocumentLimits::unique.faces &&
                    source.solids.size() > 0 && source.solids.size() <= 256,
                "STL conversion source exceeds bounds");
        for (const auto &facet : source.facets) {
            require(facet.solid < size_t(source.solids.size()), "STL facet has no source solid");
            for (auto p : facet.vertices)
                checkPoint(p);
            if (discard(degenerate(facet.vertices)))
                continue;
            auto &mesh = placement(facet.solid, facet.vertices);
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
        for (auto &mesh : meshes) {
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
        // A solid over the one-body limit is in more than one body; its facets on either side of
        // the split share no vertices, so shading across the split is not smooth (R113, D-102).
        QJsonArray bodySplits;
        for (const auto &[solid, count] : pieces)
            if (count > 1)
                bodySplits.append(QJsonObject{{"name", source.solids[qsizetype(solid)]},
                                              {"bodies", qint64(count)}});
        result.report = {{"apiVersion", 1},
                         {"source", source.report},
                         {"facets", qint64(faces)},
                         {"vertices", qint64(retainedVertices)},
                         {"bodies", qint64(result.document.bodies().size())},
                         {"bodySplits", bodySplits},
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
    require(info.isFile() && info.size() <= 128 * 1024 * 1024 && file.open(QIODevice::ReadOnly),
            "STL requires a readable regular file of at most 128 MiB");
    const auto bytes = file.read(128 * 1024 * 1024 + 1);
    require(file.error() == QFileDevice::NoError, "Cannot capture STL source");
    return importStl(parseStl(bytes, coordinates), repairs);
}
} // namespace sketchy
