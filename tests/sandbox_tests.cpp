// R109 S0 (ADR 0183): the sandbox library on its own. Each check that installs a policy runs in a
// forked child, because the policy is final for the process that installs it. The checks:
//   - the syscall table agrees with the C library's SYS_ constants;
//   - an unlisted call kills the process with SIGSYS (the kill default);
//   - an explicit EPERM entry and an ENOSYS entry return those errors;
//   - a clone without CLONE_THREAD is EPERM, and a thread can still be created;
//   - Landlock grants the listed file and refuses the rest;
//   - an unknown name, and a simulated failure of each layer, fail closed with the named reason.
// Landlock must be available on the machine: when it is not, the Landlock checks fail, and nothing
// is skipped (ADR 0183 decision 9).
#include "sandbox/sandbox.hpp"
#include "sandbox/syscalls_x86_64.hpp"

#include <cerrno>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <functional>
#include <iostream>
#include <sched.h>
#include <string>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <sys/wait.h>
#include <thread>
#include <unistd.h>
#include <vector>

using namespace sketchyup::sandbox;

namespace {

int failures = 0;

void check(bool ok, const std::string &message) {
    if (!ok) {
        std::cerr << "FAIL: " << message << "\n";
        ++failures;
    }
}

struct ChildResult {
    bool signaled = false;
    int signal = 0;
    int code = -1;
};

// Runs body in a forked child. The child exits with body's return value, so the parent can tell
// what the policy did: a kill shows as a signal.
ChildResult inChild(const std::function<int()> &body) {
    std::fflush(nullptr);
    const pid_t pid = fork();
    if (pid == 0)
        _exit(body());
    int status = 0;
    waitpid(pid, &status, 0);
    ChildResult result;
    result.signaled = WIFSIGNALED(status);
    result.signal = result.signaled ? WTERMSIG(status) : 0;
    result.code = result.signaled ? -1 : WEXITSTATUS(status);
    return result;
}

// The policy every child needs to report its result through write and exit.
std::vector<SyscallRule> reporting() {
    return {{"write", Action::allow}, {"exit_group", Action::allow}, {"close", Action::allow}};
}

std::vector<SyscallRule> withReporting(std::vector<SyscallRule> rules) {
    for (const SyscallRule &rule : reporting())
        rules.push_back(rule);
    return rules;
}

void testTableMatchesCLibrary() {
    // The numbers the C library defines for the names the policies use.
    const auto matches = [](const char *name, long number) {
        const std::optional<long> listed = syscallNumber(name);
        return listed && *listed == number;
    };
    check(matches("read", SYS_read), "read");
    check(matches("write", SYS_write), "write");
    check(matches("writev", SYS_writev), "writev");
    check(matches("close", SYS_close), "close");
    check(matches("fstat", SYS_fstat), "fstat");
    check(matches("lseek", SYS_lseek), "lseek");
    check(matches("openat", SYS_openat), "openat");
    check(matches("brk", SYS_brk), "brk");
    check(matches("mmap", SYS_mmap), "mmap");
    check(matches("mprotect", SYS_mprotect), "mprotect");
    check(matches("munmap", SYS_munmap), "munmap");
    check(matches("mremap", SYS_mremap), "mremap");
    check(matches("madvise", SYS_madvise), "madvise");
    check(matches("rt_sigaction", SYS_rt_sigaction), "rt_sigaction");
    check(matches("rt_sigprocmask", SYS_rt_sigprocmask), "rt_sigprocmask");
    check(matches("futex", SYS_futex), "futex");
    check(matches("set_robust_list", SYS_set_robust_list), "set_robust_list");
    check(matches("rseq", SYS_rseq), "rseq");
    check(matches("clock_nanosleep", SYS_clock_nanosleep), "clock_nanosleep");
    check(matches("prlimit64", SYS_prlimit64), "prlimit64");
    check(matches("exit", SYS_exit), "exit");
    check(matches("exit_group", SYS_exit_group), "exit_group");
    check(matches("clone", SYS_clone), "clone");
    check(matches("clone3", SYS_clone3), "clone3");
    check(matches("socket", SYS_socket), "socket");
    check(matches("execve", SYS_execve), "execve");
    check(matches("ptrace", SYS_ptrace), "ptrace");
    check(matches("seccomp", SYS_seccomp), "seccomp");
    check(matches("getpid", SYS_getpid), "getpid");
    check(matches("gettid", SYS_gettid), "gettid");
    check(matches("tgkill", SYS_tgkill), "tgkill");
    check(!syscallNumber("no_such_call"), "an unknown name is not in the table");
    check(kSysLandlockCreateRuleset == 444 && kSysLandlockAddRule == 445 &&
              kSysLandlockRestrictSelf == 446,
          "the Landlock numbers");
}

void testKillDefault() {
    const ChildResult result = inChild([] {
        Policy policy;
        policy.syscalls = withReporting({});
        if (apply(policy))
            return 3;
        getpid(); // not in the policy: the kill default ends the process
        return 0;
    });
    check(result.signaled && result.signal == SIGSYS,
          "an unlisted call (getpid) is killed with SIGSYS");
}

void testOwnProcessSignal() {
    // abort() signals its own process, so under the own-process rule it is SIGABRT: a crash, not a violation.
    const ChildResult aborted = inChild([] {
        Policy policy;
        policy.syscalls = withReporting({{"tgkill", Action::ownProcessOnly},
                                         {"getpid", Action::allow},
                                         {"gettid", Action::allow},
                                         {"rt_sigprocmask", Action::allow},
                                         {"rt_sigaction", Action::allow}});
        if (apply(policy)) return 3;
        std::abort();
    });
    check(aborted.signaled && aborted.signal == SIGABRT,
          "abort under the own-process rule is SIGABRT, not SIGSYS (signal " + std::to_string(aborted.signal) + ")");

    // A signal to another process is refused with EPERM.
    const pid_t parent = getpid();
    const ChildResult other = inChild([parent] {
        Policy policy;
        policy.syscalls = withReporting({{"tgkill", Action::ownProcessOnly}});
        if (apply(policy)) return 3;
        const long result = syscall(SYS_tgkill, static_cast<long>(parent), static_cast<long>(parent), SIGUSR1);
        return (result == -1 && errno == EPERM) ? 0 : 1;
    });
    check(!other.signaled && other.code == 0, "a signal to another process is EPERM");
}

void testExplicitDenials() {
    const ChildResult denied = inChild([] {
        Policy policy;
        policy.syscalls = withReporting({{"socket", Action::denied}});
        if (apply(policy))
            return 3;
        const int fd = socket(AF_INET, SOCK_STREAM, 0);
        return (fd == -1 && errno == EPERM) ? 0 : 1;
    });
    check(!denied.signaled && denied.code == 0, "a socket under an EPERM entry returns EPERM");

    const ChildResult notImplemented = inChild([] {
        Policy policy;
        policy.syscalls = withReporting({{"clone3", Action::notImplemented}});
        if (apply(policy))
            return 3;
        const long result = syscall(SYS_clone3, nullptr, 0UL);
        return (result == -1 && errno == ENOSYS) ? 0 : 1;
    });
    check(!notImplemented.signaled && notImplemented.code == 0,
          "clone3 under an ENOSYS entry returns ENOSYS");
}

void testThreadOnlyClone() {
    const ChildResult forked = inChild([] {
        // glibc's fork makes these calls before its clone, and clone3 is ENOSYS so that it falls
        // back to clone.
        Policy policy;
        policy.syscalls = withReporting({{"clone", Action::threadOnly},
                                         {"clone3", Action::notImplemented},
                                         {"rt_sigprocmask", Action::allow},
                                         {"rt_sigaction", Action::allow},
                                         {"set_robust_list", Action::allow},
                                         {"rseq", Action::allow},
                                         {"futex", Action::allow},
                                         {"getpid", Action::allow},
                                         {"gettid", Action::allow}});
        if (apply(policy))
            return 3;
        const pid_t pid = fork();
        if (pid == 0)
            _exit(0);
        return (pid == -1 && errno == EPERM) ? 0 : 1;
    });
    check(!forked.signaled && forked.code == 0,
          "a fork-like clone under a thread-only entry returns EPERM");

    const ChildResult threaded = inChild([] {
        Policy policy;
        policy.syscalls = withReporting({{"clone", Action::threadOnly},
                                         {"clone3", Action::notImplemented},
                                         {"mmap", Action::allow},
                                         {"mprotect", Action::allow},
                                         {"munmap", Action::allow},
                                         {"futex", Action::allow},
                                         {"set_robust_list", Action::allow},
                                         {"rseq", Action::allow},
                                         {"rt_sigprocmask", Action::allow},
                                         {"rt_sigaction", Action::allow},
                                         {"madvise", Action::allow},
                                         {"exit", Action::allow}});
        if (apply(policy))
            return 3;
        bool ran = false;
        std::thread worker([&ran] { ran = true; });
        worker.join();
        return ran ? 0 : 1;
    });
    check(!threaded.signaled && threaded.code == 0,
          "a thread can be created under the thread-only entry");
}

void testForkOrThreadClone() {
    // ADR 0183 decision 5: a fork-like clone runs (the IFC diagnosis children), and its child
    // inherits the filter. A clone that asks for a new namespace is EPERM: a namespace gives the
    // process capabilities inside it.
    const ChildResult forked = inChild([] {
        Policy policy;
        policy.syscalls = withReporting({{"clone", Action::forkOrThread},
                                         {"clone3", Action::notImplemented},
                                         {"wait4", Action::allow},
                                         {"rt_sigprocmask", Action::allow},
                                         {"rt_sigaction", Action::allow},
                                         {"set_robust_list", Action::allow},
                                         {"rseq", Action::allow},
                                         {"futex", Action::allow},
                                         {"getpid", Action::allow},
                                         {"gettid", Action::allow}});
        if (apply(policy))
            return 3;
        const pid_t pid = fork();
        if (pid == 0)
            _exit(0);
        if (pid == -1)
            return 1;
        int status = 0;
        if (waitpid(pid, &status, 0) != pid || !WIFEXITED(status) || WEXITSTATUS(status) != 0)
            return 2;
        return 0;
    });
    check(!forked.signaled && forked.code == 0,
          "a fork-like clone under the forkOrThread entry runs and its child exits 0");

    const ChildResult namespaced = inChild([] {
        Policy policy;
        policy.syscalls = withReporting({{"clone", Action::forkOrThread}});
        if (apply(policy))
            return 3;
        const long userNamespace =
            syscall(SYS_clone, static_cast<long>(CLONE_NEWUSER | SIGCHLD), 0L, 0L, 0L, 0L);
        if (userNamespace != -1 || errno != EPERM)
            return 1;
        const long netNamespace =
            syscall(SYS_clone, static_cast<long>(CLONE_NEWNET | SIGCHLD), 0L, 0L, 0L, 0L);
        return (netNamespace == -1 && errno == EPERM) ? 0 : 2;
    });
    check(!namespaced.signaled && namespaced.code == 0,
          "a clone with CLONE_NEWUSER or CLONE_NEWNET under the forkOrThread entry returns EPERM "
          "(code " +
              std::to_string(namespaced.code) + ")");
}

void testLandlockReadExecute() {
    // The exec right is granted by readExecute and not by readOnly: an executable's open fails with
    // EACCES under readOnly, and under readExecute it gets past the open (the file is not a
    // program, so execve answers ENOEXEC).
    const std::string directory = "/tmp/sketchyup-sandbox-exec-" + std::to_string(getpid());
    mkdir(directory.c_str(), 0700);
    const std::string script = directory + "/script";
    {
        const int fd = open(script.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0755);
        if (fd >= 0) {
            const char text[] = "not a program\n";
            check(write(fd, text, sizeof text - 1) > 0, "the exec test's script is written");
            close(fd);
        }
    }
    const auto execUnder = [&](FileAccess access) {
        return inChild([&, access] {
            Policy policy;
            policy.files = {{directory, access}};
            policy.syscalls = withReporting({{"execve", Action::allow}});
            if (apply(policy))
                return 3;
            char *const argv[] = {const_cast<char *>(script.c_str()), nullptr};
            char *const envp[] = {nullptr};
            execve(script.c_str(), argv, envp);
            return errno == EACCES ? 10 : (errno == ENOEXEC ? 0 : 11);
        });
    };
    const ChildResult readOnly = execUnder(FileAccess::readOnly);
    check(!readOnly.signaled && readOnly.code == 10,
          "readOnly does not grant the exec right (EACCES, code " + std::to_string(readOnly.code) +
              ")");
    const ChildResult readExecute = execUnder(FileAccess::readExecute);
    check(!readExecute.signaled && readExecute.code == 0,
          "readExecute grants the exec right (the file is refused as a program, ENOEXEC, code " +
              std::to_string(readExecute.code) + ")");
    unlink(script.c_str());
    rmdir(directory.c_str());
}

void testLandlockGrants() {
    const std::string directory = "/tmp/sketchyup-sandbox-tests-" + std::to_string(getpid());
    mkdir(directory.c_str(), 0700);
    const std::string granted = directory + "/granted.txt";
    const std::string denied = directory + "/denied.txt";
    {
        // Two files, both made before the policy: one is granted and the other is not.
        for (const std::string &path : {granted, denied}) {
            const int fd = open(path.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0600);
            if (fd >= 0)
                close(fd);
        }
    }
    const ChildResult result = inChild([&] {
        Policy policy;
        policy.files = {{granted, FileAccess::readOnly},
                        {directory + "/private", FileAccess::readWrite}};
        policy.syscalls = withReporting(
            {{"openat", Action::allow}, {"read", Action::allow}, {"fstat", Action::allow}});
        if (apply(policy))
            return 3;
        const int grantedFd = open(granted.c_str(), O_RDONLY);
        if (grantedFd < 0)
            return 1;
        close(grantedFd);
        const int deniedFd = open(denied.c_str(), O_RDONLY);
        if (deniedFd >= 0 || errno != EACCES)
            return 2;
        const int writable = open(granted.c_str(), O_WRONLY); // read-only grant: no write
        if (writable >= 0 || errno != EACCES)
            return 4;
        return 0;
    });
    check(!result.signaled && result.code == 0,
          "Landlock: the granted file opens, the other file and a write to the read-only file are "
          "EACCES (code " +
              std::to_string(result.code) + ")");
    unlink(granted.c_str());
    unlink(denied.c_str());
    rmdir(directory.c_str());
}

void testLandlockAvailable() {
    const int abi = landlockAbiVersion();
    check(abi >= 1, "Landlock is available on this kernel (ABI " + std::to_string(abi) +
                        ", errno " + std::strerror(errno) + "); the sandbox cannot run without it");
}

void testUnknownNameFailsClosed() {
    const ChildResult result = inChild([] {
        Policy policy;
        policy.syscalls = {{"no_such_call", Action::allow}};
        const auto failure = apply(policy);
        if (!failure)
            return 1;
        return (failure->layer == Layer::seccomp && failure->errnoValue == EINVAL &&
                unavailableReason(*failure) ==
                    "sandboxUnavailable: seccomp filter (Invalid argument)")
                   ? 0
                   : 2;
    });
    check(!result.signaled && result.code == 0,
          "an unknown name fails closed at the seccomp layer");
}

void testInjectedFailures() {
    const std::vector<Layer> layers = {Layer::noNewPrivileges, Layer::landlock, Layer::seccomp};
    for (const Layer layer : layers) {
        const ChildResult result = inChild([layer] {
            ApplyOptions options;
            options.simulate = Failure{layer, EPERM};
            const auto failure = apply(Policy{}, options);
            if (!failure || failure->layer != layer)
                return 1;
            return unavailableReason(*failure) == std::string("sandboxUnavailable: ") +
                                                      layerName(layer) +
                                                      " (Operation not permitted)"
                       ? 0
                       : 2;
        });
        check(!result.signaled && result.code == 0, std::string("a simulated failure of the ") +
                                                        layerName(layer) +
                                                        " layer is named and returned");
    }
    check(std::string(layerName(Layer::seccomp)) == "seccomp filter", "the seccomp layer's name");
    check(std::string(layerName(Layer::landlock)) == "landlock", "the landlock layer's name");
}

} // namespace

int main() {
    testTableMatchesCLibrary();
    testLandlockAvailable();
    testKillDefault();
    testOwnProcessSignal();
    testExplicitDenials();
    testThreadOnlyClone();
    testForkOrThreadClone();
    testLandlockReadExecute();
    testLandlockGrants();
    testUnknownNameFailsClosed();
    testInjectedFailures();
    if (failures == 0)
        std::cout << "sandbox tests passed\n";
    return failures == 0 ? 0 : 1;
}
