#pragma once
#include "core/annotation_anchors.hpp"
#include <memory>
#include <optional>
#include <string>
namespace sketchy {
enum class AnnotationKind { Distance, Label };
struct AnnotationRecord {
    Id id{};
    std::string name;
    AnnotationKind kind{AnnotationKind::Distance};
    std::vector<AnnotationAnchor> anchors;
    Vec3 offset{};    // Model-world metres from the anchor/midpoint to the text.
    std::string text; // Label text, or optional prefix for a measured distance.
    bool leader{true};
    double textSize{12}; // Logical pixels; independent of model and display scale.
    std::array<float, 3> color{.15F, .15F, .15F};
    bool operator==(const AnnotationRecord &) const = default;
};
using AnnotationPtr = std::shared_ptr<const AnnotationRecord>;
using AnnotationRecords = std::map<Id, AnnotationPtr>;
inline constexpr size_t annotationRecordLimit = 4096;
size_t annotationBytes(const AnnotationPtr &record);
void validateAnnotationRecords(const AnnotationRecords &records, Id next);
struct AnnotationMeasurement {
    std::vector<ResolvedAnchor> anchors;
    Vec3 textPoint;
    std::optional<double> distance;
    AnchorState state{AnchorState::Resolved};
};
AnnotationMeasurement measureAnnotation(const Document &doc, const AnnotationRecord &record);
void validateAnnotationCapture(const Document &doc, const AnnotationRecord &record);
struct Edit;
void resolveAnnotationEdit(const Document &before, const Document &after,
                           const std::map<Id, TopologyChanges> &changes, Edit &edit,
                           AnnotationRecords &records);
} // namespace sketchy
