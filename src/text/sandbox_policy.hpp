#pragma once
// R109 S3 (ADR 0183 sections 4 and 5): the sandbox of the text helper. The helper applies this
// policy in its main, before QGuiApplication and before it reads the request. The request comes on
// stdin, so the helper needs no file of the user's: it reads the system fonts (fontconfig's
// configuration, its cache and the font files), the Qt platform plugin and the locale data, and it
// writes nothing. R109 S4 adds the user's font folders, read-only (below).
#include "sandbox/sandbox.hpp"

#include <optional>
#include <string>
#include <vector>

namespace sketchy {

// The exit code of a sandbox refusal (ADR 0183 section 7).
inline constexpr int textSandboxUnavailableExit = 70;

// The user's fonts, as the host passes them (ADR 0183 section 5, R109 S4). The host finds the
// folders the desktop's font list reads and writes a fontconfig file that names them, in its
// private directory. Both are granted read-only, and nothing in them is writable.
struct TextFontGrants {
    std::vector<std::string> folders{}; // existing directories, absolute
    std::string configuration{};        // the fontconfig file the host wrote, absolute
};

// Reads the helper's font arguments: --font-dir=PATH for each folder and --font-config=PATH for the
// file. Returns the reason they are refused, or nothing when they are accepted. A folder that is
// not an existing directory is refused; so is a configuration that is not an existing file, folders
// without a configuration or a configuration without folders, and any other argument. With no
// arguments, nothing is granted and nothing is refused.
std::optional<std::string> parseTextFontArguments(int argc, char **argv, TextFontGrants &grants);

// The policy of the text helper: read-only grants for the paths its trace reads, the user's font
// folders and configuration, and the calls the trace makes, with fcntl and ioctl limited to the
// values it uses. A call not listed kills the helper.
sketchyup::sandbox::Policy textSandboxPolicy(const TextFontGrants &fonts = {});

// Installs the policy in this process, fail closed. Returns the layer that failed, or nothing.
std::optional<sketchyup::sandbox::Failure>
applyTextSandbox(const TextFontGrants &fonts = {},
                 const sketchyup::sandbox::ApplyOptions &options = {});

// Writes the protocol's error reply for a layer that could not be installed, and returns the exit
// code of the refusal. The reply is written before Qt starts, so it needs no QCoreApplication.
int refuseTextSandbox(const sketchyup::sandbox::Failure &failure);

} // namespace sketchy
