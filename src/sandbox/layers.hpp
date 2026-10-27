#pragma once
// The two kernel layers behind sandbox::apply (ADR 0183 section 4.1). Each returns nothing when the
// layer is installed, or the failed layer and the errno.
#include "sandbox/sandbox.hpp"

#include <optional>
#include <vector>

namespace sketchyup::sandbox {

// A Landlock ruleset that grants the files' rules and handles every filesystem right of the kernel's
// ABI except device ioctl (ADR 0183 section 4.2). Scope features (ABI 6) are added when the kernel has
// them. A path that does not exist is skipped: it cannot be opened, so it grants nothing.
std::optional<Failure> installLandlock(const std::vector<FileRule>& files);

// A seccomp-bpf filter: the x86_64 architecture check, then one entry per named call, then the kill
// default. Returns EINVAL as a failure when a name is not in the table or is named twice.
std::optional<Failure> installSeccomp(const std::vector<SyscallRule>& rules);

}  // namespace sketchyup::sandbox
