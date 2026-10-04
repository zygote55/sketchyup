#pragma once
#include "core/model.hpp"
#include <set>
namespace sketchy {
Id createTag(Document &doc, std::string name, Id parent = 0, bool folder = false);
void editTag(Document &doc, Id id, std::optional<std::string> name = {},
             std::optional<Id> parent = {}, std::optional<bool> visible = {});
void eraseTag(Document &doc, Id id);
ChangeReport assignTag(Document &doc, Id body, Id tag);
std::set<Id> inheritedTags(const Document &doc, Id body);
} // namespace sketchy
