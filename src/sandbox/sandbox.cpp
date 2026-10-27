#include "sandbox/sandbox.hpp"

#include "sandbox/layers.hpp"

#include <cerrno>
#include <cstring>
#include <sys/prctl.h>

namespace sketchyup::sandbox {

const char* layerName(Layer layer) {
    switch (layer) {
        case Layer::noNewPrivileges:
            return "no_new_privs";
        case Layer::landlock:
            return "landlock";
        case Layer::seccomp:
            return "seccomp filter";
    }
    return "unknown";
}

std::optional<Failure> apply(const Policy& policy, const ApplyOptions& options) {
    const auto simulated = [&](Layer layer) -> std::optional<Failure> {
        if (options.simulate && options.simulate->layer == layer) return options.simulate;
        return std::nullopt;
    };

    // Landlock needs no_new_privs for an unprivileged process, and seccomp needs it too.
    if (auto failure = simulated(Layer::noNewPrivileges)) return failure;
    if (::prctl(PR_SET_NO_NEW_PRIVS, 1UL, 0UL, 0UL, 0UL) != 0) return Failure{Layer::noNewPrivileges, errno};

    // Landlock before seccomp: its own calls (create, add rule, restrict) must still be allowed to run.
    if (auto failure = simulated(Layer::landlock)) return failure;
    if (auto failure = installLandlock(policy.files)) return failure;

    if (auto failure = simulated(Layer::seccomp)) return failure;
    return installSeccomp(policy.syscalls);
}

std::string unavailableReason(const Failure& failure) {
    return std::string("sandboxUnavailable: ") + layerName(failure.layer) + " (" +
           std::strerror(failure.errnoValue) + ")";
}

}  // namespace sketchyup::sandbox
