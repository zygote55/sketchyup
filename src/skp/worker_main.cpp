// sketchyup-skp-worker: the only executable that links the OpenSKP parser (ADR 0176 decision 2).
// R102.a ships the .skp reader: --probe and --import (the SKSKPP01 conversion package).
// It takes a fixed argv, reads no environment-dependent configuration and runs no shell.
//
// The helper enforces its own limits (the IFC helper takes them from the application):
// RLIMIT_AS 4 GiB (absent in the sanitizer build, where the 4 GiB ceiling would collide with the
// sanitizer's reservations), RLIMIT_CPU 480 s and RLIMIT_FSIZE 512 MiB, plus a 120 s deadline.
// The --rlimit-* and --deadline-seconds options override the limits for the tests.
#include <sys/prctl.h>
#include <sys/resource.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <iostream>
#include <string>

#include "import.hpp"
#include "sketchyup/version.hpp"

namespace {

void usage(std::FILE* stream) {
  std::fputs(
      "usage: sketchyup-skp-worker --version\n"
      "       sketchyup-skp-worker --probe FILE.skp\n"
      "           read a SketchUp file and print its version, container, units and counts as JSON\n"
      "       sketchyup-skp-worker --import FILE.skp --output PACKAGE [--options]\n"
      "           parse a SketchUp file and write the SKSKPP01 conversion package (ADR 0176);\n"
      "           options:\n"
      "           --size-limit-bytes N   the input size ceiling (default 256 MiB)\n"
      "           --deadline-seconds N   the wall-clock deadline (default 120)\n"
      "           --rlimit-as BYTES      override the 4 GiB address limit (0 disables it)\n"
      "           --rlimit-cpu SECONDS   override the 480 s CPU limit\n"
      "           --rlimit-fsize BYTES   override the 512 MiB output limit\n"
      "           --dry-run              parse and count, write no package\n",
      stream);
}

int fail(const std::string& message) {
  std::fprintf(stderr, "sketchyup-skp-worker: %s\n", message.c_str());
  usage(stderr);
  return 2;
}

// The helper's own limits. The IFC helper takes them from the application, but this helper runs
// under the application's own supervisor and sets them itself (ADR 0176 section 1.6).
void applyDefaults(sketchyup::skp::SkpImportOptions& options) {
  if (options.rlimitAs == 0) {
#ifdef SKP_WORKER_NO_ADDRESS_LIMIT
    options.rlimitAs = 0;  // the sanitizer build: the 4 GiB ceiling would collide with its reservations
#else
    options.rlimitAs = 4ull * 1024 * 1024 * 1024;
#endif
  }
  if (options.rlimitCpu == 0) options.rlimitCpu = 480;
  if (options.rlimitFsize == 0) options.rlimitFsize = 512 * 1024 * 1024;
}

}  // namespace

int main(int argc, char** argv) {
  using namespace sketchyup::skp;
  try {
    if (argc == 2 && std::strcmp(argv[1], "--version") == 0) {
      std::printf("sketchyup-skp-worker %s\n", SKETCHYUP_VERSION);
      return 0;
    }
    if (argc == 3 && std::strcmp(argv[1], "--probe") == 0) {
      Progress progress;
      return runProbe(argv[2], progress);
    }
    if (argc >= 2 && std::strcmp(argv[1], "--import") == 0) {
      SkpImportOptions options;
      for (int i = 2; i < argc; ++i) {
        const std::string argument = argv[i];
        const bool hasValue = i + 1 < argc;
        if (argument == "--output" && hasValue) options.output = argv[++i];
        else if (argument == "--size-limit-bytes" && hasValue) options.sizeLimitBytes = std::stoull(argv[++i]);
        else if (argument == "--deadline-seconds" && hasValue) options.deadlineSeconds = std::stoi(argv[++i]);
        else if (argument == "--rlimit-as" && hasValue) options.rlimitAs = std::stoull(argv[++i]);
        else if (argument == "--rlimit-cpu" && hasValue) options.rlimitCpu = std::stoi(argv[++i]);
        else if (argument == "--rlimit-fsize" && hasValue) options.rlimitFsize = std::stoull(argv[++i]);
        else if (argument == "--dry-run") options.dryRun = true;
        else if (argument.rfind("--", 0) == 0) return fail("unknown option " + argument);
        else if (options.input.empty()) options.input = argument;
        else return fail("one input file only");
      }
      if (options.input.empty() || (options.output.empty() && !options.dryRun))
        return fail("--import needs an input file and --output");
      prctl(PR_SET_NO_NEW_PRIVS, 1, 0, 0, 0);
      applyDefaults(options);
      Progress progress;
      return runImport(options, progress);
    }
    usage(stderr);
    return 2;
  } catch (const std::exception& problem) {
    std::fprintf(stderr, "sketchyup-skp-worker: %s\n", problem.what());
    return 1;
  }
}
