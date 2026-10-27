// The seccomp-bpf layer (ADR 0183 sections 3 and 4.3): a raw BPF program, with no library. The
// program checks the architecture, loads the call number and compares it with each named call in
// turn. A match runs that call's block: one return for allow, EPERM or ENOSYS, for a thread-only clone a
// test of CLONE_THREAD, and for a clone that may fork a test of the namespace flags. Any other call falls
// through to the kill default.
#include "sandbox/layers.hpp"
#include "sandbox/syscalls_x86_64.hpp"

#include <cerrno>
#include <cstdint>
#include <linux/audit.h>
#include <linux/filter.h>
#include <linux/seccomp.h>
#include <map>
#include <unistd.h>
#include <vector>

namespace sketchyup::sandbox {
namespace {

constexpr std::uint32_t kCloneThread = 0x00010000; // CLONE_THREAD
// The namespace flags of clone: CLONE_NEWNS, CLONE_NEWCGROUP, CLONE_NEWUTS, CLONE_NEWIPC, CLONE_NEWUSER,
// CLONE_NEWPID and CLONE_NEWNET. A child in a new namespace gets capabilities inside it, so such a clone is refused.
constexpr std::uint32_t kCloneNewNamespaces = 0x7E020000;
constexpr std::uint32_t kSeccompDataNr = 0; // seccomp_data.nr, the call number

// The always-allowed calls (ADR 0183 section 4.3): the process ends, and a signal handler returns.
constexpr const char *kAlwaysAllowed[] = {"exit", "exit_group", "rt_sigreturn"};

sock_filter statement(std::uint16_t code, std::uint32_t k) { return sock_filter{code, 0, 0, k}; }
sock_filter jump(std::uint16_t code, std::uint32_t k, std::uint8_t ifTrue, std::uint8_t ifFalse) {
    return sock_filter{code, ifTrue, ifFalse, k};
}

// The instructions that answer for one call. A jump over a block takes its length, so each block is
// short: the longest are the clone, the own-process signal and the argument test, at four instructions.
std::vector<sock_filter> blockFor(const SyscallRule &rule, std::uint32_t self) {
    switch (rule.action) {
    case Action::allow:
        return {statement(BPF_RET | BPF_K, SECCOMP_RET_ALLOW)};
    case Action::denied:
        return {statement(BPF_RET | BPF_K, SECCOMP_RET_ERRNO | EPERM)};
    case Action::notImplemented:
        return {statement(BPF_RET | BPF_K, SECCOMP_RET_ERRNO | ENOSYS)};
    case Action::threadOnly:
        // flags & CLONE_THREAD: allow; otherwise EPERM (a fork-like clone).
        return {statement(BPF_LD | BPF_W | BPF_ABS, kSeccompDataArg0),
                jump(BPF_JMP | BPF_JSET | BPF_K, kCloneThread, 1, 0),
                statement(BPF_RET | BPF_K, SECCOMP_RET_ERRNO | EPERM),
                statement(BPF_RET | BPF_K, SECCOMP_RET_ALLOW)};
    case Action::forkOrThread:
        // flags & CLONE_NEW*: EPERM; otherwise allow (a thread, or a fork-like child that inherits this filter).
        return {statement(BPF_LD | BPF_W | BPF_ABS, kSeccompDataArg0),
                jump(BPF_JMP | BPF_JSET | BPF_K, kCloneNewNamespaces, 0, 1),
                statement(BPF_RET | BPF_K, SECCOMP_RET_ERRNO | EPERM),
                statement(BPF_RET | BPF_K, SECCOMP_RET_ALLOW)};
    case Action::ownProcessOnly:
        // args[0] is this process: allow; another process: EPERM.
        return {statement(BPF_LD | BPF_W | BPF_ABS, kSeccompDataArg0),
                jump(BPF_JMP | BPF_JEQ | BPF_K, self, 1, 0),
                statement(BPF_RET | BPF_K, SECCOMP_RET_ERRNO | EPERM),
                statement(BPF_RET | BPF_K, SECCOMP_RET_ALLOW)};
    case Action::whenArgument:
        // the argument is the value: allow; otherwise EPERM.
        return {statement(BPF_LD | BPF_W | BPF_ABS, seccompDataArg(rule.argumentIndex)),
                jump(BPF_JMP | BPF_JEQ | BPF_K, rule.argumentValue, 0, 1),
                statement(BPF_RET | BPF_K, SECCOMP_RET_ALLOW),
                statement(BPF_RET | BPF_K, SECCOMP_RET_ERRNO | EPERM)};
    }
    return {statement(BPF_RET | BPF_K, SECCOMP_RET_KILL_PROCESS)};
}

// The whole program, or nothing when a name is unknown or appears twice.
std::optional<std::vector<sock_filter>> buildProgram(const std::vector<SyscallRule> &rules) {
    std::vector<SyscallRule> all = rules;
    for (const char *name : kAlwaysAllowed)
        all.push_back(SyscallRule{name, Action::allow});

    std::vector<sock_filter> program = {
        statement(BPF_LD | BPF_W | BPF_ABS, kSeccompDataArch),
        jump(BPF_JMP | BPF_JEQ | BPF_K, kAuditArchX86_64, 1, 0),
        statement(BPF_RET | BPF_K, SECCOMP_RET_KILL_PROCESS),
        statement(BPF_LD | BPF_W | BPF_ABS, kSeccompDataNr),
    };
    // A call named twice with the same rule is one entry; with two different rules it is an error.
    // This process's ID, for the own-process rule: read here, before the filter is installed.
    const auto self = static_cast<std::uint32_t>(getpid());
    std::map<long, SyscallRule> seen;
    for (const SyscallRule &rule : all) {
        const std::optional<long> number = syscallNumber(rule.name);
        if (!number)
            return std::nullopt;
        if (rule.argumentIndex > 5)
            return std::nullopt;
        const auto [entry, fresh] = seen.emplace(*number, rule);
        if (!fresh) {
            const SyscallRule &previous = entry->second;
            if (previous.action != rule.action || previous.argumentIndex != rule.argumentIndex ||
                previous.argumentValue != rule.argumentValue)
                return std::nullopt;
            continue;
        }
        const std::vector<sock_filter> block = blockFor(rule, self);
        program.push_back(jump(BPF_JMP | BPF_JEQ | BPF_K, static_cast<std::uint32_t>(*number), 0,
                               static_cast<std::uint8_t>(block.size())));
        program.insert(program.end(), block.begin(), block.end());
    }
    program.push_back(statement(BPF_RET | BPF_K, SECCOMP_RET_KILL_PROCESS));
    return program;
}

} // namespace

std::optional<Failure> installSeccomp(const std::vector<SyscallRule> &rules) {
    const std::optional<std::vector<sock_filter>> program = buildProgram(rules);
    if (!program)
        return Failure{Layer::seccomp, EINVAL};
    sock_fprog filter{static_cast<unsigned short>(program->size()),
                      const_cast<sock_filter *>(program->data())};
    const long installed =
        syscall(kSysSeccomp, static_cast<long>(SECCOMP_SET_MODE_FILTER), 0L, &filter);
    if (installed != 0)
        return Failure{Layer::seccomp, errno};
    return std::nullopt;
}

} // namespace sketchyup::sandbox
