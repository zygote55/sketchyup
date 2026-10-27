#include "ifc/sandbox_policy.hpp"

#include "ifc/json_out.hpp"

#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <sys/prctl.h>  // PR_SET_PDEATHSIG: the diagnosis children set it

namespace sketchyup::ifc {
namespace {

// The request glibc's isatty issues on a character device: TCGETS2, _IOR('T', 0x2a, struct termios2) (44 bytes).
constexpr std::uint32_t kTcgets2 = 0x802c542a;

}  // namespace

std::string parentDirectory(const std::string& path) {
    const std::filesystem::path parent = std::filesystem::path(path).parent_path();
    return parent.empty() ? std::string(".") : parent.string();
}

sandbox::Policy ifcSandboxPolicy(const IfcSandboxScope& scope) {
    using sandbox::Action;
    using sandbox::FileAccess;
    using sandbox::SyscallRule;

    sandbox::Policy policy;
    // The library's own code: the plugins the parser loads during the parse are in the directory of
    // libifcopenshell.plugin.so (ADR 0183 section 4.2), and OpenCASCADE is in /usr/lib (the private copy of
    // the `ifc` package sits beside the helper in /usr/lib/sketchyup, which is inside it).
    if (!scope.pluginDirectory.empty())
        policy.files.push_back({scope.pluginDirectory, FileAccess::readExecute});
    policy.files.push_back({"/usr/lib", FileAccess::readExecute});
    // Single files the library reads (the list is the trace's, ADR 0183 section 4.2): the loader's cache, the local time
    // zone (the header's time), the probe's own memory map, and the CPU count and the overcommit policy OpenCASCADE reads.
    // Nothing else under /etc, /proc or /sys is granted: the user's credentials and other processes stay out of reach.
    for (const char* file : {"/etc/ld.so.cache", "/etc/localtime", "/proc/self/maps", "/proc/sys/vm/overcommit_memory",
                             "/sys/devices/system/cpu/online"})
        policy.files.push_back({file, FileAccess::readOnly});
    for (const std::string& input : scope.inputs)
        policy.files.push_back({input, FileAccess::readOnly});
    for (const std::string& output : scope.outputDirectories)
        policy.files.push_back({output, FileAccess::readWrite});

    // ADR 0183 section 5 (IFC and STEP), re-derived from a trace of every mode on the fixtures and the application's
    // tests (the implementation notes of ADR 0183). A call not listed here kills the helper with SIGSYS.
    policy.syscalls = {
        // the files: reading the input, writing the package, and the lookups of the libraries
        {"read", Action::allow},
        {"write", Action::allow},
        {"writev", Action::allow},
        {"close", Action::allow},
        {"fstat", Action::allow},
        {"lseek", Action::allow},
        {"openat", Action::allow},
        {"newfstatat", Action::allow},
        {"statx", Action::allow},
        {"readlink", Action::allow},
        // memory
        {"brk", Action::allow},
        {"mmap", Action::allow},
        {"mprotect", Action::allow},
        {"madvise", Action::allow},
        {"munmap", Action::allow},
        {"mremap", Action::allow},  // no trace reaches it: kept for large buffers (the SKP helper's note in ADR 0183)
        // threads: a thread, or a fork-like child for the diagnosis (decision 5); clone3 is ENOSYS so that glibc falls
        // back to clone; a child in a new namespace is EPERM
        {"clone", Action::forkOrThread},
        {"clone3", Action::notImplemented},
        {"futex", Action::allow},
        {"set_robust_list", Action::allow},
        {"rseq", Action::allow},
        {"clock_nanosleep", Action::allow},
        // signals: abort() signals its own process (a crash, not a violation); a signal to another is EPERM
        {"rt_sigaction", Action::allow},
        {"rt_sigprocmask", Action::allow},
        {"tgkill", Action::ownProcessOnly},
        // the diagnosis children: their own death signal, the alarm, and the parent's wait
        {"prctl", Action::whenArgument, 0, PR_SET_PDEATHSIG},
        {"alarm", Action::allow},
        {"wait4", Action::allow},
        // the process's identity and clock; uname only names the machine (no call here can send it anywhere)
        {"getpid", Action::allow},
        {"getppid", Action::allow},
        {"gettid", Action::allow},
        {"getuid", Action::allow},
        {"uname", Action::allow},
        {"times", Action::allow},
        // isatty on a character device (stdout as /dev/null or a terminal): glibc asks TCGETS2 and nothing else
        {"ioctl", Action::whenArgument, 1, kTcgets2},
        // The network: refused with EPERM, not killed (ADR 0183 section 4.3). OpenCASCADE's STEP writer asks for the
        // host's address when it writes the AP203 header (gethostbyname, through the name service), so the call is made
        // and must fail cleanly, not end the helper. No socket is ever created.
        {"socket", Action::denied},
        {"socketpair", Action::denied},
        {"bind", Action::denied},
        {"connect", Action::denied},
#ifdef SKP_WORKER_NO_ADDRESS_LIMIT
        // The sanitizer builds (ASan and UBSan, the global flag of the top CMakeLists.txt): their runtimes make these calls
        // after main. UBSan's type cache opens a pipe on a first-time vptr check; ASan gives each thread an alternate signal
        // stack and reads the affinity of the thread pool; a release helper does not carry them (ADR 0183 section 5).
        {"pipe2", Action::allow},
        {"sigaltstack", Action::allow},
        {"sched_getaffinity", Action::allow},
        {"fcntl", Action::allow},
#endif
        // no exec, no ptrace, no other clone flags: a call not listed here kills the helper (ADR 0183 section 5)
    };
    return policy;
}

std::optional<sandbox::Failure> applyIfcSandbox(const IfcSandboxScope& scope, const sandbox::ApplyOptions& options) {
    return sandbox::apply(ifcSandboxPolicy(scope), options);
}

int refuseSandboxUnavailable(const sandbox::Failure& failure) {
    JsonOut json;
    json.beginObject()
        .field("status", "refused")
        .field("code", "sandboxUnavailable")
        .field("message", sandbox::unavailableReason(failure))
        .endObject();
    std::printf("R %s\n", json.str().c_str());
    std::fflush(stdout);
    return kSandboxUnavailableExit;
}

}  // namespace sketchyup::ifc
