#pragma once
#include "core/model.hpp"
namespace sketchy {
Id createAnnotation(Document &doc, AnnotationRecord value);
void updateAnnotation(Document &doc, Id id, AnnotationRecord value);
void eraseAnnotation(Document &doc, Id id);
} // namespace sketchy
