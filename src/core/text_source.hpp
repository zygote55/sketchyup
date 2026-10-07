#pragma once
#include <cstddef>
#include <string>
#include <vector>
namespace sketchy {
struct TextSourceFont {
    std::string family, style, fingerprint;
    size_t glyphs{};
    bool operator==(const TextSourceFont &) const = default;
};
// Cached geometry stays authoritative when fonts are unavailable. Regeneration
// explicitly uses these source settings and reports changes in font provenance.
struct TextSource {
    std::string text, family, style;
    double height{.1}, depth{}, lineSpacing{1.2};
    bool allowSubstitution{};
    std::string actualFamily, actualStyle;
    bool substituted{}, fallback{};
    std::vector<TextSourceFont> fonts;
    std::string geometryDigest;
    size_t regions{};
    bool operator==(const TextSource &) const = default;
};
void validateTextSource(const TextSource &source);
size_t textSourceBytes(const TextSource &source);
} // namespace sketchy
