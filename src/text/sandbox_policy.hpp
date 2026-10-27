#pragma once
// R109 S3 (ADR 0183 sections 4 and 5): the sandbox of the text helper. The helper applies this
// policy in its main, before QGuiApplication and before it reads the request. The request comes on
// stdin, so the helper needs no file of the user's: it reads the system fonts (fontconfig's
// configuration, its cache and the font files), the Qt platform plugin and the locale data, and it
// writes nothing.
#include "sandbox/sandbox.hpp"

#include <optional>

namespace sketchy {

// The exit code of a sandbox refusal (ADR 0183 section 7).
inline constexpr int textSandboxUnavailableExit = 70;

// The policy of the text helper: read-only grants for the paths its trace reads, and the calls the
// trace makes, with fcntl and ioctl limited to the values it uses. A call not listed kills the
// helper.
sketchyup::sandbox::Policy textSandboxPolicy();

// Installs the policy in this process, fail closed. Returns the layer that failed, or nothing.
std::optional<sketchyup::sandbox::Failure>
applyTextSandbox(const sketchyup::sandbox::ApplyOptions &options = {});

// Writes the protocol's error reply for a layer that could not be installed, and returns the exit
// code of the refusal. The reply is written before Qt starts, so it needs no QCoreApplication.
int refuseTextSandbox(const sketchyup::sandbox::Failure &failure);

} // namespace sketchy
