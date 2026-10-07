#include "io/new_file.hpp"
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryFile>
#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <stdexcept>
#include <unistd.h>
namespace sketchy {
namespace {
void syncFile(int descriptor, bool published) {
    int status;
    do {
        status = ::fsync(descriptor);
    } while (status < 0 && errno == EINTR);
    if (status != 0)
        throw std::runtime_error(
            published ? "Output file published, but directory durability is uncertain"
                      : "Could not synchronize output file");
}
} // namespace
void publishNewFile(const QString &path, const QByteArray &bytes) {
    const QFileInfo info(path);
    if (path.isEmpty() || info.exists() || info.isSymLink() || info.fileName().isEmpty())
        throw std::runtime_error(
            "A new output path is required; existing files are never replaced");
    const auto parent = info.dir().canonicalPath();
    if (parent.isEmpty())
        throw std::runtime_error("Output parent must already exist");
    const auto target = QDir(parent).filePath(info.fileName());
    QTemporaryFile temporary(QDir(parent).filePath(".sketchyup-new-XXXXXX"));
    if (!temporary.open() || temporary.write(bytes) != bytes.size() || !temporary.flush())
        throw std::runtime_error("Could not write output file");
    syncFile(temporary.handle(), false);
    // Hard-link publication is atomic and fails on every pre-existing destination,
    // including a racing writer or dangling symbolic link. Both paths share a parent.
    const auto directory =
        ::open(QFile::encodeName(parent).constData(), O_RDONLY | O_DIRECTORY | O_CLOEXEC);
    if (directory < 0)
        throw std::runtime_error("Could not open output directory");
    if (::link(QFile::encodeName(temporary.fileName()).constData(),
               QFile::encodeName(target).constData()) != 0) {
        const auto error = errno;
        ::close(directory);
        throw std::runtime_error(std::string("Could not publish output file: ") +
                                 std::strerror(error));
    }
    try {
        syncFile(directory, true);
    } catch (...) {
        ::close(directory);
        throw;
    }
    ::close(directory);
}
} // namespace sketchy
