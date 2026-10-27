// R109 S3 (ADR 0183): the text helper under its sandbox. The checks:
//   - the real helper, with the request on stdin, gives the same geometry as the same code run
//   unsandboxed in
//     a child process, for the Latin requests and for a large one; the mixed-script request
//     returns;
//   - the attack probes under the policy: socket and execve are killed with SIGSYS, fork is EPERM,
//   abort is
//     SIGABRT, /etc/passwd and a file outside the private directory are EACCES, and the fontconfig
//     configuration can be read;
//   - a layer that cannot be installed refuses with sandboxUnavailable and no reply geometry:
//   simulated in
//     process, and for the real helper under a filter that refuses the layer's calls (ADR 0183
//     section 6.7);
//   - the host classes a helper killed by SIGSYS as sandboxViolation, a helper's refusal as its own
//   error, and
//     a crash (SIGABRT) as a crash; the host's private directory is HOME, is empty, and is removed
//     after the call; the locale the helper gets is C.UTF-8 whatever the user's LANG and LC_ALL
//     are;
//   - a user LANG that is not installed does not stop the helper (the host sets C.UTF-8).
// Needs sketchyup-text-worker (TEXT_WORKER_PATH). Landlock must be available: the checks fail, not
// skip, when it is not (ADR 0183 decision 9).
#include "sandbox/sandbox.hpp"
#include "sandbox/syscalls_x86_64.hpp"
#include "text/sandbox_policy.hpp"
#include "text/text_geometry.hpp"
#include "text/text_worker.hpp"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QGuiApplication>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>

#include <cerrno>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <linux/filter.h>
#include <linux/seccomp.h>
#include <string>
#include <sys/prctl.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <sys/wait.h>
#include <unistd.h>
#include <vector>

using namespace sketchy;
namespace fs = std::filesystem;

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
    std::string out; // the child's stdout
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

// The text helper, with the request in a file on stdin, as a child process. An optional filter is
// installed in the child before exec: the test's own refusal of some calls (the layer's calls, for
// the fail-closed check).
Outcome runHelper(const std::string &stdoutFile, const std::string &requestFile,
                  const std::vector<long> &refused = {}, int refusedErrno = 0) {
    std::vector<sock_filter> filter;
    if (!refused.empty()) {
        // [0] load the call number; one jump per refused call to the refusal; then allow; then
        // refuse.
        const std::size_t count = refused.size();
        filter.push_back(BPF_STMT(BPF_LD | BPF_W | BPF_ABS, 0));
        for (std::size_t i = 0; i < count; ++i)
            filter.push_back(BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, static_cast<unsigned>(refused[i]),
                                      static_cast<unsigned char>(count - i), 0));
        filter.push_back(BPF_STMT(BPF_RET | BPF_K, SECCOMP_RET_ALLOW));
        filter.push_back(
            BPF_STMT(BPF_RET | BPF_K, SECCOMP_RET_ERRNO | static_cast<unsigned>(refusedErrno)));
    }
    std::string helper = TEXT_WORKER_PATH;
    char *const argv[] = {helper.data(), nullptr};
    // The environment the host gives the helper: one empty private directory for HOME, the XDG
    // directories and TMPDIR, and C.UTF-8 for the locale.
    const std::string home = fs::path(requestFile).parent_path().string() + "/helper-home";
    fs::create_directories(home);
    return inChild(stdoutFile, [&] {
        const int in = open(requestFile.c_str(), O_RDONLY);
        if (in < 0)
            return 97;
        dup2(in, 0);
        close(in);
        for (const char *name :
             {"HOME", "XDG_CACHE_HOME", "XDG_CONFIG_HOME", "XDG_DATA_HOME", "TMPDIR"})
            setenv(name, home.c_str(), 1);
        setenv("LANG", "C.UTF-8", 1);
        setenv("LC_ALL", "C.UTF-8", 1);
        if (!filter.empty()) {
            prctl(PR_SET_NO_NEW_PRIVS, 1UL, 0UL, 0UL, 0UL);
            sock_fprog program{static_cast<unsigned short>(filter.size()), filter.data()};
            if (syscall(SYS_seccomp, SECCOMP_SET_MODE_FILTER, 0UL, &program) != 0)
                return 99;
        }
        execv(helper.c_str(), argv);
        return 98;
    });
}

// The request the host sends for a settings value, as a file.
void writeRequest(const std::string &path, const TextGeometrySettings &settings) {
    const QByteArray bytes =
        QJsonDocument(QJsonObject{{"protocol", 1}, {"settings", encodeTextSettings(settings)}})
            .toJson(QJsonDocument::Compact);
    std::ofstream(path, std::ios::binary).write(bytes.constData(), bytes.size());
}

// The geometry JSON without the random document identity (ADR 0183 section 6.1: the document's ID
// is random per run and is removed before any comparison).
QJsonObject comparable(const TextGeometry &geometry) {
    QJsonObject encoded = encodeTextGeometry(geometry);
    QJsonObject document = encoded["document"].toObject();
    document.remove("documentId");
    encoded["document"] = document;
    return encoded;
}

TextGeometrySettings latin(const QString &text, const QString &family) {
    TextGeometrySettings settings;
    settings.text = text;
    settings.family = family;
    settings.height = .2;
    settings.depth = .03;
    settings.allowSubstitution = false;
    return settings;
}

// The same code the helper runs (shapeTextGeometry and the encoder), unsandboxed, in a child
// process with the environment the host gives the helper. Its geometry is written to a file.
bool plainGeometry(const std::string &scratch, const std::string &name,
                   const TextGeometrySettings &settings, QJsonObject &result) {
    const std::string jsonPath = scratch + "/" + name + ".plain.json";
    const std::string home = scratch + "/plain-home";
    fs::create_directories(home);
    const Outcome outcome = inChild(scratch + "/" + name + ".plain.out", [&] {
        qputenv("HOME", home.c_str());
        qputenv("XDG_CACHE_HOME", home.c_str());
        qputenv("XDG_CONFIG_HOME", home.c_str());
        qputenv("XDG_DATA_HOME", home.c_str());
        qputenv("TMPDIR", home.c_str());
        qputenv("LANG", "C.UTF-8");
        qputenv("LC_ALL", "C.UTF-8");
        qputenv("QT_QPA_PLATFORM", "offscreen");
        qputenv("QT_QPA_PLATFORMTHEME", "generic");
        qputenv("QT_IM_MODULE", "compose");
        try {
            int argc = 1;
            char name0[] = "text-sandbox-baseline";
            char *argv[] = {name0, nullptr};
            QGuiApplication app(argc, argv);
            const QByteArray bytes = QJsonDocument(encodeTextGeometry(shapeTextGeometry(settings)))
                                         .toJson(QJsonDocument::Compact);
            std::ofstream(jsonPath, std::ios::binary).write(bytes.constData(), bytes.size());
            return 0;
        } catch (const std::exception &) {
            return 1;
        }
    });
    if (outcome.signaled || outcome.code != 0)
        return false;
    QJsonParseError parse;
    const QJsonDocument document =
        QJsonDocument::fromJson(QByteArray::fromStdString(readAll(jsonPath)), &parse);
    if (parse.error != QJsonParseError::NoError || !document.isObject())
        return false;
    result = document.object();
    QJsonObject geometryDocument = result["document"].toObject();
    geometryDocument.remove("documentId");
    result["document"] = geometryDocument;
    return true;
}

void testFixturesIdentical(const std::string &scratch) {
    QStringList lines;
    for (int i = 0; i < 24; ++i)
        lines << QStringLiteral("Line %1 cafe").arg(i, 2, 10, QLatin1Char('0'));
    const std::vector<std::pair<std::string, TextGeometrySettings>> requests = {
        {"latin-a", latin(QStringLiteral("SketchyUp 1"), QStringLiteral("DejaVu Sans"))},
        {"latin-b", latin(QStringLiteral("Hello world"), QStringLiteral("Liberation Sans"))},
        {"large", latin(lines.join('\n'), QStringLiteral("DejaVu Sans"))},
    };
    for (const auto &[name, settings] : requests) {
        QJsonObject plain;
        check(plainGeometry(scratch, name, settings, plain),
              name + ": the unsandboxed run succeeds");
        TextWorkerOptions options;
        options.executable = TEXT_WORKER_PATH;
        const TextGeometry sandboxed = runTextWorker(settings, options);
        check(!sandboxed.regions.empty(), name + ": the sandboxed helper returns geometry");
        check(comparable(sandboxed) == plain,
              name + ": the sandboxed geometry is identical to the unsandboxed one");
    }

    // Mixed-script text falls back between fonts, and its output varies between unsandboxed runs
    // (ADR 0183 section 6.3), so the check is the return code and a non-empty result.
    TextWorkerOptions options;
    options.executable = TEXT_WORKER_PATH;
    TextGeometrySettings mixed =
        latin(QString::fromUtf8("O café\nلا"), QStringLiteral("DejaVu Sans"));
    mixed.allowSubstitution = true;
    const TextGeometry mixedGeometry = runTextWorker(mixed, options);
    check(!mixedGeometry.regions.empty() && mixedGeometry.lines == 2,
          "the mixed-script request returns (rc 0) with its geometry under the sandbox");

    // The same request through the wire: the helper's reply, the way the test runs the helper
    // directly.
    const std::string request = scratch + "/latin-a.request.json";
    writeRequest(request, requests.front().second);
    const Outcome direct = runHelper(scratch + "/latin-a.direct.out", request);
    check(!direct.signaled && direct.code == 0 &&
              direct.out.find("\"ok\":true") != std::string::npos,
          "the helper, run directly on its stdin, replies ok and exits 0 (code " +
              std::to_string(direct.code) + ", signal " + std::to_string(direct.signal) + ")");
}

// The policy the helper applies, before any of the attacks (the same call the helper makes).
Outcome armed(const std::string &stdoutFile, const std::function<int()> &attack) {
    return inChild(stdoutFile, [&, attack] {
        if (applyTextSandbox())
            return 3;
        return attack();
    });
}

void testPolicyRefusesAttacks(const std::string &scratch) {
    const std::string home = scratch + "/attack-home";
    fs::create_directories(home);

    const Outcome socketCall = armed(scratch + "/attack-socket.out", [] {
        socket(AF_INET, SOCK_STREAM, 0);
        return 0;
    });
    check(socketCall.signaled && socketCall.signal == SIGSYS,
          "the text helper cannot create a socket (killed with SIGSYS)");

    const Outcome execCall = armed(scratch + "/attack-exec.out", [] {
        char *const argv[] = {const_cast<char *>("/bin/true"), nullptr};
        execv("/bin/true", argv);
        return 0;
    });
    check(execCall.signaled && execCall.signal == SIGSYS,
          "the text helper cannot exec another program (killed with SIGSYS)");

    const Outcome forkCall = armed(scratch + "/attack-fork.out", [] {
        const pid_t pid = fork();
        if (pid == 0)
            _exit(0);
        return (pid == -1 && errno == EPERM) ? 0 : 1;
    });
    check(!forkCall.signaled && forkCall.code == 0, "the text helper's fork is EPERM");

    const Outcome abortCall = armed(scratch + "/attack-abort.out", [] {
        std::abort();
        return 0;
    });
    check(abortCall.signaled && abortCall.signal == SIGABRT,
          "a parser's abort under the policy is SIGABRT, a crash and not a sandbox violation");

    const Outcome readPasswd = armed(scratch + "/attack-passwd.out", [] {
        const int fd = open("/etc/passwd", O_RDONLY);
        return (fd < 0 && errno == EACCES) ? 0 : 1;
    });
    check(!readPasswd.signaled && readPasswd.code == 0,
          "the text helper cannot read /etc/passwd (EACCES)");

    const Outcome writeOutside = armed(scratch + "/attack-write.out", [&] {
        const int fd = open((scratch + "/escaped.txt").c_str(), O_WRONLY | O_CREAT, 0600);
        return (fd < 0 && errno == EACCES) ? 0 : 1;
    });
    check(!writeOutside.signaled && writeOutside.code == 0,
          "the text helper cannot create a file outside its private directory (EACCES)");

    const Outcome writeHome = armed(scratch + "/attack-home.out", [&] {
        const int fd = open((home + "/inside.txt").c_str(), O_WRONLY | O_CREAT, 0600);
        return (fd < 0 && errno == EACCES) ? 0 : 1;
    });
    check(!writeHome.signaled && writeHome.code == 0,
          "the text helper cannot write into its private directory: the text policy has no write "
          "rule (EACCES)");

    const Outcome readFontconfig = armed(scratch + "/attack-fontconfig.out", [] {
        const int fd = open("/etc/fonts/fonts.conf", O_RDONLY);
        return fd >= 0 ? 0 : 1;
    });
    check(!readFontconfig.signaled && readFontconfig.code == 0,
          "the text helper can read the fontconfig configuration");
}

void testRefusalsFailClosed(const std::string &scratch) {
    // In process: each layer that cannot be installed is named, and nothing is applied after it.
    for (const sketchyup::sandbox::Layer layer :
         {sketchyup::sandbox::Layer::noNewPrivileges, sketchyup::sandbox::Layer::landlock,
          sketchyup::sandbox::Layer::seccomp}) {
        const Outcome outcome = inChild(scratch + "/simulate.out", [&] {
            sketchyup::sandbox::ApplyOptions options;
            options.simulate = sketchyup::sandbox::Failure{layer, ENOSYS};
            const auto failure = applyTextSandbox(options);
            if (!failure || failure->layer != layer)
                return 1;
            return refuseTextSandbox(*failure) == textSandboxUnavailableExit ? 0 : 2;
        });
        const std::string expected = std::string("\"error\":\"sandboxUnavailable: ") +
                                     sketchyup::sandbox::layerName(layer) +
                                     " (Function not implemented)\"";
        check(!outcome.signaled && outcome.code == 0 &&
                  outcome.out.find(expected) != std::string::npos,
              std::string("a simulated ") + sketchyup::sandbox::layerName(layer) +
                  " failure refuses with sandboxUnavailable");
    }

    // The real helper, with a filter that refuses the layer's calls (the measured method of ADR
    // 0183 6.7).
    const std::string request = scratch + "/refusal.request.json";
    writeRequest(request, latin(QStringLiteral("SketchyUp 1"), QStringLiteral("DejaVu Sans")));
    const Outcome noLandlock =
        runHelper(scratch + "/no-landlock.out", request, {444, 445, 446}, ENOSYS);
    check(!noLandlock.signaled && noLandlock.code == textSandboxUnavailableExit,
          "the text helper without Landlock exits " + std::to_string(textSandboxUnavailableExit) +
              " (code " + std::to_string(noLandlock.code) + ")");
    check(noLandlock.out.find(
              "\"error\":\"sandboxUnavailable: landlock (Function not implemented)\"") !=
              std::string::npos,
          "the text helper without Landlock names the layer and replies with no geometry");

    const Outcome noSeccomp = runHelper(scratch + "/no-seccomp.out", request, {317}, EPERM);
    check(!noSeccomp.signaled && noSeccomp.code == textSandboxUnavailableExit,
          "the text helper without a seccomp filter exits " +
              std::to_string(textSandboxUnavailableExit));
    check(noSeccomp.out.find("sandboxUnavailable: seccomp filter (Operation not permitted)") !=
              std::string::npos,
          "the text helper without a seccomp filter names the layer");
}

// A stand-in helper: a shell script that reads its request and ends the way the test needs.
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

// The message the host throws for a stand-in helper, or nothing when the call succeeds.
std::string hostMessage(const std::string &executable) {
    TextWorkerOptions options;
    options.executable = QString::fromStdString(executable);
    options.timeoutMs = 5000;
    try {
        runTextWorker(latin(QStringLiteral("SketchyUp 1"), QStringLiteral("DejaVu Sans")), options);
    } catch (const std::exception &error) {
        return error.what();
    }
    return "";
}

void testHostClassifies(const std::string &scratch) {
    const std::string violation = hostMessage(standIn(scratch, "sigsys-helper.sh", "kill -31 $$"));
    check(violation.find("sandboxViolation") != std::string::npos,
          "a helper killed by SIGSYS is sandboxViolation, not a crash (got: " + violation + ")");

    const std::string refused = hostMessage(standIn(
        scratch, "unavailable-helper.sh",
        "printf '%s\\n' '{\"protocol\":1,\"ok\":false,\"error\":\"sandboxUnavailable: landlock "
        "(Function not implemented)\"}'\nexit 70"));
    check(refused == "sandboxUnavailable: landlock (Function not implemented)",
          "a helper refused with exit 70 gives its refusal as the error (got: " + refused + ")");

    const std::string crash = hostMessage(standIn(scratch, "abort-helper.sh", "kill -6 $$"));
    check(crash.find("crashed") != std::string::npos &&
              crash.find("sandboxViolation") == std::string::npos,
          "a helper killed by SIGABRT is still a crash (got: " + crash + ")");

    // The private directory: the helper's HOME, XDG_* and TMPDIR, and LANG and LC_ALL, as the
    // helper sees them.
    const std::string record = scratch + "/environment.txt";
    const std::string recorder =
        standIn(scratch, "record-helper.sh",
                "printf '%s\\n%s\\n%s\\n%s\\n%s\\n%s\\n%s\\n' \"$HOME\" \"$XDG_CACHE_HOME\" "
                "\"$XDG_CONFIG_HOME\" "
                "\"$XDG_DATA_HOME\" \"$TMPDIR\" \"$LANG\" \"$LC_ALL\" > '" +
                    record +
                    "'\n"
                    "printf '%s\\n' '{\"protocol\":1,\"ok\":false,\"error\":\"recorded\"}'");
    hostMessage(recorder);
    const std::string seen = readAll(record);
    std::vector<std::string> values;
    for (std::size_t start = 0, end; start < seen.size(); start = end + 1) {
        end = seen.find('\n', start);
        if (end == std::string::npos)
            end = seen.size();
        values.push_back(seen.substr(start, end - start));
    }
    check(values.size() == 7, "the recorder saw the environment it was given");
    if (values.size() == 7) {
        const std::string home = values[0];
        bool allSame = true;
        for (std::size_t i = 1; i < 5; ++i)
            allSame = allSame && values[i] == home;
        check(allSame, "HOME, the XDG directories and TMPDIR are one private directory");
        check(!home.empty() && home.find(QDir::tempPath().toStdString()) == 0,
              "the private directory is under the temporary directory (" + home + ")");
        check(values[5] == "C.UTF-8" && values[6] == "C.UTF-8", "LANG and LC_ALL are C.UTF-8");
        check(!QFileInfo::exists(QString::fromStdString(home)),
              "the private directory is removed after the call");
    }
}

// A user locale that is not installed must not stop the helper: the host gives it C.UTF-8
// (measured: a missing locale makes Qt log through the journal's socket, which the sandbox
// refuses).
void testMissingUserLocale() {
    qputenv("LANG", "xx_YY.UTF-8");
    qputenv("LC_ALL", "xx_YY.UTF-8");
    TextWorkerOptions options;
    options.executable = TEXT_WORKER_PATH;
    bool ok = false;
    try {
        const TextGeometry geometry = runTextWorker(
            latin(QStringLiteral("SketchyUp 1"), QStringLiteral("DejaVu Sans")), options);
        ok = !geometry.regions.empty();
    } catch (const std::exception &error) {
        std::cerr << error.what() << "\n";
    }
    qunsetenv("LANG");
    qunsetenv("LC_ALL");
    check(ok, "a helper runs under a user locale that is not installed");
}

} // namespace

int main() {
    qunsetenv("DISPLAY");
    qunsetenv("WAYLAND_DISPLAY");
    QTemporaryDir temporary;
    if (!temporary.isValid()) {
        std::cerr << "FAIL: no temporary directory\n";
        return 1;
    }
    const std::string scratch = temporary.path().toStdString();
    testFixturesIdentical(scratch);
    testPolicyRefusesAttacks(scratch);
    testRefusalsFailClosed(scratch);
    testHostClassifies(scratch);
    testMissingUserLocale();
    if (failures == 0)
        std::cout << "text sandbox tests passed\n";
    return failures == 0 ? 0 : 1;
}
