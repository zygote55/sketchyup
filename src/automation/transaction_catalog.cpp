#include "automation/commands.hpp"
#include "automation/transactions.hpp"
namespace sketchy {
namespace {
QJsonObject text(int maximum, QString pattern = {}) {
    QJsonObject result{{"type", "string"}, {"minLength", 1}, {"maxLength", maximum}};
    if (!pattern.isEmpty())
        result["pattern"] = pattern;
    return result;
}
QJsonObject integer(int minimum, int maximum) {
    return {{"type", "integer"}, {"minimum", minimum}, {"maximum", maximum}};
}
QJsonObject object(QJsonObject properties, QJsonArray required = {}) {
    return {{"type", "object"},
            {"properties", properties},
            {"required", required},
            {"additionalProperties", false}};
}
} // namespace
QJsonArray transactionCatalog() {
    const auto revision = text(20, "^(0|[1-9][0-9]*)$");
    const auto draft = text(36, "^[0-9a-f]{8}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{12}$");
    const auto requestId = text(20, "^[1-9][0-9]*$");
    const auto hash = text(64, "^[0-9a-f]{64}$");
    QJsonArray commands, queries;
    for (const auto &entry : commandCatalog())
        commands.append(entry.toObject()["parameters"]);
    for (const auto &entry : inspectionCatalog())
        queries.append(entry.toObject()["parameters"]);
    QJsonArray result;
    auto add = [&](QString name, QString description, QString effects, QJsonObject properties,
                   QJsonArray required) {
        properties["apiVersion"] = QJsonObject{{"const", 1}};
        properties["documentId"] = text(128);
        properties["operation"] = QJsonObject{{"const", name}};
        for (const auto *key : {"apiVersion", "documentId", "operation"})
            required.append(key);
        auto schema = object(properties, required);
        schema["$schema"] = "https://json-schema.org/draft/2020-12/schema";
        result.append(QJsonObject{{"name", name},
                                  {"description", description},
                                  {"sideEffects", effects},
                                  {"parameters", schema}});
    };
    add("transaction.begin", "Create an empty private draft at the current revision", "staging",
        {{"expectedRevision", revision},
         {"ttlSeconds", integer(1, 300)},
         {"history", object({{"label", text(512)},
                             {"taskId", text(128)},
                             {"request", QJsonObject{{"type", "string"}, {"maxLength", 4096}}},
                             {"assistant", QJsonObject{{"type", "boolean"}}}})}},
        {"expectedRevision"});
    add("transaction.apply", "Append validated commands once to an unsealed draft", "staging",
        {{"transactionId", draft},
         {"expectedVersion", integer(0, 100)},
         {"operationId", text(128, "^[a-zA-Z0-9_-]+$")},
         {"commands", QJsonObject{{"type", "array"},
                                  {"minItems", 1},
                                  {"maxItems", 100},
                                  {"items", QJsonObject{{"oneOf", commands}}}}}},
        {"transactionId", "expectedVersion", "operationId", "commands"});
    add("transaction.describe", "Read draft version, lifetime and prepared result", "none",
        {{"transactionId", draft}}, {"transactionId"});
    add("transaction.inspect", "Run a bounded read against the private proposed model", "none",
        {{"transactionId", draft}, {"request", QJsonObject{{"oneOf", queries}}}},
        {"transactionId", "request"});
    add("transaction.diff", "Page direct changed records in the private proposed model", "none",
        {{"transactionId", draft}, {"offset", integer(0, 100000)}, {"limit", integer(1, 100)}},
        {"transactionId"});
    add("transaction.preview",
        "Seal a complete draft and issue an immutable durable commit identity", "durable-intent",
        {{"transactionId", draft}, {"expectedVersion", integer(0, 100)}},
        {"transactionId", "expectedVersion"});
    add("transaction.abort", "Discard an uncommitted volatile draft or cancel its sealed request",
        "abort", {{"transactionId", draft}}, {"transactionId"});
    for (const auto *method : {"commit", "cancel", "status"}) {
        const auto name = QString("transaction.") + method;
        add(name,
            QString(method) == "commit"
                ? "Publish the sealed request once after confirmed durability"
            : QString(method) == "cancel"
                ? "Cancel pending work or return its original committed outcome"
                : "Resolve pending, committed, aborted or unknown durable outcome",
            QString(method) == "commit"   ? "document-commit"
            : QString(method) == "cancel" ? "abort"
                                          : "outcome-maintenance",
            {{"requestId", requestId}, {"payloadHash", hash}}, {"requestId", "payloadHash"});
    }
    add("transaction.reconcile", "Resolve uncertain publication from verified durable evidence",
        "reconciliation", {}, {});
    return result;
}
QJsonObject transactionCapabilities() {
    return {{"apiVersion", 1},
            {"transport", "In-process serialized document dispatcher"},
            {"operations", transactionCatalog()},
            {"limits", QJsonObject{{"requestBytes", transactionRequestBytes},
                                   {"responseBytes", transactionResponseBytes},
                                   {"drafts", 4},
                                   {"retainedBytes", 128 * 1024 * 1024},
                                   {"commandsPerDraft", 100},
                                   {"defaultTtlSeconds", 60},
                                   {"maximumTtlSeconds", 300},
                                   {"maximumDiffPage", 100},
                                   {"outcomeBytes", 64 * 1024 * 1024},
                                   {"outcomes", 10000},
                                   {"minimumOutcomeRetentionDays", 30}}},
            {"identity", "Volatile transactionId until preview seals immutable "
                         "requestId/payloadHash; unknown IDs never create work"},
            {"publication", "One revision and undo entry after co-recorded candidate/outcome "
                            "durability; no command replay"},
            {"sealedDraftsMutable", false},
            {"arbitraryFiles", false},
            {"shell", false},
            {"authorization", "Caller must authorize scope and every retry before dispatch"}};
}
} // namespace sketchy
