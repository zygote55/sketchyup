#include "core/sections.hpp"
namespace sketchy {
namespace {
SectionPtr requireSection(const Document &doc, Id id) {
    const auto found = doc.sections().find(id);
    if (found == doc.sections().end())
        throw std::runtime_error("Section plane does not exist");
    return found->second;
}
} // namespace
Id createSection(Document &doc, std::string name, Id context, SectionPlane plane) {
    const auto id = doc.nextSectionId();
    auto section =
        std::make_shared<SectionRecord>(SectionRecord{id, std::move(name), context, plane});
    validateSectionContext(doc, *section);
    Edit edit{"Create section plane", {}};
    edit.sections.push_back({id, nullptr, section});
    doc.apply(std::move(edit), doc.revision());
    return id;
}
void updateSection(Document &doc, Id id, SectionRecord value) {
    const auto before = requireSection(doc, id);
    if (value.id != id)
        throw std::runtime_error("Section update cannot change identity");
    if (value == *before)
        return;
    if (value.context != before->context)
        validateSectionContext(doc, value);
    Edit edit{"Update section plane", {}};
    edit.sections.push_back({id, before, std::make_shared<SectionRecord>(std::move(value))});
    if (edit.sections.back().after->context != before->context &&
        doc.activeSections().contains(before->context) &&
        doc.activeSections().at(before->context) == id) {
        auto active = doc.activeSections();
        active.erase(before->context);
        edit.activeSections = std::pair{doc.activeSections(), std::move(active)};
    }
    doc.apply(std::move(edit), doc.revision());
}
void eraseSection(Document &doc, Id id) {
    const auto before = requireSection(doc, id);
    Edit edit{"Delete section plane", {}};
    edit.sections.push_back({id, before, nullptr});
    if (doc.activeSections().contains(before->context) &&
        doc.activeSections().at(before->context) == id) {
        auto active = doc.activeSections();
        active.erase(before->context);
        edit.activeSections = std::pair{doc.activeSections(), std::move(active)};
    }
    doc.apply(std::move(edit), doc.revision());
}
void setActiveSection(Document &doc, Id context, std::optional<Id> section) {
    auto active = doc.activeSections();
    if (section) {
        const auto record = requireSection(doc, *section);
        if (record->context != context)
            throw std::runtime_error("Section plane belongs to another context");
        validateSectionContext(doc, *record);
        active[context] = *section;
    } else
        active.erase(context);
    if (active == doc.activeSections())
        return;
    Edit edit{"Activate section plane", {}};
    edit.activeSections = std::pair{doc.activeSections(), std::move(active)};
    doc.apply(std::move(edit), doc.revision());
}
} // namespace sketchy
