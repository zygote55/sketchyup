#pragma once
#include "core/body.hpp"
namespace sketchy {
struct TagRecord {
    Id id{}, parent{};
    std::string name;
    bool folder{}, visible{true};
    bool operator==(const TagRecord &) const = default;
};
using TagPtr = std::shared_ptr<const TagRecord>;
using TagRecords = std::map<Id, TagPtr>;
void validateTagRecords(const TagRecords &tags, Id next);
bool tagVisible(const TagRecords &tags, Id tag);
void validateTagAssignments(const TagRecords &tags, const std::map<Id, BodyPtr> &bodies);
} // namespace sketchy
