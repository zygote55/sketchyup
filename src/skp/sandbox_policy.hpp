#pragma once
// R109 S1 (ADR 0183 sections 4 and 5): the sandbox of the .skp helper. The helper applies this policy
// before it opens its input, in every mode that reads a file: --import, --probe and --dry-run.
#include "sandbox/sandbox.hpp"

#include <optional>
#include <string>

namespace sketchyup::skp {

// The policy of one command. The input file is read. The directory of the output package (the
// application's private directory, which holds the input copy as well) is read and written. Nothing
// else is granted: no socket (each socket call is EPERM), no exec, and no thread but the deadline's.
sandbox::Policy skpSandboxPolicy(const std::string& input, const std::string& output);

// Installs the policy in this process, fail closed. Returns the layer that failed, or nothing.
std::optional<sandbox::Failure> applySkpSandbox(const std::string& input, const std::string& output,
                                                const sandbox::ApplyOptions& options = {});

// Prints the refusal line for a layer that could not be installed (the "R" line on stdout, as every
// refusal), and returns the helper's exit code for it (70, the dedicated code of ADR 0183 section 7).
int refuseSandboxUnavailable(const sandbox::Failure& failure);

// The exit code of a sandbox refusal.
inline constexpr int kSandboxUnavailableExit = 70;

}  // namespace sketchyup::skp
