#include "report.hpp"

namespace sketchyup::skp {

std::string clip(const std::string& text, std::size_t bytes) {
  if (text.size() <= bytes) return text;
  std::size_t end = bytes;
  // Back up to the start of a code point: a continuation byte is 10xxxxxx.
  while (end > 0 && (static_cast<unsigned char>(text[end]) & 0xC0) == 0x80) --end;
  return text.substr(0, end);
}

void SkpReport::locate(const std::string& kind, const std::string& definition, const std::string& message) {
  int& total = locationsPerKind_[kind];
  ++total;
  if (total > 32) return;
  locations_.push_back({kind, clip(definition, 64), clip(message, 256)});
}

}  // namespace sketchyup::skp
