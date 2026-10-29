// sketchyup-ifc-worker: the only executable that links IfcOpenShell (ADR 0171 decision 19).
// R097.a ships the skeleton: --probe and the API probes. Import and export arrive in R097.b to d.
// It takes a fixed argv, reads no environment-dependent configuration and runs no shell.
// Before it opens a file, every mode that reads or writes one applies the sandbox (ADR 0183, R109 S2): the
// input is readable, the output's directory is writable, and a call outside the policy kills the helper.
#include <arpa/inet.h>
#include <netdb.h>
#include <sys/resource.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <filesystem>
#include <iostream>
#include <string>
#include <vector>

#include "ifc/check.hpp"
#include "ifc/export.hpp"
#include "ifc/import.hpp"
#include "ifc/probe.hpp"
#include "ifc/sandbox_policy.hpp"
#include "ifc/step_export.hpp"
#include "ifc/step_import.hpp"
#include "ifc/step_probe.hpp"
#include "sketchyup/version.hpp"

// The AP203 header's person and organization carry the host's address: OpenCASCADE's OSD_Host::InternetAddress asks the
// name service (gethostbyname), and it does not test the answer. The sandbox refuses the sockets the name service needs
// (ADR 0183 section 4.3), so the answer is NULL and the STEP writer crashes (pinned OpenCASCADE 7.9.3). The helper answers
// the question itself, with the unspecified address 0.0.0.0 and no lookup, so the header no longer names the machine's
// address. This is the helper's only call of gethostbyname; the change is recorded in ADR 0183's implementation notes.
extern "C" struct hostent* gethostbyname(const char*) {
  static char name[] = "localhost";
  static char* aliases[] = {nullptr};
  static in_addr_t address = INADDR_ANY;
  static char* addresses[] = {reinterpret_cast<char*>(&address), nullptr};
  static struct hostent host = {name, aliases, AF_INET, sizeof address, addresses};
  return &host;
}

#ifdef SKP_WORKER_NO_ADDRESS_LIMIT
// The sanitizer builds (the global flag of the top CMakeLists.txt; ADR 0183 section 3). LeakSanitizer checks for leaks at
// exit by stopping the helper's threads with ptrace, which the sandbox refuses, so the helper would be killed at exit. The
// application starts the helper with an empty environment, so ASAN_OPTIONS cannot turn it off there. IfcParse leaks by
// design (the test environment sets detect_leaks=0 for the same reason); this is that setting, built into the sanitized helper.
extern "C" const char* __lsan_default_options() { return "detect_leaks=0"; }
#endif

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
      "           write a small IFC4 file with the typed writer, read it back and print the result\n"
      "       sketchyup-ifc-worker --probe-step FILE.step [--round-trip-directory DIR]\n"
      "           read a STEP file with the basic OCCT reader and print what it reports as JSON\n"
      "       sketchyup-ifc-worker --import-step FILE.step [--output PKG] [--step-length-unit mm|cm|m|in|ft]\n"
      "           [--step-deflection-mm N] [--step-angle-deg N] [--step-max-triangles N] [--step-max-total-triangles N]\n"
      "           [--exclude-step N]...\n"
      "           [--rlimit-as BYTES] [--rlimit-cpu SECONDS] [--rlimit-fsize BYTES]\n"
      "           read a STEP file, apply the ADR 0172 unit policy; without --output print the metadata as\n"
      "           JSON, with --output tessellate the transferred solids (BRepMesh, the options' deflection\n"
      "           and angle) and write the conversion package\n"
      "       sketchyup-ifc-worker --export-step PACKAGE --output FILE.step [--step-schema ap214|ap203]\n"
      "           [--rlimit-as BYTES] [--rlimit-cpu SECONDS] [--rlimit-fsize BYTES]\n"
      "           write a STEP file from the application's STEP export package (R098.c): AP214 by default, AP203, AP242 refused\n"
      "       sketchyup-ifc-worker --write-step DIR\n"
      "           write the self-generated STEP corpus of ADR 0172 (box, cylinder, assembly) into DIR\n",
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

// The directories a command creates files in: the directory of its output, when it has one.
std::vector<std::string> outputDirectoryOf(const std::string& output) {
  if (output.empty()) return {};
  return {sketchyup::ifc::parentDirectory(output)};
}

}  // namespace

int main(int argc, char** argv) {
  using namespace sketchyup::ifc;
  try {
    const std::string pluginDirectory = pinPluginDirectory();
    if (argc == 2 && std::strcmp(argv[1], "--version") == 0) {
      std::printf("sketchyup-ifc-worker %s\n", SKETCHYUP_VERSION);
      return 0;
    }
    if (argc == 2 && std::strcmp(argv[1], "--probe") == 0) {
      // The probe loads the plugins and reads /proc, so it is sandboxed too (ADR 0183 decision 7).
      if (const auto failure = applyIfcSandbox(IfcSandboxScope{pluginDirectory})) return refuseSandboxUnavailable(*failure);
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
      if (const auto failure = applyIfcSandbox(IfcSandboxScope{pluginDirectory, {options.input}, outputDirectoryOf(options.output)}))
        return refuseSandboxUnavailable(*failure);
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
      if (const auto failure = applyIfcSandbox(IfcSandboxScope{pluginDirectory, {input}})) return refuseSandboxUnavailable(*failure);
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
      if (const auto failure = applyIfcSandbox(IfcSandboxScope{pluginDirectory, {options.input}, outputDirectoryOf(options.output)}))
        return refuseSandboxUnavailable(*failure);
      return runExport(options);
    }
    if (argc == 3 && std::strcmp(argv[1], "--probe-write") == 0) {
      if (const auto failure = applyIfcSandbox(IfcSandboxScope{pluginDirectory, {}, outputDirectoryOf(argv[2])}))
        return refuseSandboxUnavailable(*failure);
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
      if (const auto failure = applyIfcSandbox(IfcSandboxScope{pluginDirectory, {options.file}})) return refuseSandboxUnavailable(*failure);
      std::puts(probeGeometry(options).c_str());
      return 0;
    }
    if (argc >= 3 && std::strcmp(argv[1], "--probe-step") == 0) {
      StepProbeOptions options;
      for (int i = 2; i < argc; ++i) {
        const std::string argument = argv[i];
        const bool hasValue = i + 1 < argc;
        if (argument == "--round-trip-directory" && hasValue) options.roundTripDirectory = argv[++i];
        else if (argument.rfind("--", 0) == 0) return fail("unknown option " + argument);
        else if (options.file.empty()) options.file = argument;
        else return fail("one input file only");
      }
      if (options.file.empty()) return fail("--probe-step needs an input file");
      // The round-trip directory is created here, before the sandbox: its files are written inside it.
      if (!options.roundTripDirectory.empty()) std::filesystem::create_directories(options.roundTripDirectory);
      const std::vector<std::string> roundTrip = options.roundTripDirectory.empty() ? std::vector<std::string>{} : std::vector<std::string>{options.roundTripDirectory};
      if (const auto failure = applyIfcSandbox(IfcSandboxScope{pluginDirectory, {options.file}, roundTrip}))
        return refuseSandboxUnavailable(*failure);
      std::puts(probeStep(options).c_str());
      return runStepProbe(options);
    }
    if (argc >= 2 && std::strcmp(argv[1], "--export-step") == 0) {
      StepExportOptions options;
      for (int i = 2; i < argc; ++i) {
        const std::string argument = argv[i];
        const bool hasValue = i + 1 < argc;
        if (argument == "--output" && hasValue) options.output = argv[++i];
        else if (argument == "--step-schema" && hasValue) options.schema = argv[++i];
        else if (argument == "--rlimit-as" && hasValue) { if (!setLimit(RLIMIT_AS, argv[++i])) return fail("cannot set RLIMIT_AS"); }
        else if (argument == "--rlimit-cpu" && hasValue) { if (!setLimit(RLIMIT_CPU, argv[++i])) return fail("cannot set RLIMIT_CPU"); }
        else if (argument == "--rlimit-fsize" && hasValue) { if (!setLimit(RLIMIT_FSIZE, argv[++i])) return fail("cannot set RLIMIT_FSIZE"); }
        else if (argument.rfind("--", 0) == 0) return fail("unknown option " + argument);
        else if (options.input.empty()) options.input = argument;
        else return fail("one package only");
      }
      if (options.input.empty() || options.output.empty()) return fail("--export-step needs a package and --output");
      if (const auto failure = applyIfcSandbox(IfcSandboxScope{pluginDirectory, {options.input}, outputDirectoryOf(options.output)}))
        return refuseSandboxUnavailable(*failure);
      return runExportStep(options);
    }
    if (argc == 3 && std::strcmp(argv[1], "--write-step") == 0) {
      // The corpus directory is created here, before the sandbox, and the corpus is written inside it.
      std::filesystem::create_directories(argv[2]);
      if (const auto failure = applyIfcSandbox(IfcSandboxScope{pluginDirectory, {}, {argv[2]}})) return refuseSandboxUnavailable(*failure);
      std::puts(writeStep(argv[2]).c_str());
      return 0;
    }
    if (argc >= 3 && std::strcmp(argv[1], "--import-step") == 0) {
      StepImportOptions options;
      for (int i = 2; i < argc; ++i) {
        const std::string argument = argv[i];
        const bool hasValue = i + 1 < argc;
        if (argument == "--output" && hasValue) options.output = argv[++i];
        else if (argument == "--step-length-unit" && hasValue) options.lengthUnitOverride = argv[++i];
        else if (argument == "--step-deflection-mm" && hasValue) options.deflectionMm = std::stod(argv[++i]);
        else if (argument == "--step-angle-deg" && hasValue) options.angleDeg = std::stod(argv[++i]);
        else if (argument == "--step-max-triangles" && hasValue) options.maxTrianglesPerElement = std::stoull(argv[++i]);
        else if (argument == "--step-max-total-triangles" && hasValue) options.maxTotalTriangles = std::stoull(argv[++i]);
        else if (argument == "--exclude-step" && hasValue) options.excludeSteps.push_back(std::stoi(argv[++i]));
        else if (argument == "--rlimit-as" && hasValue) { if (!setLimit(RLIMIT_AS, argv[++i])) return fail("cannot set RLIMIT_AS"); }
        else if (argument == "--rlimit-cpu" && hasValue) { if (!setLimit(RLIMIT_CPU, argv[++i])) return fail("cannot set RLIMIT_CPU"); }
        else if (argument == "--rlimit-fsize" && hasValue) { if (!setLimit(RLIMIT_FSIZE, argv[++i])) return fail("cannot set RLIMIT_FSIZE"); }
        else if (argument.rfind("--", 0) == 0) return fail("unknown option " + argument);
        else if (options.file.empty()) options.file = argument;
        else return fail("one input file only");
      }
      if (options.file.empty()) return fail("--import-step needs an input file");
      if (const auto failure = applyIfcSandbox(IfcSandboxScope{pluginDirectory, {options.file}, outputDirectoryOf(options.output)}))
        return refuseSandboxUnavailable(*failure);
      if (options.output.empty()) return runStepImport(options);
      return runStepImportPackage(options);
    }
    usage(stderr);
    return 2;
  } catch (const std::exception& problem) {
    std::fprintf(stderr, "sketchyup-ifc-worker: %s\n", problem.what());
    return 1;
  }
}
