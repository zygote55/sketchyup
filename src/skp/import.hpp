#pragma once
// The import (ADR 0176): read the file, check the header, parse it with the vendored OpenSKP
// parser, and write the SKSKPP01 package. The helper enforces its own limits (RLIMIT_AS 4 GiB,
// absent in the sanitizer build, RLIMIT_CPU 480 s, RLIMIT_FSIZE 512 MiB) and a 120 s deadline;
// it prints "P ..." progress lines and one final "R {json}" line, and exits 0 for a package
// written and 3 for a refusal (also reported on stdout).
#include "package.hpp"
#include "progress.hpp"

#include <string>

namespace sketchyup::skp {

struct SkpImportOptions {
  std::string input;
  std::string output;
  std::uint64_t sizeLimitBytes{2048 * 1024 * 1024};  // the --size-limit-bytes test override
  int deadlineSeconds{120};
  std::uint64_t rlimitAs{};  // 0 = leave the address-space limit alone
  int rlimitCpu{480};
  std::uint64_t rlimitFsize{512 * 1024 * 1024};
  bool dryRun{};  // parse and count, write no package
};

// Returns 0 for a package written (or a dry run completed), 3 for a refusal.
int runImport(const SkpImportOptions& options, Progress& progress);

// Reads and parses the file and prints one probe line:
//   {"version":"...","container":"vff","units":"Inch","counts":{...}}
// Returns 0, or 3 when the file cannot be read or parsed (refusal on stdout either way).
int runProbe(const std::string& input, Progress& progress);

// The model's unit-system string to metres per raw unit.
double unitScale(const std::string& units);

}  // namespace sketchyup::skp
