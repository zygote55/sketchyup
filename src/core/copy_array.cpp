#include "core/copy_array.hpp"
#include <algorithm>
namespace sketchy {
namespace {
using Counts = std::array<size_t, 7>;
Counts counts(const Document &doc) {
    Counts result{doc.bodies().size(), 0, 0, 0, 0, 0, 0};
    for (const auto &[id, body] : doc.bodies()) {
        result[1] += body->surface.vertices.size();
        result[2] += body->surface.faces.size();
        result[3] += body->surface.wires.size();
        result[4] += body->topology.edges.size();
        result[5] += body->curves.size();
        result[6] += body->guides.size();
    }
    return result;
}
} // namespace
ArrayResult copyArraySelected(Document &doc, const TransformTargets &targets,
                              const CopyArray &array, Vec3 pivot, TransformSpace space) {
    if (!array.copies || array.copies > maxArrayCopies)
        throw std::runtime_error("Array requires 1–100 new copies");
    if (array.mode != ArrayMode::Linear && array.mode != ArrayMode::Radial)
        throw std::runtime_error("Unknown array mode");
    checkPoint(pivot);
    if (array.mode == ArrayMode::Linear) {
        checkPoint(array.delta);
        if (length(array.delta) <= tolerance)
            throw std::runtime_error("Linear array requires nonzero displacement");
    } else if (!std::isfinite(array.angle) || std::abs(array.angle) <= 1e-12)
        throw std::runtime_error("Radial array requires a nonzero finite angle");
    std::vector<Transform> matrices;
    matrices.reserve(array.copies);
    for (unsigned i = 1; i <= array.copies; ++i) {
        const auto factor = array.divide ? double(i) / array.copies : double(i);
        matrices.push_back(array.mode == ArrayMode::Linear
                               ? Transform::translation(array.delta * factor)
                               : Transform::rotation(array.axis, array.angle * factor));
    }
    // Probe one private copy to obtain exact expansion costs, including hierarchy
    // descendants and deduplicated raw boundaries. No original identities advance.
    Document staged = doc;
    ArrayResult result;
    result.instances.push_back(transformSelected(staged, targets, matrices[0], pivot, space, true));
    const auto before = counts(doc), after = counts(staged);
    const Counts limits{10000, 100000, 100000, 100000, Topology::edgeLimit, 10000, 10000};
    for (size_t i = 0; i < before.size(); ++i)
        if (before[i] + (after[i] - before[i]) * array.copies > limits[i])
            throw std::runtime_error("Array exceeds document complexity limits");
    for (const auto &[id, body] : doc.bodies()) {
        const auto &next = *staged.bodies().at(id);
        if (body->curves.size() + (next.curves.size() - body->curves.size()) * array.copies >
                1024 ||
            body->guides.size() + (next.guides.size() - body->guides.size()) * array.copies > 1024)
            throw std::runtime_error("Array exceeds context curve or guide limits");
    }
    for (unsigned i = 1; i < array.copies; ++i)
        result.instances.push_back(
            transformSelected(staged, targets, matrices[i], pivot, space, true));
    Edit edit{"Copy array", {}};
    appendComponentChanges(edit, doc, staged);
    edit.nextIdFloor = staged.nextId();
    for (const auto &[id, body] : staged.bodies()) {
        const auto old = doc.bodies().contains(id) ? doc.bodies().at(id) : nullptr;
        if (old != body)
            edit.changes.push_back({id, old, body});
    }
    for (auto &instance : result.instances) {
        for (const auto &[id, copies] : instance.geometryCopies) {
            auto &change = *std::find_if(edit.changes.begin(), edit.changes.end(),
                                         [&](const auto &value) { return value.id == id; });
            auto append = [](const auto &copies, auto &descendants) {
                for (const auto &[source, target] : copies) {
                    auto &all = descendants[source];
                    if (all.empty())
                        all.push_back(source);
                    all.push_back(target);
                }
            };
            append(copies.vertices, change.vertexDescendants);
            append(copies.faces, change.faceDescendants);
            append(copies.edges, change.edgeDescendants);
        }
        instance.changes.clear();
    }
    result.changes = doc.apply(std::move(edit), doc.revision());
    return result;
}
} // namespace sketchy
