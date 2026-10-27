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

    // ADR 0183 section 5 (SKP): 19 calls measured in the traces, from main onward (see the notes in
    // the ADR for the calls removed and added). The calls below are the ones a fixture run makes, with
    // the deadline thread (clone, the thread's stack and guard, its robust list and its sleep) and the
    // memory calls glibc makes for a large buffer. The last group is added without a trace: the
    // fixtures are at most 10 KB, so no buffer reaches the mmap threshold, and the frees of such a buffer
    // need munmap and mremap (ADR 0183 section 6.9).
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
        // no socket, no exec, no fork: a call not listed here (socket, execve, fork, ptrace...) kills
        // the helper with SIGSYS (ADR 0183 section 5: the SKP network and exec rows are "none").
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
