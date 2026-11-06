// The kernel layers on a system without Landlock and seccomp. Every layer fails with ENOSYS, so on
// a system with no other layer a helper refuses with "sandboxUnavailable" before it parses
// anything: the sandbox fails closed (ADR 0183 section 7) rather than running the helper
// unconfined. On macOS the Seatbelt layer (seatbelt.cpp) is installed instead, and only
// landlockAbiVersion is called.
#include "sandbox/layers.hpp"

#include <cerrno>

namespace sketchyup::sandbox {

std::optional<Failure> buildRuleset(const std::vector<FileRule> &, int &rulesetFd) {
    rulesetFd = -1;
    return Failure{Layer::landlock, ENOSYS};
}

std::optional<Failure> restrictWithRuleset(int) noexcept { return Failure{Layer::landlock, ENOSYS}; }

std::optional<Failure> buildFilter(const std::vector<SyscallRule> &, Filter &) {
    return Failure{Layer::seccomp, ENOSYS};
}

std::optional<Failure> installFilter(Filter &) noexcept { return Failure{Layer::seccomp, ENOSYS}; }

int landlockAbiVersion() {
    errno = ENOSYS;
    return -1;
}

} // namespace sketchyup::sandbox
