#include "automation/inspection_session.hpp"
#include "automation/inspection_validation.hpp"
#include <QJsonDocument>
#include <QUuid>
namespace sketchy {
namespace {
[[noreturn]] void fail(const char *code, const char *message) {
    throw InspectionError(code, message);
}
QJsonObject tokenSchema() {
    return {{"type", "string"},
            {"maxLength", 36},
            {"description", "Opaque snapshot identifier returned by snapshot.begin"}};
}
QJsonObject spec(QString name, QJsonObject properties, QJsonArray required) {
    properties["apiVersion"] = QJsonObject{{"const", 1}};
    properties["documentId"] = QJsonObject{{"type", "string"}, {"maxLength", 128}};
    properties["expectedRevision"] =
        QJsonObject{{"type", "string"}, {"maxLength", 20}, {"pattern", "^(0|[1-9][0-9]*)$"}};
    properties["query"] = QJsonObject{{"const", name}};
    for (const auto *key : {"apiVersion", "documentId", "expectedRevision", "query"})
        required.append(key);
    return {{"name", name},
            {"paged", false},
            {"requiresRevision", true},
            {"sideEffects", "ephemeral snapshot retention only"},
            {"parameters", QJsonObject{{"$schema", "https://json-schema.org/draft/2020-12/schema"},
                                       {"type", "object"},
                                       {"properties", properties},
                                       {"required", required},
                                       {"additionalProperties", false}}}};
}
QJsonArray catalog() {
    auto schemas = inspectionCatalog();
    for (qsizetype i = 0; i < schemas.size(); ++i) {
        auto entry = schemas[i].toObject();
        auto parameters = entry["parameters"].toObject();
        auto properties = parameters["properties"].toObject();
        properties["snapshotId"] = tokenSchema();
        parameters["properties"] = properties;
        entry["parameters"] = parameters;
        schemas[i] = entry;
    }
    schemas.append(spec(
        "snapshot.begin",
        {{"ttlSeconds", QJsonObject{{"type", "integer"}, {"minimum", 1}, {"maximum", 300}}}}, {}));
    schemas.append(spec("snapshot.release", {{"snapshotId", tokenSchema()}}, {"snapshotId"}));
    return schemas;
}
void validate(const QJsonObject &request) {
    if (QJsonDocument(request).toJson(QJsonDocument::Compact).size() > inspectionRequestBytes)
        fail("LIMIT_EXCEEDED", "Inspection request exceeds 16 KiB");
    if (request["apiVersion"] != 1)
        fail("UNSUPPORTED_VERSION", "Inspection session requires apiVersion 1");
    static const auto schemas = catalog();
    for (const auto &entry : schemas)
        if (entry.toObject()["name"] == request["query"]) {
            inspection_detail::validateParameters(request,
                                                  entry.toObject()["parameters"].toObject());
            return;
        }
    fail("UNSUPPORTED_CAPABILITY", "Unknown inspection session query");
}
uint64_t revision(const QJsonValue &value) {
    bool valid{};
    const auto n = value.toString().toULongLong(&valid);
    if (!valid || QString::number(n) != value.toString())
        fail("INVALID_REQUEST", "Expected canonical uint64 revision");
    return n;
}
bool sameEditor(const Selection &a, const Selection &b) {
    return a.entities() == b.entities() && a.hiddenEntities() == b.hiddenEntities() &&
           a.lockedBodies() == b.lockedBodies() && a.context() == b.context() &&
           a.showingHidden() == b.showingHidden();
}
} // namespace
QJsonObject inspectionSessionCapabilities() {
    auto result = inspectionCapabilities();
    result["queries"] = catalog();
    result["transport"] =
        "Shared session API exposed by the headless CLI; MCP adapter remains R043";
    result["snapshot"] = QJsonObject{
        {"maximumCount", 4},
        {"retainedByteBudget", 32 * 1024 * 1024},
        {"defaultTtlSeconds", 60},
        {"maximumTtlSeconds", 300},
        {"retention", "Conservative scene, resource, allocator and editor-state charge; shared "
                      "immutable records, no history"},
        {"lifetime",
         "Monotonic deadline; process/session lifetime; invalidated on document replacement"}};
    return result;
}
InspectionSession::InspectionSession() : InspectionSession(Limits{}) {}
InspectionSession::InspectionSession(Limits limits, Now now)
    : limits_(limits), now_(std::move(now)) {
    if (!limits_.snapshots || limits_.snapshots > 4 || !limits_.retainedBytes ||
        limits_.retainedBytes > 32 * 1024 * 1024 || !now_)
        fail("INVALID_REQUEST", "Inspection session limits exceed supported bounds");
}
void InspectionSession::clear() {
    snapshots_.clear();
    bytes_ = 0;
}
void InspectionSession::prune(const Document &live) {
    const auto now = now_();
    for (auto it = snapshots_.begin(); it != snapshots_.end();) {
        if (now >= it->second.expires || !live.owns(it->second.source)) {
            bytes_ -= it->second.retainedBytes;
            it = snapshots_.erase(it);
        } else
            ++it;
    }
}
QJsonObject InspectionSession::execute(const Document &live, const QJsonObject &request,
                                       const Selection *editor) {
    validate(request);
    if (request["documentId"] != QString::fromStdString(live.identity()))
        fail("WRONG_DOCUMENT", "Inspection session document identity does not match");
    const auto expected = revision(request["expectedRevision"]);
    prune(live);
    const auto query = request["query"].toString();
    if (query == "snapshot.begin") {
        if (expected != live.revision())
            fail("STALE_REVISION", "Cannot capture a changed document");
        if (snapshots_.size() >= limits_.snapshots)
            fail("SNAPSHOT_LIMIT", "Release a snapshot before capturing another");
        // Preflight uses record sizes without constructing JSON, copying history,
        // duplicating assets or allocating the captured editor sets.
        Snapshot snapshot;
        snapshot.retainedBytes = live.readSnapshotBytes();
        if (editor) {
            if (!editor->belongsTo(live))
                fail("STALE_SELECTION", "Editor belongs to another document session");
            snapshot.retainedBytes +=
                128 * (editor->entities().size() + editor->hiddenEntities().size() +
                       editor->lockedBodies().size() + live.bodies().size()) +
                sizeof(Selection);
        }
        if (snapshot.retainedBytes > limits_.retainedBytes - bytes_)
            fail("SNAPSHOT_LIMIT", "Snapshot exceeds the remaining retained-byte budget");
        if (editor) {
            snapshot.editor = *editor;
            snapshot.editor->sync(live);
            if (!sameEditor(*snapshot.editor, *editor))
                fail("STALE_SELECTION", "Refresh stale editor state before capture");
        }
        snapshot.document = std::make_unique<const Document>(live.readSnapshot());
        snapshot.source = live.saveStamp();
        snapshot.revision = live.revision();
        snapshot.capturedAt = QDateTime::currentDateTimeUtc();
        const auto ttl = request["ttlSeconds"].toInt(60);
        snapshot.expires = now_() + std::chrono::seconds(ttl);
        const auto id = QUuid::createUuid().toString(QUuid::WithoutBraces);
        QJsonObject result{{"apiVersion", 1},
                           {"documentId", request["documentId"]},
                           {"revision", request["expectedRevision"]},
                           {"query", query},
                           {"snapshotId", id},
                           {"capturedAt", snapshot.capturedAt.toString(Qt::ISODateWithMs)},
                           {"expiresInMs", ttl * 1000},
                           {"retainedBytes", qint64(snapshot.retainedBytes)}};
        const auto cost = snapshot.retainedBytes;
        snapshots_.emplace(id, std::move(snapshot));
        bytes_ += cost;
        return result;
    }
    if (!request.contains("snapshotId"))
        return inspectDocument(live, request, editor);
    const auto id = request["snapshotId"].toString();
    const auto found = snapshots_.find(id);
    if (found == snapshots_.end())
        fail("SNAPSHOT_UNAVAILABLE",
             "Snapshot is unknown, expired, released or from a replaced document");
    const auto &snapshot = found->second;
    if (expected != snapshot.revision)
        fail("STALE_REVISION", "Expected revision does not match the captured snapshot");
    if (query == "snapshot.release") {
        bytes_ -= snapshot.retainedBytes;
        snapshots_.erase(found);
        return {{"apiVersion", 1},
                {"documentId", request["documentId"]},
                {"revision", request["expectedRevision"]},
                {"query", query},
                {"snapshotId", id},
                {"released", true}};
    }
    auto inspection = request;
    inspection.remove("snapshotId");
    auto result = inspectDocument(*snapshot.document, inspection,
                                  snapshot.editor ? &*snapshot.editor : nullptr);
    // CPU work may take time; never publish a result that completed after expiry.
    const auto remaining =
        std::chrono::duration_cast<std::chrono::milliseconds>(snapshot.expires - now_()).count();
    if (remaining <= 0) {
        bytes_ -= snapshot.retainedBytes;
        snapshots_.erase(found);
        fail("SNAPSHOT_UNAVAILABLE", "Snapshot expired during inspection");
    }
    result["snapshotId"] = id;
    result["capturedAt"] = snapshot.capturedAt.toString(Qt::ISODateWithMs);
    result["expiresInMs"] = qint64(remaining);
    result["liveRevision"] = QString::number(live.revision());
    result["differsFromLiveRevision"] = live.revision() != snapshot.revision;
    if (QJsonDocument(result).toJson(QJsonDocument::Compact).size() > inspectionResponseBytes)
        fail("LIMIT_EXCEEDED", "Snapshot inspection exceeds response byte budget");
    return result;
}
} // namespace sketchy
