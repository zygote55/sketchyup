#include "core/annotation_records.hpp"
#include "core/model.hpp"
#include <algorithm>
#include <set>
namespace sketchy {
size_t annotationBytes(const AnnotationPtr &record) {
    return record ? sizeof(AnnotationRecord) + record->name.size() + record->text.size() +
                        record->anchors.size() * sizeof(AnnotationAnchor) + 64
                  : 0;
}
void validateAnnotationRecords(const AnnotationRecords &records, Id next) {
    if (!next || records.size() > annotationRecordLimit)
        throw std::runtime_error("Annotation records exceed count or allocator bounds");
    std::set<std::string> names;
    for (const auto &[id, record] : records) {
        if (!record || !id || id >= next || record->id != id)
            throw std::runtime_error("Invalid annotation identity or allocator");
        const auto &name = record->name;
        if (name.empty() || name.size() > 1024 || name.front() == ' ' || name.back() == ' ' ||
            std::any_of(name.begin(), name.end(),
                        [](unsigned char c) { return c < 32 || c == 127; }) ||
            !names.insert(name).second)
            throw std::runtime_error("Annotation names must be bounded, trimmed and unique");
        if (record->kind != AnnotationKind::Distance && record->kind != AnnotationKind::Label)
            throw std::runtime_error("Invalid annotation kind");
        if (record->anchors.size() != (record->kind == AnnotationKind::Distance ? 2u : 1u))
            throw std::runtime_error("Annotation has the wrong number of anchors");
        for (const auto &anchor : record->anchors)
            validateAnnotationAnchor(anchor);
        checkPoint(record->offset);
        if (record->text.size() > 4096 ||
            (record->kind == AnnotationKind::Label && record->text.empty()) ||
            std::any_of(record->text.begin(), record->text.end(), [](unsigned char c) {
                return (c < 32 && c != '\n' && c != '\t') || c == 127;
            }))
            throw std::runtime_error("Annotation text exceeds content or size bounds");
        if (!std::isfinite(record->textSize) || record->textSize < 8 || record->textSize > 48)
            throw std::runtime_error("Annotation text size must be in [8,48] logical pixels");
        for (auto channel : record->color)
            if (!std::isfinite(channel) || channel < 0 || channel > 1)
                throw std::runtime_error("Annotation color must be finite RGB in [0,1]");
    }
}
AnnotationMeasurement measureAnnotation(const Document &doc, const AnnotationRecord &record) {
    AnnotationMeasurement result;
    Vec3 center;
    for (const auto &anchor : record.anchors) {
        auto resolved = resolveAnnotationAnchor(doc, anchor);
        result.anchors.push_back(resolved);
        center = center + resolved.point;
        if (resolved.state == AnchorState::Missing)
            result.state = AnchorState::Missing;
        else if (resolved.state == AnchorState::Ambiguous && result.state == AnchorState::Resolved)
            result.state = AnchorState::Ambiguous;
    }
    if (record.anchors.empty())
        throw std::runtime_error("Annotation has no anchors");
    result.textPoint = center * (1. / record.anchors.size()) + record.offset;
    // Two bounded world values may place the text beyond the geometry bounds.
    if (record.kind == AnnotationKind::Distance && result.state == AnchorState::Resolved) {
        if (result.anchors.size() != 2)
            throw std::runtime_error("Distance annotation needs two anchors");
        result.distance = length(result.anchors[1].point - result.anchors[0].point);
    }
    return result;
}
void validateAnnotationCapture(const Document &doc, const AnnotationRecord &record) {
    if (measureAnnotation(doc, record).state != AnchorState::Resolved)
        throw std::runtime_error("New annotation attachment must resolve to current geometry");
}
} // namespace sketchy

namespace sketchy {
void resolveAnnotationEdit(const Document &before, const Document &after,
                           const std::map<Id, TopologyChanges> &changes, Edit &edit,
                           AnnotationRecords &records) {
    if (edit.annotationsResolved)
        return;
    for (auto &[id, pointer] : records) {
        const auto old = before.annotations().find(id);
        if (old == before.annotations().end() || old->second->anchors != pointer->anchors) {
            validateAnnotationCapture(after, *pointer);
            continue;
        }
        if (changes.empty())
            continue; // Style, units and metadata cannot move geometric support.
        auto updated = *pointer;
        for (auto &anchor : updated.anchors)
            anchor = remapAnnotationAnchor(before, after, changes, anchor);
        if (updated == *pointer)
            continue;
        pointer = std::make_shared<AnnotationRecord>(std::move(updated));
        const auto authored = std::find_if(edit.annotations.begin(), edit.annotations.end(),
                                           [&](const auto &change) { return change.id == id; });
        if (authored != edit.annotations.end())
            authored->after = pointer;
        else
            edit.annotations.push_back({id, old->second, pointer});
    }
}
} // namespace sketchy
