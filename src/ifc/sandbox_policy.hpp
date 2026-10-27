#pragma once
// R109 S2 (ADR 0183 sections 4 and 5): the sandbox of the IFC and STEP helper. Every mode of
// sketchyup-ifc-worker that reads or writes a file applies this policy before it opens that file:
// the import, the check, the export, the STEP import and export, and the developer probes (decision 7).
// One allow-list serves every mode; each mode grants only the paths it uses.
#include "sandbox/sandbox.hpp"

#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace sketchyup::ifc {

// What one command of the helper touches. An empty field grants nothing.
struct IfcSandboxScope {
    IfcSandboxScope(std::string plugins, std::vector<std::string> readFiles = {}, std::vector<std::string> writeDirectories = {})
        : pluginDirectory(std::move(plugins)), inputs(std::move(readFiles)), outputDirectories(std::move(writeDirectories)) {}

    std::string pluginDirectory;                // IfcOpenShell's plugins, from pinPluginDirectory(): read and execute
    std::vector<std::string> inputs;            // files the command reads
    std::vector<std::string> outputDirectories; // directories the command creates files in: read and write
};

// The directory a file is created in: the parent of its path, or "." for a bare name.
std::string parentDirectory(const std::string& path);

// The policy of one command: the scope's rules, the library paths the parser loads lazily (the plugin
// directory and /usr/lib: OpenCASCADE, ADR 0183 section 6.3), and the call list of section 5. No exec; the
// network calls are refused with EPERM (section 4.3); a fork-like clone runs for the diagnosis children, and
// they inherit the filter.
sandbox::Policy ifcSandboxPolicy(const IfcSandboxScope& scope);

// Installs the policy in this process, fail closed. Returns the layer that failed, or nothing.
std::optional<sandbox::Failure> applyIfcSandbox(const IfcSandboxScope& scope,
                                                const sandbox::ApplyOptions& options = {});

// Prints the refusal (an "R" line with code sandboxUnavailable, on stdout) and returns the helper's exit
// code for it: 70, the dedicated code of ADR 0183 section 7.
int refuseSandboxUnavailable(const sandbox::Failure& failure);

inline constexpr int kSandboxUnavailableExit = 70;

}  // namespace sketchyup::ifc
