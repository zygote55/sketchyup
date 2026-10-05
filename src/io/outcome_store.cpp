#include "io/outcome_store.hpp"
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QLockFile>
#include <QRegularExpression>
#include <QSaveFile>
#include <QtEndian>
#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <unistd.h>
namespace sketchy {
namespace {
constexpr size_t hardLimit = 64 * 1024 * 1024;
constexpr size_t resultLimit = 256 * 1024;
constexpr qint64 retentionDays = 30;
const QByteArray magic("SKUPOUT1", 8);
[[noreturn]] void fail(const char *code, const char *message) {
    throw OutcomeStoreError(code, message);
}
void require(bool condition, const char *code, const char *message) {
    if (!condition)
        fail(code, message);
}
QString digest(const QByteArray &bytes) {
    return QString::fromLatin1(QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex());
}
bool hex(const QString &value, int length) {
    return value.size() == length && QRegularExpression("^[0-9a-f]+$").match(value).hasMatch();
}
uint64_t integer(const QJsonValue &value) {
    bool ok = false;
    const auto text = value.toString();
    const auto n = text.toULongLong(&ok);
    require(ok && text == QString::number(n), "CORRUPT_OUTCOMES", "Invalid outcome integer");
    return n;
}
QDateTime timestamp(const QJsonValue &value) {
    const auto t = QDateTime::fromString(value.toString(), Qt::ISODateWithMs);
    require(t.isValid() && t.timeSpec() == Qt::UTC, "CORRUPT_OUTCOMES",
            "Invalid outcome timestamp");
    return t;
}
void syncFile(int fd) {
    int result;
    do {
        result = ::fsync(fd);
    } while (result < 0 && errno == EINTR);
    require(result == 0, "IO_FAILURE", "Could not synchronize outcome storage");
}
void syncDirectory(const QString &path) {
    const int fd = ::open(QFile::encodeName(path).constData(),
                          O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW);
    require(fd >= 0, "IO_FAILURE", "Could not open outcome directory");
    try {
        syncFile(fd);
    } catch (...) {
        ::close(fd);
        throw;
    }
    ::close(fd);
}
void ensureDirectory(const QString &path) {
    const QFileInfo info(path);
    if (info.exists()) {
        require(info.isDir() && !info.isSymLink(), "INVALID_STORAGE",
                "Outcome directory must be a real directory");
        return;
    }
    const auto parent = info.absolutePath();
    ensureDirectory(parent);
    require(QDir().mkdir(info.absoluteFilePath()), "IO_FAILURE",
            "Could not create outcome directory");
    require(QFile::setPermissions(info.absoluteFilePath(), QFileDevice::ReadOwner |
                                                               QFileDevice::WriteOwner |
                                                               QFileDevice::ExeOwner),
            "IO_FAILURE", "Could not protect outcome directory");
    syncDirectory(info.absoluteFilePath());
    syncDirectory(parent);
}
void fields(const QJsonObject &object, const QStringList &names) {
    require(object.size() == names.size(), "CORRUPT_OUTCOMES", "Unexpected outcome fields");
    for (const auto &name : names)
        require(object.contains(name), "CORRUPT_OUTCOMES", "Missing outcome field");
}
Document validatedDocument(const QByteArray &bytes) {
    try {
        return decodeContainer(bytes);
    } catch (const std::runtime_error &) {
        fail("CORRUPT_OUTCOMES", "Outcome checkpoint contains an invalid native document");
    }
}
struct Checkpoint {
    uint64_t nextId{1};
    QJsonObject records;
    QString latest;
    QByteArray before, after;
};
QJsonObject metadata(const Checkpoint &state, const QString &doc) {
    return {{"version", 1},
            {"documentId", doc},
            {"epoch", "1"},
            {"nextRequestId", QString::number(state.nextId)},
            {"outcomes", state.records},
            {"latest", state.latest},
            {"beforeBytes", QString::number(state.before.size())},
            {"afterBytes", QString::number(state.after.size())},
            {"beforeHash", digest(state.before)},
            {"afterHash", digest(state.after)}};
}
QByteArray encode(const Checkpoint &state, const QString &doc, size_t limit) {
    const auto json = QJsonDocument(metadata(state, doc)).toJson(QJsonDocument::Compact);
    require(size_t(json.size()) <= limit && size_t(state.before.size()) <= limit &&
                size_t(state.after.size()) <= limit &&
                size_t(json.size()) + size_t(state.before.size()) + size_t(state.after.size()) +
                        44 <=
                    limit,
            "OUTCOME_LIMIT", "Durable outcomes exceed the storage budget");
    QByteArray result = magic;
    result.resize(12);
    qToLittleEndian<quint32>(json.size(), result.data() + 8);
    const auto payload = json + state.before + state.after;
    result += QCryptographicHash::hash(payload, QCryptographicHash::Sha256);
    result += payload;
    return result;
}
Checkpoint decode(const QByteArray &bytes, const QString &doc, size_t limit) {
    require(bytes.size() >= 44 && size_t(bytes.size()) <= limit && bytes.first(8) == magic,
            "CORRUPT_OUTCOMES", "Invalid outcome checkpoint header");
    const auto metaBytes = qFromLittleEndian<quint32>(bytes.constData() + 8);
    require(metaBytes <= size_t(bytes.size() - 44), "CORRUPT_OUTCOMES",
            "Truncated outcome metadata");
    const auto payload = bytes.mid(44);
    require(bytes.mid(12, 32) == QCryptographicHash::hash(payload, QCryptographicHash::Sha256),
            "CORRUPT_OUTCOMES", "Outcome checkpoint checksum mismatch");
    QJsonParseError error;
    const auto parsed = QJsonDocument::fromJson(payload.first(metaBytes), &error);
    require(error.error == QJsonParseError::NoError && parsed.isObject(), "CORRUPT_OUTCOMES",
            "Invalid outcome metadata JSON");
    const auto meta = parsed.object();
    fields(meta, {"version", "documentId", "epoch", "nextRequestId", "outcomes", "latest",
                  "beforeBytes", "afterBytes", "beforeHash", "afterHash"});
    require(meta["version"] == 1 && meta["documentId"] == doc && meta["epoch"] == "1" &&
                meta["outcomes"].isObject() && meta["latest"].isString(),
            "CORRUPT_OUTCOMES", "Outcome checkpoint identity/version mismatch");
    Checkpoint state;
    state.nextId = integer(meta["nextRequestId"]);
    state.records = meta["outcomes"].toObject();
    state.latest = meta["latest"].toString();
    require(state.nextId > 0 && state.records.size() <= 10000, "CORRUPT_OUTCOMES",
            "Invalid outcome count/high-water mark");
    const auto before = integer(meta["beforeBytes"]), after = integer(meta["afterBytes"]);
    require(before <= limit && after <= limit &&
                before + after + metaBytes == size_t(payload.size()),
            "CORRUPT_OUTCOMES", "Invalid outcome snapshot ranges");
    state.before = payload.mid(metaBytes, before);
    state.after = payload.mid(metaBytes + before, after);
    require(meta["beforeHash"] == digest(state.before) && meta["afterHash"] == digest(state.after),
            "CORRUPT_OUTCOMES", "Outcome snapshot hash mismatch");
    for (auto i = state.records.begin(); i != state.records.end(); ++i) {
        const auto id = integer(i.key());
        const auto record = i.value().toObject();
        fields(record, {"requestId", "payloadHash", "status", "baseRevision", "baseHash",
                        "acceptedAt", "expiresAt", "finishedAt", "result"});
        require(id > 0 && id < state.nextId && record["requestId"] == i.key() &&
                    hex(record["payloadHash"].toString(), 64) &&
                    hex(record["baseHash"].toString(), 64) && record["result"].isObject(),
                "CORRUPT_OUTCOMES", "Invalid outcome identity or hash");
        (void)integer(record["baseRevision"]);
        const auto accepted = timestamp(record["acceptedAt"]),
                   expires = timestamp(record["expiresAt"]);
        require(expires > accepted && accepted.secsTo(expires) <= 300, "CORRUPT_OUTCOMES",
                "Invalid pending lifetime");
        const auto status = record["status"].toString();
        require(status == "pending" || status == "committed" || status == "aborted",
                "CORRUPT_OUTCOMES", "Invalid outcome state");
        if (status == "pending")
            require(record["finishedAt"].isNull() && record["result"].toObject().isEmpty(),
                    "CORRUPT_OUTCOMES", "Pending request has terminal data");
        else
            (void)timestamp(record["finishedAt"]);
        require(
            size_t(
                QJsonDocument(record["result"].toObject()).toJson(QJsonDocument::Compact).size()) <=
                resultLimit,
            "CORRUPT_OUTCOMES", "Outcome result exceeds budget");
    }
    if (state.latest.isEmpty())
        require(state.before.isEmpty() && state.after.isEmpty(), "CORRUPT_OUTCOMES",
                "Snapshots without a committed outcome");
    else {
        require(state.records.contains(state.latest) &&
                    state.records[state.latest].toObject()["status"] == "committed",
                "CORRUPT_OUTCOMES", "Latest committed outcome is missing");
        const auto baseline = validatedDocument(state.before),
                   candidate = validatedDocument(state.after);
        const auto record = state.records[state.latest].toObject();
        const auto base = integer(record["baseRevision"]);
        require(base < UINT64_MAX && baseline.identity() == doc.toStdString() &&
                    candidate.identity() == baseline.identity() && baseline.revision() == base &&
                    candidate.revision() == base + 1 && digest(state.before) == record["baseHash"],
                "CORRUPT_OUTCOMES", "Latest commit snapshot/revision mismatch");
    }
    return state;
}
} // namespace
struct OutcomeStore::State {
    QString doc, dir, path;
    Limits limits;
    Now now;
    Fault fault;
    std::unique_ptr<QLockFile> lock;
    Checkpoint checkpoint;
    bool unknown{};
    QDateTime time() const {
        const auto value = now();
        require(value.isValid(), "INVALID_CLOCK", "Outcome clock is unavailable");
        return value.toUTC();
    }
    void ready() const {
        require(!unknown, "OUTCOME_UNKNOWN",
                "Reconcile uncertain outcome storage before further operations");
    }
    void phase(Phase value) {
        if (fault)
            fault(value);
    }
    void capacity(const Checkpoint &next, const QByteArray &bytes) const {
        require(size_t(next.records.size()) <= limits.outcomes, "OUTCOME_LIMIT",
                "Durable outcome count is full");
        size_t pending = 0;
        for (const auto &value : next.records)
            if (value.toObject()["status"] == "pending")
                ++pending;
        require(size_t(bytes.size()) + pending * (resultLimit + 4096) <= limits.bytes,
                "OUTCOME_LIMIT", "No capacity reserved for pending outcomes");
    }
    void write(Checkpoint next) {
        ready();
        const auto bytes = encode(next, doc, limits.bytes);
        capacity(next, bytes);
        bool replacing = false;
        try {
            phase(Phase::BeforeWrite);
            require(!QFileInfo(path).isSymLink(), "INVALID_STORAGE",
                    "Outcome checkpoint cannot be a symbolic link");
            QSaveFile file(path);
            file.setDirectWriteFallback(false);
            require(file.open(QIODevice::WriteOnly) &&
                        file.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner) &&
                        file.write(bytes) == bytes.size() && file.flush(),
                    "IO_FAILURE", "Cannot write outcome checkpoint");
            phase(Phase::AfterWrite);
            syncFile(file.handle());
            phase(Phase::AfterFileSync);
            replacing = true;
            require(file.commit(), "IO_FAILURE", "Cannot replace outcome checkpoint");
            phase(Phase::AfterRename);
            syncDirectory(dir);
            phase(Phase::AfterDirectorySync);
            checkpoint = std::move(next);
        } catch (...) {
            if (replacing) {
                unknown = true;
                fail("OUTCOME_UNKNOWN",
                     "Checkpoint replacement may have committed; reconciliation is required");
            }
            throw;
        }
    }
    void load() {
        const QFileInfo info(path);
        require(info.isFile() && !info.isSymLink(), "CORRUPT_OUTCOMES",
                "Outcome checkpoint is missing or linked");
        QFile file(path);
        require(file.open(QIODevice::ReadOnly) && file.size() <= qint64(limits.bytes), "IO_FAILURE",
                "Cannot read bounded outcome checkpoint");
        const auto bytes = file.read(qint64(limits.bytes) + 1);
        require(file.error() == QFileDevice::NoError && size_t(bytes.size()) <= limits.bytes,
                "IO_FAILURE", "Outcome checkpoint read failed");
        auto next = decode(bytes, doc, limits.bytes);
        capacity(next, bytes);
        syncFile(file.handle());
        syncDirectory(dir);
        checkpoint = std::move(next);
        unknown = false;
    }
    QJsonObject record(const QString &id, const QString &hash) const {
        ready();
        require(hex(hash, 64), "INVALID_REQUEST", "Payload hash must be SHA-256 hex");
        bool ok = false;
        const auto number = id.toULongLong(&ok);
        require(ok && number > 0 && QString::number(number) == id, "INVALID_REQUEST",
                "Request identity is not canonical");
        if (!checkpoint.records.contains(id))
            return {{"requestId", id}, {"status", "unknown"}, {"reconciliationRequired", true}};
        const auto value = checkpoint.records[id].toObject();
        require(value["payloadHash"] == hash, "REQUEST_CONFLICT",
                "Request identity is bound to another payload");
        return value;
    }
    QJsonObject publicResult(const QJsonObject &record) const {
        auto result = record;
        result["apiVersion"] = 1;
        result["documentId"] = doc;
        result["epoch"] = "1";
        return result;
    }
    void prune(Checkpoint &next, const QDateTime &current) {
        for (auto i = next.records.begin(); i != next.records.end();) {
            const auto record = i.value().toObject();
            if (i.key() != next.latest && record["status"] != "pending" &&
                timestamp(record["finishedAt"]).addDays(retentionDays) <= current)
                i = next.records.erase(i);
            else
                ++i;
        }
    }
};
OutcomeStore::OutcomeStore(const QString &root, const QString &doc)
    : OutcomeStore(root, doc, Limits{}) {}
OutcomeStore::OutcomeStore(const QString &root, const QString &doc, Limits limits, Now now,
                           Fault fault)
    : state_(std::make_unique<State>()) {
    require(hex(doc, 32) && !root.isEmpty(), "INVALID_REQUEST",
            "Outcome storage requires a document identity and root");
    require(limits.outcomes > 0 && limits.outcomes <= 10000 && limits.bytes >= 1024 &&
                limits.bytes <= hardLimit && bool(now),
            "INVALID_REQUEST", "Invalid outcome storage limits");
    auto &s = *state_;
    s.doc = doc;
    s.limits = limits;
    s.now = std::move(now);
    s.fault = std::move(fault);
    s.dir = QDir(root).absoluteFilePath(doc + "-1");
    const bool existed = QFileInfo::exists(s.dir);
    ensureDirectory(s.dir);
    s.path = QDir(s.dir).filePath("OUTCOMES");
    s.lock = std::make_unique<QLockFile>(QDir(s.dir).filePath("writer.lock"));
    s.lock->setStaleLockTime(0);
    require(s.lock->tryLock(0), "STORE_BUSY", "Another actor owns this document outcome store");
    if (QFileInfo::exists(s.path))
        s.load();
    else {
        require(!existed, "CORRUPT_OUTCOMES",
                "Existing outcome directory has no checkpoint; refusing to reset identities");
        s.write(Checkpoint{});
    }
}
OutcomeStore::~OutcomeStore() = default;
bool OutcomeStore::uncertain() const { return state_->unknown; }
QString OutcomeStore::directory() const { return state_->dir; }
void OutcomeStore::reconcile() {
    state_->unknown = true;
    state_->load();
}
QJsonObject OutcomeStore::lookup(const QString &id, const QString &hash) const {
    return state_->publicResult(state_->record(id, hash));
}
QJsonObject OutcomeStore::begin(const Document &base, const QString &hash, int ttl) {
    auto &s = *state_;
    s.ready();
    require(base.identity() == s.doc.toStdString(), "WRONG_DOCUMENT",
            "Begin targets a different document");
    require(hex(hash, 64) && ttl >= 1 && ttl <= 300, "INVALID_REQUEST",
            "Invalid payload hash or lifetime");
    require(base.readSnapshotBytes() <= s.limits.bytes, "OUTCOME_LIMIT",
            "Base document exceeds durable storage capacity");
    const auto baseBytes = encodeContainer(base);
    if (!s.checkpoint.latest.isEmpty()) {
        const auto previous = s.checkpoint.records[s.checkpoint.latest].toObject();
        const auto previousRevision = integer(previous["baseRevision"]) + 1;
        require(base.revision() >= previousRevision &&
                    (base.revision() != previousRevision || baseBytes == s.checkpoint.after),
                "RECONCILIATION_REQUIRED",
                "Live document disagrees with the last committed outcome");
    }
    auto next = s.checkpoint;
    const auto now = s.time();
    s.prune(next, now);
    require(next.nextId < UINT64_MAX, "OUTCOME_LIMIT", "Request identity space exhausted");
    const auto id = QString::number(next.nextId++);
    QJsonObject record{{"requestId", id},
                       {"payloadHash", hash},
                       {"status", "pending"},
                       {"baseRevision", QString::number(base.revision())},
                       {"baseHash", digest(baseBytes)},
                       {"acceptedAt", now.toString(Qt::ISODateWithMs)},
                       {"expiresAt", now.addSecs(ttl).toString(Qt::ISODateWithMs)},
                       {"finishedAt", QJsonValue()},
                       {"result", QJsonObject{}}};
    next.records[id] = record;
    s.write(std::move(next));
    return s.publicResult(record);
}
QJsonObject OutcomeStore::abort(const QString &id, const QString &hash, const QString &reason) {
    auto &s = *state_;
    auto record = s.record(id, hash);
    if (record["status"] != "pending")
        return s.publicResult(record);
    require(!reason.isEmpty() && reason.toUtf8().size() <= 1024, "INVALID_REQUEST",
            "Abort reason is outside bounds");
    record["status"] = "aborted";
    record["finishedAt"] =
        std::max(s.time(), timestamp(record["acceptedAt"])).toString(Qt::ISODateWithMs);
    record["result"] = QJsonObject{{"reason", reason}};
    auto next = s.checkpoint;
    next.records[id] = record;
    s.write(std::move(next));
    return s.publicResult(record);
}
QJsonObject OutcomeStore::commit(const QString &id, const QString &hash, const Document &before,
                                 const Document &after, const QJsonObject &result) {
    auto &s = *state_;
    auto record = s.record(id, hash);
    if (record["status"] != "pending")
        return s.publicResult(record);
    const auto now = s.time();
    if (now >= timestamp(record["expiresAt"]))
        return abort(id, hash, "expired_before_commit");
    require(before.identity() == s.doc.toStdString() && after.identity() == before.identity(),
            "WRONG_DOCUMENT", "Commit targets another document");
    const auto revision = integer(record["baseRevision"]);
    require(revision < UINT64_MAX && before.revision() == revision &&
                after.revision() == revision + 1,
            "STALE_REVISION", "Commit does not follow the accepted base revision");
    require(before.readSnapshotBytes() <= s.limits.bytes &&
                after.readSnapshotBytes() <= s.limits.bytes,
            "OUTCOME_LIMIT", "Commit snapshots exceed storage capacity");
    require(size_t(QJsonDocument(result).toJson(QJsonDocument::Compact).size()) <= resultLimit,
            "OUTCOME_LIMIT", "Commit result exceeds its reserved budget");
    auto next = s.checkpoint;
    next.before = encodeContainer(before);
    next.after = encodeContainer(after);
    require(digest(next.before) == record["baseHash"], "STALE_REVISION",
            "Commit baseline differs from the accepted document");
    if (!s.checkpoint.latest.isEmpty()) {
        const auto previous = s.checkpoint.records[s.checkpoint.latest].toObject();
        const auto previousRevision = integer(previous["baseRevision"]) + 1;
        require(revision >= previousRevision &&
                    (revision != previousRevision || next.before == s.checkpoint.after),
                "RECONCILIATION_REQUIRED",
                "Live document disagrees with the last committed outcome");
    }
    record["status"] = "committed";
    record["finishedAt"] =
        std::max(now, timestamp(record["acceptedAt"])).toString(Qt::ISODateWithMs);
    record["result"] = result;
    next.latest = id;
    next.records[id] = record;
    s.write(std::move(next));
    return s.publicResult(record);
}
QByteArray OutcomeStore::latestBefore() const {
    state_->ready();
    return state_->checkpoint.before;
}
QByteArray OutcomeStore::latestAfter() const {
    state_->ready();
    return state_->checkpoint.after;
}
QJsonObject OutcomeStore::latestOutcome() const {
    state_->ready();
    if (state_->checkpoint.latest.isEmpty())
        return {};
    return state_->publicResult(state_->checkpoint.records[state_->checkpoint.latest].toObject());
}
} // namespace sketchy
