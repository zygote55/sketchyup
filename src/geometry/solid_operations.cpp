#include "geometry/solid_operations.hpp"
#include <algorithm>
#include <set>
namespace sketchy {
namespace {
[[noreturn]] void fail(const char *code, const std::string &message) {
    throw BooleanError(code, message);
}
void checkSize(const std::vector<const BooleanResult *> &results) {
    size_t parts{}, vertices{}, triangles{};
    for (const auto *result : results)
        for (const auto &part : result->parts) {
            ++parts;
            vertices += part.surface.vertices.size();
            triangles += part.surface.triangles().size();
            if (parts > 64 || vertices > 16384 || triangles > 32768)
                fail("BOOLEAN_LIMIT", "Combined solid operation exceeds the native output limit");
        }
}
std::pair<Vec3, Vec3> bounds(const Surface &surface) {
    auto low = surface.vertices.begin()->second, high = low;
    for (const auto &[id, p] : surface.vertices) {
        low = {std::min(low.x, p.x), std::min(low.y, p.y), std::min(low.z, p.z)};
        high = {std::max(high.x, p.x), std::max(high.y, p.y), std::max(high.z, p.z)};
    }
    return {low, high};
}
bool boundsContain(const Surface &outer, const Surface &inner) {
    const auto [a, b] = bounds(outer);
    const auto [c, d] = bounds(inner);
    return a.x <= c.x + tolerance && a.y <= c.y + tolerance && a.z <= c.z + tolerance &&
           b.x >= d.x - tolerance && b.y >= d.y - tolerance && b.z >= d.z - tolerance;
}
bool encloses(const Surface &outer, const Surface &inner, size_t &budget) {
    if (!boundsContain(outer, inner))
        return false;
    const auto count = outer.triangles().size() + inner.triangles().size();
    const auto cost = count * count + 4 * count;
    if (cost > budget)
        fail("BOOLEAN_LIMIT", "Outer-shell containment exceeds its native pair work budget");
    budget -= cost;
    auto probe = outer;
    std::map<Id, Id> vertices;
    for (const auto &[id, p] : inner.vertices) {
        const auto next = probe.nextId++;
        probe.vertices.emplace(next, p);
        vertices[id] = next;
    }
    // The diagnostic copy treats the inner boundary as a cavity. Reversal is
    // never published: both candidates' original geometry/provenance stay intact.
    for (const auto &[id, face] : inner.faces) {
        auto loops = face.loops;
        for (auto &loop : loops) {
            for (auto &vertex : loop)
                vertex = vertices.at(vertex);
            std::reverse(loop.begin(), loop.end());
        }
        probe.addFaceIds(std::move(loops));
    }
    const auto analysis = analyzeSolidShells(probe, Topology::rebuild(probe, {}));
    if (analysis.report.status != "validated_shells")
        throw BooleanError("BOOLEAN_OUTPUT", "Outer-shell containment: " + analysis.report.status,
                           -1, analysis.report);
    if (analysis.shells.size() != 2)
        fail("BOOLEAN_OUTPUT", "Outer-shell candidate is not one closed boundary");
    return analysis.shells[1].parent == 0;
}
void fillCavities(BooleanPart &part) {
    const auto analysis = analyzeSolidShells(part.surface, Topology::rebuild(part.surface, {}));
    if (analysis.report.status != "validated_shells")
        throw BooleanError("BOOLEAN_OUTPUT", "Outer-shell source: " + analysis.report.status, -1,
                           analysis.report);
    const SolidShell *outer{};
    for (const auto &shell : analysis.shells)
        if (!shell.parent) {
            if (outer)
                fail("BOOLEAN_OUTPUT", "Outer-shell source has multiple roots");
            outer = &shell;
        }
    if (!outer)
        fail("BOOLEAN_OUTPUT", "Outer-shell source has no outer boundary");
    const std::set<Id> faces(outer->faces.begin(), outer->faces.end());
    std::erase_if(part.surface.faces,
                  [&](const auto &entry) { return !faces.contains(entry.first); });
    std::erase_if(part.sources, [&](const auto &entry) { return !faces.contains(entry.first); });
    std::set<Id> vertices;
    for (const auto &[id, face] : part.surface.faces)
        for (const auto &loop : face.loops)
            vertices.insert(loop.begin(), loop.end());
    std::erase_if(part.surface.vertices,
                  [&](const auto &entry) { return !vertices.contains(entry.first); });
    part.surface.validate();
    const auto report = inspectSolid(part.surface, Topology::rebuild(part.surface, {}));
    if (report.status != "solid")
        throw BooleanError("BOOLEAN_OUTPUT", "Filled outer shell: " + report.status, -1, report);
    part.volume = *report.volume;
}
} // namespace
SolidSplitResult splitSolids(const Surface &target, const Surface &tool) {
    SolidSplitResult result;
    result.targetOnly = booleanSolids(target, tool, BooleanOperation::Subtract);
    try {
        result.toolOnly = booleanSolids(tool, target, BooleanOperation::Subtract);
    } catch (const BooleanError &error) {
        throw BooleanError(error.code(), error.what(),
                           error.operand() < 0 ? -1 : 1 - error.operand(), error.report());
    }
    for (auto &part : result.toolOnly.parts)
        for (auto &[face, source] : part.sources)
            source.operand = 1 - source.operand;
    result.overlap = booleanSolids(target, tool, BooleanOperation::Intersect);
    checkSize({&result.targetOnly, &result.toolOnly, &result.overlap});
    return result;
}
BooleanResult outerShellSolids(const Surface &target, const Surface &tool) {
    auto result = booleanSolids(target, tool, BooleanOperation::Union);
    for (auto &part : result.parts)
        fillCavities(part);
    std::vector<bool> redundant(result.parts.size());
    size_t pairBudget = 4000000;
    for (size_t inner = 0; inner < result.parts.size(); ++inner)
        for (size_t outer = 0; outer < result.parts.size(); ++outer)
            if (inner != outer && result.parts[outer].volume > result.parts[inner].volume &&
                encloses(result.parts[outer].surface, result.parts[inner].surface, pairBudget)) {
                redundant[inner] = true;
                break;
            }
    BooleanResult output;
    for (size_t i = 0; i < result.parts.size(); ++i)
        if (!redundant[i]) {
            output.volume += result.parts[i].volume;
            output.parts.push_back(std::move(result.parts[i]));
        }
    checkSize({&output});
    return output;
}
} // namespace sketchy
