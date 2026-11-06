// The sandbox library on macOS, where the policy is one Seatbelt profile
// (src/sandbox/seatbelt.cpp). The checks follow sandbox_tests.cpp, the Linux library's, for what
// the profile is meant to do; each one that installs a policy runs in a forked child, because the
// policy is final for the process that installs it. Seatbelt refuses with EPERM and never kills, so
// where Linux's checks expect SIGSYS these expect the call to fail. The checks:
//   - the granted files open, other files and writes to read-only grants are EPERM, and metadata
//     stays readable (Landlock does not limit stat either);
//   - a read grant follows the symbolic links beneath it, and a write grant does not;
//   - exec needs both the execve rule and readExecute, and a program started under the profile
//     runs;
//   - fork needs a fork rule, and a thread needs none;
//   - the network: connect and bind are EPERM unless the policy allows them;
//   - signals: abort still ends the process, while a signal to another process is EPERM (a
//     debugger attach is not checked: macOS refuses it to a process without the debugging
//     entitlement, with a profile or without one);
//   - PreparedPolicy prepares in the parent and enters in a forked child;
//   - an unknown or conflicting rule, and a simulated failure of each layer, fail closed.
#include "sandbox/layers.hpp"
#include "sandbox/sandbox.hpp"

#include <arpa/inet.h>
#include <cerrno>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <filesystem>
#include <functional>
#include <iostream>
#include <netinet/in.h>
#include <string>
#include <sys/socket.h>
#include <sys/stat.h>
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

std::string describe(const ChildResult &result) {
    return result.signaled ? "signal " + std::to_string(result.signal)
                           : "code " + std::to_string(result.code);
}

// The calls every child makes to report its result, as the Linux tests name them.
std::vector<SyscallRule> withReporting(std::vector<SyscallRule> rules) {
    for (const char *name : {"write", "exit_group", "close"})
        rules.push_back({name, Action::allow});
    return rules;
}

// A scratch directory, canonical (the temporary directory is a link on macOS), removed at exit.
struct Scratch {
    std::string path;
    Scratch() {
        std::string pattern =
            (std::filesystem::temp_directory_path() / "sketchyup-seatbelt-XXXXXX").string();
        path = std::filesystem::canonical(::mkdtemp(pattern.data())).string();
    }
    ~Scratch() {
        std::error_code ignored;
        std::filesystem::remove_all(path, ignored);
    }
    std::string file(const std::string &name, const char *text = "text\n") const {
        const std::string full = path + "/" + name;
        std::filesystem::create_directories(std::filesystem::path(full).parent_path());
        const int fd = ::open(full.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0700);
        check(fd >= 0 && ::write(fd, text, std::strlen(text)) > 0, "write " + full);
        ::close(fd);
        return full;
    }
};

// open() in the child: 0 when it opens, the errno when it does not.
int openError(const std::string &path, int flags) {
    const int fd = ::open(path.c_str(), flags, 0600);
    if (fd >= 0) {
        ::close(fd);
        return 0;
    }
    return errno;
}

void testFileGrants() {
    const Scratch scratch;
    const std::string granted = scratch.file("granted.txt");
    const std::string other = scratch.file("other.txt");
    const std::string inReadOnly = scratch.file("read-only/inside.txt");
    std::filesystem::create_directories(scratch.path + "/private");
    const ChildResult result = inChild([&] {
        Policy policy;
        policy.files = {{granted, FileAccess::readOnly},
                        {scratch.path + "/read-only", FileAccess::readOnly},
                        {scratch.path + "/private", FileAccess::readWrite},
                        {scratch.path + "/missing", FileAccess::readWrite}};
        policy.syscalls = withReporting({{"openat", Action::allow}, {"read", Action::allow}});
        if (apply(policy))
            return 3;
        if (openError(granted, O_RDONLY) != 0 || openError(inReadOnly, O_RDONLY) != 0)
            return 1;
        if (openError(other, O_RDONLY) != EPERM)
            return 2;
        if (openError(granted, O_WRONLY) != EPERM)
            return 4;
        if (openError(scratch.path + "/read-only/new.txt", O_WRONLY | O_CREAT) != EPERM)
            return 5;
        if (openError(scratch.path + "/private/new.txt", O_WRONLY | O_CREAT) != 0)
            return 6;
        if (openError(scratch.path + "/new.txt", O_WRONLY | O_CREAT) != EPERM)
            return 7;
        struct stat status{};
        if (::stat(other.c_str(), &status) != 0)
            return 8;
        return 0;
    });
    check(!result.signaled && result.code == 0,
          "the granted files open, the private directory is writable, and other files and "
          "writes outside it are EPERM, while stat still answers (" +
              describe(result) + ")");
}

void testLinks() {
    const Scratch scratch;
    const std::string target = scratch.file("outside/target.txt");
    const std::string writeTarget = scratch.file("outside/write-target.txt");
    std::filesystem::create_directories(scratch.path + "/read");
    std::filesystem::create_directories(scratch.path + "/write");
    std::filesystem::create_symlink(scratch.path + "/outside/target.txt",
                                    scratch.path + "/read/link.txt");
    std::filesystem::create_symlink(writeTarget, scratch.path + "/write/link.txt");
    const ChildResult result = inChild([&] {
        Policy policy;
        policy.files = {{scratch.path + "/read", FileAccess::readOnly},
                        {scratch.path + "/write", FileAccess::readWrite}};
        policy.syscalls = withReporting({{"openat", Action::allow}, {"read", Action::allow}});
        if (apply(policy))
            return 3;
        if (openError(scratch.path + "/read/link.txt", O_RDONLY) != 0)
            return 1;
        if (openError(scratch.path + "/write/link.txt", O_RDONLY) != EPERM ||
            openError(scratch.path + "/write/link.txt", O_WRONLY) != EPERM)
            return 2;
        return 0;
    });
    check(!result.signaled && result.code == 0,
          "a read grant reaches the target of a link beneath it, and a write grant does not (" +
              describe(result) + ")");
}

// The system paths a program started under the profile needs: the loader maps the shared cache.
void grantSystem(Policy &policy) {
    for (const char *path :
         {"/usr/lib", "/System/Library", "/System/Cryptexes", "/System/Volumes/Preboot/Cryptexes"})
        policy.files.push_back({path, FileAccess::readOnly});
}

void testExec() {
    const auto execUnder = [](FileAccess access, bool execve) {
        return inChild([=] {
            Policy policy;
            grantSystem(policy);
            policy.files.push_back({"/bin/echo", access});
            policy.files.push_back({"/dev/null", FileAccess::readWrite});
            policy.syscalls = withReporting({});
            if (execve)
                policy.syscalls.push_back({"execve", Action::allow});
            if (apply(policy))
                return 3;
            const int devNull = ::open("/dev/null", O_WRONLY);
            if (devNull >= 0)
                ::dup2(devNull, 1);
            char *const argv[] = {const_cast<char *>("echo"), nullptr};
            ::execv("/bin/echo", argv);
            return errno == EPERM || errno == EACCES ? 10 : 11;
        });
    };
    const ChildResult noRule = execUnder(FileAccess::readExecute, false);
    check(!noRule.signaled && noRule.code == 10,
          "exec without an execve rule is refused (" + describe(noRule) + ")");
    const ChildResult readOnly = execUnder(FileAccess::readOnly, true);
    check(!readOnly.signaled && readOnly.code == 10,
          "readOnly does not grant exec (" + describe(readOnly) + ")");
    const ChildResult readExecute = execUnder(FileAccess::readExecute, true);
    check(!readExecute.signaled && readExecute.code == 0,
          "readExecute with an execve rule runs the program to its end (" + describe(readExecute) +
              ")");
}

void testForkAndThreads() {
    const auto forkUnder = [](std::vector<SyscallRule> rules) {
        return inChild([rules] {
            Policy policy;
            policy.syscalls = withReporting(rules);
            if (apply(policy))
                return 3;
            bool threaded = false;
            std::thread([&] { threaded = true; }).join();
            if (!threaded)
                return 4;
            const pid_t pid = ::fork();
            if (pid == 0)
                _exit(0);
            if (pid < 0)
                return errno == EPERM ? 10 : 11;
            int status = 0;
            ::waitpid(pid, &status, 0);
            return 0;
        });
    };
    const ChildResult threadOnly = forkUnder({{"clone", Action::threadOnly}});
    check(!threadOnly.signaled && threadOnly.code == 10,
          "threadOnly: a thread starts and fork is EPERM (" + describe(threadOnly) + ")");
    const ChildResult forkOrThread =
        forkUnder({{"clone", Action::forkOrThread}, {"wait4", Action::allow}});
    check(!forkOrThread.signaled && forkOrThread.code == 0,
          "forkOrThread: fork runs (" + describe(forkOrThread) + ")");
}

void testNetwork() {
    // A listener in this process, on the loopback interface: the child may not reach it.
    const int listener = ::socket(AF_INET, SOCK_STREAM, 0);
    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    socklen_t length = sizeof address;
    check(::bind(listener, reinterpret_cast<sockaddr *>(&address), sizeof address) == 0 &&
              ::listen(listener, 4) == 0 &&
              ::getsockname(listener, reinterpret_cast<sockaddr *>(&address), &length) == 0,
          "a loopback listener for the network checks");
    const auto connectUnder = [&](Action connect) {
        return inChild([&, connect] {
            Policy policy;
            policy.syscalls = withReporting({{"socket", Action::allow}, {"connect", connect}});
            if (apply(policy))
                return 3;
            const int fd = ::socket(AF_INET, SOCK_STREAM, 0);
            if (fd < 0)
                return errno == EPERM ? 10 : 11;
            if (::connect(fd, reinterpret_cast<const sockaddr *>(&address), sizeof address) != 0)
                return errno == EPERM ? 10 : 12;
            return 0;
        });
    };
    const ChildResult denied = connectUnder(Action::denied);
    check(!denied.signaled && denied.code == 10,
          "connect to a loopback listener is EPERM (" + describe(denied) + ")");
    const ChildResult allowed = connectUnder(Action::allow);
    check(!allowed.signaled && allowed.code == 0,
          "connect runs when the policy allows it (" + describe(allowed) + ")");
    ::close(listener);

    const ChildResult bound = inChild([] {
        Policy policy;
        policy.syscalls = withReporting({{"socket", Action::denied}, {"bind", Action::denied}});
        if (apply(policy))
            return 3;
        const int fd = ::socket(AF_INET, SOCK_STREAM, 0);
        if (fd < 0)
            return errno == EPERM ? 0 : 11;
        sockaddr_in any{};
        any.sin_family = AF_INET;
        any.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        if (::bind(fd, reinterpret_cast<const sockaddr *>(&any), sizeof any) != 0)
            return errno == EPERM ? 0 : 12;
        return 1;
    });
    check(!bound.signaled && bound.code == 0, "bind is EPERM (" + describe(bound) + ")");
}

void testSignals() {
    const ChildResult aborted = inChild([] {
        Policy policy;
        policy.syscalls = withReporting({{"tgkill", Action::ownProcessOnly}});
        if (apply(policy))
            return 3;
        std::abort();
    });
    check(aborted.signaled && aborted.signal == SIGABRT,
          "abort signals its own process (" + describe(aborted) + ")");

    // Another process: a sibling the parent starts and ends.
    const pid_t sibling = ::fork();
    if (sibling == 0) {
        ::pause();
        _exit(0);
    }
    const ChildResult other = inChild([sibling] {
        Policy policy;
        policy.syscalls = withReporting({{"tgkill", Action::ownProcessOnly}});
        if (apply(policy))
            return 3;
        return ::kill(sibling, SIGTERM) == -1 && errno == EPERM ? 0 : 1;
    });
    check(!other.signaled && other.code == 0,
          "a signal to another process is EPERM (" + describe(other) + ")");
    ::kill(sibling, SIGKILL);
    ::waitpid(sibling, nullptr, 0);
}

void testPreparedPolicy() {
    const Scratch scratch;
    const std::string granted = scratch.file("granted.txt");
    const std::string other = scratch.file("other.txt");
    Policy policy;
    policy.files = {{granted, FileAccess::readOnly}};
    policy.syscalls = withReporting({{"openat", Action::allow}});
    PreparedPolicy prepared;
    check(!prepared.prepare(policy), "the policy prepares in the parent");
    const ChildResult result = inChild([&] {
        if (prepared.enter())
            return 3;
        return openError(granted, O_RDONLY) == 0 && openError(other, O_RDONLY) == EPERM ? 0 : 1;
    });
    check(!result.signaled && result.code == 0,
          "a forked child enters the prepared policy (" + describe(result) + ")");
    check(openError(other, O_RDONLY) == 0, "the parent stays unrestricted");
}

void testFailsClosed() {
    const auto refusal = [](std::vector<SyscallRule> rules) {
        Policy policy;
        policy.syscalls = std::move(rules);
        PreparedPolicy prepared;
        return prepared.prepare(policy);
    };
    const auto unknown = refusal({{"no_such_call", Action::allow}});
    check(unknown && unknown->layer == Layer::seatbelt && unknown->errnoValue == EINVAL &&
              unavailableReason(*unknown) == "sandboxUnavailable: seatbelt (Invalid argument)",
          "an unknown name fails closed at the seatbelt layer");
    const auto twice = refusal({{"read", Action::allow}, {"read", Action::denied}});
    check(twice && twice->layer == Layer::seatbelt && twice->errnoValue == EINVAL,
          "one call named with two rules fails closed");
    check(!refusal({{"read", Action::allow}, {"read", Action::allow}}),
          "one call named twice with the same rule is accepted");

    for (const Layer layer : {Layer::noNewPrivileges, Layer::landlock, Layer::seccomp}) {
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
    check(std::string(layerName(Layer::seatbelt)) == "seatbelt", "the seatbelt layer's name");
}

void testProfileText() {
    Policy policy;
    policy.files = {{"/path/that/does/not/exist", FileAccess::readWrite}};
    policy.syscalls = {{"socket", Action::denied}, {"connect", Action::denied}};
    const std::string text = profileText(policy);
    check(text.find("(deny default)") != std::string::npos, "the profile denies by default");
    check(text.find("does/not/exist") == std::string::npos,
          "a path that does not exist grants nothing");
    check(text.find("network") == std::string::npos && text.find("process-") == std::string::npos,
          "denied sockets, and no fork or exec rule, grant no network, fork or exec");
}

} // namespace

int main() {
    testProfileText();
    testFileGrants();
    testLinks();
    testExec();
    testForkAndThreads();
    testNetwork();
    testSignals();
    testPreparedPolicy();
    testFailsClosed();
    if (failures == 0)
        std::cout << "seatbelt tests passed\n";
    return failures == 0 ? 0 : 1;
}
