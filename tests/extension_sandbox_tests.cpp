// R109 S4 (ADR 0183): the extension worker under its sandbox. The checks:
//   - the panel fixture gives the same bytes as the same code run unsandboxed in this process,
//   under each of
//     the stdio setups (pipes, files, a pseudo-terminal for stdout) and for stdout as /dev/null
//     (exit 0), and for a request padded to 300 KiB;
//   - the attack probes under the policy: socket is EPERM (R109 S6), execve and fork are killed with SIGSYS, abort is
//     SIGABRT, /etc/passwd and a file outside the worker's directory are EACCES, and the locale can
//     be read;
//   - a layer that cannot be installed refuses with sandboxUnavailable: simulated in process, and
//   for the real
//     worker under a filter that refuses the layer's calls (ADR 0183 section 6.7);
//   - the host classes a worker killed by SIGSYS as sandboxViolation, a worker's refusal as its own
//   error, and
//     a crash (SIGABRT) as a crash; the host passes the worker only PATH and LANG (C.UTF-8);
//   - a user locale that is not installed does not stop the worker.
// Needs sketchyup-extension-worker (EXTENSION_WORKER_PATH). Landlock must be available: the checks
// fail, not skip, when it is not (ADR 0183 decision 9).
#include "automation/extension_sandbox_policy.hpp"
#include "automation/extension_worker.hpp"
#include "sandbox/sandbox.hpp"
#include "sandbox/syscalls_x86_64.hpp"

#include <QByteArray>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>

#include <cerrno>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <fstream>
#include <functional>
#include <iostream>
#include <linux/filter.h>
#include <linux/seccomp.h>
#include <pty.h>
#include <string>
#include <sys/prctl.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <sys/wait.h>
#include <unistd.h>
#include <vector>

using namespace sketchy;

namespace {

int failures = 0;

void check(bool ok, const std::string &message) {
    if (!ok) {
        std::cerr << "FAIL: " << message << "\n";
        ++failures;
    }
}

std::string readAll(const std::string &path) {
    std::ifstream in(path, std::ios::binary);
    return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}

struct Outcome {
    bool signaled = false;
    int signal = 0;
    int code = -1;
    std::string out; // the child's stdout, where the check reads it
};

// Runs body in a forked child whose stdout is a file, and reports how it ended. body returns the
// exit code.
Outcome inChild(const std::string &stdoutFile, const std::function<int()> &body) {
    std::fflush(nullptr);
    const pid_t pid = fork();
    if (pid == 0) {
        const int fd = open(stdoutFile.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0600);
        if (fd >= 0)
            dup2(fd, 1);
        _exit(body());
    }
    int status = 0;
    waitpid(pid, &status, 0);
    Outcome outcome;
    outcome.signaled = WIFSIGNALED(status);
    outcome.signal = outcome.signaled ? WTERMSIG(status) : 0;
    outcome.code = outcome.signaled ? -1 : WEXITSTATUS(status);
    outcome.out = readAll(stdoutFile);
    return outcome;
}

// The filter a test installs in a child before exec: the calls in refused fail with refusedErrno,
// the others are allowed (the test's own refusal of the layer's calls, ADR 0183 section 6.7).
void installRefusal(const std::vector<long> &refused, int refusedErrno) {
    if (refused.empty())
        return;
    std::vector<sock_filter> filter;
    const std::size_t count = refused.size();
    filter.push_back(BPF_STMT(BPF_LD | BPF_W | BPF_ABS, 0));
    for (std::size_t i = 0; i < count; ++i)
        filter.push_back(BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, static_cast<unsigned>(refused[i]),
                                  static_cast<unsigned char>(count - i), 0));
    filter.push_back(BPF_STMT(BPF_RET | BPF_K, SECCOMP_RET_ALLOW));
    filter.push_back(
        BPF_STMT(BPF_RET | BPF_K, SECCOMP_RET_ERRNO | static_cast<unsigned>(refusedErrno)));
    prctl(PR_SET_NO_NEW_PRIVS, 1UL, 0UL, 0UL, 0UL);
    sock_fprog program{static_cast<unsigned short>(filter.size()), filter.data()};
    syscall(SYS_seccomp, SECCOMP_SET_MODE_FILTER, 0UL, &program);
}

// The stdio setups of ADR 0183 section 4.5 that the worker meets in production and in the tests.
enum class Stdio { pipes, files, terminal, devNull };

struct Direct {
    bool signaled = false;
    int signal = 0;
    int code = -1;
    std::string out;
};

// Runs the worker directly, with the request on stdin, in the given stdio setup. stderr goes to a
// file.
Direct runDirect(Stdio mode, const std::string &requestPath, const std::string &scratch,
                 const std::string &stem, const std::vector<long> &refused = {},
                 int refusedErrno = 0) {
    const std::string worker = EXTENSION_WORKER_PATH;
    const std::string request = readAll(requestPath);
    const std::string errorFile = scratch + "/" + stem + ".stderr";
    std::fflush(nullptr);

    int inPipe[2] = {-1, -1}, outPipe[2] = {-1, -1}, master = -1, slave = -1;
    if (mode == Stdio::pipes) {
        if (pipe(inPipe) != 0 || pipe(outPipe) != 0)
            return {};
    } else if (mode == Stdio::terminal) {
        if (openpty(&master, &slave, nullptr, nullptr, nullptr) != 0)
            return {};
    }

    const pid_t pid = fork();
    if (pid == 0) {
        int in = -1, out = -1;
        if (mode == Stdio::pipes) {
            in = inPipe[0];
            out = outPipe[1];
            close(inPipe[1]);
            close(outPipe[0]);
        } else if (mode == Stdio::terminal) {
            out = slave;
            close(master);
        }
        if (mode == Stdio::files || mode == Stdio::devNull || mode == Stdio::terminal)
            in = open(requestPath.c_str(), O_RDONLY);
        if (mode == Stdio::files)
            out = open((scratch + "/" + stem + ".out").c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0600);
        if (mode == Stdio::devNull)
            out = open("/dev/null", O_WRONLY);
        const int err = open(errorFile.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0600);
        if (in < 0 || out < 0)
            _exit(97);
        dup2(in, 0);
        dup2(out, 1);
        if (err >= 0)
            dup2(err, 2);
        // The environment the host gives the worker: PATH and LANG, nothing else.
        clearenv();
        setenv("PATH", "/usr/bin:/bin", 1);
        setenv("LANG", "C.UTF-8", 1);
        installRefusal(refused, refusedErrno);
        char *const argv[] = {const_cast<char *>(worker.c_str()), nullptr};
        execv(worker.c_str(), argv);
        _exit(98);
    }

    Direct result;
    if (mode == Stdio::pipes) {
        close(inPipe[0]);
        close(outPipe[1]);
        // The request is far below the pipe's capacity, so this write cannot block on the worker.
        const ssize_t written = write(inPipe[1], request.data(), request.size());
        close(inPipe[1]);
        if (written != static_cast<ssize_t>(request.size())) {
            result.code = -2; // a short write: the check on the reply fails
        }
        char buffer[4096];
        ssize_t count;
        while ((count = read(outPipe[0], buffer, sizeof buffer)) > 0)
            result.out.append(buffer, static_cast<std::size_t>(count));
        close(outPipe[0]);
    } else if (mode == Stdio::terminal) {
        close(slave);
        char buffer[4096];
        ssize_t count;
        while ((count = read(master, buffer, sizeof buffer)) > 0)
            result.out.append(buffer, static_cast<std::size_t>(count));
        close(master);
    }

    int status = 0;
    waitpid(pid, &status, 0);
    if (mode == Stdio::files)
        result.out = readAll(scratch + "/" + stem + ".out");
    result.signaled = WIFSIGNALED(status);
    result.signal = result.signaled ? WTERMSIG(status) : 0;
    result.code = result.signaled ? -1 : WEXITSTATUS(status);
    return result;
}

// The request the host sends for an action, as a file the direct runs read on stdin.
void writeRequest(const std::string &path, const QByteArray &manifest,
                  const QJsonObject &parameters) {
    const QByteArray bytes =
        QJsonDocument(QJsonObject{{"protocol", 1},
                                  {"manifest", QString::fromLatin1(manifest.toBase64())},
                                  {"action", "create-panel"},
                                  {"parameters", parameters}})
            .toJson(QJsonDocument::Compact);
    std::ofstream(path, std::ios::binary).write(bytes.constData(), bytes.size());
}

// The policy the worker applies, before any of the attacks (the same call the worker makes).
Outcome armed(const std::string &stdoutFile, const std::function<int()> &attack) {
    return inChild(stdoutFile, [&, attack] {
        if (applyExtensionSandbox())
            return 3;
        return attack();
    });
}

void testFixturesIdentical(const std::string &scratch, const QByteArray &panel) {
    const QJsonObject parameters{{"width", 7}, {"height", 3}};
    const std::string panelRequest = scratch + "/panel.request.json";
    writeRequest(panelRequest, panel, parameters);
    const std::string largeRequest = scratch + "/large.request.json";
    {
        std::string bytes = readAll(panelRequest);
        bytes.append(300 * 1024,
                     ' '); // valid JSON with whitespace after it, read in full by the worker
        std::ofstream(largeRequest, std::ios::binary) << bytes;
    }

    // The same code, unsandboxed, in this process (the request's bytes are the worker's input).
    const QByteArray plainPanel =
        resolveExtensionWorkerRequest(QByteArray::fromStdString(readAll(panelRequest)));
    const QByteArray plainLarge =
        resolveExtensionWorkerRequest(QByteArray::fromStdString(readAll(largeRequest)));
    check(!plainPanel.isEmpty() && !plainLarge.isEmpty(), "the unsandboxed run resolves the panel");

    const auto same = [](const std::string &got, const QByteArray &expected) {
        return got == (expected + '\n').toStdString();
    };
    const Direct pipes = runDirect(Stdio::pipes, panelRequest, scratch, "pipes");
    check(!pipes.signaled && pipes.code == 0, "the worker on pipes exits 0 (code " +
                                                  std::to_string(pipes.code) + ", signal " +
                                                  std::to_string(pipes.signal) + ")");
    check(same(pipes.out, plainPanel), "the panel on pipes is identical to the unsandboxed run");

    const Direct files = runDirect(Stdio::files, panelRequest, scratch, "files");
    check(!files.signaled && files.code == 0, "the worker on files exits 0");
    check(same(files.out, plainPanel), "the panel on files is identical to the unsandboxed run");

    // A pseudo-terminal is a character device: the worker's stdout check asks it for its terminal
    // attributes, which the policy allows. The line discipline turns each newline into CR LF.
    const Direct terminal = runDirect(Stdio::terminal, panelRequest, scratch, "terminal");
    check(!terminal.signaled && terminal.code == 0, "the worker on a terminal exits 0");
    std::string normalised;
    for (const char c : terminal.out)
        if (c != '\r')
            normalised += c;
    check(same(normalised, plainPanel),
          "the panel on a terminal is identical to the unsandboxed run");

    const Direct devNull = runDirect(Stdio::devNull, panelRequest, scratch, "devnull");
    check(!devNull.signaled && devNull.code == 0,
          "the worker with stdout on /dev/null exits 0 (signal " + std::to_string(devNull.signal) +
              ")");

    const Direct large = runDirect(Stdio::pipes, largeRequest, scratch, "large");
    check(!large.signaled && large.code == 0 && same(large.out, plainLarge),
          "a request padded to 300 KiB is identical to the unsandboxed run");
}

void testPolicyRefusesAttacks(const std::string &scratch) {
    const Outcome socketCall = armed(scratch + "/attack-socket.out", [] {
        return socket(AF_INET, SOCK_STREAM, 0) == -1 && errno == EPERM ? 0 : 1;
    });
    check(!socketCall.signaled && socketCall.code == 0,
          "the extension worker cannot create a socket (EPERM)");

    const Outcome execCall = armed(scratch + "/attack-exec.out", [] {
        char *const argv[] = {const_cast<char *>("/bin/true"), nullptr};
        execv("/bin/true", argv);
        return 0;
    });
    check(execCall.signaled && execCall.signal == SIGSYS,
          "the extension worker cannot exec another program (killed with SIGSYS)");

    // The worker's policy has no clone (the ADR's row says it creates no thread), so a fork is
    // killed.
    const Outcome forkCall = armed(scratch + "/attack-fork.out", [] {
        const pid_t pid = fork();
        if (pid == 0)
            _exit(0);
        return (pid == -1 && errno == EPERM) ? 0 : 1;
    });
    check(forkCall.signaled && forkCall.signal == SIGSYS,
          "the extension worker's fork is killed with SIGSYS (its policy has no clone)");

    const Outcome abortCall = armed(scratch + "/attack-abort.out", [] {
        std::abort();
        return 0;
    });
    check(abortCall.signaled && abortCall.signal == SIGABRT,
          "an abort under the policy is SIGABRT, a crash and not a sandbox violation");

    const Outcome readPasswd = armed(scratch + "/attack-passwd.out", [] {
        const int fd = open("/etc/passwd", O_RDONLY);
        return (fd < 0 && errno == EACCES) ? 0 : 1;
    });
    check(!readPasswd.signaled && readPasswd.code == 0,
          "the extension worker cannot read /etc/passwd (EACCES)");

    const Outcome writeOutside = armed(scratch + "/attack-write.out", [&] {
        const int fd = open((scratch + "/escaped.txt").c_str(), O_WRONLY | O_CREAT, 0600);
        return (fd < 0 && errno == EACCES) ? 0 : 1;
    });
    check(!writeOutside.signaled && writeOutside.code == 0,
          "the extension worker cannot create a file (EACCES): it has no write rule");

    const Outcome readLocale = armed(scratch + "/attack-locale.out", [] {
        const int fd = open("/usr/lib/locale/C.utf8/LC_CTYPE", O_RDONLY);
        return fd >= 0 ? 0 : 1;
    });
    check(!readLocale.signaled && readLocale.code == 0,
          "the extension worker can read the C.UTF-8 locale");
}

void testRefusalsFailClosed(const std::string &scratch, const std::string &panelRequest) {
    // In process: each layer that cannot be installed is named, and nothing is applied after it.
    for (const sketchyup::sandbox::Layer layer :
         {sketchyup::sandbox::Layer::noNewPrivileges, sketchyup::sandbox::Layer::landlock,
          sketchyup::sandbox::Layer::seccomp}) {
        const Outcome outcome = inChild(scratch + "/simulate.out", [&] {
            sketchyup::sandbox::ApplyOptions options;
            options.simulate = sketchyup::sandbox::Failure{layer, ENOSYS};
            const auto failure = applyExtensionSandbox(options);
            if (!failure || failure->layer != layer)
                return 1;
            return refuseExtensionSandbox(*failure) == extensionSandboxUnavailableExit ? 0 : 2;
        });
        const std::string expected = std::string("\"error\":\"sandboxUnavailable: ") +
                                     sketchyup::sandbox::layerName(layer) +
                                     " (Function not implemented)\"";
        check(!outcome.signaled && outcome.code == 0 &&
                  outcome.out.find(expected) != std::string::npos,
              std::string("a simulated ") + sketchyup::sandbox::layerName(layer) +
                  " failure refuses with sandboxUnavailable");
    }

    // The real worker, with a filter that refuses the layer's calls.
    const Direct noLandlock =
        runDirect(Stdio::files, panelRequest, scratch, "no-landlock", {444, 445, 446}, ENOSYS);
    check(!noLandlock.signaled && noLandlock.code == extensionSandboxUnavailableExit,
          "the extension worker without Landlock exits " +
              std::to_string(extensionSandboxUnavailableExit) + " (code " +
              std::to_string(noLandlock.code) + ")");
    check(noLandlock.out.find(
              "\"error\":\"sandboxUnavailable: landlock (Function not implemented)\"") !=
              std::string::npos,
          "the extension worker without Landlock names the layer and replies with no commands");

    const Direct noSeccomp =
        runDirect(Stdio::files, panelRequest, scratch, "no-seccomp", {317}, EPERM);
    check(!noSeccomp.signaled && noSeccomp.code == extensionSandboxUnavailableExit,
          "the extension worker without a seccomp filter exits " +
              std::to_string(extensionSandboxUnavailableExit));
    check(noSeccomp.out.find("sandboxUnavailable: seccomp filter (Operation not permitted)") !=
              std::string::npos,
          "the extension worker without a seccomp filter names the layer");
}

// A stand-in worker: a shell script that reads its request and ends the way the test needs.
std::string standIn(const std::string &directory, const std::string &name,
                    const std::string &body) {
    const std::string path = directory + "/" + name;
    {
        std::ofstream script(path);
        script << "#!/bin/sh\ncat > /dev/null\n" << body << "\n";
    }
    chmod(path.c_str(), 0755);
    return path;
}

// The message the host throws for a stand-in worker, or nothing when the call succeeds.
std::string hostMessage(const ExtensionManifest &manifest, const std::string &executable) {
    ExtensionWorkerOptions options;
    options.executable = QString::fromStdString(executable);
    options.timeoutMs = 5000;
    try {
        runExtensionWorker(manifest, "create-panel", QJsonObject{{"width", 7}, {"height", 3}},
                           options);
    } catch (const std::exception &error) {
        return error.what();
    }
    return "";
}

void testHostClassifies(const std::string &scratch, const ExtensionManifest &manifest) {
    const std::string violation =
        hostMessage(manifest, standIn(scratch, "sigsys-worker.sh", "kill -31 $$"));
    check(violation.find("sandboxViolation") != std::string::npos,
          "a worker killed by SIGSYS is sandboxViolation, not a crash (got: " + violation + ")");

    const std::string refused = hostMessage(
        manifest,
        standIn(
            scratch, "unavailable-worker.sh",
            "printf '%s\\n' '{\"protocol\":1,\"ok\":false,\"error\":\"sandboxUnavailable: landlock "
            "(Function not implemented)\"}'\nexit 70"));
    check(refused == "sandboxUnavailable: landlock (Function not implemented)",
          "a worker refused with exit 70 gives its refusal as the error (got: " + refused + ")");

    const std::string crash =
        hostMessage(manifest, standIn(scratch, "abort-worker.sh", "kill -6 $$"));
    check(crash.find("crashed") != std::string::npos &&
              crash.find("sandboxViolation") == std::string::npos,
          "a worker killed by SIGABRT is still a crash (got: " + crash + ")");

    // The host passes the worker its PATH and LANG only: a user LC_ALL is not passed.
    const std::string record = scratch + "/environment.txt";
    const std::string recorder = standIn(
        scratch, "record-worker.sh",
        "printf '%s\\n%s\\n%s\\n' \"$LANG\" \"${LC_ALL-unset}\" \"${HOME-unset}\" > '" + record +
            "'\n"
            "printf '%s\\n' '{\"protocol\":1,\"ok\":false,\"error\":\"recorded\"}'");
    hostMessage(manifest, recorder);
    const std::string seen = readAll(record);
    check(seen == "C.UTF-8\nunset\nunset\n",
          "the worker gets LANG=C.UTF-8 and no LC_ALL or HOME (got: " + seen + ")");
}

// A user locale that is not installed must not stop the worker (the host gives the worker a clean
// environment with C.UTF-8 as its LANG).
void testMissingUserLocale(const ExtensionManifest &manifest) {
    qputenv("LC_ALL", "xx_YY.UTF-8");
    qputenv("LANG", "xx_YY.UTF-8");
    ExtensionWorkerOptions options;
    options.executable = EXTENSION_WORKER_PATH;
    bool ok = false;
    try {
        ok = !runExtensionWorker(manifest, "create-panel", QJsonObject{{"width", 7}, {"height", 3}},
                                 options)
                  .isEmpty();
    } catch (const std::exception &error) {
        std::cerr << error.what() << "\n";
    }
    qunsetenv("LC_ALL");
    qunsetenv("LANG");
    check(ok, "a worker runs under a user locale that is not installed");
}

} // namespace

int main() {
    QTemporaryDir temporary;
    if (!temporary.isValid()) {
        std::cerr << "FAIL: no temporary directory\n";
        return 1;
    }
    const std::string scratch = temporary.path().toStdString();

    QFile sample(QStringLiteral(SOURCE_DIR) + "/examples/extensions/panel.sketchyext");
    if (!sample.open(QIODevice::ReadOnly)) {
        std::cerr << "FAIL: the panel fixture is missing\n";
        return 1;
    }
    const QByteArray panel = sample.readAll();
    const ExtensionManifest manifest = parseExtensionManifest(panel);
    const std::string panelRequest = scratch + "/panel.request.json";
    writeRequest(panelRequest, panel, QJsonObject{{"width", 7}, {"height", 3}});

    testFixturesIdentical(scratch, panel);
    testPolicyRefusesAttacks(scratch);
    testRefusalsFailClosed(scratch, panelRequest);
    testHostClassifies(scratch, manifest);
    testMissingUserLocale(manifest);
    if (failures == 0)
        std::cout << "extension sandbox tests passed\n";
    return failures == 0 ? 0 : 1;
}
