#include "automation/transactions.hpp"
#include "automation/inspection_validation.hpp"
#include <QCryptographicHash>
#include <QJsonDocument>
#include <QUuid>
#include <algorithm>
namespace sketchy {
namespace {
[[noreturn]] void fail(const char *code, const char *message) {
    throw InspectionError(code, message);
}
qsizetype bytes(const QJsonObject &object) {
    return QJsonDocument(object).toJson(QJsonDocument::Compact).size();
}
QString digest(const QJsonObject &object) {
    return QString::fromLatin1(
        QCryptographicHash::hash(QJsonDocument(object).toJson(QJsonDocument::Compact),
                                 QCryptographicHash::Sha256)
            .toHex());
}
} // namespace
TransactionDispatcher::TransactionDispatcher(TransactionCoordinator &actor)
    : TransactionDispatcher(actor, Limits{}) {}
TransactionDispatcher::TransactionDispatcher(TransactionCoordinator &actor, Limits limits,
                                             StagingSession::Now now)
    : actor_(actor), limits_(limits), now_(std::move(now)) {
    if (!limits.drafts || limits.drafts > 4 || !limits.retainedBytes ||
        limits.retainedBytes > 128 * 1024 * 1024 || !now_)
        fail("INVALID_REQUEST", "Invalid transaction draft limits");
}
size_t TransactionDispatcher::charge(const Draft &draft) const {
    size_t total = sizeof(Draft) + 2048 + (draft.stage ? draft.stage->retainedBytes() : 0);
    total +=
        size_t(bytes(
            {{"commands", draft.commands}, {"history", draft.history}, {"sealed", draft.sealed}})) *
        2;
    for (const auto &[id, applied] : draft.applied)
        total += 512 + size_t(bytes(applied.reply)) * 2;
    return total;
}
size_t TransactionDispatcher::retainedBytes() const {
    size_t total = actor_.retainedStagingBytes();
    for (const auto &[id, draft] : drafts_)
        total += charge(draft);
    return total;
}
void TransactionDispatcher::retire(const QString &id) {
    const auto i = drafts_.find(id);
    if (i == drafts_.end())
        return;
    if (!i->second.sealed.isEmpty()) {
        const auto requestId = i->second.sealed["requestId"].toString();
        const auto hash = i->second.sealed["payloadHash"].toString();
        // Preserve the authoritative stale/expired reason before cleanup.
        if (actor_.status(requestId, hash)["status"] == "pending")
            actor_.cancel(requestId, hash);
    }
    drafts_.erase(i);
}
void TransactionDispatcher::prune(const QString &skip) {
    const auto &doc = actor_.document();
    const auto now = now_();
    std::vector<QString> retired;
    for (const auto &[id, draft] : drafts_)
        if (id != skip && (now >= draft.expires || doc.revision() != draft.revision ||
                           !doc.isCurrentSnapshot(draft.base)))
            retired.push_back(id);
    for (const auto &id : retired)
        retire(id);
}
TransactionDispatcher::Draft &TransactionDispatcher::get(const QString &id) {
    const auto i = drafts_.find(id);
    if (i == drafts_.end())
        fail("TRANSACTION_UNAVAILABLE", "Transaction draft is unknown or has been released");
    const auto &doc = actor_.document();
    if (doc.revision() != i->second.revision || !doc.isCurrentSnapshot(i->second.base)) {
        retire(id);
        fail("STALE_REVISION", "Document changed after the draft began; start a new transaction");
    }
    if (now_() >= i->second.expires) {
        retire(id);
        fail("TRANSACTION_EXPIRED", "Transaction draft lifetime expired");
    }
    return i->second;
}
int TransactionDispatcher::remainingSeconds(const Draft &draft) const {
    const auto seconds =
        std::chrono::duration_cast<std::chrono::seconds>(draft.expires - now_()).count();
    if (seconds < 1)
        fail("TRANSACTION_EXPIRED", "Too little draft lifetime remains to seal a commit request");
    return int(std::min<int64_t>(300, seconds));
}
QJsonObject TransactionDispatcher::describe(const QString &id, const Draft &draft) const {
    QJsonObject result;
    if (!draft.sealed.isEmpty())
        result = actor_.preview(draft.sealed["requestId"].toString(),
                                draft.sealed["payloadHash"].toString());
    else if (draft.stage)
        result = draft.stage->describe(actor_.document(), draft.stageId);
    result.remove("stageId");
    result["transactionId"] = id;
    result["stageVersion"] = draft.version;
    result["currentVersion"] = draft.version;
    result["baseRevision"] = QString::number(draft.revision);
    result["status"] = draft.sealed.isEmpty() ? "draft" : "sealed";
    result["commandCount"] = draft.commands.size();
    result["expiresInMs"] = double(std::max<int64_t>(
        0, std::chrono::duration_cast<std::chrono::milliseconds>(draft.expires - now_()).count()));
    if (!draft.sealed.isEmpty()) {
        result["requestId"] = draft.sealed["requestId"];
        result["payloadHash"] = draft.sealed["payloadHash"];
    }
    return result;
}
QJsonObject TransactionDispatcher::execute(const QJsonObject &request) {
    // Validate owner before touching dispatcher state, even for malformed requests.
    (void)actor_.document();
    if (active_)
        fail("REENTRANT_TRANSACTION", "Transaction dispatch cannot reenter itself");
    struct Guard {
        bool &active;
        ~Guard() { active = false; }
    } guard{active_};
    active_ = true;
    if (bytes(request) > transactionRequestBytes)
        fail("LIMIT_EXCEEDED", "Transaction request exceeds 64 KiB");
    if (request["apiVersion"] != 1)
        fail("UNSUPPORTED_VERSION", "Transactions require API version 1");
    QJsonObject spec;
    for (const auto &entry : transactionCatalog())
        if (entry.toObject()["name"] == request["operation"]) {
            spec = entry.toObject();
            break;
        }
    if (spec.isEmpty())
        fail("UNSUPPORTED_CAPABILITY", "Unknown transaction operation");
    inspection_detail::validateParameters(request, spec["parameters"].toObject());
    if (request["documentId"].toString().toStdString() != actor_.document().identity())
        fail("WRONG_DOCUMENT", "Transaction request targets another document");
    const auto operation = request["operation"].toString();
    if (actor_.uncertain() && operation != "transaction.status" &&
        operation != "transaction.reconcile")
        fail("OUTCOME_UNKNOWN", "Reconcile uncertain publication before dependent operations");
    QJsonObject result;
    try {
        if (!actor_.uncertain())
            prune(request["transactionId"].toString());
        result = run(request);
    } catch (const OutcomeStoreError &error) {
        throw InspectionError(error.code(), error.what());
    } catch (const InspectionError &) {
        throw;
    } catch (const std::runtime_error &error) {
        throw InspectionError("INVALID_OPERATION", error.what());
    } catch (const std::logic_error &error) {
        throw InspectionError("INVALID_OPERATION", error.what());
    }
    result["apiVersion"] = 1;
    result["documentId"] = request["documentId"];
    result["operation"] = operation;
    if (bytes(result) > transactionResponseBytes)
        fail("LIMIT_EXCEEDED", "Transaction response exceeds its bounded envelope");
    return result;
}
QJsonObject TransactionDispatcher::run(const QJsonObject &request) {
    const auto operation = request["operation"].toString();
    const auto &doc = actor_.document();
    if (operation == "transaction.begin") {
        bool ok = false;
        const auto revision = request["expectedRevision"].toString().toULongLong(&ok);
        if (!ok)
            fail("INVALID_REQUEST", "Expected canonical uint64 revision");
        if (revision != doc.revision())
            fail("STALE_REVISION", "Begin requires the current document revision");
        if (drafts_.size() >= limits_.drafts)
            fail("STAGE_LIMIT", "Too many active transaction drafts");
        QString id;
        do {
            id = QUuid::createUuid().toString(QUuid::WithoutBraces);
        } while (drafts_.contains(id));
        Draft draft;
        draft.base = doc.saveStamp();
        draft.revision = doc.revision();
        draft.expires = now_() + std::chrono::seconds(request["ttlSeconds"].toInt(60));
        draft.history = request["history"].toObject();
        if (!draft.history.contains("label"))
            draft.history["label"] = "Modeling task";
        if (!draft.history.contains("taskId"))
            draft.history["taskId"] = id;
        if (draft.history.value("assistant") == true &&
            draft.history.value("request").toString().isEmpty())
            fail("INVALID_REQUEST", "Assistant task metadata requires the original request");
        if (retainedBytes() + charge(draft) > limits_.retainedBytes)
            fail("STAGE_LIMIT", "No capacity for another draft");
        auto reply = describe(id, draft);
        drafts_.emplace(id, std::move(draft));
        return reply;
    }
    if (operation == "transaction.commit" || operation == "transaction.cancel" ||
        operation == "transaction.status") {
        const auto id = request["requestId"].toString(), hash = request["payloadHash"].toString();
        auto outcome = operation == "transaction.commit"   ? actor_.commit(id, hash)
                       : operation == "transaction.cancel" ? actor_.cancel(id, hash)
                                                           : actor_.status(id, hash);
        if (outcome["status"] == "committed" || outcome["status"] == "aborted")
            std::erase_if(drafts_,
                          [&](const auto &pair) { return pair.second.sealed["requestId"] == id; });
        return outcome;
    }
    if (operation == "transaction.reconcile")
        return actor_.reconcile();
    const auto id = request["transactionId"].toString();
    auto &draft = get(id);
    if (operation == "transaction.describe")
        return describe(id, draft);
    if (operation == "transaction.abort") {
        QJsonObject result{{"transactionId", id}, {"status", "aborted"}};
        if (!draft.sealed.isEmpty())
            result = actor_.cancel(draft.sealed["requestId"].toString(),
                                   draft.sealed["payloadHash"].toString());
        drafts_.erase(id);
        return result;
    }
    if (operation == "transaction.apply") {
        const auto operationId = request["operationId"].toString();
        const auto hash = digest(
            {{"expectedVersion", request["expectedVersion"]}, {"commands", request["commands"]}});
        if (draft.applied.contains(operationId)) {
            const auto &previous = draft.applied.at(operationId);
            if (previous.hash != hash)
                fail("REQUEST_CONFLICT", "Apply identity is bound to different commands/version");
            auto reply = previous.reply;
            reply["replayed"] = true;
            reply["currentVersion"] = draft.version;
            reply["expiresInMs"] = describe(id, draft).value("expiresInMs");
            return reply;
        }
        if (!draft.sealed.isEmpty())
            fail("SEALED_TRANSACTION", "Start a new draft to change a sealed payload");
        if (request["expectedVersion"].toInt() != draft.version)
            fail("STALE_STAGE_VERSION", "Inspect the latest draft version before appending");
        auto commands = draft.commands;
        for (const auto &command : request["commands"].toArray())
            commands.append(command);
        if (commands.size() > 100)
            fail("LIMIT_EXCEEDED", "A transaction accepts at most 100 cumulative commands");
        const QJsonObject batch{{"apiVersion", 1},
                                {"documentId", request["documentId"]},
                                {"expectedRevision", QString::number(draft.revision)},
                                {"commands", commands},
                                {"history", draft.history}};
        if (bytes(batch) > transactionRequestBytes)
            fail("LIMIT_EXCEEDED", "Cumulative transaction batch exceeds 64 KiB");
        const auto used = retainedBytes() - charge(draft);
        if (used >= limits_.retainedBytes)
            fail("STAGE_LIMIT", "No capacity for a replacement proposal");
        auto candidate = std::make_unique<StagingSession>(
            StagingSession::Limits{1, limits_.retainedBytes - used}, now_);
        const auto milliseconds =
            std::chrono::duration_cast<std::chrono::milliseconds>(draft.expires - now_()).count();
        const auto ttl = std::clamp<int64_t>((milliseconds + 999) / 1000, 1, 300);
        auto reply = candidate->prepare(doc, batch, int(ttl));
        Draft next;
        next.base = draft.base;
        next.revision = draft.revision;
        next.version = draft.version + 1;
        next.expires = draft.expires;
        next.commands = commands;
        next.history = draft.history;
        next.applied = draft.applied;
        next.stageId = reply["stageId"].toString();
        next.stage = std::move(candidate);
        reply = describe(id, next);
        reply["operationId"] = operationId;
        reply["replayed"] = false;
        next.applied.emplace(operationId, Applied{hash, reply});
        if (used + charge(next) > limits_.retainedBytes)
            fail("STAGE_LIMIT", "Proposal and retry receipts exceed retained capacity");
        if (now_() >= draft.expires)
            fail("TRANSACTION_EXPIRED", "Draft expired during command preparation");
        if (doc.revision() != draft.revision || !doc.isCurrentSnapshot(draft.base))
            fail("STALE_REVISION", "Document changed during preparation");
        draft = std::move(next);
        return reply;
    }
    if (operation == "transaction.preview") {
        if (request["expectedVersion"].toInt() != draft.version)
            fail("STALE_STAGE_VERSION", "Preview must target the current draft version");
        if (!draft.sealed.isEmpty())
            return describe(id, draft);
        if (!draft.stage)
            fail("EMPTY_TRANSACTION", "Apply at least one command before preview");
        const QJsonObject batch{{"apiVersion", 1},
                                {"documentId", request["documentId"]},
                                {"expectedRevision", QString::number(draft.revision)},
                                {"commands", draft.commands},
                                {"history", draft.history}};
        auto prepared = actor_.prepare(batch, remainingSeconds(draft));
        const auto requestId = prepared["requestId"].toString(),
                   hash = prepared["payloadHash"].toString();
        const auto oldCharge = draft.stage->retainedBytes();
        const auto sealedCharge = size_t(bytes(prepared)) * 2;
        if (now_() >= draft.expires) {
            actor_.cancel(requestId, hash);
            fail("TRANSACTION_EXPIRED", "Draft expired while sealing its commit request");
        }
        if (retainedBytes() - oldCharge + sealedCharge > limits_.retainedBytes) {
            actor_.cancel(requestId, hash);
            fail("STAGE_LIMIT", "Sealed proposal exceeds retained capacity");
        }
        draft.sealed = std::move(prepared);
        draft.stage.reset();
        draft.stageId.clear();
        return describe(id, draft);
    }
    if (!draft.stage && draft.sealed.isEmpty())
        fail("EMPTY_TRANSACTION", "Apply commands before inspecting the proposed model");
    QJsonObject result;
    if (!draft.sealed.isEmpty()) {
        const auto r = draft.sealed["requestId"].toString(),
                   h = draft.sealed["payloadHash"].toString();
        result = operation == "transaction.inspect"
                     ? actor_.inspect(r, h, request["request"].toObject())
                     : actor_.diff(r, h, size_t(request["offset"].toInt(0)),
                                   size_t(request["limit"].toInt(50)));
    } else
        result = operation == "transaction.inspect"
                     ? draft.stage->inspect(doc, draft.stageId, request["request"].toObject())
                     : draft.stage->changes(doc, draft.stageId, size_t(request["offset"].toInt(0)),
                                            size_t(request["limit"].toInt(50)));
    if (now_() >= draft.expires)
        fail("TRANSACTION_EXPIRED", "Draft expired during inspection");
    result.remove("stageId");
    result["transactionId"] = id;
    result["stageVersion"] = draft.version;
    return result;
}
} // namespace sketchy
