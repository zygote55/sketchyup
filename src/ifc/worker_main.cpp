// sketchyup-ifc-worker: the only executable that links IfcOpenShell (ADR 0171 decision 19).
// R097.a ships the skeleton: --probe and the API probes. Import and export arrive in R097.b to d.
// It takes a fixed argv, reads no environment-dependent configuration and runs no shell.
#include <sys/prctl.h>
#include <sys/resource.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <iostream>
#include <string>

#include "ifc/check.hpp"
#include "ifc/export.hpp"
#include "ifc/import.hpp"
#include "ifc/probe.hpp"
#include "sketchyup/version.hpp"

namespace {

void usage(std::FILE* stream) {
  std::fputs(
      "usage: sketchyup-ifc-worker --version\n"
      "       sketchyup-ifc-worker --import FILE.ifc --output PACKAGE [options]\n"
      "           read an IFC file and write an IFC conversion package (R097.b); options:\n"
      "           --kernel NAME  --threads N  --deflection METRES  --angular-deflection RADIANS\n"
      "           --length-unit mm|cm|m|in|ft  --skip-spaces  --georef keep|grid  --max-voids N  --max-nodes N  --max-entities N\n"
      "           --exclude-step N  --diagnose-from TASK  --diagnose-seconds N  --rlimit-as BYTES  --rlimit-cpu SECONDS  --rlimit-fsize BYTES\n"
      "           --test-fault crash|abort|hang|alloc:N  --dry-run\n"
      "       sketchyup-ifc-worker --export PACKAGE --output FILE.ifc [--rlimit-as BYTES] [--rlimit-cpu SECONDS] [--rlimit-fsize BYTES]\n"
      "           build an IFC4 file from the application's export package (R097.d)\n"
      "       sketchyup-ifc-worker --check FILE.ifc [--rlimit-as BYTES] [--rlimit-cpu SECONDS]\n"
      "           re-read an IFC4 file and check every attribute against the schema (R097.d)\n"
      "       sketchyup-ifc-worker --probe\n"
      "           print versions, loaded libraries and kernel availability as JSON\n"
      "       sketchyup-ifc-worker --probe-api [--kernel NAME] [--threads N] [--set NAME=VALUE]...\n"
      "                            [--exclude-step N]... FILE.ifc\n"
      "           run FILE through the geometry iterator and print what the library returns\n"
      "       sketchyup-ifc-worker --probe-write FILE.ifc\n"
      "           write a small IFC4 file with the typed writer, read it back and print the result\n",
      stream);
}

bool setLimit(int resource, const char* text) {
  char* end = nullptr;
  const unsigned long long value = std::strtoull(text, &end, 10);
  if (end == text || *end != '\0') return false;
  const rlimit limit{static_cast<rlim_t>(value), static_cast<rlim_t>(value)};
  return setrlimit(resource, &limit) == 0;
}

int fail(const std::string& message) {
  std::fprintf(stderr, "sketchyup-ifc-worker: %s\n", message.c_str());
  usage(stderr);
  return 2;
}

}  // namespace

int main(int argc, char** argv) {
  using namespace sketchyup::ifc;
  try {
    pinPluginDirectory();
    if (argc == 2 && std::strcmp(argv[1], "--version") == 0) {
      std::printf("sketchyup-ifc-worker %s\n", SKETCHYUP_VERSION);
      return 0;
    }
    if (argc == 2 && std::strcmp(argv[1], "--probe") == 0) {
      std::puts(probeEnvironment().c_str());
      return 0;
    }
    if (argc >= 2 && std::strcmp(argv[1], "--import") == 0) {
      ImportOptions options;
      for (int i = 2; i < argc; ++i) {
        const std::string argument = argv[i];
        const bool hasValue = i + 1 < argc;
        if (argument == "--import" ) continue;
        if (argument == "--output" && hasValue) options.output = argv[++i];
        else if (argument == "--kernel" && hasValue) options.kernel = argv[++i];
        else if (argument == "--threads" && hasValue) options.threads = std::stoi(argv[++i]);
        else if (argument == "--deflection" && hasValue) options.linearDeflectionMetres = std::stod(argv[++i]);
        else if (argument == "--angular-deflection" && hasValue) options.angularDeflectionRadians = std::stod(argv[++i]);
        else if (argument == "--length-unit" && hasValue) options.lengthUnitOverride = argv[++i];
        else if (argument == "--skip-spaces") options.skipSpaces = true;
        else if (argument == "--georef" && hasValue) {
          const std::string mode = argv[++i];
          if (mode != "keep" && mode != "grid") return fail("--georef is keep or grid");
          options.gridAligned = mode == "grid";
        }
        else if (argument == "--max-voids" && hasValue) options.maxVoidsPerElement = std::stoi(argv[++i]);
        else if (argument == "--max-nodes" && hasValue) options.maxNodes = static_cast<std::size_t>(std::stoull(argv[++i]));
        else if (argument == "--max-entities" && hasValue) options.maxEntities = static_cast<std::size_t>(std::stoull(argv[++i]));
        else if (argument == "--exclude-step" && hasValue) options.excludeSteps.push_back(std::stoi(argv[++i]));
        else if (argument == "--diagnose-from" && hasValue) options.diagnoseFrom = std::stoi(argv[++i]);
        else if (argument == "--diagnose-seconds" && hasValue) options.diagnoseSeconds = std::stoi(argv[++i]);
        else if (argument == "--test-fault" && hasValue) options.testFault = argv[++i];
        else if (argument == "--dry-run") options.dryRun = true;
        else if (argument == "--rlimit-as" && hasValue) { if (!setLimit(RLIMIT_AS, argv[++i])) return fail("cannot set RLIMIT_AS"); }
        else if (argument == "--rlimit-cpu" && hasValue) { if (!setLimit(RLIMIT_CPU, argv[++i])) return fail("cannot set RLIMIT_CPU"); }
        else if (argument == "--rlimit-fsize" && hasValue) { if (!setLimit(RLIMIT_FSIZE, argv[++i])) return fail("cannot set RLIMIT_FSIZE"); }
        else if (argument.rfind("--", 0) == 0) return fail("unknown option " + argument);
        else if (options.input.empty()) options.input = argument;
        else return fail("one input file only");
      }
      if (options.input.empty() || (options.output.empty() && !options.dryRun)) return fail("--import needs an input file and --output");
      prctl(PR_SET_NO_NEW_PRIVS, 1, 0, 0, 0);
      return runImport(options);
    }
    if (argc >= 3 && std::strcmp(argv[1], "--check") == 0) {
      std::string input;
      for (int i = 2; i < argc; ++i) {
        const std::string argument = argv[i];
        const bool hasValue = i + 1 < argc;
        if (argument == "--rlimit-as" && hasValue) { if (!setLimit(RLIMIT_AS, argv[++i])) return fail("cannot set RLIMIT_AS"); }
        else if (argument == "--rlimit-cpu" && hasValue) { if (!setLimit(RLIMIT_CPU, argv[++i])) return fail("cannot set RLIMIT_CPU"); }
        else if (argument.rfind("--", 0) == 0) return fail("unknown option " + argument);
        else if (input.empty()) input = argument;
        else return fail("one file only");
      }
      if (input.empty()) return fail("--check needs a file");
      prctl(PR_SET_NO_NEW_PRIVS, 1, 0, 0, 0);
      return runCheck(input);
    }
    if (argc >= 2 && std::strcmp(argv[1], "--export") == 0) {
      ExportOptions options;
      for (int i = 2; i < argc; ++i) {
        const std::string argument = argv[i];
        const bool hasValue = i + 1 < argc;
        if (argument == "--output" && hasValue) options.output = argv[++i];
        else if (argument == "--rlimit-as" && hasValue) { if (!setLimit(RLIMIT_AS, argv[++i])) return fail("cannot set RLIMIT_AS"); }
        else if (argument == "--rlimit-cpu" && hasValue) { if (!setLimit(RLIMIT_CPU, argv[++i])) return fail("cannot set RLIMIT_CPU"); }
        else if (argument == "--rlimit-fsize" && hasValue) { if (!setLimit(RLIMIT_FSIZE, argv[++i])) return fail("cannot set RLIMIT_FSIZE"); }
        else if (argument.rfind("--", 0) == 0) return fail("unknown option " + argument);
        else if (options.input.empty()) options.input = argument;
        else return fail("one package only");
      }
      if (options.input.empty() || options.output.empty()) return fail("--export needs a package and --output");
      prctl(PR_SET_NO_NEW_PRIVS, 1, 0, 0, 0);
      return runExport(options);
    }
    if (argc == 3 && std::strcmp(argv[1], "--probe-write") == 0) {
      std::puts(probeWriter(argv[2]).c_str());
      return 0;
    }
    if (argc >= 3 && std::strcmp(argv[1], "--probe-api") == 0) {
      GeometryRunOptions options;
      for (int i = 2; i < argc; ++i) {
        const std::string argument = argv[i];
        const bool hasValue = i + 1 < argc;
        if (argument == "--kernel" && hasValue) {
          options.kernel = argv[++i];
        } else if (argument == "--threads" && hasValue) {
          options.threads = std::stoi(argv[++i]);
        } else if (argument == "--exclude-step" && hasValue) {
          options.excludeSteps.push_back(std::stoi(argv[++i]));
        } else if (argument == "--set" && hasValue) {
          const std::string setting = argv[++i];
          const auto equals = setting.find('=');
          if (equals == std::string::npos) return fail("--set needs NAME=VALUE");
          options.settings.emplace_back(setting.substr(0, equals), setting.substr(equals + 1));
        } else if (argument.rfind("--", 0) == 0) {
          return fail("unknown option " + argument);
        } else if (options.file.empty()) {
          options.file = argument;
        } else {
          return fail("one input file only");
        }
      }
      if (options.file.empty()) return fail("--probe-api needs an input file");
      std::puts(probeGeometry(options).c_str());
      return 0;
    }
    usage(stderr);
    return 2;
  } catch (const std::exception& problem) {
    std::fprintf(stderr, "sketchyup-ifc-worker: %s\n", problem.what());
    return 1;
  }
}
