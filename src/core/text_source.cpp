#include "core/text_source.hpp"
#include <algorithm>
#include <cmath>
#include <set>
#include <stdexcept>
namespace sketchy {
namespace {
void require(bool value, const char *message) {
    if (!value)
        throw std::runtime_error(message);
}
void string(const std::string &value, size_t limit, bool multiline = false) {
    require(value.size() <= limit, "Text source string exceeds byte limit");
    size_t lines = 1;
    for (size_t i = 0; i < value.size();) {
        const auto first = static_cast<unsigned char>(value[i++]);
        unsigned code = first, extra = 0, minimum = 0;
        if (first >= 0xc2 && first <= 0xdf) {
            code = first & 31;
            extra = 1;
            minimum = 0x80;
        } else if (first >= 0xe0 && first <= 0xef) {
            code = first & 15;
            extra = 2;
            minimum = 0x800;
        } else if (first >= 0xf0 && first <= 0xf4) {
            code = first & 7;
            extra = 3;
            minimum = 0x10000;
        } else
            require(first < 0x80, "Text source contains invalid UTF-8");
        require(i + extra <= value.size(), "Text source contains truncated UTF-8");
        while (extra--) {
            const auto next = static_cast<unsigned char>(value[i++]);
            require((next & 0xc0) == 0x80, "Text source contains invalid UTF-8");
            code = (code << 6) | (next & 63);
        }
        require(code >= minimum && code <= 0x10ffff && !(code >= 0xd800 && code <= 0xdfff),
                "Text source contains invalid Unicode");
        require(code >= 32 || (multiline && (code == '\n' || code == '\t')),
                "Text source contains a control character");
        require(code != 127, "Text source contains a control character");
        if (code == '\n')
            ++lines;
    }
    require(lines <= 128, "Text source exceeds line limit");
}
void digest(const std::string &value) {
    require(
        value.size() == 64 &&
            std::all_of(value.begin(), value.end(),
                        [](char c) { return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'); }),
        "Text source requires a SHA-256 fingerprint");
}
} // namespace
void validateTextSource(const TextSource &s) {
    string(s.text, 4096, true);
    string(s.family, 256);
    string(s.style, 256);
    string(s.actualFamily, 256);
    string(s.actualStyle, 256);
    require(!s.text.empty() && !s.family.empty() && !s.actualFamily.empty(),
            "Text source requires content and font families");
    require(std::isfinite(s.height) && s.height >= .001 && s.height <= 1000 &&
                std::isfinite(s.depth) && (s.depth == 0 || (s.depth >= 1e-6 && s.depth <= 1000)) &&
                std::isfinite(s.lineSpacing) && s.lineSpacing >= .5 && s.lineSpacing <= 10,
            "Text source dimensions exceed supported bounds");
    require(!s.substituted || s.allowSubstitution, "Text substitution was not authorized");
    require(!s.fonts.empty() && s.fonts.size() <= 64 && s.regions > 0 && s.regions <= 1024,
            "Text source exceeds font or region bounds");
    size_t glyphs{};
    std::set<std::pair<std::string, std::string>> faces;
    for (const auto &font : s.fonts) {
        string(font.family, 256);
        string(font.style, 256);
        digest(font.fingerprint);
        require(!font.family.empty() && font.glyphs > 0 && font.glyphs <= 1024 &&
                    faces.emplace(font.family, font.style).second,
                "Invalid or duplicate text font provenance");
        glyphs += font.glyphs;
    }
    require(glyphs <= 1024, "Text source exceeds glyph limit");
    digest(s.geometryDigest);
}
size_t textSourceBytes(const TextSource &s) {
    size_t n = s.text.size() + s.family.size() + s.style.size() + s.actualFamily.size() +
               s.actualStyle.size() + s.geometryDigest.size() +
               s.fonts.size() * sizeof(TextSourceFont);
    for (const auto &font : s.fonts)
        n += font.family.size() + font.style.size() + font.fingerprint.size();
    return n;
}
} // namespace sketchy
