#pragma once
// The two kernel layers behind sandbox::apply and PreparedPolicy (ADR 0183 section 4.1). Each
// builder returns nothing when it succeeds, or the failed layer and the errno. The installers issue
// system calls only: PreparedPolicy::enter runs them after fork().
#include "sandbox/sandbox.hpp"

#include <linux/filter.h>

#include <cstddef>
#include <optional>
#include <vector>

namespace sketchyup::sandbox {

// A Landlock ruleset that grants the files' rules and handles every filesystem right of the
// kernel's ABI except device ioctl (ADR 0183 section 4.2). Scope features (ABI 6) are added when
// the kernel has them. A path that does not exist is skipped: it cannot be opened, so it grants
// nothing. On success the ruleset's descriptor is returned in rulesetFd; the caller closes it.
std::optional<Failure> buildRuleset(const std::vector<FileRule> &files, int &rulesetFd);

// landlock_restrict_self on a ruleset built by buildRuleset. System calls only.
std::optional<Failure> restrictWithRuleset(int rulesetFd) noexcept;

// A seccomp-bpf program: the x86_64 architecture check, then one entry per named call, then the
// kill default. selfSlots lists the instructions whose immediate is this process's ID (the
// own-process rule); they are filled in when the filter is installed, in the process that installs
// it.
struct Filter {
    std::vector<sock_filter> program;
    std::vector<std::size_t> selfSlots;
};

// Builds the program. Returns EINVAL as a failure when a name is not in the table, is named twice
// with two actions, or the program is longer than the kernel accepts.
std::optional<Failure> buildFilter(const std::vector<SyscallRule> &rules, Filter &filter);

// Fills in the own-process immediates and installs the program. System calls only.
std::optional<Failure> installFilter(Filter &filter) noexcept;

} // namespace sketchyup::sandbox
