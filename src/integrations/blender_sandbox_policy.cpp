#include "integrations/blender_sandbox_policy.hpp"

#include <QDir>
#include <QFileInfo>

#include <initializer_list>

namespace sketchy::blender_sandbox {

namespace {

using sketchyup::sandbox::Action;
using sketchyup::sandbox::FileAccess;

// The directories inside the attempt directory that the environment names. They are created with
// the attempt, so Blender finds them and need not create them.
const char *const kSandboxDirectories[] = {"home",     "tmp",       "xdg/cache",  "xdg/config",
                                           "xdg/data", "xdg/state", "xdg/runtime"};

// The variables a child does not inherit. The session (display, bus, runtime directory), the
// loader's search paths, and Blender's and Python's own paths are either useless to a child with no
// socket or reach outside the attempt. The rest of the caller's environment passes through.
const char *const kRemovedVariables[] = {
    "DISPLAY",    "WAYLAND_DISPLAY", "DBUS_SESSION_BUS_ADDRESS", "XDG_RUNTIME_DIR", "PYTHONPATH",
    "PYTHONHOME", "LD_PRELOAD",      "LD_LIBRARY_PATH",
};

} // namespace

sketchyup::sandbox::Policy policy(const Layout &layout) {
    sketchyup::sandbox::Policy result;
    const auto grant = [&](const QString &path, FileAccess access) {
        if (!path.isEmpty())
            result.files.push_back({path.toStdString(), access});
    };

    // The one program the child may execute, and the dynamic loader the kernel opens to start it.
    // Nothing else has the execute right, so /bin/sh and every other program are refused (EACCES).
    grant(layout.executable, FileAccess::readExecute);
    grant(QFileInfo("/lib64/ld-linux-x86-64.so.2").canonicalFilePath(), FileAccess::readExecute);

    // System paths, read only. Libraries (and the Python standard library they ship with),
    // Blender's data, the oneAPI runtime Blender links, and the graphics configuration the EEVEE
    // preview reads. /etc is not granted as a whole: /etc/passwd and the rest stay refused.
    for (const char *path : {"/usr/lib", "/usr/share/blender", "/usr/share/locale",
                             "/usr/share/fonts", "/usr/share/drirc.d", "/usr/share/glvnd",
                             "/usr/share/egl", "/opt/intel", "/proc", "/sys", "/dev/dri"})
        grant(QString::fromLatin1(path), FileAccess::readOnly);
    for (const char *path : {"/etc/ld.so.cache", "/etc/ld.so.preload", "/etc/nsswitch.conf",
                             "/etc/localtime", "/etc/drirc", "/etc/glvnd", "/etc/egl"})
        grant(QString::fromLatin1(path), FileAccess::readOnly);

    // Devices: the render node for the EEVEE preview, and /dev/null (the redirections Blender
    // makes).
    grant("/dev/dri/renderD128", FileAccess::readWrite);
    grant("/dev/null", FileAccess::readWrite);

    // The render snapshot and the frames of a video, read only; the attempt directory, read and
    // write. These are the only places the child may write: HOME, the XDG directories, TMPDIR, the
    // preferences and scripts, and the results.
    grant(layout.inputDirectory, FileAccess::readOnly);
    for (const QString &frame : layout.inputFiles)
        grant(frame, FileAccess::readOnly);
    grant(layout.attemptDirectory, FileAccess::readWrite);

    result.syscalls = syscalls();
    return result;
}

std::vector<sketchyup::sandbox::SyscallRule> syscalls() {
    // ADR 0183 section 5 (Blender). The calls are those of the four real runs on this machine
    // (probe, Cycles and EEVEE render, handoff, animation and presentation encode), taken without
    // the child processes that were refused: their union is 77 names. The sandboxed runs add
    // fallocate and flock (a cold Mesa shader cache), and the CI runner adds statx. The rules that
    // follow are the choices.
    std::vector<sketchyup::sandbox::SyscallRule> rules;
    const auto allow = [&](std::initializer_list<const char *> names) {
        for (const char *name : names)
            rules.push_back({name, Action::allow});
    };
    // Memory, files and paths. Writes are limited by Landlock to the attempt directory.
    allow({"read",       "write",    "writev",        "close",     "open",       "openat",
           "stat",       "fstat",    "newfstatat",    "lseek",     "access",     "readlink",
           "getdents64", "getcwd",   "fchdir",        "mkdir",     "rmdir",      "rename",
           "unlink",     "unlinkat", "statfs",        "fcntl",     "ioctl",      "dup3",
           "pipe2",      "poll",     "epoll_create1", "epoll_ctl", "epoll_wait", "eventfd2"});
    // statx and openat2: the sandboxed probe makes them on the CI runner (the ADR 0183 S5 notes,
    // CI runner heading). statx returns metadata that newfstatat already returns. openat2 opens a
    // path as openat does, under the same Landlock rules.
    allow({"statx", "openat2"});
    // memfd_create, ftruncate and sysinfo: the software OpenGL renderer of the CI runner makes them
    // (no GPU, so Mesa's llvmpipe sizes an anonymous memory file and reads the RAM size for its
    // caches). The file has no path, so no path rule grants it, and execve, which Landlock limits
    // by path, cannot run it. sysinfo only reads counters.
    allow({"memfd_create", "ftruncate", "sysinfo"});
    allow({"mmap", "mprotect", "munmap", "madvise", "mremap", "brk"});
    // fallocate and flock: Mesa's shader cache sizes and locks its index when the EEVEE preview
    // starts with an empty cache, which every attempt does, as HOME is private. Landlock limits
    // both to the attempt directory, and neither reaches a program or a socket.
    allow({"fallocate", "flock"});
    // chmod: Blender saves its scene (handoff) with owner-only permissions. Landlock (ABI 10) has
    // no right over chmod, so the call is not limited by path; it changes only the mode of files
    // this user owns (ADR 0183 section 10, residual risk).
    allow({"chmod"});
    // Threads and processes. Blender runs --threads 4; a process is never created (clone without
    // CLONE_THREAD is EPERM), and execve may only start Blender itself (Landlock).
    rules.push_back({"clone", Action::threadOnly});
    rules.push_back({"clone3", Action::notImplemented});
    allow({"execve", "wait4", "futex", "set_robust_list", "set_tid_address", "rseq", "arch_prctl",
           "prctl", "prlimit64", "sched_yield", "sched_getaffinity", "sched_setaffinity",
           "sched_getscheduler", "sched_setscheduler", "sched_getparam", "sched_get_priority_max",
           "sched_get_priority_min", "setpriority"});
    // Signals. abort() signals its own process (tgkill): a Blender abort is a crash, not a
    // violation.
    allow({"rt_sigaction", "rt_sigprocmask"});
    rules.push_back({"tgkill", Action::ownProcessOnly});
    // Identity, time and randomness.
    allow({"getpid", "getppid", "gettid", "getuid", "geteuid", "getgid", "getegid", "getresuid",
           "getresgid", "uname", "clock_gettime", "getrandom"});
    // Sockets are refused (ADR 0183 decision 6): creating, connecting, binding or using one is
    // EPERM. Blender's Wayland, D-Bus and PipeWire attempts, and its IPv6 probe, then fail as they
    // do without a network.
    for (const char *name :
         {"socket", "socketpair", "bind", "connect", "listen", "accept", "accept4", "sendto",
          "sendmsg", "recvfrom", "recvmsg", "getsockname", "getsockopt", "setsockopt", "shutdown"})
        rules.push_back({name, Action::denied});
    // Not named, so killed (SIGSYS): fork and vfork, ptrace, execveat, kill, io_uring, bpf and the
    // rest. The host reports a SIGSYS as sandboxViolation (ADR 0183 section 4.6).
    return rules;
}

QProcessEnvironment environment(QProcessEnvironment base, const QString &attemptDirectory) {
    for (const char *key : kRemovedVariables)
        base.remove(key);
    for (const auto &key : base.keys())
        if (key.startsWith("BLENDER_"))
            base.remove(key);
    const QString root = QDir(attemptDirectory).absolutePath();
    base.insert("HOME", root + "/home");
    base.insert("TMPDIR", root + "/tmp");
    base.insert("XDG_CACHE_HOME", root + "/xdg/cache");
    base.insert("XDG_CONFIG_HOME", root + "/xdg/config");
    base.insert("XDG_DATA_HOME", root + "/xdg/data");
    base.insert("XDG_STATE_HOME", root + "/xdg/state");
    base.insert("XDG_RUNTIME_DIR", root + "/xdg/runtime");
    base.insert("ALSOFT_DRIVERS", "null");
    return base;
}

QStringList sandboxDirectories() {
    QStringList directories;
    for (const char *directory : kSandboxDirectories)
        directories.append(QString::fromLatin1(directory));
    return directories;
}

} // namespace sketchy::blender_sandbox
