#include "io/document_io.hpp"
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>
#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <unistd.h>
namespace sketchy {
namespace {
constexpr qint64 fileLimit = 16 + 1024 * 1024 + 32 * 1024 * 1024;
class Directory {
  public:
    explicit Directory(const QString &path) {
        fd_ = ::open(QFile::encodeName(path).constData(), O_RDONLY | O_DIRECTORY | O_CLOEXEC);
        if (fd_ < 0)
            throw std::runtime_error(std::string("Could not open save directory: ") +
                                     std::strerror(errno));
    }
    ~Directory() { ::close(fd_); }
    int fd() const { return fd_; }

  private:
    int fd_;
};
void sync(int fd, bool replaced) {
    int result;
    do {
        result = ::fsync(fd);
    } while (result < 0 && errno == EINTR);
    if (result < 0)
        throw std::runtime_error(std::string(replaced
                                                 ? "File replaced, but durability is uncertain: "
                                                 : "Could not sync save: ") +
                                 std::strerror(errno));
}
QByteArray read(const QString &path) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        throw std::runtime_error(file.errorString().toStdString());
    if (file.size() > fileLimit)
        throw std::runtime_error("File exceeds supported container size");
    auto bytes = file.read(fileLimit + 1);
    if (file.error() != QFileDevice::NoError || bytes.size() > fileLimit)
        throw std::runtime_error("Could not read bounded document file");
    return bytes;
}
void prepare(QSaveFile &file, const QByteArray &bytes) {
    file.setDirectWriteFallback(false);
    if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size() || !file.flush())
        throw std::runtime_error(("Could not write save: " + file.errorString()).toStdString());
    sync(file.handle(), false);
}
void commit(QSaveFile &file) {
    if (!file.commit())
        throw std::runtime_error(("Could not replace save: " + file.errorString()).toStdString());
}
} // namespace
SaveSnapshot captureSave(const Document &doc) {
    SaveSnapshot snapshot;
    snapshot.bytes_ = encodeContainer(doc);
    snapshot.stamp_ = doc.saveStamp();
    snapshot.revision_ = doc.revision();
    return snapshot;
}
void saveSnapshot(Document &doc, const SaveSnapshot &snapshot, const QString &path) {
    if (!doc.owns(snapshot.stamp_))
        throw std::runtime_error("Save snapshot belongs to a different document session");
    if (path.isEmpty())
        throw std::runtime_error("Missing save path");
    QFileInfo info(path);
    if (info.isSymLink() && !info.exists())
        throw std::runtime_error("Save target is a broken symbolic link");
    // Resolve an existing target's symlink so its actual parent is synchronized.
    const auto target = info.exists() ? info.canonicalFilePath() : info.absoluteFilePath();
    if (info.exists() && !info.isFile())
        throw std::runtime_error("Save target is not a regular file");
    Directory directory(QFileInfo(target).absolutePath());
    QByteArray previous;
    if (info.exists()) {
        previous = read(target); // An unreadable old file must not be overwritten.
        try {
            (void)decodeContainer(previous);
        } catch (const std::exception &) {
            previous.clear();
        }
    }
    QSaveFile file(target);
    prepare(file, snapshot.bytes_);
    if (!previous.isEmpty()) {
        const auto backupPath = target + ".bak";
        if (QFileInfo(backupPath).isSymLink())
            throw std::runtime_error("Backup path must not be a symbolic link");
        QSaveFile backup(backupPath);
        prepare(backup, previous);
        commit(backup);
        sync(directory.fd(), false); // Old target is still intact if this fails.
    }
    commit(file);
    sync(directory.fd(), true);
    doc.markSaved(snapshot.stamp_);
}
void saveDocument(Document &doc, const QString &path) { saveSnapshot(doc, captureSave(doc), path); }
Document loadDocument(const QString &path) { return decodeContainer(read(path)); }
} // namespace sketchy
