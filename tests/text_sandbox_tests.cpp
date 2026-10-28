// R109 S3 (ADR 0183): the text helper under its sandbox. The checks:
//   - the real helper, with the request on stdin, gives the same geometry as the same code run
//   unsandboxed in
//     a child process, for the Latin requests and for a large one; the mixed-script request
//     returns;
//   - the attack probes under the policy: socket is EPERM (R109 S6), execve is killed with SIGSYS,
//   fork is EPERM, abort is
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
//   - a user LANG that is not installed does not stop the helper (the host sets C.UTF-8);
//   - R109 S4: a font in a user's font folder ($XDG_DATA_HOME/fonts, ~/.fonts or
//   ~/.local/share/fonts)
//     resolves in the helper with no fallback, and is missing without its folder; the folder is
//     granted read-only (a read is allowed; a write, a create, a read beside it or a remove is
//     not); the helper refuses bad font arguments.
// Needs sketchyup-text-worker (TEXT_WORKER_PATH) and DejaVu Sans and DejaVu Serif under
// /usr/share/fonts, both from ttf-dejavu, the package's only font dependency (the
// user-font tests copy it). Landlock must be available: the checks fail, not skip, when it is not
// (ADR 0183 decision 9).
#include "sandbox/sandbox.hpp"
#include "sandbox/syscalls_x86_64.hpp"
#include "text/sandbox_policy.hpp"
#include "text/text_geometry.hpp"
#include "text/text_worker.hpp"

#include <QDir>
#include <QDirIterator>
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
#include <optional>
#include <string>
#include <sys/prctl.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <sys/wait.h>
#include <unistd.h>
#include <utility>
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
        {"latin-b", latin(QStringLiteral("Hello world"), QStringLiteral("DejaVu Serif"))},
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
Outcome armed(const std::string &stdoutFile, const std::function<int()> &attack,
              const TextFontGrants &fonts = {}) {
    return inChild(stdoutFile, [&, attack] {
        if (applyTextSandbox(fonts))
            return 3;
        return attack();
    });
}

void testPolicyRefusesAttacks(const std::string &scratch) {
    const std::string home = scratch + "/attack-home";
    fs::create_directories(home);

    const Outcome socketCall = armed(scratch + "/attack-socket.out", [] {
        return (socket(AF_INET, SOCK_STREAM, 0) == -1 && errno == EPERM) ? 0 : 1;
    });
    check(!socketCall.signaled && socketCall.code == 0,
          "the text helper cannot create a socket (EPERM)");

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
            const auto failure = applyTextSandbox({}, options);
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

// R109 S4 (ADR 0183 implementation notes): the user's font folders. The family of the test font is
// not installed on any system, so a request for it resolves only through a user folder. The repo
// ships no font, so the font is a copy of the system's DejaVu Sans with its family renamed. The new
// name has the same length, so the font's table offsets hold.
constexpr const char *kUserFamily = "UserFontTst";

// Writes the renamed copy of DejaVu Sans to destination. Returns false when no DejaVu Sans is
// installed or the copy cannot be written.
bool makeUserFont(const std::string &destination) {
    QDirIterator found("/usr/share/fonts", {"DejaVuSans.ttf"}, QDir::Files,
                       QDirIterator::Subdirectories);
    if (!found.hasNext())
        return false;
    QFile source(found.next());
    if (!source.open(QIODevice::ReadOnly))
        return false;
    QByteArray bytes = source.readAll();
    // Windows family names are UTF-16BE and Mac family names are ASCII: rename both. Each pair has
    // the same length, so the replacement keeps every offset in the file.
    auto utf16 = [](const char *text) {
        QByteArray encoded;
        for (const char *p = text; *p; ++p)
            encoded.append('\0').append(*p);
        return encoded;
    };
    const std::vector<std::pair<QByteArray, QByteArray>> renames = {
        {"DejaVu Sans", kUserFamily}, {utf16("DejaVu Sans"), utf16(kUserFamily)}};
    int renamed = 0;
    for (const auto &[from, to] : renames) {
        check(from.size() == to.size(), "the renamed family has the same length");
        for (qsizetype at = bytes.indexOf(from); at >= 0;
             at = bytes.indexOf(from, at + from.size())) {
            bytes.replace(at, from.size(), to);
            ++renamed;
        }
    }
    if (renamed == 0)
        return false;
    QFile out(QString::fromStdString(destination));
    return out.open(QIODevice::WriteOnly) && out.write(bytes) == bytes.size();
}

// The geometry the helper returns for a request, with HOME and XDG_DATA_HOME set as the test
// needs (an empty dataHome unsets XDG_DATA_HOME). Returns the message the host gives, or nothing.
std::string resolveAs(const std::string &home, const std::string &dataHome,
                      const TextGeometrySettings &settings, TextGeometry &result) {
    qputenv("HOME", home.c_str());
    if (dataHome.empty())
        qunsetenv("XDG_DATA_HOME");
    else
        qputenv("XDG_DATA_HOME", dataHome.c_str());
    TextWorkerOptions options;
    options.executable = TEXT_WORKER_PATH;
    try {
        result = runTextWorker(settings, options);
        return "";
    } catch (const std::exception &error) {
        return error.what();
    }
}

// The three places the host reads a user's fonts from: $XDG_DATA_HOME/fonts, which the user named
// and which lies outside HOME here (its name has XML characters, which the configuration must
// escape); ~/.fonts; and ~/.local/share/fonts. A font in each resolves in the helper with no
// fallback. System fonts still resolve with the folder present, and the test font is missing when
// the folder is absent.
void testUserFontFolders(const std::string &scratch) {
    struct Layout {
        std::string name, home, dataHome, folder;
    };
    const std::vector<Layout> layouts = {
        {"XDG_DATA_HOME", scratch + "/xdg-home", scratch + "/xdg & <data>",
         scratch + "/xdg & <data>/fonts"},
        {"~/.fonts", scratch + "/dot-fonts-home", "", scratch + "/dot-fonts-home/.fonts"},
        {"~/.local/share/fonts", scratch + "/local-home", "",
         scratch + "/local-home/.local/share/fonts"},
    };
    const TextGeometrySettings user =
        latin(QStringLiteral("SketchyUp 1"), QString::fromLatin1(kUserFamily));
    const TextGeometrySettings system =
        latin(QStringLiteral("SketchyUp 1"), QStringLiteral("DejaVu Sans"));
    for (const Layout &layout : layouts) {
        fs::create_directories(layout.folder);
        check(makeUserFont(layout.folder + "/user-font.ttf"),
              layout.name + ": the test font is copied into the user's folder");

        TextGeometry geometry;
        const std::string error = resolveAs(layout.home, layout.dataHome, user, geometry);
        check(error.empty() && geometry.actualFamily == kUserFamily && !geometry.substituted &&
                  !geometry.fallback,
              layout.name + ": the font in the user's folder resolves without fallback (" + error +
                  ", family " + geometry.actualFamily.toStdString() + ")");

        TextGeometry systemGeometry;
        const std::string systemError =
            resolveAs(layout.home, layout.dataHome, system, systemGeometry);
        check(systemError.empty() && systemGeometry.actualFamily == "DejaVu Sans" &&
                  !systemGeometry.substituted && !systemGeometry.fallback,
              layout.name + ": a system font still resolves with the user's folder present (" +
                  systemError + ")");

        // The same request with no folder: the family is not installed, so the host reports it
        // missing (substitution is off).
        const std::string empty = scratch + "/empty-" + std::to_string(&layout - layouts.data());
        fs::create_directories(empty);
        TextGeometry none;
        const std::string missing = resolveAs(empty, "", user, none);
        check(missing.find("missing") != std::string::npos,
              layout.name + ": without the folder the font is missing (got: " + missing + ")");
    }
}

// The grants the host makes are read-only (ADR 0183 section 5): the helper reads a font in the
// user's folder, and it cannot read a file beside the folder, open the folder's parent, write or
// create anything in the folder, or remove a font (unlink returns EPERM under the policy).
void testUserFontFolderIsReadOnly(const std::string &scratch) {
    const std::string parent = scratch + "/read-only & <parent>";
    const std::string folder = parent + "/fonts";
    const std::string font = folder + "/user-font.ttf";
    const std::string sibling = parent + "/secret.txt";
    fs::create_directories(folder);
    {
        std::ofstream(sibling) << "private";
    }
    check(makeUserFont(font), "the read-only test font is copied");

    TextFontGrants grants;
    grants.folders = {folder};
    const std::string out = scratch + "/read-only";

    const Outcome read =
        armed(out + "-read.out", [&] { return open(font.c_str(), O_RDONLY) >= 0 ? 0 : 1; }, grants);
    check(!read.signaled && read.code == 0, "the helper can read a font in its user folder");

    const Outcome beside = armed(
        out + "-beside.out",
        [&] {
            const int fd = open(sibling.c_str(), O_RDONLY);
            return (fd < 0 && errno == EACCES) ? 0 : 1;
        },
        grants);
    check(!beside.signaled && beside.code == 0,
          "the helper cannot read a file beside its user folder (EACCES)");

    const Outcome above = armed(
        out + "-above.out",
        [&] {
            const int fd = open(parent.c_str(), O_RDONLY | O_DIRECTORY);
            return (fd < 0 && errno == EACCES) ? 0 : 1;
        },
        grants);
    check(!above.signaled && above.code == 0,
          "the helper cannot open the parent of its user folder (EACCES)");

    const Outcome overwrite = armed(
        out + "-overwrite.out",
        [&] {
            const int fd = open(font.c_str(), O_WRONLY);
            return (fd < 0 && errno == EACCES) ? 0 : 1;
        },
        grants);
    check(!overwrite.signaled && overwrite.code == 0,
          "the helper cannot write a font in its user folder (EACCES)");

    const Outcome create = armed(
        out + "-create.out",
        [&] {
            const int fd = open((folder + "/new.ttf").c_str(), O_WRONLY | O_CREAT, 0600);
            return (fd < 0 && errno == EACCES) ? 0 : 1;
        },
        grants);
    check(!create.signaled && create.code == 0,
          "the helper cannot create a file in its user folder (EACCES)");

    const Outcome remove = armed(
        out + "-remove.out",
        [&] {
            const int removed = unlink(font.c_str());
            return (removed != 0 && errno == EPERM) ? 0 : 1;
        },
        grants);
    check(!remove.signaled && remove.code == 0,
          "the helper cannot remove a font in its user folder (EPERM: unlink is denied)");

    const Outcome makeDirectory = armed(
        out + "-mkdir.out",
        [&] {
            const int made = mkdir((folder + "/sub").c_str(), 0700);
            return (made != 0 && errno == EPERM) ? 0 : 1;
        },
        grants);
    check(!makeDirectory.signaled && makeDirectory.code == 0,
          "the helper cannot make a directory in its user folder (EPERM: mkdir is denied)");

    const Outcome changeMode = armed(
        out + "-chmod.out",
        [&] {
            const int changed = chmod(font.c_str(), 0777);
            return (changed != 0 && errno == EPERM) ? 0 : 1;
        },
        grants);
    check(!changeMode.signaled && changeMode.code == 0,
          "the helper cannot change the mode of a font (EPERM: chmod is denied)");

    check(fs::exists(font) && !fs::exists(folder + "/sub") && readAll(sibling) == "private",
          "the user's folder and the file beside it are unchanged after the attacks");
}

// The helper's font arguments: a folder and its configuration are accepted; a folder that is not an
// existing directory, a configuration that is not an existing file, a folder without its
// configuration, a configuration without folders, a relative path and any other argument are
// refused. No arguments are accepted and grant nothing.
void testFontArguments(const std::string &scratch) {
    const std::string folder = scratch + "/args-folder";
    const std::string configuration = scratch + "/args.conf";
    fs::create_directories(folder);
    {
        std::ofstream(configuration) << "<fontconfig/>";
    }
    // Each call parses with its own grants: a partial result from one call must not reach the next.
    auto parse = [](const std::vector<std::string> &args, TextFontGrants &grants) {
        std::vector<std::string> storage = {"sketchyup-text-worker"};
        storage.insert(storage.end(), args.begin(), args.end());
        std::vector<char *> argv;
        for (auto &arg : storage)
            argv.push_back(arg.data());
        argv.push_back(nullptr);
        return parseTextFontArguments(int(storage.size()), argv.data(), grants);
    };
    auto refused = [&](const std::vector<std::string> &args) {
        TextFontGrants grants;
        return parse(args, grants).has_value();
    };

    TextFontGrants grants;
    check(!parse({"--font-dir=" + folder, "--font-config=" + configuration}, grants),
          "a folder and its configuration are accepted");
    check(grants.folders == std::vector<std::string>{folder} &&
              grants.configuration == configuration,
          "the folder and the configuration are the ones given");

    TextFontGrants none;
    check(!parse({}, none) && none.folders.empty() && none.configuration.empty(),
          "no arguments are accepted and grant nothing");

    check(refused({"--font-dir=" + scratch + "/missing", "--font-config=" + configuration}),
          "a folder that does not exist is refused");
    check(refused({"--font-dir=" + configuration, "--font-config=" + configuration}),
          "a file given as a folder is refused");
    check(refused({"--font-dir=relative", "--font-config=" + configuration}),
          "a relative folder is refused");
    check(refused({"--font-config=" + scratch + "/missing.conf", "--font-dir=" + folder}),
          "a configuration that does not exist is refused");
    check(refused({"--font-dir=" + folder}), "a folder without its configuration is refused");
    check(refused({"--font-config=" + configuration}),
          "a configuration without folders is refused");
    check(refused({"--font-dir=" + folder, "--font-config=" + configuration, "--other"}),
          "any other argument is refused");
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
    // The user's fonts are the test's own: HOME and XDG_DATA_HOME name empty folders, so no test
    // reads the fonts of the machine it runs on. The user-font tests set their own.
    fs::create_directories(scratch + "/no-home");
    fs::create_directories(scratch + "/no-data");
    qputenv("HOME", (scratch + "/no-home").c_str());
    qputenv("XDG_DATA_HOME", (scratch + "/no-data").c_str());
    testFixturesIdentical(scratch);
    testPolicyRefusesAttacks(scratch);
    testRefusalsFailClosed(scratch);
    testHostClassifies(scratch);
    testMissingUserLocale();
    testUserFontFolders(scratch);
    testUserFontFolderIsReadOnly(scratch);
    testFontArguments(scratch);
    if (failures == 0)
        std::cout << "text sandbox tests passed\n";
    return failures == 0 ? 0 : 1;
}
