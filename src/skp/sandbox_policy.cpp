#include "skp/sandbox_policy.hpp"

#include "skp/json_out.hpp"
#include "skp/progress.hpp"

#include <filesystem>

namespace sketchyup::skp {

sandbox::Policy skpSandboxPolicy(const std::string& input, const std::string& output) {
    using sandbox::Action;
    using sandbox::FileAccess;

    sandbox::Policy policy;
    policy.files.push_back({input, FileAccess::readOnly});
    if (!output.empty()) {
        const std::filesystem::path directory = std::filesystem::path(output).parent_path();
        policy.files.push_back({directory.empty() ? std::string(".") : directory.string(), FileAccess::readWrite});
    }

    // ADR 0183 section 5 (SKP). The calls a fixture makes from main onward were traced: read, write,
    // writev, close, fstat, lseek, openat, brk, prlimit64 and exit_group. The deadline's watch thread
    // needs the thread calls that follow (a driver showed each one necessary; the ADR's implementation
    // notes). mremap is listed for large buffers, which no fixture reaches, so it has no trace. abort()
    // signals and reads the IDs of its own process, so a parser abort is a crash (tgkill is for this
    // process only).
    policy.syscalls = {
        // reading the file and writing the package
        {"read", Action::allow},
        {"write", Action::allow},
        {"writev", Action::allow},
        {"close", Action::allow},
        {"fstat", Action::allow},
        {"lseek", Action::allow},
        {"openat", Action::allow},
        // memory
        {"brk", Action::allow},
        {"mmap", Action::allow},
        {"mprotect", Action::allow},
        {"madvise", Action::allow},
        {"munmap", Action::allow},
        {"mremap", Action::allow},
        // limits, signals and the deadline thread
        {"prlimit64", Action::allow},
        {"rt_sigaction", Action::allow},
        {"rt_sigprocmask", Action::allow},
        {"futex", Action::allow},
        {"set_robust_list", Action::allow},
        {"rseq", Action::allow},
        {"clock_nanosleep", Action::allow},
        {"exit_group", Action::allow},
        // a thread (CLONE_THREAD) only: fork-like clone is EPERM, and clone3 is ENOSYS so that glibc
        // falls back to clone (ADR 0183 section 4.3)
        {"clone", Action::threadOnly},
        {"clone3", Action::notImplemented},
        // abort() signals its own process (tgkill), so a parser abort is a crash and not a violation;
        // a signal to any other process is EPERM
        {"tgkill", Action::ownProcessOnly},
        {"getpid", Action::allow},
        {"gettid", Action::allow},
#ifdef SKP_WORKER_NO_ADDRESS_LIMIT
        // The sanitizer builds (ASan and UBSan, and the fuzzer build) only: their runtimes make these calls
        // after main. UBSan's type cache opens a pipe on a first-time vptr check; ASan gives each thread an
        // alternate signal stack. A release helper does not carry them (ADR 0183 section 5).
        {"pipe2", Action::allow},
        {"sigaltstack", Action::allow},
        {"fcntl", Action::allow},
        {"sched_getaffinity", Action::allow},
#endif
        // The socket calls are refused with EPERM, not killed, as in every helper (ADR 0183 section 4.3
        // and decision 4; R109 S6, the parent's decision of 2026-10-27). The helper creates no socket.
        {"socket", Action::denied},
        {"socketpair", Action::denied},
        {"bind", Action::denied},
        {"connect", Action::denied},
        // no exec, no fork: a call not listed here (execve, a raw fork, ptrace...) kills the helper
        // with SIGSYS (ADR 0183 section 5: the SKP exec row is "none").
    };
    return policy;
}

std::optional<sandbox::Failure> applySkpSandbox(const std::string& input, const std::string& output,
                                                const sandbox::ApplyOptions& options) {
    return sandbox::apply(skpSandboxPolicy(input, output), options);
}

int refuseSandboxUnavailable(const sandbox::Failure& failure) {
    const std::string message = sandbox::unavailableReason(failure);
    JsonOut json;
    json.beginObject()
        .field("status", "refused")
        .field("code", "sandboxUnavailable")
        .field("message", message)
        .endObject();
    Progress progress;
    progress.line("R " + json.str());
    return kSandboxUnavailableExit;
}

}  // namespace sketchyup::skp
