#pragma once
#include "core/annotation_records.hpp"
#include <QJsonArray>
namespace sketchy {
QJsonArray encodeAnnotations(const AnnotationRecords &records);
AnnotationRecords decodeAnnotations(const QJsonValue &value, Id next);
} // namespace sketchy
