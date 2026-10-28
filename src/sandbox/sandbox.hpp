#pragma once
// The helper process sandbox (ADR 0183, R109 S0). Every file-parsing helper calls apply() once,
// before it opens the file it was given. Three layers are installed in this order:
//   1. PR_SET_NO_NEW_PRIVS;
//   2. a Landlock ruleset that allows the listed paths and nothing else;
//   3. a seccomp-bpf allow-list whose default action kills the process (SIGSYS).
// A layer that cannot be installed is a failure, and the helper refuses before it parses anything
// (fail closed, ADR 0183 section 7). The library has no dependency: the BPF program and the
// Landlock calls are written here against the kernel's ABI.
//
// A helper that is started by fork (the Blender job, ADR 0183 section 4.1) uses PreparedPolicy
// instead: the parent prepares everything that allocates or opens files, and the child calls
// enter(), which issues only system calls.
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace sketchyup::sandbox {

enum class Layer { noNewPrivileges, landlock, seccomp };

// The name a refusal gives the layer: "no_new_privs", "landlock" or "seccomp filter".
const char *layerName(Layer layer);

// How the policy treats one system call. A call the policy does not name is killed (the kill
// default).
enum class Action {
    allow,          // the call runs
    denied,         // the call returns EPERM and does nothing
    notImplemented, // the call returns ENOSYS (glibc then falls back to clone)
    threadOnly,     // clone: runs when it creates a thread (CLONE_THREAD), returns EPERM otherwise
    forkOrThread,   // clone: runs with any flags except a new namespace (CLONE_NEW*), else EPERM (ADR
                    // 0183 decision 5)
    ownProcessOnly, // tgkill: runs when its first argument is this process (abort signals itself)
    whenArgument,   // runs when one argument has the given value (the low 32 bits), else EPERM
    argumentOneOf,  // runs when one argument is one of the rule's values (the low 32 bits), else
                    // EPERM (fcntl's command and ioctl's request, ADR 0183 S3)
};

struct SyscallRule {
    std::string_view name; // a name in syscalls_x86_64.hpp
    Action action;
    // whenArgument and argumentOneOf: the argument compared (0 to 5) and, for whenArgument, the
    // value it must have. For example prctl's option (argument 0) must be PR_SET_PDEATHSIG, and
    // ioctl's request (argument 1) must be TCGETS.
    unsigned argumentIndex = 0;
    std::uint32_t argumentValue = 0;
    // argumentOneOf only: the values the argument may have.
    std::vector<std::uint32_t> values{};
};

// A rule that runs the call when the argument at argumentIndex is one of the values (argumentOneOf,
// ADR 0183 S3): fcntl's command and ioctl's request are each a list in the text and extension
// policies.
inline SyscallRule oneOfArgument(std::string_view name, unsigned argumentIndex,
                                 std::vector<std::uint32_t> values) {
    return SyscallRule{name, Action::argumentOneOf, argumentIndex, 0, std::move(values)};
}

// What the Landlock ruleset grants on one path. A path that does not exist is not granted.
enum class FileAccess {
    readOnly,    // read the file, or list and read the directory
    readExecute, // read, and execute: a library directory the helper loads its plugins from (ADR
                 // 0183 section 4.2), or the one program a helper may run (ADR 0183 section 4.4)
    readWrite,   // read, write and create (a directory: every right except execute)
};

struct FileRule {
    std::string path;
    FileAccess access;
};

struct Policy {
    std::vector<FileRule> files;
    std::vector<SyscallRule> syscalls; // "exit", "exit_group" and "rt_sigreturn" are always allowed
};

// The layer that did not install, and the errno the kernel returned (or the one a test simulated).
struct Failure {
    Layer layer;
    int errnoValue;
};

struct ApplyOptions {
    // Test injection, never set by the helpers: the layer fails as if the kernel had refused it
    // with this errno, and the kernel is not asked. A Landlock failure is reported by prepare(), a
    // no_new_privs or seccomp failure by enter(). Layers the failed one would install are not run.
    std::optional<Failure> simulate;
};

// A policy whose layers are installed in two steps: prepare() in the parent, where it may allocate
// and open files, and enter() in the child after fork(), where it issues only system calls. The
// child calls enter() and then exec(). It is safe in a multithreaded process: enter() takes no lock
// and allocates nothing.
class PreparedPolicy {
  public:
    PreparedPolicy();
    ~PreparedPolicy();
    PreparedPolicy(PreparedPolicy &&other) noexcept;
    PreparedPolicy &operator=(PreparedPolicy &&other) noexcept;
    PreparedPolicy(const PreparedPolicy &) = delete;
    PreparedPolicy &operator=(const PreparedPolicy &) = delete;

    // Builds the Landlock ruleset (the rule paths are opened here) and the BPF program. Returns the
    // layer that could not be built, or nothing.
    [[nodiscard]] std::optional<Failure> prepare(const Policy &policy,
                                                 const ApplyOptions &options = {});

    // Installs no_new_privs, restricts the process with the ruleset, and installs the filter, in
    // that order. Returns the layer that failed, or nothing when all three are installed. Call it
    // only after prepare() succeeded. The process is restricted for good: a failed call leaves the
    // caller to exit.
    [[nodiscard]] std::optional<Failure> enter() noexcept;

  private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

// Installs the policy in this process. Returns the first layer that failed, or nothing when all
// three are installed. Equal to PreparedPolicy::prepare followed by enter.
[[nodiscard]] std::optional<Failure> apply(const Policy &policy, const ApplyOptions &options = {});

// The refusal's name for a failed layer: "sandboxUnavailable: <layer> (<errno text>)".
std::string unavailableReason(const Failure &failure);

// Writes the refusal's first line to standard error with write(2) only, for a child that has not
// yet exec'd: "sandboxUnavailable: <layer> (errno <n>)". It allocates nothing, so a parent can
// recognise it in the child's output.
void writeUnavailableLine(const Failure &failure) noexcept;

// The Landlock ABI version the kernel offers (1 or more), or -1 with errno set when Landlock is
// off.
int landlockAbiVersion();

} // namespace sketchyup::sandbox
