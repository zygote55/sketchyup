#pragma once
// Fault hooks stand in front of libc calls (write, fsync, link) for one test process.
//
// Linux: the test executable defines the hooks under libc's own names, and the link resolves every
// call in the process, Qt's included, to them. The hook reaches the kernel with syscall().
//
// macOS: the two-level namespace binds calls from Qt and libSystem straight to libSystem, so a
// definition in the executable catches nothing. The hooks are built into a dylib (CMake's
// sketchyup_fault_hooks) whose __DATA,__interpose section makes dyld redirect the call in every
// other image. dyld does not redirect the interposing image's own calls, so a hook reaches the real
// function by calling it.
#include <cstddef>
#include <fcntl.h>
#ifdef __APPLE__
#include <cstring>
#include <dlfcn.h>
#endif
#include <sys/syscall.h>
#include <sys/types.h>
#include <unistd.h>

#ifdef __APPLE__
#define FAULT_HOOK(name) faultHook_##name
#define FAULT_INTERPOSE(name)                                                                      \
    __attribute__((used)) static const struct {                                                    \
        const void *replacement, *replacee;                                                        \
    } faultInterpose_##name __attribute__((section("__DATA,__interpose"))) = {                     \
        reinterpret_cast<const void *>(&faultHook_##name),                                         \
        reinterpret_cast<const void *>(&::name)};
#else
#define FAULT_HOOK(name) name
#define FAULT_INTERPOSE(name)
#endif

// The real call behind a hook. Only the hooks call these.
inline ssize_t realWrite(int fd, const void *bytes, std::size_t size) {
#ifdef __APPLE__
    return ::write(fd, bytes, size);
#else
    return ::syscall(SYS_write, fd, bytes, size);
#endif
}
inline int realFsync(int fd) {
#ifdef __APPLE__
    return ::fsync(fd);
#else
    return static_cast<int>(::syscall(SYS_fsync, fd));
#endif
}
inline int realLink(const char *source, const char *destination) {
#ifdef __APPLE__
    return ::link(source, destination);
#elif defined(SYS_link)
    return static_cast<int>(::syscall(SYS_link, source, destination));
#else
    // aarch64 has linkat only.
    return static_cast<int>(::syscall(SYS_linkat, AT_FDCWD, source, AT_FDCWD, destination, 0));
#endif
}

// Whether a call comes from Qt's own file engine syncing a QSaveFile at commit. On Linux Qt syncs
// with fdatasync, which no hook stands in front of, so a test counts only the product's own fsync
// calls; macOS Qt calls fsync, and this lets a hook leave that call out of its count the same way.
inline bool calledFromQt([[maybe_unused]] const void *returnAddress) {
#ifdef __APPLE__
    Dl_info info{};
    return dladdr(returnAddress, &info) != 0 && info.dli_fname &&
           std::strstr(info.dli_fname, "/QtCore.framework/") != nullptr;
#else
    return false;
#endif
}
