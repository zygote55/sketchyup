// The worker's part of the fidelity report (ADR 0176 section 4): the shape is the IFC report's
// (apiVersion 1, at most 64 KiB, messages clipped to 256 bytes, at most 32 sample locations per
// kind), with the .skp fields: the format version and container, the source SHA-256, the model
// units, the counts, the notices and the plain-language lines.
#pragma once

#include "package.hpp"

#include <map>
#include <string>
#include <vector>

namespace sketchyup::skp {
// The report's counts are the package's counts, so the worker copies them through as-is.
using SkpCounts = SkpPackageCounts;
using SkpLocation = SkpPackageLocation;

class SkpReport {
public:
  void notice(const std::string& code, long long count = 1) { notices_[code] += count; }
  // An entity the conversion left out; the report's identity is imported + omitted = counted.
  void omitted(const std::string& code, long long count = 1) { omitted_[code] += count; }
  // Keeps at most 32 locations per kind; the totals stay complete.
  void locate(const std::string& kind, const std::string& definition, const std::string& message);

  SkpCounts counts_;
  std::map<std::string, long long> notices_;
  std::map<std::string, long long> omitted_;
  std::vector<SkpLocation> locations_;
  std::map<std::string, int> locationsPerKind_;
};

// Shortens at a UTF-8 code point boundary to at most `bytes` bytes (the IFC helper's clip).
std::string clip(const std::string& text, std::size_t bytes);

}  // namespace sketchyup::skp
