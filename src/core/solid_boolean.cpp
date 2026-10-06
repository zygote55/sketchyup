#include "core/solid_boolean.hpp"
#include "core/appearance.hpp"
#include "core/face_textures.hpp"
#include "core/selection.hpp"
#include "geometry/solid_operations.hpp"
#include <algorithm>
#include <limits>
namespace sketchy {
SolidBooleanResult booleanBodies(Document &doc, Id target, Id tool, BooleanOperation operation,
                                 Id context, bool keepOperands) {
    return solidBodies(doc, target, tool,
                       operation == BooleanOperation::Union      ? SolidAction::Union
                       : operation == BooleanOperation::Subtract ? SolidAction::Subtract
                                                                 : SolidAction::Intersect,
                       context, keepOperands);
}
SolidBooleanResult solidBodies(Document &doc, Id target, Id tool, SolidAction action, Id context,
                               bool keepOriginals) {
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
    auto transformSurface = [](Surface &surface, const Transform &transform) {
        for (auto &[id, point] : surface.vertices)
            point = transform.point(point);
        // Placement reflections preserve physical front/back, as in rendering
        // and consolidation. Keep that orientation when baking either frame.
        if (transform.determinant() < 0)
            for (auto &[id, face] : surface.faces)
                for (auto &loop : face.loops)
                    std::reverse(loop.begin(), loop.end());
    };
    transformSurface(left, frame);
    transformSurface(right, toolFrame);
    struct GeneratedPart {
        BooleanPart geometry;
        std::string portion;
        bool fromTool{};
    };
    std::vector<GeneratedPart> generated;
    auto appendParts = [&](BooleanResult result, std::string portion = "result",
                           bool fromTool = false) {
        for (auto &part : result.parts)
            generated.push_back({std::move(part), portion, fromTool});
    };
    try {
        switch (action) {
        case SolidAction::Split: {
            auto split = splitSolids(left, right);
            appendParts(std::move(split.targetOnly), "target");
            appendParts(std::move(split.toolOnly), "tool", true);
            appendParts(std::move(split.overlap), "overlap");
            break;
        }
        case SolidAction::OuterShell:
            appendParts(outerShellSolids(left, right));
            break;
        default:
            appendParts(booleanSolids(left, right,
                                      action == SolidAction::Union ? BooleanOperation::Union
                                      : action == SolidAction::Intersect
                                          ? BooleanOperation::Intersect
                                          : BooleanOperation::Subtract));
        }
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
    const auto name = action == SolidAction::Union       ? "Union"
                      : action == SolidAction::Subtract  ? "Subtract"
                      : action == SolidAction::Intersect ? "Intersection"
                      : action == SolidAction::Trim      ? "Trim"
                      : action == SolidAction::Split     ? "Split"
                                                         : "Outer shell";
    Edit edit{"Solid " + std::string(name), {}};
    Id next = doc.nextId();
    SolidBooleanResult output;
    for (auto &generatedPart : generated) {
        auto &part = generatedPart.geometry;
        const auto &owner = generatedPart.fromTool ? b : a;
        const auto inverse = (generatedPart.fromTool ? toolFrame : frame).inverse();
        if (next == std::numeric_limits<Id>::max())
            throw BooleanError("BOOLEAN_LIMIT", "Solid result would exhaust body identities");
        auto created = std::make_shared<Body>();
        created->id = next++;
        created->name = name;
        if (action == SolidAction::Split)
            created->name += " " + generatedPart.portion;
        created->parent = owner->parent;
        created->transform = owner->transform;
        created->color = owner->color;
        created->materials = owner->materials;
        created->tag = owner->tag;
        created->surface = std::move(part.surface);
        transformSurface(created->surface, inverse);
        for (const auto &[face, source] : part.sources) {
            const auto &body = source.operand ? *b : *a;
            const auto color = faceColor(body, source.face);
            auto mapping = transformTextureMappings(faceTextureMappings(body, source.face),
                                                    inverse * (source.operand ? toolFrame : frame));
            if (source.reversed)
                std::swap(mapping.front, mapping.back);
            setFaceTextureMappings(*created, face, mapping);
            auto materials = faceMaterials(body, source.face);
            if (source.reversed)
                std::swap(materials.front, materials.back);
            if (color != created->color)
                created->faceColors[face] = color;
            if (materials != created->materials)
                created->faceMaterials[face] = materials;
        }
        output.parts.push_back(
            {created->id, std::move(part.sources), part.volume, generatedPart.portion});
        edit.changes.push_back({created->id, nullptr, created});
    }
    if (!keepOriginals) {
        edit.changes.push_back({target, a, nullptr});
        if (action != SolidAction::Trim)
            edit.changes.push_back({tool, b, nullptr});
    }
    if (!edit.changes.empty())
        output.changes = doc.apply(std::move(edit), doc.revision());
    return output;
}
} // namespace sketchy
