#include "core/tag_records.hpp"
#include <set>
namespace sketchy {
void validateTagRecords(const TagRecords &tags, Id next) {
    if (!next || tags.size() > 1024)
        throw std::runtime_error("Invalid tag allocator or count");
    std::set<std::pair<Id, std::string>> names;
    for (const auto &[id, tag] : tags) {
        if (!tag || !id || id >= next || tag->id != id || tag->name.empty() ||
            tag->name.size() > 1024)
            throw std::runtime_error("Invalid tag identity or name");
        if (!names.insert({tag->parent, tag->name}).second)
            throw std::runtime_error("Tag and folder names must be unique within their folder");
        std::set<Id> path{id};
        for (auto parent = tag->parent; parent; parent = tags.at(parent)->parent) {
            if (!tags.contains(parent) || !tags.at(parent) || !tags.at(parent)->folder)
                throw std::runtime_error("Tag parent must be an existing folder");
            if (!path.insert(parent).second || path.size() > 32)
                throw std::runtime_error("Cyclic or excessively deep tag folder hierarchy");
        }
    }
}
bool tagVisible(const TagRecords &tags, Id tag) {
    size_t depth = 0;
    for (; tag; tag = tags.at(tag)->parent) {
        if (++depth > 32)
            throw std::runtime_error("Invalid tag folder hierarchy");
        if (!tags.at(tag)->visible)
            return false;
    }
    return true;
}
void validateTagAssignments(const TagRecords &tags, const std::map<Id, BodyPtr> &bodies) {
    for (const auto &[id, body] : bodies)
        if (body->tag && (!tags.contains(body->tag) || tags.at(body->tag)->folder))
            throw std::runtime_error("Entity tag must reference a tag, not a folder");
}
} // namespace sketchy
