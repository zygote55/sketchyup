#include "io/recovery.hpp"
#include "io/native_limits.hpp"
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QLockFile>
#include <QRegularExpression>
#include <QSaveFile>
#include <QUuid>
#include <QtEndian>
#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <unistd.h>
namespace sketchy {
namespace {
constexpr qsizetype frameLimit = 64 * 1024, containerLimit = NativeLimits::fileBytes;
constexpr qsizetype journalLimit = 8 * frameLimit;
QString token() { return QUuid::createUuid().toString(QUuid::Id128); }
QString digest(const QByteArray &bytes) {
    return QString::fromLatin1(QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex());
}
void require(bool condition, const char *message) {
    if (!condition)
        throw std::runtime_error(message);
}
void sync(int fd) {
    int result;
    do {
        result = ::fsync(fd);
    } while (result < 0 && errno == EINTR);
    if (result < 0)
        throw std::runtime_error(std::string("Recovery sync failed; durability unconfirmed: ") +
                                 std::strerror(errno));
}
void syncDirectory(const QString &path) {
    const auto fd = ::open(QFile::encodeName(path).constData(),
                           O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW);
    require(fd >= 0, "Cannot open recovery directory");
    try {
        sync(fd);
    } catch (...) {
        ::close(fd);
        throw;
    }
    ::close(fd);
}
void ensureDirectory(const QString &path) {
    QFileInfo info(path);
    if (info.exists()) {
        require(info.isDir() && !info.isSymLink(), "Recovery directory is not a real directory");
        return;
    }
    const auto parent = info.absolutePath();
    ensureDirectory(parent);
    require(QDir().mkdir(info.absoluteFilePath()), "Cannot create recovery directory");
    require(QFile::setPermissions(info.absoluteFilePath(), QFileDevice::ReadOwner |
                                                               QFileDevice::WriteOwner |
                                                               QFileDevice::ExeOwner),
            "Cannot protect recovery directory");
    syncDirectory(info.absoluteFilePath());
    syncDirectory(parent);
}
QString directory(const QString &root, const QString &key) {
    static const QRegularExpression pattern("^[0-9a-f]{32}-1-[0-9a-f]{32}$");
    require(pattern.match(key).hasMatch(), "Invalid recovery session key");
    const auto path = QDir(root).filePath(key);
    const QFileInfo info(path);
    require(info.isDir() && !info.isSymLink(), "Recovery session is missing or linked");
    return path;
}
QByteArray read(const QString &path, qsizetype limit) {
    const QFileInfo info(path);
    require(info.isFile() && !info.isSymLink(), "Recovery file is missing or linked");
    QFile file(path);
    require(file.open(QIODevice::ReadOnly) && file.size() <= limit,
            "Cannot read bounded recovery file");
    const auto bytes = file.read(limit + 1);
    require(file.error() == QFileDevice::NoError && bytes.size() <= limit, "Recovery read failed");
    return bytes;
}
void atomicWrite(const QString &path, const QByteArray &bytes) {
    require(!QFileInfo(path).isSymLink(), "Recovery target must not be a symbolic link");
    QSaveFile file(path);
    file.setDirectWriteFallback(false);
    require(file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size() && file.flush(),
            "Recovery write failed (check free space and permissions)");
    sync(file.handle());
    require(file.commit(), "Cannot replace recovery file");
    syncDirectory(QFileInfo(path).absolutePath());
}
QByteArray frame(const QJsonObject &object) {
    const auto payload = QJsonDocument(object).toJson(QJsonDocument::Compact);
    require(payload.size() <= frameLimit, "Recovery frame exceeds bound");
    QByteArray bytes(4, '\0');
    qToLittleEndian<quint32>(payload.size(), bytes.data());
    return bytes + QCryptographicHash::hash(payload, QCryptographicHash::Sha256) + payload;
}
QJsonObject unframe(const QByteArray &bytes) {
    require(bytes.size() >= 36 && qFromLittleEndian<quint32>(bytes.constData()) <= frameLimit &&
                bytes.size() == 36 + qFromLittleEndian<quint32>(bytes.constData()),
            "Invalid recovery frame length");
    const auto payload = bytes.mid(36);
    require(bytes.mid(4, 32) == QCryptographicHash::hash(payload, QCryptographicHash::Sha256),
            "Recovery checksum mismatch");
    QJsonParseError error;
    const auto parsed = QJsonDocument::fromJson(payload, &error);
    require(error.error == QJsonParseError::NoError && parsed.isObject(),
            "Invalid recovery metadata");
    return parsed.object();
}
quint64 integer(const QJsonValue &value) {
    bool ok = false;
    const auto text = value.toString();
    const auto number = text.toULongLong(&ok);
    require(ok && QString::number(number) == text, "Invalid recovery integer");
    return number;
}
void fields(const QJsonObject &object, const QStringList &names) {
    require(object.size() == names.size(), "Invalid recovery fields");
    for (const auto &name : names)
        require(object.contains(name), "Missing recovery field");
}
QString filename(const QJsonValue &value, const QString &prefix, const QString &suffix) {
    const auto name = value.toString();
    // Only locally generated names; never accept paths from recovery metadata.
    require(name.size() == prefix.size() + 32 + suffix.size() && name.startsWith(prefix) &&
                name.endsWith(suffix),
            "Invalid recovery filename");
    const auto id = name.mid(prefix.size(), 32);
    static const QRegularExpression hex("^[0-9a-f]{32}$");
    require(hex.match(id).hasMatch(), "Invalid recovery filename token");
    return name;
}
QJsonObject metadata(const RecoveryInfo &info) {
    return {{"documentId", info.documentId},
            {"epoch", "1"},
            {"revision", QString::number(info.revision)},
            {"sourcePath", info.sourcePath},
            {"savedRevision", info.savedRevision ? QJsonValue(QString::number(*info.savedRevision))
                                                 : QJsonValue(QJsonValue::Null)},
            {"savedAt",
             info.savedAt.isValid() ? info.savedAt.toUTC().toString(Qt::ISODateWithMs) : QString{}},
            {"capturedAt", info.capturedAt.toUTC().toString(Qt::ISODateWithMs)}};
}
RecoveryInfo info(const QJsonObject &object, const QString &key) {
    RecoveryInfo result;
    result.key = key;
    result.documentId = object["documentId"].toString();
    require(key.startsWith(result.documentId + "-1-") && result.documentId.size() == 32 &&
                object["epoch"] == "1",
            "Recovery document/epoch mismatch");
    result.revision = integer(object["revision"]);
    require(object["sourcePath"].isString() && object["sourcePath"].toString().size() <= 4096,
            "Invalid recovery source path");
    result.sourcePath = object["sourcePath"].toString();
    require(result.sourcePath.isEmpty() || (QDir::isAbsolutePath(result.sourcePath) &&
                                            !result.sourcePath.contains(QChar('\0'))),
            "Invalid recovery source path");
    if (!object["savedRevision"].isNull()) {
        result.savedRevision = integer(object["savedRevision"]);
        require(*result.savedRevision <= result.revision,
                "Recovery saved revision is ahead of snapshot");
    }
    require(object["savedAt"].isString() && object["capturedAt"].isString(),
            "Invalid recovery dates");
    result.savedAt = QDateTime::fromString(object["savedAt"].toString(), Qt::ISODateWithMs);
    result.capturedAt = QDateTime::fromString(object["capturedAt"].toString(), Qt::ISODateWithMs);
    require(result.capturedAt.isValid() && (object["savedAt"] == "" || result.savedAt.isValid()),
            "Invalid recovery dates");
    return result;
}
QJsonObject entry(const RecoveryInfo &info, const QString &snapshot, const QString &hash,
                  quint64 sequence, quint64 baseRevision, const QString &previous) {
    auto result = metadata(info);
    result["snapshot"] = snapshot;
    result["sha256"] = hash;
    result["sequence"] = QString::number(sequence);
    result["baseRevision"] = QString::number(baseRevision);
    result["previous"] = previous;
    return result;
}
Document validateEntry(const QJsonObject &object, const QString &path, const QString &key,
                       quint64 sequence, quint64 baseRevision, const QString &previous) {
    fields(object, {"documentId", "epoch", "revision", "sourcePath", "savedRevision", "savedAt",
                    "capturedAt", "snapshot", "sha256", "sequence", "baseRevision", "previous"});
    const auto meta = info(object, key);
    require(integer(object["sequence"]) == sequence &&
                integer(object["baseRevision"]) == baseRevision && meta.revision >= baseRevision &&
                object["previous"] == previous,
            "Recovery journal chain is not contiguous");
    const auto bytes =
        read(QDir(path).filePath(filename(object["snapshot"], "snapshot-", ".sketchyup")),
             containerLimit);
    require(object["sha256"] == digest(bytes), "Recovery snapshot checksum mismatch");
    auto doc = decodeContainer(bytes);
    require(QString::fromStdString(doc.identity()) == meta.documentId &&
                doc.revision() == meta.revision,
            "Recovery snapshot identity/revision mismatch");
    doc.markRecovered();
    return doc;
}
RecoveryRead inspect(const QString &path, const QString &key) {
    RecoveryRead result;
    result.info.key = key;
    try {
        const auto current = unframe(read(QDir(path).filePath("CURRENT"), frameLimit + 36));
        fields(current, {"format", "version", "checkpoint", "journal"});
        require(current["format"] == "sketchyup-recovery" && current["version"] == 1 &&
                    current["checkpoint"].isObject(),
                "Unsupported recovery format");
        const auto journal = filename(current["journal"], "journal-", ".bin");
        auto last = current["checkpoint"].toObject();
        result.document = validateEntry(last, path, key, 0, integer(last["revision"]), {});
        result.info = info(last, key);
        result.verified = true;
        auto previous = digest(frame(last));
        quint64 sequence = 1;
        const auto bytes = read(QDir(path).filePath(journal), journalLimit);
        qsizetype offset = 0;
        while (offset < bytes.size()) {
            if (bytes.size() - offset < 4) {
                result.incompleteTail = true;
                break;
            }
            const auto size = qFromLittleEndian<quint32>(bytes.constData() + offset);
            require(size <= frameLimit, "Recovery journal frame exceeds bound");
            if (bytes.size() - offset < qsizetype(size) + 36) {
                result.incompleteTail = true;
                break;
            }
            const auto encoded = bytes.mid(offset, size + 36);
            const auto next = unframe(encoded);
            auto doc = validateEntry(next, path, key, sequence++, result.info.revision, previous);
            result.document = std::move(doc);
            result.info = info(next, key);
            previous = digest(encoded);
            offset += size + 36;
        }
    } catch (const std::exception &error) {
        result.issue = QString::fromUtf8(error.what());
    }
    if (result.document)
        for (const auto &[id, asset] : result.document->assets())
            if (!asset->payload)
                result.missingAssets.push_back(QString::fromStdString(asset->name));
    return result;
}
void removeSession(const QString &root, const QString &key) {
    const auto path = directory(root, key);
    const auto retired = QDir(root).filePath(".discarded-" + key + "-" + token());
    require(QDir().rename(path, retired), "Cannot retire recovery session");
    syncDirectory(root);
    // Retired names are never recovery candidates. A cleanup failure is harmless.
    QDir(retired).removeRecursively();
}
} // namespace
RecoverySnapshot captureRecovery(const Document &doc, const RecoveryContext &context) {
    RecoverySnapshot snapshot;
    snapshot.info_.documentId = QString::fromStdString(doc.identity());
    snapshot.info_.revision = doc.revision();
    snapshot.info_.sourcePath =
        context.sourcePath.isEmpty() ? QString{} : QFileInfo(context.sourcePath).absoluteFilePath();
    snapshot.info_.savedRevision = context.savedRevision;
    snapshot.info_.savedAt = context.savedAt;
    snapshot.info_.capturedAt = QDateTime::currentDateTimeUtc();
    (void)info(metadata(snapshot.info_), snapshot.info_.documentId + "-1-" + token());
    snapshot.bytes_ = encodeContainer(doc);
    return snapshot;
}
struct RecoveryWriter::State {
    QString root, key, path, journal, previous;
    std::unique_ptr<QLockFile> lock;
    quint64 sequence{}, revision{};
    qsizetype retainedBytes{};
    bool failed{}, closed{};
};
RecoveryWriter::RecoveryWriter(const QString &root, const QString &documentId)
    : state_(std::make_unique<State>()) {
    static const QRegularExpression id("^[0-9a-f]{32}$");
    require(id.match(documentId).hasMatch(), "Invalid recovery document identity");
    state_->root = QFileInfo(root).absoluteFilePath();
    ensureDirectory(state_->root);
    state_->key = documentId + "-1-" + token();
    state_->path = QDir(state_->root).filePath(state_->key);
    ensureDirectory(state_->path);
    state_->lock = std::make_unique<QLockFile>(QDir(state_->path).filePath("session.lock"));
    state_->lock->setStaleLockTime(0);
    require(state_->lock->tryLock(0), "Recovery session is already active");
}
RecoveryWriter::~RecoveryWriter() = default;
const QString &RecoveryWriter::key() const { return state_->key; }
RecoveryInfo RecoveryWriter::write(const RecoverySnapshot &snapshot) {
    auto &s = *state_;
    require(!s.closed, "Recovery session is closed");
    require(s.key.startsWith(snapshot.info_.documentId + "-1-") &&
                snapshot.info_.revision >= s.revision,
            "Recovery snapshot belongs to a different or older document");
    try {
        const auto name = "snapshot-" + token() + ".sketchyup";
        atomicWrite(QDir(s.path).filePath(name), snapshot.bytes_);
        const bool compact = s.journal.isEmpty() || s.failed || s.sequence >= 4 ||
                             s.retainedBytes + snapshot.bytes_.size() > containerLimit;
        auto record =
            entry(snapshot.info_, name, digest(snapshot.bytes_), compact ? 0 : s.sequence,
                  compact ? snapshot.info_.revision : s.revision, compact ? QString{} : s.previous);
        const auto encoded = frame(record);
        if (compact) {
            const auto journal = "journal-" + token() + ".bin";
            atomicWrite(QDir(s.path).filePath(journal), {});
            atomicWrite(QDir(s.path).filePath("CURRENT"), frame({{"format", "sketchyup-recovery"},
                                                                 {"version", 1},
                                                                 {"checkpoint", record},
                                                                 {"journal", journal}}));
            s.journal = journal;
            s.sequence = 1;
            s.retainedBytes = snapshot.bytes_.size();
            // Only retire old files after the new pointer and its directory are durable.
            for (const auto &file :
                 QDir(s.path).entryList({"snapshot-*.sketchyup", "journal-*.bin"}, QDir::Files))
                if (file != name && file != journal)
                    QFile::remove(QDir(s.path).filePath(file));
        } else {
            QFile journal(QDir(s.path).filePath(s.journal));
            require(!QFileInfo(journal).isSymLink() &&
                        journal.open(QIODevice::WriteOnly | QIODevice::Append) &&
                        journal.write(encoded) == encoded.size() && journal.flush(),
                    "Recovery journal write failed (check free space and permissions)");
            sync(journal.handle());
            ++s.sequence;
            s.retainedBytes += snapshot.bytes_.size();
        }
        s.previous = digest(encoded);
        s.revision = snapshot.info_.revision;
        s.failed = false;
        auto result = snapshot.info_;
        result.key = s.key;
        return result;
    } catch (...) {
        s.failed = true;
        throw;
    }
}
void RecoveryWriter::discard() {
    require(!state_->closed, "Recovery session is closed");
    removeSession(state_->root, state_->key);
    state_->closed = true;
    state_->lock.reset();
}
RecoveryRead readRecovery(const QString &root, const QString &key) {
    RecoveryRead result;
    result.info.key = key;
    try {
        const auto path = directory(root, key);
        QLockFile lock(QDir(path).filePath("session.lock"));
        lock.setStaleLockTime(0);
        if (!lock.tryLock(0)) {
            result.busy = lock.error() == QLockFile::LockFailedError;
            result.issue = result.busy ? "Recovery session is active"
                                       : "Cannot lock recovery session (check permissions)";
            return result;
        }
        return inspect(path, key);
    } catch (const std::exception &error) {
        result.issue = QString::fromUtf8(error.what());
    }
    return result;
}
QJsonObject describeRecovery(const RecoveryRead &read) {
    QJsonArray missing;
    for (const auto &name : read.missingAssets)
        missing.append(name);
    auto result = metadata(read.info);
    if (!read.verified) {
        result["revision"] = QJsonValue::Null;
        result["capturedAt"] = QJsonValue::Null;
    }
    result["key"] = read.info.key;
    result["verified"] = read.verified;
    result["busy"] = read.busy;
    result["incompleteTail"] = read.incompleteTail;
    result["issue"] = read.issue;
    result["missingAssets"] = missing;
    return result;
}
std::vector<RecoveryRead> listRecoveries(const QString &root) {
    std::vector<RecoveryRead> result;
    static const QRegularExpression key("^[0-9a-f]{32}-1-[0-9a-f]{32}$");
    for (const auto &name :
         QDir(root).entryList(QDir::Dirs | QDir::NoDotAndDotDot | QDir::NoSymLinks)) {
        if (!key.match(name).hasMatch())
            continue;
        auto candidate = readRecovery(root, name);
        if (!candidate.busy) {
            candidate.document.reset();
            result.push_back(std::move(candidate));
        }
    }
    return result;
}
void discardRecovery(const QString &root, const QString &key) {
    const auto path = directory(root, key);
    QLockFile lock(QDir(path).filePath("session.lock"));
    lock.setStaleLockTime(0);
    require(lock.tryLock(0), "Recovery session is active or cannot be locked");
    removeSession(root, key);
}
} // namespace sketchy
