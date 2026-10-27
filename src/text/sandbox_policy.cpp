#include "text/sandbox_policy.hpp"

#include <QJsonDocument>
#include <QJsonObject>
#include <QLibraryInfo>
#include <QStringList>

#include <cstdint>
#include <fcntl.h>
#include <iostream>

namespace sketchy {
namespace {

// The terminal queries glibc's isatty and tcgetattr issue on a character device (stdout as
// /dev/null): TCGETS2 is what the current glibc uses, TCGETS is the older request. Nothing else is
// listed.
constexpr std::uint32_t kTcgets = 0x5401;
constexpr std::uint32_t kTcgets2 = 0x802C542A;

} // namespace

sketchyup::sandbox::Policy textSandboxPolicy() {
    using sketchyup::sandbox::Action;
    using sketchyup::sandbox::FileAccess;

    sketchyup::sandbox::Policy policy;
    // ADR 0183 section 5 (text), re-derived from a trace of the helper on the fixtures and the
    // large request, under the three stdio setups (ADR 0183 section 4.5). Every path is read only:
    // the trace opens nothing for writing. fontconfig reads /etc/fonts and /usr/share/fontconfig
    // (its configuration, through symbolic links into conf.avail), /usr/share/fonts and
    // /usr/local/share/fonts (the font directories its configuration names), and
    // /var/cache/fontconfig (its caches). Qt loads its platform plugin from the plugins directory,
    // which the library reports, so the rule follows the installation. The host sets LANG and
    // LC_ALL to C.UTF-8, so glibc reads that locale from /usr/lib/locale. /etc/ssl is not granted:
    // OpenSSL's configuration is opened, the open is refused, and the output is unchanged (ADR
    // 0183, implementation notes S3 and S4). A path that does not exist grants nothing.
    const QStringList readOnly{QLibraryInfo::path(QLibraryInfo::PluginsPath),
                               QStringLiteral("/etc/fonts"),
                               QStringLiteral("/usr/share/fontconfig"),
                               QStringLiteral("/usr/share/fonts"),
                               QStringLiteral("/usr/local/share/fonts"),
                               QStringLiteral("/var/cache/fontconfig"),
                               QStringLiteral("/usr/lib/locale")};
    for (const QString &path : readOnly)
        policy.files.push_back({path.toStdString(), FileAccess::readOnly});

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
        // memory: a large result grows its buffers with mremap and munmap (the large-request trace)
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
        // signals: rt_sigaction and rt_sigprocmask, and abort()'s own-process tgkill, so that a
        // parser's
        // abort is a crash and not a violation (ADR 0183 section 5)
        {"rt_sigaction", Action::allow},
        {"rt_sigprocmask", Action::allow},
        {"tgkill", Action::ownProcessOnly},
        // fcntl: the descriptor flags and the file status flags of stdin (a pipe or a file).
        // F_SETOWN
        // would let the kernel signal another process, so it is refused.
        {"fcntl", Action::argumentOneOf, {F_SETFD, F_GETFL, F_SETFL}},
        // ioctl: the terminal queries of the stdout check, and nothing else
        {"ioctl", Action::argumentOneOf, {kTcgets, kTcgets2}},
        // no socket, no exec, no fork and no clone: the trace made none of them, so a call to any
        // of them
        // kills the helper with SIGSYS (the host reports sandboxViolation). exit and exit_group are
        // always
        // allowed.
    };
    return policy;
}

std::optional<sketchyup::sandbox::Failure>
applyTextSandbox(const sketchyup::sandbox::ApplyOptions &options) {
    return sketchyup::sandbox::apply(textSandboxPolicy(), options);
}

int refuseTextSandbox(const sketchyup::sandbox::Failure &failure) {
    const QByteArray reply =
        QJsonDocument(QJsonObject{{"protocol", 1},
                                  {"ok", false},
                                  {"error", QString::fromStdString(
                                                sketchyup::sandbox::unavailableReason(failure))}})
            .toJson(QJsonDocument::Compact);
    std::cout.write(reply.constData(), reply.size());
    std::cout << '\n';
    return textSandboxUnavailableExit;
}

} // namespace sketchy
