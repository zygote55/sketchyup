#pragma once
#include "core/annotation_records.hpp"
#include <QJsonArray>
#include <QJsonObject>
namespace sketchy {
QJsonArray encodeAnnotations(const AnnotationRecords &records);
AnnotationRecords decodeAnnotations(const QJsonValue &value, Id next);
// The ADR 0080 anchor object, shared by drawing-sheet items (ADR 0164).
QJsonObject encodeAnnotationAnchor(const AnnotationAnchor &anchor);
AnnotationAnchor decodeAnnotationAnchor(const QJsonValue &value);
} // namespace sketchy
