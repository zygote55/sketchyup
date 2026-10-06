#include "core/section_records.hpp"
#include "core/model.hpp"
#include <algorithm>
namespace sketchy {
size_t sectionBytes(const SectionPtr &section) {
    return section ? sizeof(SectionRecord) + section->name.size() + 64 : 0;
}
void validateSectionRecords(const SectionRecords &records, Id next, const ActiveSections &active) {
    if (!next || records.size() > sectionRecordLimit || active.size() > sectionRecordLimit)
        throw std::runtime_error("Section records exceed count or allocator bounds");
    std::set<std::pair<Id, std::string>> names;
    for (const auto &[id, record] : records) {
        if (!record || !id || id >= next || record->id != id)
            throw std::runtime_error("Invalid section identity or allocator");
        const auto &name = record->name;
        if (name.empty() || name.size() > 1024 || name.front() == ' ' || name.back() == ' ' ||
            std::any_of(name.begin(), name.end(),
                        [](unsigned char c) { return c < 32 || c == 127; }) ||
            !names.emplace(record->context, name).second)
            throw std::runtime_error(
                "Section names must be bounded, trimmed and unique in context");
        record->plane.validate();
        for (float channel : record->color)
            if (!std::isfinite(channel) || channel < 0 || channel > 1)
                throw std::runtime_error("Section fill color must be finite RGB in [0,1]");
    }
    for (const auto &[context, plane] : active)
        if (!records.contains(plane) || records.at(plane)->context != context)
            throw std::runtime_error("Active section must belong to its context");
}
void validateSectionContext(const Document &doc, const SectionRecord &section) {
    if (section.context && !doc.bodies().contains(section.context))
        throw std::runtime_error("New or relocated section requires an existing context");
}
namespace {
std::vector<Id> ancestors(const Document &doc, Id body) {
    std::vector<Id> path;
    std::set<Id> seen;
    for (Id id = body; id; id = doc.bodies().at(id)->parent) {
        if (!doc.bodies().contains(id) || !seen.insert(id).second)
            throw std::runtime_error("Invalid section context ancestry");
        path.push_back(id);
    }
    path.push_back(0);
    std::reverse(path.begin(), path.end());
    return path;
}
} // namespace
void validateSectionDepth(const Document &doc, const ActiveSections *overrideActive) {
    const auto &active = overrideActive ? *overrideActive : doc.activeSections();
    if (active.size() <= sectionPlaneLimit)
        return;
    for (const auto &[body, record] : doc.bodies()) {
        size_t count{};
        for (Id context : ancestors(doc, body))
            count += active.contains(context);
        if (count > sectionPlaneLimit)
            throw std::runtime_error("A context path may have at most eight active sections");
    }
}
std::set<Id> missingSectionContexts(const Document &doc) {
    std::set<Id> result;
    for (const auto &[id, section] : doc.sections())
        if (section->context && !doc.bodies().contains(section->context))
            result.insert(id);
    return result;
}
std::vector<SectionCut> effectiveSectionCuts(const Document &doc, Id body,
                                             const ActiveSections *overrideActive) {
    const auto &active = overrideActive ? *overrideActive : doc.activeSections();
    // Published document records/activation are already validated and immutable.
    // Only a caller-supplied view override needs validation on this read path.
    if (overrideActive)
        validateSectionRecords(doc.sections(), doc.nextSectionId(), active);
    std::vector<SectionCut> result;
    for (Id context : ancestors(doc, body)) {
        const auto found = active.find(context);
        if (found == active.end())
            continue;
        const auto &section = *doc.sections().at(found->second);
        result.push_back({section.id, context
                                          ? section.plane.transformed(doc.worldTransform(context))
                                          : section.plane});
    }
    if (result.size() > sectionPlaneLimit)
        throw std::runtime_error("Section view exceeds eight active planes");
    return result;
}
} // namespace sketchy
