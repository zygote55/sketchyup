// The libc hooks of recovery_tests (fault_hooks.hpp): fault injection is local to that test
// process, including its killed child processes.
#include "recovery_hooks.hpp"
#include "fault_hooks.hpp"

#include <algorithm>
#include <cerrno>
#include <csignal>
#include <sys/stat.h>

namespace recovery_faults {
int syncs{}, failSync{}, killSync{};
bool fullDisk{}, partialWrite{};
} // namespace recovery_faults

using namespace recovery_faults;

extern "C" int FAULT_HOOK(fsync)(int fd) {
    if (calledFromQt(__builtin_return_address(0)))
        return realFsync(fd);
    ++syncs;
    if (syncs == killSync)
        raise(SIGKILL);
    if (syncs == failSync) {
        errno = ENOSPC;
        return -1;
    }
    return realFsync(fd);
}
extern "C" ssize_t FAULT_HOOK(write)(int fd, const void *bytes, size_t count) {
    struct stat st{};
    if (fstat(fd, &st) == 0 && S_ISREG(st.st_mode)) {
        if (fullDisk) {
            errno = ENOSPC;
            return -1;
        }
        if (partialWrite) {
            partialWrite = false;
            fullDisk = true;
            return realWrite(fd, bytes, std::min<size_t>(13, count));
        }
    }
    return realWrite(fd, bytes, count);
}
FAULT_INTERPOSE(fsync)
FAULT_INTERPOSE(write)
