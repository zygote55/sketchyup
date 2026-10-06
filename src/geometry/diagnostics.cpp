#include "geometry/diagnostics.hpp"
#include <algorithm>
#include <set>
namespace sketchy {
namespace {
struct Collector {
    GeometryDiagnostics report;
    std::map<std::string, size_t> indices;
    std::vector<std::set<DiagnosticReference>> samples;
    size_t category(std::string code, std::string message, DiagnosticKind counted,
                    DiagnosticSeverity severity = DiagnosticSeverity::Error) {
        if (indices.contains(code))
            return indices.at(code);
        if (report.findings.size() == GeometryDiagnostics::findingLimit)
            throw std::logic_error("Diagnostic category bound exceeded");
        const auto index = report.findings.size();
        indices[code] = index;
        report.findings.push_back(
            {std::move(code), std::move(message), severity, counted, 0, true, {}, false, false});
        samples.emplace_back();
        return index;
    }
    void reference(size_t index, DiagnosticKind kind, Id id) {
        if (!id || kind == DiagnosticKind::None)
            return;
        auto &sample = samples[index];
        const DiagnosticReference ref{kind, id};
        if (sample.contains(ref))
            return;
        if (sample.size() == GeometryDiagnostics::referenceLimit) {
            report.findings[index].truncated = true;
            return;
        }
        sample.insert(ref);
    }
    void primary(size_t index, Id id) {
        ++report.findings[index].count;
        reference(index, report.findings[index].countedKind, id);
    }
    GeometryDiagnostics finish() {
        for (size_t i = 0; i < report.findings.size(); ++i) {
            auto &finding = report.findings[i];
            finding.references.assign(samples[i].begin(), samples[i].end());
            if (finding.truncated)
                finding.reverseShells = false;
        }
        return std::move(report);
    }
};
void limit(Collector &out) {
    out.report.solidStatus = "analysis_limit";
    const auto index =
        out.category("analysis_limit", "Geometry analysis reached its resource limit",
                     DiagnosticKind::None, DiagnosticSeverity::Warning);
    out.primary(index, 0);
}
std::string detail(const std::string &status) {
    if (status == "self_intersection")
        return "Faces cross or overlap away from shared boundaries";
    if (status == "degenerate")
        return "Geometry encloses no stable material volume";
    if (status == "inconsistent_winding")
        return "Nested shell directions do not alternate between material and cavities";
    if (status == "ambiguous_containment")
        return "Shell containment is touching or ambiguous at the geometry tolerance";
    if (status == "non_manifold")
        return "Incident faces do not form one connected fan at this vertex";
    return "Geometry does not meet closed-solid prerequisites";
}
} // namespace
GeometryDiagnostics diagnoseGeometry(const Surface &surface, const Topology &topology) {
    Collector out;
    if (surface.faces.size() > 100000 || surface.vertices.size() > 100000 ||
        topology.edges.size() > Topology::edgeLimit) {
        limit(out);
        return out.finish();
    }
    size_t corners = 0;
    for (const auto &[id, face] : surface.faces)
        for (const auto &loop : face.loops) {
            if (loop.size() > 10000 || loop.size() > GeometryDiagnostics::cornerLimit - corners) {
                limit(out);
                return out.finish();
            }
            corners += loop.size();
        }
    const auto adjacency = topology.adjacency(surface);
    bool topologyBlocked = false;
    for (const auto &[edge, record] : topology.edges) {
        const auto &incident = adjacency.edgeFaces.at(edge);
        if (record.wire) {
            const auto index =
                out.category("loose_edges", "Loose wire edges do not enclose a solid",
                             DiagnosticKind::Edge, DiagnosticSeverity::Warning);
            out.primary(index, edge);
            topologyBlocked = true;
        }
        if (incident.empty())
            continue;
        std::optional<size_t> finding;
        if (incident.size() == 1)
            finding = out.category("open_boundary", "Boundary edges have only one incident face",
                                   DiagnosticKind::Edge);
        else if (incident.size() > 2)
            finding = out.category("non_manifold_edge", "Edges have more than two incident faces",
                                   DiagnosticKind::Edge);
        else if (incident[0].reversed == incident[1].reversed)
            finding = out.category("inconsistent_winding",
                                   "Adjacent faces traverse shared edges in the same direction",
                                   DiagnosticKind::Edge);
        if (finding) {
            out.primary(*finding, edge);
            for (const auto &face : incident)
                out.reference(*finding, DiagnosticKind::Face, face.face);
            topologyBlocked = true;
        }
    }
    for (const auto &[vertex, incident] : adjacency.vertexEdges)
        if (incident.empty()) {
            const auto index =
                out.category("loose_vertices", "Vertices are not attached to any edge",
                             DiagnosticKind::Vertex, DiagnosticSeverity::Warning);
            out.primary(index, vertex);
            topologyBlocked = true;
        }
    // A valid face can be dangerously narrow without violating the model's
    // hard validity threshold. Report its geometry; never silently remove it.
    for (const auto &[id, face] : surface.faces) {
        const auto normal = surface.normal(id);
        double area = 0, perimeter = 0;
        for (size_t loopIndex = 0; loopIndex < face.loops.size(); ++loopIndex) {
            const auto &loop = face.loops[loopIndex];
            const auto origin = surface.vertices.at(loop.front());
            Vec3 vector{};
            for (size_t i = 0; i < loop.size(); ++i) {
                const auto a = surface.vertices.at(loop[i]),
                           b = surface.vertices.at(loop[(i + 1) % loop.size()]);
                vector = vector + cross(a - origin, b - origin);
                perimeter += length(b - a);
            }
            const auto loopArea = std::abs(dot(vector, normal)) * .5;
            area += loopIndex == 0 ? loopArea : -loopArea;
        }
        if (2 * area <= 4 * tolerance * perimeter) {
            const auto index =
                out.category("near_degenerate_face",
                             "Faces have area-to-perimeter width within four geometry tolerances",
                             DiagnosticKind::Face, DiagnosticSeverity::Warning);
            out.primary(index, id);
        }
    }
    if (surface.faces.empty()) {
        out.report.solidStatus = topologyBlocked ? "loose_geometry" : "empty";
        out.report.analysisComplete = !topologyBlocked;
        return out.finish();
    }
    if (topologyBlocked) {
        out.report.solidStatus = "topology_blocked";
        return out.finish();
    }
    const auto analysis = analyzeSolidShells(surface, topology);
    out.report.solidStatus = analysis.report.status;
    if (analysis.report.status == "analysis_limit") {
        limit(out);
        return out.finish();
    }
    if (analysis.report.status != "validated_shells") {
        const auto &report = analysis.report;
        const auto kind = !report.faces.empty()      ? DiagnosticKind::Face
                          : !report.edges.empty()    ? DiagnosticKind::Edge
                          : !report.vertices.empty() ? DiagnosticKind::Vertex
                                                     : DiagnosticKind::None;
        const auto code = report.status == "non_manifold" && kind == DiagnosticKind::Vertex
                              ? "non_manifold_vertex"
                              : report.status;
        const auto index = out.category(code, detail(report.status), kind);
        for (auto face : report.faces)
            out.reference(index, DiagnosticKind::Face, face);
        for (auto edge : report.edges)
            out.reference(index, DiagnosticKind::Edge, edge);
        for (auto vertex : report.vertices) {
            out.reference(index, DiagnosticKind::Vertex, vertex);
            for (auto edge : adjacency.vertexEdges.at(vertex))
                out.reference(index, DiagnosticKind::Edge, edge);
        }
        out.report.findings[index].countExact = false; // Deep analysis reports its first defect.
        out.report.findings[index].count = kind == DiagnosticKind::Face     ? report.faces.size()
                                           : kind == DiagnosticKind::Edge   ? report.edges.size()
                                           : kind == DiagnosticKind::Vertex ? report.vertices.size()
                                                                            : 1;
        return out.finish();
    }
    size_t materialParts = 0;
    for (const auto &shell : analysis.shells) {
        materialParts += shell.depth % 2 == 0;
        const bool expectedPositive = shell.depth % 2 == 0;
        if ((shell.signedVolume > 0) != expectedPositive) {
            const auto index =
                out.category("inverted_shells",
                             "Closed shell faces point away from the material's outward direction",
                             DiagnosticKind::Face, DiagnosticSeverity::Warning);
            out.report.findings[index].reverseShells = true;
            for (auto face : shell.faces)
                out.primary(index, face);
        }
    }
    if (materialParts > 1) {
        const auto index = out.category("multiple_material_parts",
                                        "Disconnected material parts are not one Boolean operand",
                                        DiagnosticKind::None, DiagnosticSeverity::Information);
        out.report.findings[index].count = materialParts;
    }
    out.report.solidStatus = materialParts == 1 ? "solid" : "multiple_shells";
    out.report.materialVolume = analysis.report.volume;
    out.report.analysisComplete = true;
    return out.finish();
}
} // namespace sketchy
