// R109 S6 (ADR 0183 decision 9): the runner can install the sandbox at all. This is a required
// fast-tier check. It fails when the kernel offers no Landlock, no PR_SET_NO_NEW_PRIVS or no
// seccomp filter, and nothing in it skips. It logs the kernel and the Landlock ABI, so the job log
// records what the runner offered. The checks:
//   1. the kernel and machine (uname), and the Landlock ABI (the version flag of
//      landlock_create_ruleset);
//   2. seccomp: the three actions the policies use (kill process, errno and allow) are available;
//   3. PR_SET_NO_NEW_PRIVS is accepted, and reads back as set, in a child;
//   4. the whole install in a child: apply() with a policy that allows only openat, then open("/")
//      must fail with EACCES, which shows that the Landlock ruleset restricts the process. A layer
//      that cannot be installed is named on stderr, and the child exits 70.
// The checks that change the process run in forked children, so this process stays unrestricted.
#include "sandbox/sandbox.hpp"
#include "sandbox/syscalls_x86_64.hpp"

#include <cerrno>
#include <cstdint>
#include <cstring>
#include <fcntl.h>
#include <functional>
#include <iostream>
#include <linux/seccomp.h>
#include <string>
#include <sys/prctl.h>
#include <sys/syscall.h>
#include <sys/utsname.h>
#include <sys/wait.h>
#include <unistd.h>

using namespace sketchyup::sandbox;

namespace {

int failures = 0;

void report(bool ok, const std::string &line) {
    std::cout << "sandbox_available: " << (ok ? "ok: " : "FAIL: ") << line << "\n";
    if (!ok)
        ++failures;
}

// Runs body in a forked child. Returns the child's exit status, or -1 when a signal ended it.
int inChild(const std::function<int()> &body) {
    std::cout << std::flush;
    const pid_t pid = fork();
    if (pid == 0)
        _exit(body());
    int status = 0;
    waitpid(pid, &status, 0);
    return WIFEXITED(status) ? WEXITSTATUS(status) : -1;
}

bool seccompActionAvailable(std::uint32_t action) {
    std::uint32_t value = action;
    return syscall(kSysSeccomp, SECCOMP_GET_ACTION_AVAIL, 0, &value) == 0;
}

void reportKernel() {
    utsname names{};
    uname(&names);
    std::cout << "sandbox_available: kernel " << names.release << " " << names.machine << "\n";
}

void checkLandlock() {
    const int abi = landlockAbiVersion();
    const int error = errno;
    if (abi >= 1)
        report(true, "Landlock ABI " + std::to_string(abi));
    else
        report(false, std::string("Landlock is not available (") + std::strerror(error) +
                          "); the sandbox cannot be installed on this runner");
}

void checkSeccomp() {
    const bool available = seccompActionAvailable(SECCOMP_RET_KILL_PROCESS) &&
                           seccompActionAvailable(SECCOMP_RET_ERRNO) &&
                           seccompActionAvailable(SECCOMP_RET_ALLOW);
    report(available, available ? "seccomp filter actions kill_process, errno and allow"
                                : "a seccomp filter action the sandbox uses is not available");
}

void checkNoNewPrivileges() {
    const int status = inChild([] {
        if (prctl(PR_SET_NO_NEW_PRIVS, 1UL, 0UL, 0UL, 0UL) != 0)
            return 1;
        return prctl(PR_GET_NO_NEW_PRIVS, 0UL, 0UL, 0UL, 0UL) == 1 ? 0 : 2;
    });
    report(status == 0, "PR_SET_NO_NEW_PRIVS is accepted");
}

void checkInstall() {
    const int status = inChild([] {
        Policy policy;
        policy.syscalls = {{"openat", Action::allow}};
#ifdef SKP_WORKER_NO_ADDRESS_LIMIT
        // The ASan runtime sets an alternate signal stack after the install (the helpers allow it
        // in their sanitizer builds too; ADR 0183 S6).
        policy.syscalls.push_back({"sigaltstack", Action::allow});
#endif
        if (const std::optional<Failure> failure = apply(policy)) {
            writeUnavailableLine(*failure);
            return 70;
        }
        // The policy grants no file, so Landlock refuses the open of the root directory.
        const int fd = ::open("/", O_RDONLY | O_DIRECTORY);
        return (fd < 0 && errno == EACCES) ? 0 : 1;
    });
    if (status == 0)
        report(true, "the sandbox installs (no_new_privs, Landlock and seccomp), and Landlock "
                     "refuses an open outside its grants");
    else if (status == 70)
        report(false,
               "the sandbox cannot be installed on this runner (the line above names the layer)");
    else
        report(false, "the installed sandbox did not refuse an open outside its grants (status " +
                          std::to_string(status) + ")");
}

} // namespace

int main() {
    reportKernel();
    checkLandlock();
    checkSeccomp();
    checkNoNewPrivileges();
    checkInstall();
    if (failures == 0)
        std::cout << "sandbox_available: passed\n";
    return failures == 0 ? 0 : 1;
}
