// sketchyup-ifc-worker: the only executable that links IfcOpenShell (ADR 0171 decision 19).
// R097.a ships the skeleton: --probe and the API probes. Import and export arrive in R097.b to d.
// It takes a fixed argv, reads no environment-dependent configuration and runs no shell.
#include <cstdio>
#include <cstring>
#include <exception>
#include <iostream>
#include <string>

#include "ifc/probe.hpp"
#include "sketchyup/version.hpp"

namespace {

void usage(std::FILE* stream) {
  std::fputs(
      "usage: sketchyup-ifc-worker --version\n"
      "       sketchyup-ifc-worker --probe\n"
      "           print versions, loaded libraries and kernel availability as JSON\n"
      "       sketchyup-ifc-worker --probe-api [--kernel NAME] [--threads N] [--set NAME=VALUE]...\n"
      "                            [--exclude-step N]... FILE.ifc\n"
      "           run FILE through the geometry iterator and print what the library returns\n"
      "       sketchyup-ifc-worker --probe-write FILE.ifc\n"
      "           write a small IFC4 file with the typed writer, read it back and print the result\n",
      stream);
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
