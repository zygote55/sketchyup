#pragma once
// R109 S4 (ADR 0183 sections 4 and 5): the sandbox of the extension worker. The worker applies this
// policy at the top of its main, before QCoreApplication and before it reads the request. The
// request comes on stdin and the worker reads no other file, so the policy grants read-only access
// to the locale and SSL configuration that its trace opens, and nothing for writing (ADR 0136's
// child had the user's file and network access before this).
#include "sandbox/sandbox.hpp"

#include <optional>

namespace sketchy {

// The exit code of a sandbox refusal (ADR 0183 section 7).
inline constexpr int extensionSandboxUnavailableExit = 70;

// The policy of the extension worker: read-only grants for the paths its trace opens, and the calls
// the trace makes, with fcntl and ioctl limited to the values it uses. A call not listed kills the
// worker.
sketchyup::sandbox::Policy extensionSandboxPolicy();

// Installs the policy in this process, fail closed. Returns the layer that failed, or nothing.
std::optional<sketchyup::sandbox::Failure>
applyExtensionSandbox(const sketchyup::sandbox::ApplyOptions &options = {});

// Writes the protocol's error reply for a layer that could not be installed, and returns the exit
// code of the refusal. The reply is written before Qt starts, so it needs no QCoreApplication.
int refuseExtensionSandbox(const sketchyup::sandbox::Failure &failure);

} // namespace sketchy
