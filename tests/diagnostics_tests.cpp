#include "geometry/diagnostics.hpp"
#include <algorithm>
#include <iostream>
using namespace sketchy;
namespace {
void check(bool value, const char *message) {
    if (!value)
        throw std::runtime_error(message);
}
Surface box(Vec3 origin = {}, Vec3 size = {1, 1, 1}) {
    Surface s;
    const auto face = s.addFace({{origin, origin + Vec3{size.x, 0, 0},
                                  origin + Vec3{size.x, size.y, 0}, origin + Vec3{0, size.y, 0}}});
    s.extrude(face, size.z);
    return s;
}
void reverse(Surface &s) {
    for (auto &[id, face] : s.faces)
        for (auto &loop : face.loops)
            std::reverse(loop.begin(), loop.end());
}
void append(Surface &target, const Surface &source) {
    for (const auto &[id, face] : source.faces) {
        std::vector<std::vector<Vec3>> loops;
        for (const auto &loop : face.loops) {
            loops.emplace_back();
            for (auto vertex : loop)
                loops.back().push_back(source.vertices.at(vertex));
        }
        target.addFace(loops);
    }
}
GeometryDiagnostics diagnose(const Surface &s) {
    s.validate();
    const auto topology = Topology::rebuild(s, {});
    const auto before = s;
    auto report = diagnoseGeometry(s, topology);
    check(s == before, "Diagnostics do not mutate geometry");
    check(report.findings.size() <= GeometryDiagnostics::findingLimit, "Finding count bounded");
    for (const auto &finding : report.findings) {
        check(finding.references.size() <= GeometryDiagnostics::referenceLimit &&
                  std::is_sorted(finding.references.begin(), finding.references.end()),
              "References are bounded and deterministic");
        for (const auto &ref : finding.references)
            check(ref.kind == DiagnosticKind::Face     ? s.faces.contains(ref.id)
                  : ref.kind == DiagnosticKind::Edge   ? topology.edges.contains(ref.id)
                  : ref.kind == DiagnosticKind::Vertex ? s.vertices.contains(ref.id)
                                                       : false,
                  "Every diagnostic reference names a real typed entity");
    }
    return report;
}
const GeometryFinding &finding(const GeometryDiagnostics &report, const std::string &code) {
    for (const auto &entry : report.findings)
        if (entry.code == code)
            return entry;
    throw std::runtime_error("Missing finding: " + code);
}
void topology() {
    auto s = box();
    s.faces.erase(s.faces.begin());
    auto report = diagnose(s);
    const auto &open = finding(report, "open_boundary");
    check(open.count == 4 && open.countExact && open.countedKind == DiagnosticKind::Edge &&
              !open.truncated && !report.analysisComplete && !report.materialVolume &&
              report.solidStatus == "topology_blocked",
          "Open box reports all four boundary edges and makes no deep clean claim");
    s = box();
    auto &loop = s.faces.begin()->second.loops.front();
    std::reverse(loop.begin(), loop.end());
    report = diagnose(s);
    check(finding(report, "inconsistent_winding").count == 4,
          "One reversed cube face identifies its four inconsistent edges");
    // An extra triangle attached to an existing cube edge also has open sides:
    // both categories must be retained, rather than returning the first defect.
    s.addFace({{{0, 0, 0}, {1, 0, 0}, {.5, -1, .5}}});
    report = diagnose(s);
    check(finding(report, "non_manifold_edge").count == 1 &&
              finding(report, "open_boundary").count == 2 &&
              finding(report, "inconsistent_winding").count == 3,
          "All topological categories survive a mixed defect report");
    s = box();
    append(s, box({1, 1, 1}));
    report = diagnose(s);
    check(finding(report, "non_manifold_vertex").count == 1 && !report.analysisComplete,
          "Point-contact shells identify disconnected vertex fans");
    s = box();
    s.vertex({8, 8, 8});
    report = diagnose(s);
    check(finding(report, "loose_vertices").count == 1, "Isolated vertices have typed diagnostics");
}
void orientation() {
    auto s = box();
    auto report = diagnose(s);
    check(report.findings.empty() && report.analysisComplete && report.solidStatus == "solid" &&
              report.materialVolume && std::abs(*report.materialVolume - 1) < 1e-9,
          "Valid unit cube has no findings and a proved volume");
    reverse(s);
    report = diagnose(s);
    const auto &inward = finding(report, "inverted_shells");
    check(inward.count == 6 && inward.references.size() == 6 && inward.reverseShells &&
              !inward.truncated && report.analysisComplete && report.materialVolume,
          "Globally inverted cube supplies complete explicit reversal faces");
    s = box({}, {4, 4, 4});
    auto inner = box({1, 1, 1}, {2, 2, 2});
    reverse(inner);
    append(s, inner);
    report = diagnose(s);
    check(report.findings.empty() && report.materialVolume &&
              std::abs(*report.materialVolume - 56) < 1e-9,
          "Correct inward cavity is not mislabeled inverted");
    reverse(s);
    report = diagnose(s);
    check(finding(report, "inverted_shells").count == 12 &&
              finding(report, "inverted_shells").reverseShells,
          "Inverted cavity tree names outer and cavity faces together");
    s = box();
    append(s, box({3, 0, 0}));
    report = diagnose(s);
    check(report.analysisComplete && finding(report, "multiple_material_parts").count == 2 &&
              finding(report, "multiple_material_parts").severity ==
                  DiagnosticSeverity::Information,
          "Separate valid solids are informational, not guessed defects");
    s = box({}, {4, 4, 4});
    append(s, box({1, 1, 1}, {2, 2, 2}));
    report = diagnose(s);
    check(finding(report, "inconsistent_winding").count == 2 && !report.analysisComplete &&
              !finding(report, "inconsistent_winding").reverseShells,
          "Invalid cavity orientation identifies shells without guessing a repair");
}
void geometryAndLimits() {
    Surface s;
    const auto sliver = s.addFace({{{0, 0, 0}, {1, 0, 0}, {0, 2 * tolerance, 0}}});
    auto report = diagnose(s);
    check(finding(report, "near_degenerate_face").references ==
              std::vector<DiagnosticReference>{{DiagnosticKind::Face, sliver}},
          "Valid but narrow face has an explicit near-degeneracy warning");
    s = {};
    const Vec3 a{0, 0, 0}, b{2, 0, 0}, c{0, 2, 0}, d{.5, .5, 0};
    for (const auto &face :
         std::vector<std::vector<Vec3>>{{a, c, b}, {a, b, d}, {b, c, d}, {c, a, d}})
        s.addFace({face});
    report = diagnose(s);
    check(!finding(report, "self_intersection").references.empty() &&
              !finding(report, "self_intersection").countExact && !report.materialVolume,
          "Deep geometric overlap identifies source faces");
    s = {};
    constexpr size_t wireCount = 50000;
    for (size_t i = 0; i < wireCount; ++i) {
        const auto a = s.nextId++, b = s.nextId++;
        s.vertices[a] = {double(i * 2), 0, 0};
        s.vertices[b] = {double(i * 2 + 1), 0, 0};
        s.wires.push_back({a, b});
    }
    report = diagnose(s);
    const auto &wires = finding(report, "loose_edges");
    check(wires.count == wireCount &&
              wires.references.size() == GeometryDiagnostics::referenceLimit && wires.truncated &&
              !wires.reverseShells && !report.analysisComplete,
          "Fifty thousand findings retain exact counts and only a bounded sample");
    // Seventy-two inverted faces exceed a single finding's reference limit;
    // the report must never offer its first sixty-four as a complete repair.
    s = {};
    for (size_t i = 0; i < 12; ++i)
        append(s, box({double(i * 3), 0, 0}));
    reverse(s);
    report = diagnose(s);
    const auto &large = finding(report, "inverted_shells");
    check(large.count == 72 && large.countExact && large.references.size() == 64 &&
              large.truncated && !large.reverseShells,
          "Truncated shell faces are never advertised as a whole-shell reversal");
    s = {};
    for (size_t i = 0; i < 65; ++i)
        append(s, box({double(i * 3), 0, 0}));
    report = diagnose(s);
    check(report.solidStatus == "analysis_limit" && !report.analysisComplete &&
              !report.materialVolume && finding(report, "analysis_limit").count == 1,
          "Deep shell limit stays explicit rather than reporting clean geometry");
    Surface oversized;
    oversized.faces[1] = {1, {std::vector<Id>(10001, 1)}};
    report = diagnoseGeometry(oversized, {});
    check(report.solidStatus == "analysis_limit" && !report.analysisComplete,
          "Input work bound is checked before allocating adjacency");
}
} // namespace
int main() {
    try {
        topology();
        orientation();
        geometryAndLimits();
        std::cout
            << "Bounded topology, shell orientation, degeneracy and geometry diagnostics passed\n";
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
