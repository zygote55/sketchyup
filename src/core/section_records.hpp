#pragma once
#include "geometry/section.hpp"
#include <map>
#include <memory>
#include <set>
namespace sketchy {
struct SectionRecord {
    Id id{};
    std::string name;
    Id context{}; // Zero is the model frame; other IDs are body editing frames.
    SectionPlane plane;
    bool fill{true}, edges{true};
    std::array<float, 3> color{.85F, .75F, .55F};
    bool operator==(const SectionRecord &) const = default;
};
using SectionPtr = std::shared_ptr<const SectionRecord>;
using SectionRecords = std::map<Id, SectionPtr>;
using ActiveSections = std::map<Id, Id>; // At most one active plane per context.
inline constexpr size_t sectionRecordLimit = 256;
size_t sectionBytes(const SectionPtr &section);
void validateSectionRecords(const SectionRecords &records, Id next, const ActiveSections &active);
class Document;
void validateSectionContext(const Document &doc, const SectionRecord &section);
void validateSectionDepth(const Document &doc);
std::set<Id> missingSectionContexts(const Document &doc);
std::vector<SectionCut> effectiveSectionCuts(const Document &doc, Id body,
                                             const ActiveSections *overrideActive = nullptr);
} // namespace sketchy
