#pragma once
#include "core/model.hpp"
namespace sketchy {
Id createSection(Document &doc, std::string name, Id context, SectionPlane plane);
void updateSection(Document &doc, Id id, SectionRecord value);
void eraseSection(Document &doc, Id id);
void setActiveSection(Document &doc, Id context, std::optional<Id> section);
} // namespace sketchy
