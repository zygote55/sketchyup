#include "automation/staging.hpp"
#include "automation/commands.hpp"
#include <QJsonDocument>
#include <QUuid>
namespace sketchy {
namespace {
constexpr qsizetype requestLimit = 64 * 1024;
constexpr qsizetype responseLimit = 256 * 1024;
[[noreturn]] void fail(const char *code, const char *message) {
    throw InspectionError(code, message);
}
} // namespace
StagingSession::StagingSession() : StagingSession(Limits{}) {}
StagingSession::StagingSession(Limits limits, Now now) : limits_(limits), now_(std::move(now)) {
    if (!limits.proposals || limits.proposals > 4 || !limits.retainedBytes ||
        limits.retainedBytes > 128 * 1024 * 1024 || !now_)
        throw std::runtime_error("Invalid staging limits");
}
void StagingSession::prune() {
    const auto now = now_();
    for (auto i = stages_.begin(); i != stages_.end();) {
        if (now >= i->second.expires) {
            bytes_ -= i->second.bytes;
            i = stages_.erase(i);
        } else
            ++i;
    }
}
StagingSession::Stage &StagingSession::get(const Document &live, const QString &stageId) {
    prune();
    const auto i = stages_.find(stageId);
    if (i == stages_.end())
        fail("STAGE_UNAVAILABLE", "Proposal was released, expired or is unknown");
    if (!live.canApply(*i->second.prepared))
        fail("STALE_PROPOSAL", "Live document changed; prepare a new proposal");
    return i->second;
}
QJsonObject StagingSession::envelope(const Document &live, const QString &stageId,
                                     const Stage &stage) const {
    return {{"apiVersion", 1},
            {"documentId", QString::fromStdString(live.identity())},
            {"stageId", stageId},
            {"status", "staged"},
            {"baseRevision", QString::number(stage.prepared->baseRevision())},
            {"proposedRevision", QString::number(stage.prepared->snapshot().revision())},
            {"provisionalIds", true},
            {"changeCount", int(stage.changes.size())},
            {"expiresInMs",
             double(std::max<int64_t>(
                 0, std::chrono::duration_cast<std::chrono::milliseconds>(stage.expires - now_())
                        .count()))}};
}
QJsonObject StagingSession::prepare(const Document &live, const QJsonObject &batch,
                                    int ttlSeconds) {
    prune();
    if (ttlSeconds < 1 || ttlSeconds > 300)
        fail("INVALID_REQUEST", "Staging TTL must be between 1 and 300 seconds");
    if (QJsonDocument(batch).toJson(QJsonDocument::Compact).size() > requestLimit)
        fail("LIMIT_EXCEEDED", "Staging request exceeds 64 KiB");
    if (batch["apiVersion"] != 1)
        fail("UNSUPPORTED_VERSION", "Staging requires API version 1");
    if (batch["documentId"].toString().toStdString() != live.identity())
        fail("WRONG_DOCUMENT", "Batch does not target this document");
    if (batch["expectedRevision"] != QString::number(live.revision()))
        fail("STALE_REVISION", "Batch does not target the current revision");
    if (stages_.size() >= limits_.proposals ||
        live.readSnapshotBytes() > (limits_.retainedBytes - bytes_) / 3)
        fail("STAGE_LIMIT", "No capacity to prepare another private proposal");
    const auto expires = now_() + std::chrono::seconds(ttlSeconds);
    Stage stage;
    stage.expires = expires;
    stage.prepared =
        std::make_shared<const Document::PreparedEdit>(live.prepareEdit([&](Document &draft) {
            stage.result = executeBatch(draft, batch, BatchResponse::CreatedIds);
        }));
    stage.result.remove("status");
    stage.result.remove("revision");
    const auto &snapshot = stage.prepared->snapshot();
    auto diff = [&](QString kind, const auto &before, const auto &after, auto equal) {
        auto a = before.begin();
        auto b = after.begin();
        while (a != before.end() || b != after.end()) {
            if (b == after.end() || (a != before.end() && a->first < b->first)) {
                stage.changes.push_back({kind, "removed", a->first});
                ++a;
            } else if (a == before.end() || b->first < a->first) {
                stage.changes.push_back({kind, "created", b->first});
                ++b;
            } else {
                if (!equal(a->second, b->second))
                    stage.changes.push_back({kind, "updated", a->first});
                ++a;
                ++b;
            }
        }
    };
    diff("body", live.bodies(), snapshot.bodies(), std::equal_to<>{});
    diff("definition", live.definitions(), snapshot.definitions(), std::equal_to<>{});
    diff("instance", live.instances(), snapshot.instances(), std::equal_to<>{});
    diff("tag", live.tags(), snapshot.tags(), std::equal_to<>{});
    diff("material", live.materials(), snapshot.materials(), std::equal_to<>{});
    diff("asset", live.assets(), snapshot.assets(), std::equal_to<>{});
    // Hosted aggregates freeze their inner records together; fresh pointers do
    // not imply that an unrelated host or attachment changed.
    const auto sameRecord = [](const auto &a, const auto &b) { return a == b || *a == *b; };
    diff("host", live.hostedComponents().hosts, snapshot.hostedComponents().hosts, sameRecord);
    diff("attachment", live.hostedComponents().attachments, snapshot.hostedComponents().attachments,
         sameRecord);
    if (live.displayUnits() != snapshot.displayUnits())
        stage.changes.push_back({"displayUnits", "updated", 0});
    const auto resultBytes = QJsonDocument(stage.result).toJson(QJsonDocument::Compact).size();
    if (resultBytes > responseLimit - 4096)
        fail("LIMIT_EXCEEDED", "Proposed created-ID result exceeds 256 KiB; split the task");
    stage.bytes = stage.prepared->retainedBytes() + size_t(resultBytes) * 2 +
                  stage.changes.capacity() * 256 + sizeof(Stage) + 1024;
    if (stage.bytes > limits_.retainedBytes - bytes_)
        fail("STAGE_LIMIT", "Proposal exceeds the retained staging budget");
    if (now_() >= expires)
        fail("STAGE_UNAVAILABLE", "Proposal expired during preparation");
    QString id;
    do {
        id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    } while (stages_.contains(id));
    auto result = envelope(live, id, stage);
    result["createdIds"] = stage.result;
    const auto retained = stage.bytes;
    stages_.emplace(id, std::move(stage));
    bytes_ += retained;
    return result;
}
QJsonObject StagingSession::describe(const Document &live, const QString &id) {
    const auto &stage = get(live, id);
    auto result = envelope(live, id, stage);
    result["createdIds"] = stage.result;
    return result;
}
QJsonObject StagingSession::changes(const Document &live, const QString &id, size_t offset,
                                    size_t limit) {
    const auto &stage = get(live, id);
    if (!limit || limit > 100 || offset > stage.changes.size())
        fail("INVALID_REQUEST", "Change page is outside supported bounds");
    auto result = envelope(live, id, stage);
    QJsonArray rows;
    const auto end = offset + std::min(limit, stage.changes.size() - offset);
    for (auto i = offset; i < end; ++i) {
        const auto &row = stage.changes[i];
        rows.append(QJsonObject{
            {"kind", row.kind}, {"id", QString::number(row.id)}, {"action", row.action}});
    }
    result["changes"] = rows;
    result["offset"] = double(offset);
    result["nextOffset"] = end < stage.changes.size() ? QJsonValue(double(end)) : QJsonValue();
    return result;
}
QJsonObject StagingSession::inspect(const Document &live, const QString &id,
                                    const QJsonObject &request) {
    const auto prepared = proposal(live, id);
    auto result = inspectDocument(prepared->snapshot(), request);
    // A query completing after the TTL must not return an apparently current preview.
    get(live, id);
    result["stageId"] = id;
    result["status"] = "staged";
    result["provisionalIds"] = true;
    result["baseRevision"] = QString::number(prepared->baseRevision());
    if (QJsonDocument(result).toJson(QJsonDocument::Compact).size() > responseLimit)
        fail("LIMIT_EXCEEDED", "Staged inspection exceeds response budget");
    return result;
}
std::shared_ptr<const Document::PreparedEdit> StagingSession::proposal(const Document &live,
                                                                       const QString &id) {
    return get(live, id).prepared;
}
void StagingSession::release(const QString &id) {
    const auto i = stages_.find(id);
    if (i == stages_.end())
        return;
    bytes_ -= i->second.bytes;
    stages_.erase(i);
}
void StagingSession::clear() {
    stages_.clear();
    bytes_ = 0;
}
} // namespace sketchy
