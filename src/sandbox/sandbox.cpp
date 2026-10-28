#include "sandbox/sandbox.hpp"

#include "sandbox/layers.hpp"

#include <cerrno>
#include <cstring>
#include <sys/prctl.h>
#include <unistd.h>

namespace sketchyup::sandbox {

struct PreparedPolicy::Impl {
    int rulesetFd = -1;
    Filter filter;
    std::optional<Failure> simulate;

    ~Impl() {
        if (rulesetFd >= 0)
            ::close(rulesetFd);
    }
};

PreparedPolicy::PreparedPolicy() : impl_(std::make_unique<Impl>()) {}
PreparedPolicy::~PreparedPolicy() = default;
PreparedPolicy::PreparedPolicy(PreparedPolicy &&other) noexcept = default;
PreparedPolicy &PreparedPolicy::operator=(PreparedPolicy &&other) noexcept = default;

std::optional<Failure> PreparedPolicy::prepare(const Policy &policy, const ApplyOptions &options) {
    impl_->simulate = options.simulate;
    if (impl_->simulate && impl_->simulate->layer == Layer::landlock)
        return impl_->simulate;
    if (auto failure = buildRuleset(policy.files, impl_->rulesetFd))
        return failure;
    return buildFilter(policy.syscalls, impl_->filter);
}

std::optional<Failure> PreparedPolicy::enter() noexcept {
    // Only system calls from here on: the child of a fork runs this (ADR 0183 section 4.1).
    const std::optional<Failure> simulated = impl_->simulate;
    if (simulated && simulated->layer == Layer::noNewPrivileges)
        return simulated;
    // Landlock and seccomp both need no_new_privs for an unprivileged process.
    if (::prctl(PR_SET_NO_NEW_PRIVS, 1UL, 0UL, 0UL, 0UL) != 0)
        return Failure{Layer::noNewPrivileges, errno};
    // Landlock before seccomp: restricting with the ruleset is a call the filter must not stop.
    if (impl_->rulesetFd < 0)
        return Failure{Layer::landlock, EBADF};
    if (auto failure = restrictWithRuleset(impl_->rulesetFd))
        return failure;
    // Once the ruleset is in force its descriptor is not needed. It is closed here, before the filter,
    // so that no call after the filter is installed is the policy's to allow (R109 S6: the destructor
    // used to close it after the filter, and a policy without close was killed by its own apply()).
    ::close(impl_->rulesetFd);
    impl_->rulesetFd = -1;
    if (simulated && simulated->layer == Layer::seccomp)
        return simulated;
    return installFilter(impl_->filter);
}

std::optional<Failure> apply(const Policy &policy, const ApplyOptions &options) {
    PreparedPolicy prepared;
    if (auto failure = prepared.prepare(policy, options))
        return failure;
    return prepared.enter();
}

const char *layerName(Layer layer) {
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

std::string unavailableReason(const Failure &failure) {
    return std::string("sandboxUnavailable: ") + layerName(failure.layer) + " (" +
           std::strerror(failure.errnoValue) + ")";
}

void writeUnavailableLine(const Failure &failure) noexcept {
    // Fixed strings, and the errno in decimal, written in one call: nothing here allocates.
    const char *layer = failure.layer == Layer::noNewPrivileges ? "no_new_privs"
                        : failure.layer == Layer::landlock      ? "landlock"
                                                                : "seccomp filter";
    char digits[16];
    int length = 0;
    int value = failure.errnoValue < 0 ? 0 : failure.errnoValue;
    do {
        digits[length++] = static_cast<char>('0' + value % 10);
        value /= 10;
    } while (value > 0 && length < 15);
    char line[96];
    int used = 0;
    const auto append = [&](const char *text) {
        for (; *text && used < 95; ++text)
            line[used++] = *text;
    };
    append("sandboxUnavailable: ");
    append(layer);
    append(" (errno ");
    while (length > 0 && used < 94)
        line[used++] = digits[--length];
    append(")\n");
    [[maybe_unused]] const ssize_t written = ::write(2, line, static_cast<size_t>(used));
}

} // namespace sketchyup::sandbox
