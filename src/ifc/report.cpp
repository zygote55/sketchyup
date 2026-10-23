#include "ifc/report.hpp"

#include <algorithm>

namespace sketchyup::ifc {

std::string clip(const std::string& text, std::size_t bytes) {
  if (text.size() <= bytes) return text;
  std::size_t end = bytes;
  // Back up to the start of a code point: a continuation byte is 10xxxxxx.
  while (end > 0 && (static_cast<unsigned char>(text[end]) & 0xC0) == 0x80) --end;
  return text.substr(0, end);
}

void Report::location(Location place) {
  int& total = locationsPerKind_[place.kind];
  ++total;
  if (total > 32) return;
  place.message = clip(place.message, 200);
  place.globalId = clip(place.globalId, 64);
  locations_.push_back(std::move(place));
}

void Report::kernelMessage(const std::string& category, const std::string& code, const std::string& severity,
                           const std::string& sample) {
  for (auto& entry : kernel_)
    if (entry.code == code && entry.severity == severity) {
      ++entry.count;
      return;
    }
  if (kernel_.size() >= 64) return;
  kernel_.push_back({category, code, severity, clip(sample, 200), 1});
}

}  // namespace sketchyup::ifc
