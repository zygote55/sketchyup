#pragma once
// The x86_64 system call numbers the sandbox policies name (ADR 0183 section 4.3). The numbers are
// the kernel's ABI, from arch/x86/entry/syscalls/syscall_64.tbl. Only the calls a policy can name
// are listed: a name that is not here cannot be allowed, so the policy fails closed. The sandbox
// tests check every entry against the C library's SYS_ constants where the C library defines one.
#include <cstddef>
#include <optional>
#include <string_view>

namespace sketchyup::sandbox {

struct SyscallNumber {
    std::string_view name;
    long number;
};

inline constexpr SyscallNumber kX86_64Syscalls[] = {
    {"read", 0},
    {"write", 1},
    {"close", 3},
    {"fstat", 5},
    {"poll", 7},
    {"lseek", 8},
    {"mmap", 9},
    {"mprotect", 10},
    {"munmap", 11},
    {"brk", 12},
    {"rt_sigaction", 13},
    {"rt_sigprocmask", 14},
    {"rt_sigreturn", 15},
    {"ioctl", 16},
    {"readv", 19},
    {"writev", 20},
    {"access", 21},
    {"sched_yield", 24},
    {"mremap", 25},
    {"madvise", 28},
    {"nanosleep", 35},
    {"getpid", 39},
    {"socket", 41},
    {"connect", 42},
    {"accept", 43},
    {"sendto", 44},
    {"recvfrom", 45},
    {"sendmsg", 46},
    {"recvmsg", 47},
    {"bind", 49},
    {"listen", 50},
    {"socketpair", 53},
    {"clone", 56},
    {"fork", 57},
    {"vfork", 58},
    {"execve", 59},
    {"exit", 60},
    {"wait4", 61},
    {"kill", 62},
    {"fcntl", 72},
    {"fsync", 74},
    {"getcwd", 79},
    {"readlink", 89},
    {"getuid", 102},
    {"getgid", 104},
    {"geteuid", 107},
    {"getegid", 108},
    {"getppid", 110},
    {"sigaltstack", 131},
    {"prctl", 157},
    {"arch_prctl", 158},
    {"gettid", 186},
    {"tkill", 200},
    {"futex", 202},
    {"sched_getaffinity", 204},
    {"getdents64", 217},
    {"set_tid_address", 218},
    {"clock_gettime", 228},
    {"clock_nanosleep", 230},
    {"exit_group", 231},
    {"tgkill", 234},
    {"openat", 257},
    {"newfstatat", 262},
    {"readlinkat", 267},
    {"faccessat", 269},
    {"ptrace", 101},
    {"set_robust_list", 273},
    {"prlimit64", 302},
    {"process_vm_readv", 310},
    {"process_vm_writev", 311},
    {"seccomp", 317},
    {"getrandom", 318},
    {"bpf", 321},
    {"execveat", 322},
    {"statx", 332},
    {"rseq", 334},
    {"pipe2", 293},
    {"io_uring_setup", 425},
    {"io_uring_enter", 426},
    {"io_uring_register", 427},
    {"clone3", 435},
    {"landlock_create_ruleset", 444},
    {"landlock_add_rule", 445},
    {"landlock_restrict_self", 446},
    {"mount", 165},
    {"umount2", 166},
    {"pivot_root", 155},
    {"chroot", 161},
    {"unshare", 272},
    {"setns", 308},
    {"perf_event_open", 298},
    {"keyctl", 250},
    {"add_key", 248},
    {"request_key", 249},
    {"open", 2},
    {"stat", 4},
    {"lstat", 6},
    {"fstatfs", 138},
    {"statfs", 137},
    {"uname", 63},
    {"fchdir", 81},
    {"rename", 82},
    {"mkdir", 83},
    {"mkdirat", 258},
    {"rmdir", 84},
    {"unlink", 87},
    {"unlinkat", 263},
    {"dup3", 292},
    {"eventfd2", 290},
    {"epoll_create1", 291},
    {"epoll_ctl", 233},
    {"epoll_wait", 232},
    {"setpriority", 141},
    {"sched_setscheduler", 144},
    {"sched_getscheduler", 145},
    {"sched_getparam", 143},
    {"sched_get_priority_max", 146},
    {"sched_get_priority_min", 147},
    {"sched_setaffinity", 203},
    {"getresuid", 118},
    {"getresgid", 120},
    {"getsockname", 51},
    {"getsockopt", 55},
    {"setsockopt", 54},
    {"shutdown", 48},
    {"accept4", 288},
    {"fallocate", 285},
    {"flock", 73},
    {"chmod", 90},
};

// The number of a call by its name, or nothing when the table does not list it.
inline std::optional<long> syscallNumber(std::string_view name) {
    for (const SyscallNumber &entry : kX86_64Syscalls)
        if (entry.name == name)
            return entry.number;
    return std::nullopt;
}

// The kernel's AUDIT_ARCH_X86_64 and seccomp_data layout (linux/audit.h, linux/seccomp.h).
inline constexpr unsigned kAuditArchX86_64 = 0xC000003EU;
inline constexpr std::size_t kSeccompDataArch = 4;
inline constexpr std::size_t kSeccompDataArg0 = 16; // the low 32 bits of args[0], little-endian

// Numbers the sandbox calls itself (the kernel's ABI; the C library does not define the Landlock
// ones).
inline constexpr long kSysSeccomp = 317;
inline constexpr long kSysLandlockCreateRuleset = 444;
inline constexpr long kSysLandlockAddRule = 445;
inline constexpr long kSysLandlockRestrictSelf = 446;

} // namespace sketchyup::sandbox
