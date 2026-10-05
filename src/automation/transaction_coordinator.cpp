#include "automation/transaction_coordinator.hpp"
#include "core/component_records.hpp"
#include <QCryptographicHash>
#include <QJsonDocument>
#include <set>
#include <type_traits>
namespace sketchy {
namespace {
[[noreturn]] void fail(const char *code, const char *message) {
    throw InspectionError(code, message);
}
bool equal(const ComponentDefinition &a, const ComponentDefinition &b) {
    if (a.id != b.id || a.root != b.root || a.nextMemberId != b.nextMemberId || a.name != b.name ||
        a.references != b.references || a.members.size() != b.members.size())
        return false;
    for (const auto &[id, body] : a.members)
        if (!b.members.contains(id) || *body != *b.members.at(id))
            return false;
    return true;
}
bool equal(const AssetRecord &a, const AssetRecord &b) {
    return a.id == b.id && a.name == b.name && a.mediaType == b.mediaType &&
           bool(a.payload) == bool(b.payload) &&
           (!a.payload || a.payload->bytes() == b.payload->bytes());
}
template <class T> bool equal(const T &a, const T &b) { return a == b; }
Document recovered(const OutcomeStore &store) {
    const auto outcome = store.latestOutcome();
    if (outcome.isEmpty())
        fail("RECOVERY_UNAVAILABLE", "No committed transaction checkpoint exists");
    const auto result = outcome["result"].toObject();
    if (result["transactionApiVersion"] != 1 || !result["undo"].isObject())
        fail("RECOVERY_UNAVAILABLE", "Checkpoint has no supported transaction undo metadata");
    auto before = decodeContainer(store.latestBefore());
    const auto after = decodeContainer(store.latestAfter());
    const auto undo = result["undo"].toObject();
    if (undo.size() != 5 || !undo["label"].isString() || !undo["taskId"].isString() ||
        !undo["request"].isString() || !undo["assistant"].isBool() ||
        undo["commitRevision"] != QString::number(after.revision()) ||
        result["revision"] != QString::number(after.revision()))
        fail("CORRUPT_OUTCOMES", "Stored undo metadata does not match the committed revision");
    Edit edit{undo["label"].toString().toStdString(), {}};
    edit.metadata = {undo["taskId"].toString().toStdString(),
                     undo["request"].toString().toStdString(), undo["assistant"].toBool()};
    appendSceneMetadataChanges(edit, before, after);
    auto prune = [](auto &changes) {
        std::erase_if(changes, [](const auto &change) {
            return change.before && change.after && equal(*change.before, *change.after);
        });
    };
    prune(edit.definitions);
    prune(edit.instances);
    prune(edit.tags);
    prune(edit.materials);
    prune(edit.assets);
    std::set<Id> ids;
    for (const auto &[id, body] : before.bodies())
        ids.insert(id);
    for (const auto &[id, body] : after.bodies())
        ids.insert(id);
    for (const auto id : ids) {
        const auto a = before.bodies().contains(id) ? before.bodies().at(id) : nullptr;
        const auto b = after.bodies().contains(id) ? after.bodies().at(id) : nullptr;
        if (!a || !b || *a != *b)
            edit.changes.push_back({id, a, b});
    }
    edit.nextIdFloor = after.nextId();
    // A recovered checkpoint is not an explicit save, including its undo baseline.
    before.markRecovered();
    before.apply(std::move(edit), before.revision());
    if (encodeContainer(before) != store.latestAfter())
        fail("CORRUPT_OUTCOMES", "Recovered delta does not reproduce the committed checkpoint");
    return before;
}
QJsonObject undoMetadata(const Document &candidate) {
    const auto history = candidate.history(candidate.history().position - 1, 1);
    const auto &entry = history.entries.front();
    return {{"label", QString::fromStdString(entry.label)},
            {"taskId", QString::fromStdString(entry.metadata.taskId)},
            {"request", QString::fromStdString(entry.metadata.request)},
            {"assistant", entry.metadata.assistant},
            {"commitRevision", QString::number(candidate.revision())}};
}
QJsonObject mappings(const ChangeReport &report) {
    size_t charge = 0;
    auto count = [&](const EntityChanges &values) {
        charge += 64 * (values.created.size() + values.deleted.size() + values.modified.size() + 1);
        for (const auto &[id, targets] : values.descendants)
            charge += 64 * (targets.size() + 1);
        if (charge > 256 * 1024)
            fail("LIMIT_EXCEEDED", "Commit mapping result exceeds its budget; split the task");
    };
    for (const auto &[body, changes] : report) {
        charge += 256;
        count(changes.vertices);
        count(changes.edges);
        count(changes.faces);
        count(changes.curves);
        count(changes.guides);
    }
    auto ids = [](const auto &values) {
        QJsonArray rows;
        for (const auto id : values)
            rows.append(QString::number(id));
        return rows;
    };
    auto entities = [&](const EntityChanges &values) {
        QJsonObject descendants;
        for (const auto &[id, targets] : values.descendants)
            descendants[QString::number(id)] = ids(targets);
        return QJsonObject{{"created", ids(values.created)},
                           {"deleted", ids(values.deleted)},
                           {"modified", ids(values.modified)},
                           {"descendants", descendants}};
    };
    QJsonObject result;
    for (const auto &[body, changes] : report)
        result[QString::number(body)] = QJsonObject{{"vertices", entities(changes.vertices)},
                                                    {"edges", entities(changes.edges)},
                                                    {"faces", entities(changes.faces)},
                                                    {"curves", entities(changes.curves)},
                                                    {"guides", entities(changes.guides)}};
    return result;
}
} // namespace
class TransactionCoordinator::Operation {
  public:
    explicit Operation(TransactionCoordinator &actor, bool allowUnknown = false) : actor_(actor) {
        actor_.owner();
        if (actor_.active_)
            fail("REENTRANT_TRANSACTION", "Document operations cannot reenter the actor");
        if (!allowUnknown)
            actor_.ready();
        actor_.active_ = true;
    }
    ~Operation() { actor_.active_ = false; }

  private:
    TransactionCoordinator &actor_;
};
TransactionCoordinator::TransactionCoordinator(Document document, const QString &root)
    : TransactionCoordinator(std::move(document), root, Options{}) {}
TransactionCoordinator::TransactionCoordinator(Document document, const QString &root,
                                               Options options)
    : document_(std::move(document)), staging_(options.staging, std::move(options.monotonic)) {
    store_ = std::make_unique<OutcomeStore>(root, QString::fromStdString(document_.identity()),
                                            options.outcomes, std::move(options.wallClock),
                                            std::move(options.fault));
    if (options.mode == OpenMode::RecoverLatest)
        document_ = recovered(*store_);
    else if (!store_->latestAfter().isEmpty()) {
        const auto last = decodeContainer(store_->latestAfter());
        if (document_.revision() < last.revision() ||
            (document_.revision() == last.revision() &&
             encodeContainer(document_) != store_->latestAfter()))
            fail("RECONCILIATION_REQUIRED", "Input model predates or diverges from the last "
                                            "durable transaction; explicitly recover it");
    }
    abortOrphans();
}
void TransactionCoordinator::owner() const {
    if (std::this_thread::get_id() != owner_)
        fail("WRONG_THREAD", "Document actor must run on its owning thread");
}
void TransactionCoordinator::ready() const {
    if (publication_ || store_->uncertain())
        fail("OUTCOME_UNKNOWN", "Reconcile the uncertain transaction before dependent operations");
}
const Document &TransactionCoordinator::document() const {
    owner();
    return document_;
}
bool TransactionCoordinator::uncertain() const {
    owner();
    return publication_.has_value() || store_->uncertain();
}
size_t TransactionCoordinator::retainedStagingBytes() const {
    owner();
    return staging_.retainedBytes();
}
void TransactionCoordinator::edit(const std::function<void(Document &)> &operation) {
    Operation guard(*this);
    auto candidate = document_;
    const auto base = document_.saveStamp();
    const auto revision = document_.revision();
    operation(candidate);
    if (!candidate.owns(base) || candidate.identity() != document_.identity() ||
        candidate.revision() < revision)
        fail("INVALID_EDIT",
             "Manual operation must preserve document identity/session and revision ordering");
    static_assert(std::is_nothrow_move_assignable_v<Document>);
    document_ = std::move(candidate);
}
void TransactionCoordinator::release(const QString &id) {
    const auto i = bindings_.find(id);
    if (i == bindings_.end())
        return;
    staging_.release(i->second.stageId);
    bindings_.erase(i);
}
void TransactionCoordinator::abortOrphans() {
    for (const auto &record : store_->pendingRequests()) {
        const auto id = record["requestId"].toString();
        if (!bindings_.contains(id))
            store_->abort(id, record["payloadHash"].toString(), "STAGING_UNAVAILABLE");
    }
}
QJsonObject TransactionCoordinator::prepare(const QJsonObject &batch, int ttl) {
    Operation guard(*this);
    // Retire stale/expired bindings before staging admission; durable terminal
    // outcomes remain available independently of the volatile preview storage.
    const auto previous = bindings_;
    for (const auto &[id, binding] : previous)
        checkedStatus(id, binding.hash);
    abortOrphans();
    auto preview = staging_.prepare(document_, batch, ttl);
    const auto stageId = preview["stageId"].toString();
    const auto hash = QString::fromLatin1(
        QCryptographicHash::hash(QJsonDocument(batch).toJson(QJsonDocument::Compact),
                                 QCryptographicHash::Sha256)
            .toHex());
    try {
        const auto accepted = store_->begin(document_, hash, ttl);
        const auto id = accepted["requestId"].toString();
        bindings_.emplace(id, Binding{stageId, hash});
        preview["requestId"] = id;
        preview["payloadHash"] = hash;
        preview["outcome"] = "pending";
        return preview;
    } catch (...) {
        staging_.release(stageId);
        throw;
    }
}
QJsonObject TransactionCoordinator::checkedStatus(const QString &id, const QString &hash) {
    auto outcome = store_->lookup(id, hash);
    if (outcome["status"] != "pending") {
        release(id);
        return outcome;
    }
    const auto i = bindings_.find(id);
    if (i == bindings_.end())
        return store_->abort(id, hash, "STAGING_UNAVAILABLE");
    try {
        staging_.proposal(document_, i->second.stageId);
    } catch (const InspectionError &e) {
        if (e.code() != "STALE_PROPOSAL" && e.code() != "STAGE_UNAVAILABLE")
            throw;
        outcome =
            store_->abort(id, hash, e.code() == "STALE_PROPOSAL" ? "STALE_REVISION" : "EXPIRED");
        release(id);
    }
    return outcome;
}
TransactionCoordinator::Binding &TransactionCoordinator::binding(const QString &id,
                                                                 const QString &hash) {
    const auto outcome = checkedStatus(id, hash);
    if (outcome["status"] != "pending")
        fail("TRANSACTION_NOT_PENDING", "Inspect the terminal transaction outcome");
    return bindings_.at(id);
}
QJsonObject TransactionCoordinator::preview(const QString &id, const QString &hash) {
    Operation guard(*this);
    const auto &b = binding(id, hash);
    return staging_.describe(document_, b.stageId);
}
QJsonObject TransactionCoordinator::inspect(const QString &id, const QString &hash,
                                            const QJsonObject &query) {
    Operation guard(*this);
    const auto &b = binding(id, hash);
    return staging_.inspect(document_, b.stageId, query);
}
QJsonObject TransactionCoordinator::diff(const QString &id, const QString &hash, size_t offset,
                                         size_t limit) {
    Operation guard(*this);
    const auto &b = binding(id, hash);
    return staging_.changes(document_, b.stageId, offset, limit);
}
void TransactionCoordinator::publish() {
    if (!publication_ || !document_.isCurrentSnapshot(publication_->base) ||
        document_.revision() != publication_->revision)
        fail("OUTCOME_UNKNOWN", "Live model no longer matches the pending publication baseline");
    static_assert(std::is_nothrow_move_assignable_v<Document>);
    document_ = std::move(*publication_->candidate);
    release(publication_->requestId);
    publication_.reset();
}
QJsonObject TransactionCoordinator::commit(const QString &id, const QString &hash) {
    Operation guard(*this);
    auto outcome = checkedStatus(id, hash);
    if (outcome["status"] != "pending")
        return outcome;
    const auto stageId = bindings_.at(id).stageId;
    const auto proposal = staging_.proposal(document_, stageId);
    auto candidate = std::make_unique<Document>(document_);
    const auto report = candidate->applyPrepared(*proposal);
    auto result = staging_.describe(document_, stageId)["createdIds"].toObject();
    result["transactionApiVersion"] = 1;
    result["revision"] = QString::number(candidate->revision());
    result["undo"] = undoMetadata(*candidate);
    result["mappings"] = mappings(report);
    QJsonArray changes;
    size_t offset = 0;
    do {
        const auto page = staging_.changes(document_, stageId, offset, 100);
        for (const auto &row : page["changes"].toArray())
            changes.append(row);
        if (QJsonDocument(QJsonObject{{"changes", changes}}).toJson(QJsonDocument::Compact).size() >
            256 * 1024)
            fail("LIMIT_EXCEEDED", "Commit change result exceeds 256 KiB; split the task");
        if (page["nextOffset"].isNull())
            break;
        offset = size_t(page["nextOffset"].toDouble());
    } while (true);
    result["changes"] = changes;
    if (QJsonDocument(result).toJson(QJsonDocument::Compact).size() > 256 * 1024)
        fail("LIMIT_EXCEEDED", "Commit result exceeds 256 KiB; split the task");
    // Final expiry check immediately before accepting durable commit work.
    staging_.proposal(document_, stageId);
    publication_ =
        Publication{id, hash, document_.saveStamp(), document_.revision(), std::move(candidate)};
    try {
        outcome = store_->commit(id, hash, document_, *publication_->candidate, result);
        if (outcome["status"] == "committed")
            publish();
        else {
            publication_.reset();
            release(id);
        }
        return outcome;
    } catch (...) {
        if (!store_->uncertain())
            publication_.reset();
        throw;
    }
}
QJsonObject TransactionCoordinator::cancel(const QString &id, const QString &hash) {
    Operation guard(*this);
    auto result = store_->abort(id, hash, "CANCELLED");
    if (result["status"] != "pending")
        release(id);
    return result;
}
QJsonObject TransactionCoordinator::status(const QString &id, const QString &hash) {
    Operation guard(*this, true);
    if (publication_ || store_->uncertain()) {
        if (publication_ && publication_->requestId == id && publication_->hash != hash)
            fail("REQUEST_CONFLICT", "Request identity is bound to another payload");
        return {{"apiVersion", 1},
                {"documentId", QString::fromStdString(document_.identity())},
                {"requestId", id},
                {"status", "unknown"},
                {"reconciliationRequired", true}};
    }
    return checkedStatus(id, hash);
}
QJsonObject TransactionCoordinator::reconcile() {
    Operation guard(*this, true);
    store_->reconcile();
    QJsonObject result{{"status", "ready"}};
    if (publication_) {
        result = store_->lookup(publication_->requestId, publication_->hash);
        if (result["status"] == "committed") {
            if (store_->latestOutcome()["requestId"] != publication_->requestId ||
                store_->latestAfter() != encodeContainer(*publication_->candidate))
                fail("OUTCOME_UNKNOWN", "Durable candidate differs from retained publication");
            publish();
        } else if (result["status"] == "pending" || result["status"] == "aborted") {
            result =
                store_->abort(publication_->requestId, publication_->hash, "VERIFIED_NO_COMMIT");
            release(publication_->requestId);
            publication_.reset();
        } else
            fail("OUTCOME_UNKNOWN", "Pending publication has no reconcilable durable identity");
    }
    abortOrphans();
    return result;
}
} // namespace sketchy
