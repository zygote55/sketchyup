#include "core/source_credits.hpp"

#include "core/body.hpp"
#include "core/terrain_records.hpp"

#include <algorithm>

namespace sketchy {

std::string demSourceNotice(const TerrainDemSource &dem) {
    std::string notice = dem.credit;
    if (!dem.disclaimer.empty()) {
        // The no-liability sentence follows the credit as its own sentence.
        if (!notice.empty() && notice.back() != '.')
            notice += '.';
        notice += notice.empty() ? dem.disclaimer : " " + dem.disclaimer;
        if (notice.back() != '.')
            notice += '.';
    }
    for (auto &c : notice)
        if (static_cast<unsigned char>(c) < 0x20 || c == 0x7f)
            c = ' ';
    return notice;
}

const TerrainDemSource *demSourceOf(const Document &document, Id body) {
    for (Id current = body; current; current = document.bodies().at(current)->parent) {
        const auto &record = *document.bodies().at(current);
        if (record.kind == BodyKind::Terrain && record.terrain && record.terrain->provenance.dem)
            return &*record.terrain->provenance.dem;
    }
    return nullptr;
}

const TerrainDemSource *demSourceOfSite(const Document &document) {
    const auto &site = document.site();
    if (!site || site->established.sha256.empty())
        return nullptr;
    for (const auto &[id, body] : document.bodies())
        if (body->kind == BodyKind::Terrain && body->terrain && body->terrain->provenance.dem &&
            body->terrain->provenance.sha256 == site->established.sha256)
            return &*body->terrain->provenance.dem;
    return nullptr;
}

void SourceCredits::add(const TerrainDemSource &dem) {
    auto notice = demSourceNotice(dem);
    if (std::find(notices_.begin(), notices_.end(), notice) == notices_.end())
        notices_.push_back(std::move(notice));
}

std::string SourceCredits::joined() const {
    std::string text;
    for (const auto &notice : notices_)
        text += text.empty() ? notice : "; " + notice;
    return text;
}

} // namespace sketchy
