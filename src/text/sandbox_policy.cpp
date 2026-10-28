#include "text/sandbox_policy.hpp"

#include <QDir>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLibraryInfo>
#include <QStringList>

#include <cstdint>
#include <fcntl.h>
#include <iostream>
#include <string_view>
#include <utility>
#include <vector>

namespace sketchy {
namespace {

// The terminal queries glibc's isatty and tcgetattr issue on a character device (stdout as
// /dev/null). TCGETS2 is what the current glibc uses, TCGETS is the older request. Nothing else is
// listed.
constexpr std::uint32_t kTcgets = 0x5401;
constexpr std::uint32_t kTcgets2 = 0x802C542A;

constexpr std::string_view kFolderArgument = "--font-dir=";
constexpr std::string_view kConfigurationArgument = "--font-config=";

} // namespace

std::optional<std::string> parseTextFontArguments(int argc, char **argv, TextFontGrants &grants) {
    for (int i = 1; i < argc; ++i) {
        const std::string_view argument = argv[i];
        if (argument.starts_with(kFolderArgument)) {
            const std::string folder(argument.substr(kFolderArgument.size()));
            const QString path = QString::fromStdString(folder);
            if (!QDir::isAbsolutePath(path) || !QFileInfo(path).isDir())
                return "Text font folder is not an existing directory";
            grants.folders.push_back(folder);
        } else if (argument.starts_with(kConfigurationArgument)) {
            const std::string file(argument.substr(kConfigurationArgument.size()));
            const QString path = QString::fromStdString(file);
            if (!grants.configuration.empty() || !QDir::isAbsolutePath(path) ||
                !QFileInfo(path).isFile())
                return "Text font configuration is not an existing file";
            grants.configuration = file;
        } else {
            return "Text worker does not take that argument";
        }
    }
    // The host gives both or neither: a folder that the configuration does not name is granted for
    // nothing, and a configuration that names no folder grants nothing the helper needs.
    if (grants.folders.empty() != grants.configuration.empty())
        return "Text font folders and their configuration come together";
    return std::nullopt;
}

sketchyup::sandbox::Policy textSandboxPolicy(const TextFontGrants &fonts) {
    using sketchyup::sandbox::Action;
    using sketchyup::sandbox::FileAccess;
    using sketchyup::sandbox::oneOfArgument;

    sketchyup::sandbox::Policy policy;
    // ADR 0183 section 5 (text), re-derived from gdb traces of the helper on the Latin fixtures and
    // a large request, under the stdio setups of section 4.5. No path is granted for writing: the
    // trace opens nothing for writing. fontconfig reads its configuration (/etc/fonts, and
    // /usr/share/fontconfig through the links in conf.d), the font directories its configuration
    // names (/usr/share/fonts and /usr/local/share/fonts), and its caches (/var/cache/fontconfig).
    // Qt loads its platform plugin from the plugins directory the library reports, so the rule
    // follows the installation. The host sets LANG and LC_ALL to C.UTF-8, so glibc reads that
    // locale from /usr/lib/locale. /etc/ssl is not granted: OpenSSL opens its configuration, the
    // open is refused, and the output does not change (ADR 0183 implementation notes, S3 and S4). A
    // path that does not exist grants nothing.
    const QStringList readOnly{QLibraryInfo::path(QLibraryInfo::PluginsPath),
                               QStringLiteral("/etc/fonts"),
                               QStringLiteral("/usr/share/fontconfig"),
                               QStringLiteral("/usr/share/fonts"),
                               QStringLiteral("/usr/local/share/fonts"),
                               QStringLiteral("/var/cache/fontconfig"),
                               QStringLiteral("/usr/lib/locale")};
    for (const QString &path : readOnly)
        policy.files.push_back({path.toStdString(), FileAccess::readOnly});

    // R109 S4 (ADR 0183 implementation notes, S3 and S4): the user's font folders, so that the
    // helper sees the fonts the desktop's list shows, and the fontconfig file that names them. Both
    // are read-only, as the system folders are: a font file is data, and nothing in these folders
    // is executed or written. The host chooses the folders (the helper's HOME is the private
    // directory, so it cannot tell where a folder came from); the helper checks only that each is
    // an existing directory (parseTextFontArguments).
    for (const std::string &folder : fonts.folders)
        policy.files.push_back({folder, FileAccess::readOnly});
    if (!fonts.configuration.empty())
        policy.files.push_back({fonts.configuration, FileAccess::readOnly});

    policy.syscalls = {
        // the request on stdin, the files above, and the answer on stdout
        {"read", Action::allow},
        {"write", Action::allow},
        {"close", Action::allow},
        {"fstat", Action::allow},
        {"statx", Action::allow},
        {"newfstatat", Action::allow},
        {"fstatfs", Action::allow},
        {"lseek", Action::allow}, // a regular file as stdin
        {"openat", Action::allow},
        {"access", Action::allow},
        {"readlink", Action::allow},
        {"getdents64", Action::allow}, // fontconfig and Qt scan the font directories
        // memory: a large result grows its buffers with mremap and munmap
        {"brk", Action::allow},
        {"mmap", Action::allow},
        {"mprotect", Action::allow},
        {"munmap", Action::allow},
        {"mremap", Action::allow},
        // identity, locks, random bytes and the event loop's descriptor
        {"getpid", Action::allow},
        {"gettid", Action::allow},
        {"getuid", Action::allow},
        {"geteuid", Action::allow},
        {"futex", Action::allow},
        {"sched_getaffinity", Action::allow},
        {"getrandom", Action::allow},
        {"eventfd2", Action::allow},
        // threads, as ADR 0183 section 4.3 gives them: a clone with CLONE_THREAD runs, a fork-like
        // clone is EPERM, and clone3 is ENOSYS so that glibc falls back to clone. The ADR's text
        // row
        // says Qt has threads; no fixture trace made one, so this entry follows the ADR,
        // unmeasured.
        {"clone", Action::threadOnly},
        {"clone3", Action::notImplemented},
        // R109 S4 (measured with a user font folder, the trace of the helper after the sandbox is
        // installed): fontconfig tries to write its cache. It makes the directory of its cache,
        // sets the mode of the file and removes its lock file, and the helper has no write rule,
        // so Landlock would refuse each one anyway. Each returns EPERM and does nothing, where the
        // kill default would end the helper.
        {"mkdir", Action::denied},
        {"chmod", Action::denied},
        {"unlink", Action::denied},
        // signals: rt_sigaction and rt_sigprocmask, and abort()'s tgkill to its own process, so
        // that a parser's abort is a crash and not a violation (ADR 0183 section 5)
        {"rt_sigaction", Action::allow},
        {"rt_sigprocmask", Action::allow},
        {"tgkill", Action::ownProcessOnly},
        // fcntl: the descriptor and file status flags of stdin (a pipe or a file). F_SETOWN would
        // let the kernel signal another process, so it is refused with EPERM.
        oneOfArgument("fcntl", 1, {F_SETFD, F_GETFL, F_SETFL}),
        // ioctl: the terminal queries of the stdout check, and nothing else
        oneOfArgument("ioctl", 1, {kTcgets, kTcgets2}),
#ifdef SKP_WORKER_NO_ADDRESS_LIMIT
        // The sanitizer builds (ASan and UBSan): their runtimes make these calls after main. UBSan
        // opens a
        // pipe for its type cache on the first vptr check; ASan gives each thread an alternate
        // signal stack.
        // A release helper does not carry them (ADR 0183 implementation notes, S3 and S4).
        {"pipe2", Action::allow},
        {"sigaltstack", Action::allow},
        {"madvise", Action::allow}, // ASan releases the pages of freed blocks
#endif
        // no socket and no exec: a call to either kills the helper with SIGSYS, which the host
        // reports as sandboxViolation. exit and exit_group are always allowed.
    };
    return policy;
}

std::optional<sketchyup::sandbox::Failure>
applyTextSandbox(const TextFontGrants &fonts, const sketchyup::sandbox::ApplyOptions &options) {
    return sketchyup::sandbox::apply(textSandboxPolicy(fonts), options);
}

int refuseTextSandbox(const sketchyup::sandbox::Failure &failure) {
    const QString error = QString::fromStdString(sketchyup::sandbox::unavailableReason(failure));
    const QByteArray reply =
        QJsonDocument(QJsonObject{{"protocol", 1}, {"ok", false}, {"error", error}})
            .toJson(QJsonDocument::Compact);
    std::cout.write(reply.constData(), reply.size());
    std::cout << '\n';
    std::cout.flush();
    return textSandboxUnavailableExit;
}

} // namespace sketchy
