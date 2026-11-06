// The libc hooks of new_file_fault_tests (fault_hooks.hpp): they affect only that test process
// and its explicit child processes.
#include "new_file_fault_hooks.hpp"
#include "fault_hooks.hpp"

#include <algorithm>
#include <cerrno>
#include <csignal>
#include <fcntl.h>
#include <sys/stat.h>

namespace new_file_faults {
Fault fault{};
int writes{}, syncs{};
} // namespace new_file_faults

using namespace new_file_faults;

namespace {
bool regular(int fd) {
    struct stat st{};
    return fd > 2 && fstat(fd, &st) == 0 && S_ISREG(st.st_mode);
}
bool directory(int fd) {
    struct stat st{};
    return fstat(fd, &st) == 0 && S_ISDIR(st.st_mode);
}
} // namespace

extern "C" ssize_t FAULT_HOOK(write)(int fd, const void *bytes, size_t size) {
    if (regular(fd) && (fault == Fault::NoSpace || fault == Fault::ShortWrite)) {
        ++writes;
        if (fault == Fault::ShortWrite && writes == 1)
            return realWrite(fd, bytes, std::min(size, size_t(7)));
        errno = ENOSPC;
        return -1;
    }
    return realWrite(fd, bytes, size);
}
extern "C" int FAULT_HOOK(fsync)(int fd) {
    if (fault != Fault::None)
        ++syncs;
    const bool dir = directory(fd);
    if ((!dir && fault == Fault::KillBeforePublication) ||
        (dir && fault == Fault::KillAfterPublication))
        raise(SIGKILL);
    if ((!dir && fault == Fault::FileSync) || (dir && fault == Fault::DirectorySync)) {
        errno = EIO;
        return -1;
    }
    if (fault == Fault::RetrySync && syncs == 1) {
        errno = EINTR;
        return -1;
    }
    return realFsync(fd);
}
extern "C" int FAULT_HOOK(link)(const char *source, const char *destination) {
    if (fault == Fault::Link) {
        errno = EPERM;
        return -1;
    }
    if (fault == Fault::RacingWriter) {
        const auto fd = ::open(destination, O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC, 0600);
        if (fd >= 0) {
            constexpr char sentinel[] = "racing writer";
            realWrite(fd, sentinel, sizeof(sentinel) - 1);
            ::close(fd);
        }
    }
    return realLink(source, destination);
}
FAULT_INTERPOSE(write)
FAULT_INTERPOSE(fsync)
FAULT_INTERPOSE(link)
