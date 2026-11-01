#include "core/document_limits.hpp"
#include <array>
#include <stdexcept>
#include <utility>
namespace sketchy {
namespace {
using Field = std::size_t LimitCounts::*;
constexpr std::array<std::pair<std::string_view, Field>, 8> fields{{
    {"placements", &LimitCounts::placements},
    {"bodies", &LimitCounts::bodies},
    {"vertices", &LimitCounts::vertices},
    {"faces", &LimitCounts::faces},
    {"wires", &LimitCounts::wires},
    {"edges", &LimitCounts::edges},
    {"curves", &LimitCounts::curves},
    {"guides", &LimitCounts::guides},
}};
const LimitCounts &maximum(LimitClass limits) {
    switch (limits) {
    case LimitClass::UniqueGeometry:
        return DocumentLimits::unique;
    case LimitClass::ComponentDefinition:
        return DocumentLimits::definition;
    case LimitClass::Placement:
        break;
    }
    return DocumentLimits::materialized;
}

// Placements are counted by the placement class only.
bool countedBy(LimitClass limits, Field field) {
    return !(limits != LimitClass::Placement && field == &LimitCounts::placements);
}
// Locale-independent thousands separators.
std::string grouped(std::size_t value) {
    auto digits = std::to_string(value);
    std::string result;
    for (std::size_t i = 0; i < digits.size(); ++i) {
        if (i && (digits.size() - i) % 3 == 0)
            result += ',';
        result += digits[i];
    }
    return result;
}
// Never split a UTF-8 sequence: back off over continuation bytes.
std::string utf8Prefix(std::string_view text, std::size_t bytes) {
    if (text.size() <= bytes)
        return std::string(text);
    while (bytes && (static_cast<unsigned char>(text[bytes]) & 0xC0) == 0x80)
        --bytes;
    return std::string(text.substr(0, bytes));
}
} // namespace
std::string_view exceededLimit(const LimitCounts &attempted, LimitClass limits) {
    const auto &max = maximum(limits);
    for (const auto &[name, field] : fields) {
        if (!countedBy(limits, field))
            continue;
        if (attempted.*field > max.*field)
            return name;
    }
    return {};
}
void checkLimits(const LimitCounts &attempted, LimitClass limits, std::string_view operation,
                 std::string_view context) {
    const auto exceeded = exceededLimit(attempted, limits);
    if (exceeded.empty())
        return;
    const auto &max = maximum(limits);
    Field field{};
    for (const auto &[name, candidate] : fields)
        if (name == exceeded)
            field = candidate;
    std::string message(operation);
    switch (limits) {
    case LimitClass::UniqueGeometry:
        message += " exceeds document unique geometry limits: ";
        break;
    case LimitClass::Placement:
        message += " exceeds document placement limits: ";
        break;
    case LimitClass::ComponentDefinition:
        message += " exceeds component definition limits: ";
        break;
    }
    message += exceeded;
    message += " would be " + grouped(attempted.*field) + " of " + grouped(max.*field);
    if (!context.empty()) {
        message += " (";
        message += context;
        message += ')';
    }
    throw std::runtime_error(utf8Prefix(message, limitMessageBytes));
}
std::string groupedCount(std::size_t value) { return grouped(value); }
void checkTerrainLimit(std::size_t triangles, std::string_view operation) {
    if (triangles <= terrainTriangleLimit)
        return;
    std::string message(operation);
    message += " exceeds the document terrain limit: terrain triangles would be " +
               grouped(triangles) + " of " + grouped(terrainTriangleLimit);
    throw std::runtime_error(utf8Prefix(message, limitMessageBytes));
}
std::string boundedName(std::string_view name) {
    std::string clean;
    clean.reserve(name.size());
    for (const char c : name)
        clean += static_cast<unsigned char>(c) < 0x20 || c == 0x7F ? '?' : c;
    constexpr std::string_view ellipsis = "\xE2\x80\xA6";
    if (clean.size() > limitNameBytes)
        clean = utf8Prefix(clean, limitNameBytes - ellipsis.size()) + std::string(ellipsis);
    return clean;
}
std::string quotedLimitName(std::string_view name) { return "'" + boundedName(name) + "'"; }
std::string definitionLimitContext(std::string_view name, std::size_t id) {
    return "definition " + quotedLimitName(name) + " #" + std::to_string(id);
}
} // namespace sketchy
