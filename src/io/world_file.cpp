#include "io/world_file.hpp"

#include "io/image_refusal.hpp"

#include <algorithm>
#include <cctype>
#include <charconv>
#include <cmath>

namespace sketchy {
namespace {

constexpr std::size_t worldFileBytes = 1024;

std::string_view trimmed(std::string_view text) {
    const auto isSpace = [](unsigned char c) { return std::isspace(c) != 0; };
    while (!text.empty() && isSpace(static_cast<unsigned char>(text.front())))
        text.remove_prefix(1);
    while (!text.empty() && isSpace(static_cast<unsigned char>(text.back())))
        text.remove_suffix(1);
    return text;
}

// A decimal number with a point as its separator, read without the locale. Anything else is refused.
double number(std::string_view text) {
    auto token = trimmed(text);
    if (token.size() > 1 && token.front() == '+' && token[1] != '-')
        token.remove_prefix(1); // from_chars takes no plus sign; a plus sign is a valid decimal
    double value{};
    const auto [end, error] = std::from_chars(token.data(), token.data() + token.size(), value);
    if (error != std::errc{} || end != token.data() + token.size() || !std::isfinite(value))
        throw ImageRefusal("world-file");
    return value;
}

std::string lower(std::string_view text) {
    std::string out(text);
    std::transform(out.begin(), out.end(), out.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return out;
}

} // namespace

WorldFile readWorldFile(std::string_view text) {
    if (text.size() > worldFileBytes)
        throw ImageRefusal("world-file");
    std::vector<std::string_view> lines;
    std::size_t start = 0;
    while (start <= text.size()) {
        const auto end = std::min(text.find('\n', start), text.size());
        auto line = text.substr(start, end - start);
        if (!line.empty() && line.back() == '\r')
            line.remove_suffix(1);
        lines.push_back(line);
        start = end + 1;
    }
    // A final newline ends the last line; it does not start another.
    if (!lines.empty() && lines.back().empty())
        lines.pop_back();
    if (lines.size() != 6)
        throw ImageRefusal("world-file");
    std::vector<double> values;
    for (const auto line : lines) {
        if (trimmed(line).empty())
            throw ImageRefusal("world-file");
        values.push_back(number(line));
    }
    WorldFile file{values[0], values[1], values[2], values[3], values[4], values[5]};
    if (file.b != 0 || file.d != 0)
        throw ImageRefusal("world-rotated");
    if (file.a <= 0 || file.e >= 0)
        throw ImageRefusal("world-flipped");
    return file;
}

NorthUpPlacement northUpPlacement(const WorldFile &file) {
    return NorthUpPlacement{file.c - file.a / 2, file.f - file.e / 2, file.a, -file.e};
}

std::vector<std::string> worldFileSuffixes(std::string_view imageFileName) {
    const auto dot = imageFileName.rfind('.');
    if (dot == std::string_view::npos)
        return {};
    const auto extension = lower(imageFileName.substr(dot + 1));
    if (extension == "png")
        return {"pngw", "pgw", "wld"};
    if (extension == "jpg" || extension == "jpeg")
        return {"jpgw", "jgw", "wld"};
    return {};
}

} // namespace sketchy
