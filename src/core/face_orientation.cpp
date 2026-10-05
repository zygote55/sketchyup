#include "core/face_orientation.hpp"
#include "core/materials.hpp"
#include <algorithm>
namespace sketchy {
namespace {
void validateSelection(const Document &doc, const SelectionSet &faces, Id context) {
    if (faces.empty() || faces.size() > 4096)
        throw OrientationError("ORIENTATION_SELECTION", "Choose 1–4096 editable faces");
    Selection policy;
    policy.enter(doc, context);
    std::set<Id> bodies;
    for (auto face : faces) {
        if (face.kind != SelectionKind::Face || !policy.selectable(doc, face) ||
            doc.bodies().at(face.body)->kind != BodyKind::Geometry)
            throw OrientationError("ORIENTATION_SCOPE",
                                   "Choose editable faces in the specified context", face.entity);
        bodies.insert(face.body);
    }
    if (bodies.size() > 128)
        throw OrientationError("ORIENTATION_LIMIT", "Narrow face orientation to 128 bodies");
}
void append(Edit &edit, const BodyPtr &before, FaceOrientationResult result) {
    if (result.reversed.empty())
        return;
    auto after = std::make_shared<Body>(*before);
    after->surface = std::move(result.surface);
    for (auto face : result.reversed) {
        auto sides = faceMaterials(*before, face);
        std::swap(sides.front, sides.back);
        if (sides == after->materials)
            after->faceMaterials.erase(face);
        else
            after->faceMaterials[face] = sides;
    }
    edit.changes.push_back({before->id, before, after});
}
template <class F> FaceOrientationResult forBody(Id body, F operation) {
    try {
        return operation();
    } catch (const OrientationError &error) {
        auto message = "Body " + std::to_string(body) + ": " + error.what();
        if (error.face())
            message += "; face: " + std::to_string(error.face());
        if (error.edge())
            message += "; edge: " + std::to_string(error.edge());
        throw OrientationError(error.code(), message, error.face(), error.edge());
    }
}
} // namespace
ChangeReport reverseSelectedFaces(Document &doc, const SelectionSet &faces, Id context) {
    validateSelection(doc, faces, context);
    std::map<Id, std::set<Id>> groups;
    for (auto face : faces)
        groups[face.body].insert(face.entity);
    Edit edit{"Reverse faces", {}};
    for (const auto &[body, selected] : groups) {
        const auto before = doc.bodies().at(body);
        append(edit, before, forBody(body, [&] {
                   return reverseFaces(before->surface, before->topology, selected);
               }));
    }
    return doc.apply(std::move(edit), doc.revision());
}
ChangeReport orientConnectedFaces(Document &doc, Id body, Id referenceFace, Id context) {
    validateSelection(doc, {{body, SelectionKind::Face, referenceFace}}, context);
    const auto before = doc.bodies().at(body);
    Edit edit{"Orient connected faces", {}};
    append(edit, before, forBody(body, [&] {
               return orientFaces(before->surface, before->topology, referenceFace);
           }));
    if (edit.changes.empty())
        return {};
    return doc.apply(std::move(edit), doc.revision());
}
} // namespace sketchy
