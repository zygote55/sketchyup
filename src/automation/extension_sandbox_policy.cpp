#include "automation/extension_sandbox_policy.hpp"

#include <QJsonDocument>
#include <QJsonObject>
#include <QStringList>

#include <cstdint>
#include <fcntl.h>
#include <iostream>

namespace sketchy {
namespace {

// The terminal queries glibc's isatty and tcgetattr issue on a character device (stdout as
// /dev/null). TCGETS2 is what the current glibc uses, TCGETS is the older request. Nothing else is
// listed.
constexpr std::uint32_t kTcgets = 0x5401;
constexpr std::uint32_t kTcgets2 = 0x802C542A;

} // namespace

sketchyup::sandbox::Policy extensionSandboxPolicy() {
    using sketchyup::sandbox::Action;
    using sketchyup::sandbox::FileAccess;

    sketchyup::sandbox::Policy policy;
    // ADR 0183 section 5 (extension), re-derived from gdb traces of the worker on the panel fixture
    // under the stdio setups (pipes, files, a pseudo-terminal and /dev/null for stdout), and on a
    // request padded to 300 KiB. The host sets LANG and LC_ALL to C.UTF-8, so glibc reads that
    // locale from /usr/lib/locale, and that is the only grant. The trace also opened the gconv
    // modules, the locale message catalogue and OpenSSL's configuration; a run with those denied
    // gave the same bytes, so they are not granted (ADR 0183 implementation notes, S3 and S4). Read
    // only. A path that does not exist grants nothing.
    const QStringList readOnly{QStringLiteral("/usr/lib/locale")};
    for (const QString &path : readOnly)
        policy.files.push_back({path.toStdString(), FileAccess::readOnly});

    policy.syscalls = {
        // the request on stdin, the locale above, and the answer on stdout
        {"read", Action::allow},
        {"write", Action::allow},
        {"close", Action::allow},
        {"fstat", Action::allow},
        {"statx", Action::allow},
        {"lseek", Action::allow}, // a regular file as stdin
        {"openat", Action::allow},
        // memory: a large request grows its buffers with mmap, munmap and brk
        {"brk", Action::allow},
        {"mmap", Action::allow},
        {"munmap", Action::allow},
        // identity, locks and the event loop's descriptor
        {"getpid", Action::allow},
        {"gettid", Action::allow},
        {"getuid", Action::allow},
        {"geteuid", Action::allow},
        {"futex", Action::allow},
        {"eventfd2", Action::allow},
        // signals: rt_sigaction and rt_sigprocmask, and abort()'s tgkill to its own process, so
        // that an abort is a crash and not a violation (ADR 0183 section 5)
        {"rt_sigaction", Action::allow},
        {"rt_sigprocmask", Action::allow},
        {"tgkill", Action::ownProcessOnly},
        // fcntl: the file status flags of stdin (a pipe or a file). F_SETOWN would let the kernel
        // signal another process, so it is refused with EPERM.
        {"fcntl", Action::argumentOneOf, {F_GETFL, F_SETFL}},
        // ioctl: the terminal queries of the stdout check (ADR 0183 section 6.5, item 2), and
        // nothing else
        {"ioctl", Action::argumentOneOf, {kTcgets, kTcgets2}},
        // no socket, no exec and no clone: the ADR's row for the worker says it creates no thread,
        // so a
        // call to any of them kills the worker with SIGSYS, which the host reports as
        // sandboxViolation.
        // exit and exit_group are always allowed.
    };
    return policy;
}

std::optional<sketchyup::sandbox::Failure>
applyExtensionSandbox(const sketchyup::sandbox::ApplyOptions &options) {
    return sketchyup::sandbox::apply(extensionSandboxPolicy(), options);
}

int refuseExtensionSandbox(const sketchyup::sandbox::Failure &failure) {
    const QString error = QString::fromStdString(sketchyup::sandbox::unavailableReason(failure));
    const QByteArray reply =
        QJsonDocument(QJsonObject{{"protocol", 1}, {"ok", false}, {"error", error}})
            .toJson(QJsonDocument::Compact);
    std::cout.write(reply.constData(), reply.size());
    std::cout << '\n';
    std::cout.flush();
    return extensionSandboxUnavailableExit;
}

} // namespace sketchy
