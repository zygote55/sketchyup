// The libc hooks of persistence_tests (fault_hooks.hpp): they confine fault injection to that test
// process and its explicit child processes.
#include "persistence_hooks.hpp"
#include "fault_hooks.hpp"

#include <cerrno>
#include <csignal>
#include <sys/stat.h>

namespace persistence_faults {
int failSync, syncError = EIO, directorySyncs, killDirectory;
bool failWrite, killFile, failFileSync;
} // namespace persistence_faults

using namespace persistence_faults;

namespace {
bool directory(int fd) {
    struct stat st{};
    return fstat(fd, &st) == 0 && S_ISDIR(st.st_mode);
}
} // namespace

extern "C" int FAULT_HOOK(fsync)(int fd) {
    if (directory(fd)) {
        ++directorySyncs;
        if (killDirectory == directorySyncs)
            raise(SIGKILL);
        if (failSync == directorySyncs) {
            errno = syncError;
            return -1;
        }
    } else {
        if (killFile)
            raise(SIGKILL);
        if (failFileSync) {
            errno = EIO;
            return -1;
        }
    }
    return realFsync(fd);
}
extern "C" ssize_t FAULT_HOOK(write)(int fd, const void *buffer, size_t size) {
    struct stat st{};
    if (failWrite && fstat(fd, &st) == 0 && S_ISREG(st.st_mode)) {
        errno = ENOSPC;
        return -1;
    }
    return realWrite(fd, buffer, size);
}
FAULT_INTERPOSE(fsync)
FAULT_INTERPOSE(write)
