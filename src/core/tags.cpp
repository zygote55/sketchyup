#include "core/tags.hpp"
namespace sketchy {
std::set<Id> inheritedTags(const Document &doc, Id body) {
    std::set<Id> result;
    for (; body; body = doc.bodies().at(body)->parent)
        if (doc.bodies().at(body)->tag)
            result.insert(doc.bodies().at(body)->tag);
    return result;
}
Id createTag(Document &doc, std::string name, Id parent, bool folder) {
    const auto id = doc.nextTagId();
    if (id == UINT64_MAX)
        throw std::runtime_error("Tag identity space exhausted");
    auto record = std::make_shared<TagRecord>(TagRecord{id, parent, std::move(name), folder, true});
    Edit edit{folder ? "Create tag folder" : "Create tag", {}};
    edit.tags.push_back({id, nullptr, record});
    doc.apply(std::move(edit), doc.revision());
    return id;
}
void editTag(Document &doc, Id id, std::optional<std::string> name, std::optional<Id> parent,
             std::optional<bool> visible) {
    if (!name && !parent && !visible)
        throw std::runtime_error("Tag edit requires a changed field");
    const auto old = doc.tags().at(id);
    auto record = std::make_shared<TagRecord>(*old);
    if (name)
        record->name = *name;
    if (parent)
        record->parent = *parent;
    if (visible)
        record->visible = *visible;
    if (*record == *old)
        return;
    Edit edit{"Edit tag or folder", {}};
    edit.tags.push_back({id, old, record});
    doc.apply(std::move(edit), doc.revision());
}
void eraseTag(Document &doc, Id id) {
    for (const auto &[body, record] : doc.bodies())
        if (record->tag == id)
            throw std::runtime_error("Remove this tag's entity assignments before deleting it");
    for (const auto &[definition, record] : doc.definitions())
        for (const auto &[member, body] : record->members)
            if (body->tag == id)
                throw std::runtime_error("This tag is used in a component definition");
    for (const auto &[child, tag] : doc.tags())
        if (tag->parent == id)
            throw std::runtime_error("Move or delete the folder's children first");
    Edit edit{"Delete tag or folder", {}};
    edit.tags.push_back({id, doc.tags().at(id), nullptr});
    doc.apply(std::move(edit), doc.revision());
}
ChangeReport assignTag(Document &doc, Id id, Id tag) {
    const auto old = doc.bodies().at(id);
    if (old->tag == tag)
        return {};
    auto body = std::make_shared<Body>(*old);
    body->tag = tag;
    return doc.apply({"Assign tag", {{id, old, body}}}, doc.revision());
}
} // namespace sketchy
