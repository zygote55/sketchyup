#include "core/annotations.hpp"
namespace sketchy {
namespace {
AnnotationPtr requireAnnotation(const Document &doc, Id id) {
    const auto found = doc.annotations().find(id);
    if (found == doc.annotations().end())
        throw std::runtime_error("Annotation does not exist");
    return found->second;
}
} // namespace
Id createAnnotation(Document &doc, AnnotationRecord value) {
    value.id = doc.nextAnnotationId();
    for (auto &anchor : value.anchors) {
        const auto resolved = resolveAnnotationAnchor(doc, anchor);
        if (resolved.state != AnchorState::Resolved)
            throw std::runtime_error("New annotation anchor must resolve");
        anchor.fallback = resolved.point;
    }
    Edit edit{"Create annotation", {}};
    const auto id = value.id;
    edit.annotations.push_back({id, nullptr, std::make_shared<AnnotationRecord>(std::move(value))});
    doc.apply(std::move(edit), doc.revision());
    return id;
}
void updateAnnotation(Document &doc, Id id, AnnotationRecord value) {
    const auto before = requireAnnotation(doc, id);
    if (value.id != id)
        throw std::runtime_error("Annotation update cannot change identity");
    if (value == *before)
        return;
    if (value.anchors != before->anchors)
        for (auto &anchor : value.anchors) {
            const auto resolved = resolveAnnotationAnchor(doc, anchor);
            if (resolved.state != AnchorState::Resolved)
                throw std::runtime_error("Rebound annotation anchor must resolve");
            anchor.fallback = resolved.point;
        }
    Edit edit{"Update annotation", {}};
    edit.annotations.push_back({id, before, std::make_shared<AnnotationRecord>(std::move(value))});
    doc.apply(std::move(edit), doc.revision());
}
void eraseAnnotation(Document &doc, Id id) {
    Edit edit{"Delete annotation", {}};
    edit.annotations.push_back({id, requireAnnotation(doc, id), nullptr});
    doc.apply(std::move(edit), doc.revision());
}
} // namespace sketchy
