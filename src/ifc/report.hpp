// The worker's part of the fidelity report (ADR 0171 section 4): counts, notices, locations and the
// library's messages. The application adds what only it knows (the document's counts, the planar
// merge, the caps on properties) and bounds the whole report.
#pragma once

#include <map>
#include <set>
#include <string>
#include <vector>

namespace sketchyup::ifc {

struct Location {
  std::string kind;  // notImported, notice, excluded, ...
  int step{};
  std::string globalId;
  std::string message;
};

struct KernelMessage {
  std::string category;  // openingSubtraction, unitsAssumed, convert, ...
  std::string code;      // GEO033, ...
  std::string severity;  // warning, error
  std::string sample;
  long long count{};
};

class Report {
public:
  void notice(const std::string& code, long long count = 1) { notices_[code] += count; }
  void loss(const std::string& key, long long count = 1) { losses_[key] += count; }
  void omitted(const std::string& className, long long count = 1) { omitted_[className] += count; }
  // Stores at most 32 locations per kind; the totals stay complete.
  void location(Location place);
  void count(const std::string& group, const std::string& key, long long amount = 1) { counts_[group][key] += amount; }
  void kernelMessage(const std::string& category, const std::string& code, const std::string& severity,
                     const std::string& sample);
  void unconverted(const std::string& label, long long count = 1) { unconverted_[label] += count; }
  void notImported(const std::string& className, const std::string& reason, long long count = 1) {
    notImported_[{className, reason}] += count;
  }

  const std::map<std::string, long long>& notices() const { return notices_; }
  const std::map<std::string, long long>& losses() const { return losses_; }
  const std::map<std::string, long long>& omittedEntities() const { return omitted_; }
  const std::vector<Location>& locations() const { return locations_; }
  // Every location reported, including those past the per-kind bound that is stored.
  const std::map<std::string, int>& locationTotals() const { return locationsPerKind_; }
  const std::map<std::string, std::map<std::string, long long>>& counts() const { return counts_; }
  const std::vector<KernelMessage>& kernelMessages() const { return kernel_; }
  const std::map<std::string, long long>& unconverted() const { return unconverted_; }
  const std::map<std::pair<std::string, std::string>, long long>& notImportedClasses() const { return notImported_; }

private:
  std::map<std::string, long long> notices_, losses_, omitted_, unconverted_;
  std::map<std::string, std::map<std::string, long long>> counts_;
  std::map<std::pair<std::string, std::string>, long long> notImported_;
  std::vector<Location> locations_;
  std::map<std::string, int> locationsPerKind_;
  std::vector<KernelMessage> kernel_;
};

// Shortens at a UTF-8 code point boundary to at most `bytes` bytes.
std::string clip(const std::string& text, std::size_t bytes);

}  // namespace sketchyup::ifc
