// R109 S5 (ADR 0183): the Blender job's child under its sandbox. The checks:
//   - every name in the Blender allow-list is in the system call table, and the policy prepares
//   (its
//     program fits the kernel's limit and its rule paths open);
//   - a child under the policy refuses the attacks of ADR 0183 section 6.4 and section 11 S5: a
//   socket
//     (EPERM), an exec of /bin/sh or of another program (EACCES), a fork (EPERM), a ptrace (killed
//     with SIGSYS), a read of /etc/passwd (EACCES), a write outside the attempt directory (EACCES),
//     and a write inside it, or a read of the input, which work. An abort is SIGABRT, not a
//     violation;
//   - the negative control: the same policy without its socket entries kills the child at the first
//     socket call (SIGSYS), so the kill default is in force (ADR 0183 section 6.4);
//   - a layer that cannot be installed is refused: a simulated Landlock failure before the fork,
//   and a
//     simulated no_new_privs or seccomp failure in the child, which exits 70 and names the layer;
//   - the job's host classes: a stand-in killed by SIGSYS is sandboxViolation and is not retried; a
//     stand-in that aborts is still a crash; a layer that cannot be prepared or installed is
//     sandboxUnavailable;
//   - with --real (and SKETCHYUP_BLENDER_TEST), the probes that the job runs under the sandbox give
//   the
//     same result as the same probes run by Blender directly, without a sandbox (ADR 0183
//     section 6.6). A render that writes a PNG through the launcher is blender_job_tests --real.
// The stand-ins are hard links to blender_job_tests (tests/blender_stand_in.hpp). Landlock must be
// available: the checks fail, not skip, when it is not (ADR 0183 decision 9). On macOS the layers
// are one Seatbelt profile: the probes expect its refusals (EPERM, never a kill;
// tests/sandbox_platform.hpp), and the negative control shows the profile's refusal instead of the
// kill default, which macOS does not have.
#include "blender_stand_in.hpp"
#include "integrations/blender_job.hpp"
#include "integrations/blender_sandbox_policy.hpp"
#include "sandbox/sandbox.hpp"
#include "sandbox/syscalls_x86_64.hpp"
#include "sandbox_platform.hpp"

#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QTemporaryDir>
#include <QThread>

#include <algorithm>
#include <cerrno>
#include <csignal>
#include <cstdio>
#include <cstring>
#include <fcntl.h>
#include <functional>
#include <iostream>
#include <netinet/in.h>
#include <string>
#include <sys/ptrace.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/wait.h>
#include <unistd.h>
#include <vector>

#ifdef SKP_WORKER_NO_ADDRESS_LIMIT
// The sanitizer builds. The probe children of this binary run under the Blender policy and then
// exit, and LeakSanitizer's check at exit stops the threads with ptrace, which that policy refuses
// (SIGSYS). The setting is built into the whole binary. Test code only (ADR 0183, R109 S5).
extern "C" const char *__lsan_default_options() { return "detect_leaks=0"; }
#endif

using namespace sketchy;
using Phase = BlenderJob::Phase;
namespace bsp = sketchy::blender_sandbox;
namespace sandbox = sketchyup::sandbox;

namespace {

int failures = 0;

void check(bool ok, const std::string &message) {
    if (!ok) {
        std::cerr << "FAIL: " << message << "\n";
        ++failures;
    }
}

QByteArray read(const QString &path) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return {};
    return file.readAll();
}

struct Outcome {
    bool signaled = false;
    int signal = 0;
    int code = -1;
    std::string out; // the child's standard output and standard error
};

// Runs body in a forked child whose output goes to a file, and reports how it ended. body returns
// the exit code. The child is the process the policy is applied to, so the parent is never
// restricted.
Outcome inChild(const std::string &outputFile, const std::function<int()> &body) {
    std::fflush(nullptr);
    const pid_t pid = fork();
    if (pid == 0) {
        const int fd = open(outputFile.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0600);
        if (fd >= 0) {
            dup2(fd, 1);
            dup2(fd, 2);
        }
        _exit(body());
    }
    int status = 0;
    waitpid(pid, &status, 0);
    Outcome outcome;
    outcome.signaled = WIFSIGNALED(status);
    outcome.signal = outcome.signaled ? WTERMSIG(status) : 0;
    outcome.code = outcome.signaled ? -1 : WEXITSTATUS(status);
    outcome.out = QByteArray(read(QString::fromStdString(outputFile))).toStdString();
    return outcome;
}

// The layout of an attempt in a scratch directory: its attempt directory and its input directory.
bsp::Layout layoutIn(const std::string &scratch) {
    bsp::Layout layout;
    layout.executable =
        QString::fromLocal8Bit(QCoreApplication::applicationFilePath().toLocal8Bit());
    layout.attemptDirectory = QString::fromStdString(scratch + "/attempt");
    layout.inputDirectory = QString::fromStdString(scratch + "/input");
    return layout;
}

// A connect that leaves the process: 0 when the sandbox refuses it (EPERM), 1 otherwise. Nothing
// listens on the loopback discard port, so a connect that reached the network is ECONNREFUSED.
int connectRefused() {
    const int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd == -1)
        return errno == EPERM ? 0 : 1;
    sockaddr_in loopback{};
    loopback.sin_family = AF_INET;
    loopback.sin_port = htons(9);
    loopback.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    return connect(fd, reinterpret_cast<sockaddr *>(&loopback), sizeof loopback) == -1 &&
                   errno == EPERM
               ? 0
               : 1;
}

void testPolicyBuilds(const std::string &scratch) {
    const auto rules = bsp::syscalls();
    std::size_t named = 0;
    for (const auto &rule : rules) {
        check(sandbox::syscallNumber(rule.name).has_value(),
              "the allow-list names " + std::string(rule.name) +
                  ", which the system call table lists");
        ++named;
    }
    std::cout << "Blender allow-list: " << named << " named calls\n";
    sandbox::PreparedPolicy prepared;
    const auto failure = prepared.prepare(bsp::policy(layoutIn(scratch)));
    check(!failure, "the Blender policy prepares (its program fits, its rule paths open)");
}

void testAttacks(const std::string &scratch) {
    const std::string attempt = scratch + "/attempt";
    const std::string input = scratch + "/input";
    const std::string sibling = scratch + "/outside.txt";
    {
        QDir(QString::fromStdString(attempt)).mkpath(".");
        QDir(QString::fromStdString(input)).mkpath(".");
        QFile file(QString::fromStdString(input + "/scene.glb"));
        check(file.open(QIODevice::WriteOnly), "the input fixture opens");
        file.write("scene");
        file.close();
        QFile outside(QString::fromStdString(sibling));
        check(outside.open(QIODevice::WriteOnly), "the outside fixture opens");
        outside.write("outside");
        outside.close();
    }
    // The policy is prepared here, before each fork, and entered in the child, as the job does
    // (ADR 0183 section 4.1); macOS also stops a forked child of this process that builds it.
    const auto layout = layoutIn(scratch);
    const auto refused = [&](const std::function<int()> &attack) {
        sandbox::PreparedPolicy prepared;
        const bool ready = !prepared.prepare(bsp::policy(layout));
        return inChild(scratch + "/attack.out", [&] {
            if (!ready || prepared.enter())
                return 3;
            return attack();
        });
    };
    const std::string refusal = std::string(" (") + sandbox_platform::refusedPathName + ")";

    // Linux refuses the socket itself; Seatbelt refuses its use, so macOS checks a connect.
    const auto socketCall = refused(connectRefused);
    check(!socketCall.signaled && socketCall.code == 0, "a socket is refused (EPERM)");

    const auto unixSocket = refused([] {
        const int fd = socket(AF_UNIX, SOCK_STREAM, 0);
        if (fd == -1)
            return errno == EPERM ? 0 : 1;
        // macOS: a connect to the system's DNS service socket
        sockaddr_un address{};
        address.sun_family = AF_UNIX;
        std::strcpy(address.sun_path, "/private/var/run/mDNSResponder");
        return connect(fd, reinterpret_cast<sockaddr *>(&address), sizeof address) == -1 &&
                       errno == EPERM
                   ? 0
                   : 1;
    });
    check(!unixSocket.signaled && unixSocket.code == 0, "a UNIX socket is refused (EPERM)");

    const auto shell = refused([] {
        char *const argv[] = {const_cast<char *>("/bin/sh"), const_cast<char *>("-c"),
                              const_cast<char *>("exit 7"), nullptr};
        execv("/bin/sh", argv);
        return (errno == sandbox_platform::refusedPath) ? 0 : 1;
    });
    check(!shell.signaled && shell.code == 0, "an exec of /bin/sh is refused" + refusal);

    const auto otherProgram = refused([] {
        // false: a program that ran would exit 1, not the probe's 0
        char *const argv[] = {const_cast<char *>("/usr/bin/false"), nullptr};
        execv("/usr/bin/false", argv);
        return (errno == sandbox_platform::refusedPath) ? 0 : 1;
    });
    check(!otherProgram.signaled && otherProgram.code == 0,
          "an exec of another program than Blender is refused" + refusal);

    const auto forked = refused([] {
        const pid_t pid = fork();
        if (pid == 0)
            _exit(0);
        return (pid == -1 && errno == EPERM) ? 0 : 1;
    });
    check(!forked.signaled && forked.code == 0, "a fork is refused (EPERM)");

#ifdef __linux__
    // The policy does not name ptrace, so the filter kills the child. macOS has no such probe:
    // Seatbelt does not govern PT_TRACE_ME, and macOS refuses an attach to a process without the
    // debugging entitlement whether a profile is in force or not, so a refusal there would not show
    // the profile's.
    const auto traced = refused([] {
        ptrace(PTRACE_TRACEME, 0, nullptr, nullptr);
        return 0;
    });
    check(traced.signaled && traced.signal == SIGSYS, "a ptrace is killed with SIGSYS");
#endif

    const auto passwd = refused([] {
        const int fd = open("/etc/passwd", O_RDONLY);
        return (fd < 0 && errno == sandbox_platform::refusedPath) ? 0 : 1;
    });
    check(!passwd.signaled && passwd.code == 0, "/etc/passwd is refused" + refusal);

    const auto writeOutside = refused([&] {
        const int fd = open(sibling.c_str(), O_WRONLY | O_TRUNC);
        return (fd < 0 && errno == sandbox_platform::refusedPath) ? 0 : 1;
    });
    check(!writeOutside.signaled && writeOutside.code == 0,
          "a write outside the attempt directory is refused" + refusal);

    const auto writeInside = refused([&] {
        const int fd = open((attempt + "/inside.txt").c_str(), O_WRONLY | O_CREAT, 0600);
        return fd >= 0 ? 0 : 1;
    });
    check(!writeInside.signaled && writeInside.code == 0,
          "a write inside the attempt directory works");

    const auto readInput = refused([&] {
        const int fd = open((input + "/scene.glb").c_str(), O_RDONLY);
        return fd >= 0 ? 0 : 1;
    });
    check(!readInput.signaled && readInput.code == 0, "the input can be read");

    const auto aborted = refused([] {
        std::abort();
        return 0;
    });
    check(aborted.signaled && aborted.signal == SIGABRT,
          "an abort under the policy is SIGABRT, a crash and not a sandbox violation");
}

void testNegativeControl(const std::string &scratch) {
#ifdef __APPLE__
    // macOS has no kill default. Its control shows that the refusal above is the profile's: the
    // Blender policy with its socket rules allowed instead lets the same connect reach the network
    // (ECONNREFUSED, nothing listens), where the policy itself refuses it (EPERM).
    sandbox::Policy policy = bsp::policy(layoutIn(scratch));
    for (sandbox::SyscallRule &rule : policy.syscalls)
        if (rule.name == "socket" || rule.name == "connect")
            rule.action = sandbox::Action::allow;
    sandbox::PreparedPolicy prepared;
    const bool ready = !prepared.prepare(policy);
    const auto outcome = inChild(scratch + "/negative.out", [&] {
        if (!ready || prepared.enter())
            return 3;
        return connectRefused();
    });
    check(!outcome.signaled && outcome.code == 1,
          "the negative control: with its socket rules allowed, the policy lets a connect through "
          "(code " +
              std::to_string(outcome.code) + ")");
#else
    // The Blender policy without its socket entries: a socket call is not named, so it is killed.
    static const char *const sockets[] = {"socket",     "socketpair", "bind",    "connect",
                                          "listen",     "accept",     "accept4", "sendto",
                                          "sendmsg",    "recvfrom",   "recvmsg", "getsockname",
                                          "getsockopt", "setsockopt", "shutdown"};
    auto rules = bsp::syscalls();
    rules.erase(std::remove_if(rules.begin(), rules.end(),
                               [](const sandbox::SyscallRule &rule) {
                                   for (const char *name : sockets)
                                       if (rule.name == name)
                                           return true;
                                   return false;
                               }),
                rules.end());
    const auto layout = layoutIn(scratch);
    const auto outcome = inChild(scratch + "/negative.out", [&] {
        sandbox::Policy policy = bsp::policy(layout);
        policy.syscalls = rules;
        sandbox::PreparedPolicy prepared;
        if (prepared.prepare(policy) || prepared.enter())
            return 3;
        const int fd = socket(AF_INET, SOCK_STREAM, 0);
        return fd >= 0 ? 0 : 1;
    });
    check(outcome.signaled && outcome.signal == SIGSYS,
          "the negative control: without its socket entries the policy kills the first socket "
          "(SIGSYS)");
#endif
}

void testFailClosed(const std::string &scratch) {
    const auto layout = layoutIn(scratch);
    // Before the fork: a Landlock failure is reported by prepare, and nothing is installed.
    {
        sandbox::PreparedPolicy prepared;
        const auto failure = prepared.prepare(bsp::policy(layout),
                                              {sandbox::Failure{sandbox::Layer::landlock, ENOSYS}});
        check(failure && failure->layer == sandbox::Layer::landlock,
              "a simulated Landlock failure is reported before the fork");
    }
    // In the child: a no_new_privs or seccomp failure exits 70 and writes the layer's name, and the
    // attack that follows is never reached.
    for (const sandbox::Layer layer : {sandbox::Layer::noNewPrivileges, sandbox::Layer::seccomp}) {
        sandbox::PreparedPolicy prepared;
        const bool ready = !prepared.prepare(bsp::policy(layout), {sandbox::Failure{layer, EPERM}});
        const auto simulated = inChild(scratch + "/simulated.out", [&] {
            if (!ready)
                return 3;
            if (const auto failure = prepared.enter()) {
                sandbox::writeUnavailableLine(*failure);
                return 70;
            }
            return 0;
        });
        const std::string expected = std::string("sandboxUnavailable: ") +
                                     sandbox::layerName(layer) + " (errno " +
                                     std::to_string(EPERM) + ")";
        check(!simulated.signaled && simulated.code == 70 &&
                  simulated.out.find(expected) != std::string::npos,
              "a simulated " + std::string(sandbox::layerName(layer)) +
                  " failure in the child exits 70 and names the layer (" + expected + ")");
    }
}

// The job's host classes, with the stand-ins of blender_job_tests (the same launcher, the same
// policy).
void waitFor(BlenderJob &job, int limitMs = 60000) {
    QElapsedTimer timer;
    timer.start();
    while (!job.done() && timer.elapsed() < limitMs) {
        QCoreApplication::processEvents();
        QThread::msleep(5);
    }
}

void testHostClasses(const std::string &scratch) {
    const QString directory = QString::fromStdString(scratch) + "/stand-ins";
    QDir().mkpath(directory);
    const auto options = [&](const QString &mode) {
        BlenderJob::Options job;
        job.executable = blender_stand_in::make(directory, mode);
        job.timeoutMs = 20000;
        return job;
    };

    // A seccomp kill: sandboxViolation, reported once (no retry).
    BlenderJob violation;
    violation.probe(options("violate"));
    waitFor(violation);
    check(violation.phase() == Phase::Failed &&
              violation.report().value("code").toString() == "sandboxViolation",
          "a stand-in killed by SIGSYS is sandboxViolation");
    check(violation.report().value("attempts").toArray().size() == 1,
          "a sandbox violation is not retried");

    // An ordinary crash stays a crash.
    BlenderJob crash;
    crash.probe(options("abort"));
    waitFor(crash);
    check(crash.phase() == Phase::Failed &&
              crash.report().value("code").toString() != "sandboxViolation",
          "a stand-in that aborts is a crash, not a sandbox violation");

    // A layer that cannot be prepared refuses the job before the fork.
    BlenderJob unprepared;
    auto refusedBefore = options("success");
    refusedBefore.sandboxSimulation = sandbox::Failure{sandbox::Layer::landlock, ENOSYS};
    unprepared.probe(refusedBefore);
    waitFor(unprepared);
    check(unprepared.phase() == Phase::Unavailable &&
              unprepared.report().value("code").toString() == "sandboxUnavailable" &&
              unprepared.report().value("message").toString().contains("landlock") &&
              unprepared.processId() == 0,
          "a layer that cannot be prepared refuses the job before Blender starts");

    // A layer that cannot be installed in the child exits 70: refused, and not retried.
    BlenderJob refusedAfter;
    auto refusedInChild = options("success");
    refusedInChild.sandboxSimulation = sandbox::Failure{sandbox::Layer::seccomp, EPERM};
    refusedAfter.probe(refusedInChild);
    waitFor(refusedAfter);
    check(refusedAfter.phase() == Phase::Unavailable &&
              refusedAfter.report().value("code").toString() == "sandboxUnavailable" &&
              refusedAfter.report().value("attempts").toArray().size() == 1 &&
              refusedAfter.report().value("attempts").toArray()[0].toObject().value("exitCode") ==
                  bsp::kRefusedExit,
          "a layer that cannot be installed in the child refuses the job with exit 70");
    check(refusedAfter.report().value("message").toString().contains("seccomp filter"),
          "the refusal names the layer that did not install");
}

// The reason a job did not succeed: its code and message, then each attempt's exit code or the
// signal that killed it (SIGSYS is a seccomp kill), the worker's status and the tail of its output
// (standard output and standard error, merged).
void explain(const BlenderJob &job) {
    const auto report = job.report();
    std::cerr << "  job code " << report.value("code").toString().toStdString() << ": "
              << report.value("message").toString().toStdString() << "\n";
    for (const auto item : report.value("attempts").toArray()) {
        const auto attempt = item.toObject();
        const int code = attempt.value("exitCode").toInt();
        if (attempt.value("normalExit").toBool())
            std::cerr << "  attempt exit code " << code << "\n";
        else
            std::cerr << "  attempt killed by signal " << code << " (" << strsignal(code) << ")\n";
        std::cerr << "  worker status " << attempt.value("workerStatus").toString().toStdString()
                  << ", code " << attempt.value("code").toString().toStdString() << "\n";
        std::cerr << "  output tail:\n"
                  << attempt.value("logTail").toString().right(2048).toStdString() << "\n";
    }
}

// Blender's own probe, run without a sandbox, for the comparison of --real.
QJsonObject directProbe(const QString &blender, const QString &backend, const QString &directory) {
    QFile script(QStringLiteral(SOURCE_DIR) + "/src/integrations/blender_worker.py");
    if (!script.open(QIODevice::ReadOnly))
        return {};
    QFile worker(directory + "/worker.py");
    if (!worker.open(QIODevice::WriteOnly) || worker.write(script.readAll()) < 0)
        return {};
    worker.close();
    const QJsonObject request{{"apiVersion", 1},
                              {"operation", "probe"},
                              {"backend", backend},
                              {"deviceId", backend == "CPU" ? "CPU" : "probe-only"}};
    QFile requestFile(directory + "/request.json");
    if (!requestFile.open(QIODevice::WriteOnly) ||
        requestFile.write(QJsonDocument(request).toJson(QJsonDocument::Compact)) < 0)
        return {};
    requestFile.close();
    QProcess process;
    process.setWorkingDirectory(directory);
    process.start(blender,
                  {"--background", "--factory-startup", "--disable-autoexec", "--offline-mode",
                   "--threads", "4", "--python-exit-code", "1", "--python",
                   directory + "/worker.py", "--", directory + "/request.json"});
    if (!process.waitForFinished(120000))
        return {};
    return QJsonDocument::fromJson(read(directory + "/result.json")).object();
}

void testRealProbes(const std::string &scratch) {
    const auto blender = qEnvironmentVariable("SKETCHYUP_BLENDER_TEST");
    if (blender.isEmpty())
        return;
    for (const QString backend : {QStringLiteral("CPU"), QStringLiteral("OPENGL")}) {
        const QString directory = QString::fromStdString(scratch) + "/direct-" + backend;
        QDir().mkpath(directory);
        const auto expected = directProbe(blender, backend, directory);
        BlenderJob::Options real;
        real.executable = blender;
        real.backend = backend;
        real.deviceId = backend == "CPU" ? "CPU" : "probe-only";
        real.allowCpuFallback = false;
        real.timeoutMs = 60000;
        BlenderJob probe;
        probe.probe(real);
        waitFor(probe, 60000);
        check(probe.phase() == Phase::Succeeded,
              backend.toStdString() + " probe under the sandbox succeeds");
        if (probe.phase() != Phase::Succeeded)
            explain(probe);
        const auto sandboxed = probe.report().value("worker").toObject();
        const bool same = !expected.isEmpty() && sandboxed.value("status") == "available" &&
                          sandboxed == expected;
        check(same,
              backend.toStdString() + " probe under the sandbox is the unsandboxed probe's result");
        if (!same) {
            std::cerr << "  direct probe result: "
                      << QJsonDocument(expected).toJson(QJsonDocument::Compact).toStdString()
                      << "\n  sandboxed probe result: "
                      << QJsonDocument(sandboxed).toJson(QJsonDocument::Compact).toStdString()
                      << "\n";
        }
    }
}

} // namespace

int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    const bool real = app.arguments().contains("--real");
    if (real && qEnvironmentVariableIsEmpty("SKETCHYUP_BLENDER_TEST"))
        return 77;
    QTemporaryDir temporary;
    if (!temporary.isValid()) {
        std::cerr << "FAIL: no temporary directory\n";
        return 1;
    }
    const std::string scratch = temporary.path().toStdString();
    if (real) {
        testRealProbes(scratch);
        if (failures == 0)
            std::cout << "Blender sandbox probes match the unsandboxed probes\n";
    } else {
        testPolicyBuilds(scratch);
        testAttacks(scratch);
        testNegativeControl(scratch);
        testFailClosed(scratch);
        testHostClasses(scratch);
        if (failures == 0)
            std::cout << "Blender sandbox tests passed\n";
    }
    return failures == 0 ? 0 : 1;
}
