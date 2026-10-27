#pragma once
// The helper process sandbox (ADR 0183, R109 S0). Every file-parsing helper calls apply() once,
// before it opens the file it was given. Three layers are installed in this order:
//   1. PR_SET_NO_NEW_PRIVS;
//   2. a Landlock ruleset that allows the listed paths and nothing else;
//   3. a seccomp-bpf allow-list whose default action kills the process (SIGSYS).
// A layer that cannot be installed is a failure, and the helper refuses before it parses anything
// (fail closed, ADR 0183 section 7). The library has no dependency: the BPF program and the
// Landlock calls are written here against the kernel's ABI.
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
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
    forkOrThread, // clone: runs with any flags except a new namespace (CLONE_NEW*), else EPERM (ADR
                  // 0183 decision 5)
    ownProcessOnly, // tgkill: runs when its first argument is this process (abort signals itself)
    whenArgument,   // runs when one argument has the given value (the low 32 bits), else EPERM
};

struct SyscallRule {
    std::string_view name; // a name in syscalls_x86_64.hpp
    Action action;
    // whenArgument only: the argument compared (0 to 5) and the value it must have. For example
    // prctl's option (argument 0) must be PR_SET_PDEATHSIG, and ioctl's request (argument 1) must
    // be TCGETS.
    unsigned argumentIndex = 0;
    std::uint32_t argumentValue = 0;
};

// What the Landlock ruleset grants on one path. A path that does not exist is not granted.
enum class FileAccess {
    readOnly,    // read the file, or list and read the directory
    readExecute, // read, and execute: a library directory the helper loads its plugins from (ADR
                 // 0183 section 4.2)
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
    // with this errno, and the kernel is not asked. Layers before it are still installed.
    std::optional<Failure> simulate;
};

// Installs the policy in this process. Returns the first layer that failed, or nothing when all
// three are installed. The process is restricted for good: a failed call leaves the caller to exit.
[[nodiscard]] std::optional<Failure> apply(const Policy &policy, const ApplyOptions &options = {});

// The refusal's name for a failed layer: "sandboxUnavailable: <layer> (<errno text>)".
std::string unavailableReason(const Failure &failure);

// The Landlock ABI version the kernel offers (1 or more), or -1 with errno set when Landlock is
// off.
int landlockAbiVersion();

} // namespace sketchyup::sandbox
