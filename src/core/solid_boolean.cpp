#include "core/solid_boolean.hpp"
#include "core/appearance.hpp"
#include "core/selection.hpp"
#include <limits>
namespace sketchy {
SolidBooleanResult booleanBodies(Document &doc, Id target, Id tool, BooleanOperation operation,
                                 Id context, bool keepOperands) {
    if (target == tool)
        throw BooleanError("BOOLEAN_OPERANDS", "Choose two different solid bodies");
    Selection policy;
    policy.enter(doc, context);
    for (auto id : {target, tool})
        if (!doc.bodies().contains(id) || doc.bodies().at(id)->kind != BodyKind::Geometry ||
            !policy.inContext(doc, id) || policy.locked(doc, id) ||
            policy.hidden(doc, {id, SelectionKind::Body, 0}) || id == context)
            throw BooleanError(
                "BOOLEAN_SCOPE",
                "Choose editable raw solids in one context; enter group containers first");
    const auto a = doc.bodies().at(target), b = doc.bodies().at(tool);
    const auto frame = doc.worldTransform(target), toolFrame = doc.worldTransform(tool);
    auto left = a->surface, right = b->surface;
    for (auto &[id, point] : left.vertices)
        point = frame.point(point);
    for (auto &[id, point] : right.vertices)
        point = toolFrame.point(point);
    BooleanResult result;
    try {
        result = booleanSolids(left, right, operation);
    } catch (const BooleanError &error) {
        std::string message = error.what();
        if (error.operand() >= 0)
            message =
                "Operand body " + std::to_string(error.operand() ? tool : target) + ": " + message;
        const auto &report = error.report();
        auto append = [&](const char *name, const std::vector<Id> &ids) {
            if (!ids.empty()) {
                message += std::string("; ") + name + ":";
                for (auto id : ids)
                    message += " " + std::to_string(id);
            }
        };
        append("faces", report.faces);
        append("edges", report.edges);
        append("vertices", report.vertices);
        throw BooleanError(error.code(), message, error.operand(), report);
    }
    const auto inverse = frame.inverse();
    const auto name = operation == BooleanOperation::Union      ? "Union"
                      : operation == BooleanOperation::Subtract ? "Subtract"
                                                                : "Intersection";
    Edit edit{"Solid " + std::string(name), {}};
    Id next = doc.nextId();
    SolidBooleanResult output;
    for (auto &part : result.parts) {
        if (next == std::numeric_limits<Id>::max())
            throw BooleanError("BOOLEAN_LIMIT", "Solid result would exhaust body identities");
        auto created = std::make_shared<Body>();
        created->id = next++;
        created->name = name;
        created->parent = a->parent;
        created->transform = a->transform;
        created->color = a->color;
        created->materials = a->materials;
        created->tag = a->tag;
        created->surface = std::move(part.surface);
        for (auto &[id, point] : created->surface.vertices)
            point = inverse.point(point);
        for (const auto &[face, source] : part.sources) {
            const auto &body = source.operand ? *b : *a;
            const auto color = faceColor(body, source.face);
            auto materials = faceMaterials(body, source.face);
            if (source.reversed)
                std::swap(materials.front, materials.back);
            if (color != created->color)
                created->faceColors[face] = color;
            if (materials != created->materials)
                created->faceMaterials[face] = materials;
        }
        output.parts.push_back({created->id, std::move(part.sources), part.volume});
        edit.changes.push_back({created->id, nullptr, created});
    }
    if (!keepOperands) {
        edit.changes.push_back({target, a, nullptr});
        edit.changes.push_back({tool, b, nullptr});
    }
    if (!edit.changes.empty())
        output.changes = doc.apply(std::move(edit), doc.revision());
    return output;
}
} // namespace sketchy
