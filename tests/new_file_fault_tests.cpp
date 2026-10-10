#include "io/new_file.hpp"
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <cerrno>
#include <csignal>
#include <fcntl.h>
#include <functional>
#include <iostream>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <sys/wait.h>
#include <unistd.h>
using namespace sketchy;
namespace {
enum class Fault {
    None,
    NoSpace,
    ShortWrite,
    FileSync,
    Link,
    DirectorySync,
    RetrySync,
    RacingWriter,
    KillBeforePublication,
    KillAfterPublication
};
Fault fault{};
int writes{}, syncs{};
bool regular(int fd) {
    struct stat st{};
    return fd > 2 && fstat(fd, &st) == 0 && S_ISREG(st.st_mode);
}
bool directory(int fd) {
    struct stat st{};
    return fstat(fd, &st) == 0 && S_ISDIR(st.st_mode);
}
void check(bool ok, const char *message) {
    if (!ok)
        throw std::runtime_error(message);
}
QByteArray read(const QString &path) {
    QFile file(path);
    check(file.open(QIODevice::ReadOnly), "Read publication fixture");
    return file.readAll();
}
std::string failure(const std::function<void()> &action) {
    try {
        action();
    } catch (const std::exception &error) {
        return error.what();
    }
    throw std::runtime_error("Expected publication failure");
}
struct Inject {
    explicit Inject(Fault value) {
        fault = value;
        writes = syncs = 0;
    }
    ~Inject() { fault = Fault::None; }
};
} // namespace
// Link-time interposition affects only this executable and its explicit child processes.
extern "C" ssize_t write(int fd, const void *bytes, size_t size) {
    if (regular(fd) && (fault == Fault::NoSpace || fault == Fault::ShortWrite)) {
        ++writes;
        if (fault == Fault::ShortWrite && writes == 1)
            return syscall(SYS_write, fd, bytes, std::min(size, size_t(7)));
        errno = ENOSPC;
        return -1;
    }
    return syscall(SYS_write, fd, bytes, size);
}
extern "C" int fsync(int fd) {
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
    return syscall(SYS_fsync, fd);
}
extern "C" int link(const char *source, const char *destination) {
    if (fault == Fault::Link) {
        errno = EPERM;
        return -1;
    }
    if (fault == Fault::RacingWriter) {
        const auto fd = ::open(destination, O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC, 0600);
        if (fd >= 0) {
            constexpr char sentinel[] = "racing writer";
            syscall(SYS_write, fd, sentinel, sizeof(sentinel) - 1);
            ::close(fd);
        }
    }
    return syscall(SYS_link, source, destination);
}
int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    try {
        QTemporaryDir root;
        check(root.isValid(), "Publication scratch");
        const QByteArray payload(128 * 1024, char(0x5a));
        const auto original = root.filePath("original");
        publishNewFile(original, "original bytes");
        for (const auto mode : {Fault::NoSpace, Fault::ShortWrite, Fault::FileSync, Fault::Link,
                                Fault::DirectorySync, Fault::RetrySync, Fault::RacingWriter}) {
            const auto folder = root.filePath(QString::number(int(mode)));
            check(QDir().mkdir(folder), "Create isolated fault destination");
            const auto output = folder + "/result";
            std::string error;
            {
                Inject injection(mode);
                if (mode == Fault::RetrySync)
                    publishNewFile(output, payload);
                else
                    error = failure([&] { publishNewFile(output, payload); });
            }
            const bool published = mode == Fault::DirectorySync || mode == Fault::RetrySync;
            if (published)
                check(read(output) == payload, "Complete output retained after publication");
            else if (mode == Fault::RacingWriter)
                check(read(output) == "racing writer", "Concurrent writer is never replaced");
            else
                check(!QFileInfo::exists(output), "Prepublication failure leaves no destination");
            if (mode == Fault::DirectorySync)
                check(error.find("published") != std::string::npos &&
                          error.find("uncertain") != std::string::npos,
                      "Postpublication durability uncertainty is explicit");
            if (mode == Fault::RetrySync)
                check(syncs >= 3, "Interrupted file sync is retried");
            if (mode == Fault::NoSpace || mode == Fault::ShortWrite)
                check(writes > 0, "Write fault was exercised");
            const auto entries =
                QDir(folder).entryList(QDir::Files | QDir::Hidden | QDir::NoDotAndDotDot);
            check(entries.size() == ((published || mode == Fault::RacingWriter) ? 1 : 0),
                  "Handled failure cleans staging files without deleting published data");
            check(read(original) == "original bytes",
                  "Unrelated original survives every injected fault");
        }
        // Killing a writer models process interruption, not sudden storage power loss.
        for (const auto mode : {Fault::KillBeforePublication, Fault::KillAfterPublication}) {
            const auto output = root.filePath(QString("killed-%1").arg(int(mode)));
            const auto pid = fork();
            check(pid >= 0, "Fork explicit fault child");
            if (pid == 0) {
                Inject injection(mode);
                publishNewFile(output, payload);
                _exit(2);
            }
            int status{};
            check(waitpid(pid, &status, 0) == pid && WIFSIGNALED(status) &&
                      WTERMSIG(status) == SIGKILL,
                  "Child stops at the intended publication boundary");
            if (mode == Fault::KillBeforePublication)
                check(!QFileInfo::exists(output),
                      "Killed staging writer exposes no partial destination");
            else
                check(read(output) == payload, "Killed published writer leaves a complete output");
        }
        const auto linkPath = root.filePath("existing-link");
        check(::symlink(QFile::encodeName(original).constData(),
                        QFile::encodeName(linkPath).constData()) == 0,
              "Create symlink fixture");
        failure([&] { publishNewFile(linkPath, payload); });
        failure([&] { publishNewFile(original, payload); });
        check(read(original) == "original bytes",
              "Existing original and linked targets are never replaced");
        std::cout << "New-file publication: full/partial ENOSPC, file/directory fsync, EINTR, link "
                     "failure, racing writer, killed writers and existing targets passed\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
