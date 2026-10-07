#pragma once
#include "core/annotation_records.hpp"
#include "presentation/unit_display.hpp"
namespace sketchy {
inline QString annotationText(const AnnotationRecord &record, const AnnotationMeasurement &m,
                              DisplayUnit units) {
    auto text = QString::fromStdString(record.text);
    if (record.kind == AnnotationKind::Distance && m.distance)
        text = (text.isEmpty() ? QString{} : text + " · ") + displayLength(*m.distance, units);
    if (m.state != AnchorState::Resolved)
        text =
            (m.state == AnchorState::Missing ? "[Missing reference] " : "[Ambiguous reference] ") +
            (text.isEmpty() ? QString::fromStdString(record.name) : text);
    return text;
}
} // namespace sketchy
