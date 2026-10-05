#include "automation/native_assistant_session.hpp"
#include "automation/inspection_validation.hpp"
#include <QJsonDocument>
#include <QScopeGuard>
namespace sketchy {
namespace {
[[noreturn]] void fail(const char *code, const char *message) {
    throw InspectionError(code, message);
}
} // namespace
NativeAssistantSession::NativeAssistantSession(Document &document, const QString &outcomes,
                                               const Selection *selection,
                                               std::function<bool()> busy,
                                               TransactionCoordinator::Options options)
    : document_(document), scope_(document.saveStamp()), selection_(selection),
      busy_(std::move(busy)),
      actor_(TransactionCoordinator::BorrowedDocument{document}, outcomes, std::move(options)),
      transactions_(actor_) {}
NativeAssistantSession::~NativeAssistantSession() {
    try {
        close();
    } catch (...) {
        // Durable outcomes remain in the journal, even after native scope closure.
    }
}
void NativeAssistantSession::checkScope() const {
    if (std::this_thread::get_id() != owner_)
        fail("WRONG_THREAD", "Native assistant requires the document owner thread");
    if (closed_ || !document_.owns(scope_))
        fail("SCOPE_CLOSED", "The native document session has closed or been replaced");
}
AssistantBackend NativeAssistantSession::backend() {
    checkScope();
    return {[this](const QJsonObject &request) { return execute(request); },
            [this] { return state(); }};
}
QJsonObject NativeAssistantSession::state() const {
    checkScope();
    return {{"apiVersion", 1},
            {"documentId", QString::fromStdString(document_.identity())},
            {"revision", QString::number(document_.revision())},
            {"dirty", document_.dirty()},
            {"outcomeUncertain", actor_.uncertain()},
            {"selectionAvailable", selection_ != nullptr},
            {"viewAvailable", false},
            {"saveAvailable", false},
            {"busy", busy_ && busy_()}};
}
bool NativeAssistantSession::uncertain() const {
    checkScope();
    return actor_.uncertain();
}
QJsonObject NativeAssistantSession::execute(const QJsonObject &request) {
    checkScope();
    if (active_)
        fail("REENTRANT_TRANSACTION", "Native assistant dispatch cannot reenter itself");
    active_ = true;
    const auto reset = qScopeGuard([this] { active_ = false; });
    const auto bytes = QJsonDocument(request).toJson(QJsonDocument::Compact);
    if (bytes.size() > transactionRequestBytes)
        fail("LIMIT_EXCEEDED", "Native request exceeds its byte budget");
    checkAutomationDepth(bytes);
    if (request.contains("query") == request.contains("operation"))
        fail("INVALID_REQUEST", "Use exactly one native request discriminator");
    if (request.contains("query"))
        return inspection_.execute(document_, request, selection_);
    const auto operation = request.value("operation").toString();
    if (!operation.startsWith("transaction."))
        fail("UNSUPPORTED_CAPABILITY",
             "Native assistant does not expose file or process operations");
    if (busy_ && busy_() && operation != "transaction.abort" && operation != "transaction.cancel" &&
        operation != "transaction.status" && operation != "transaction.reconcile")
        fail("EDITOR_BUSY",
             "Finish the current modeling gesture before staging or applying changes");
    if (operation == "transaction.commit") {
        // Validate before a native lock check can retire a sealed request.
        for (const auto &entry : transactionCatalog())
            if (entry.toObject().value("name") == operation)
                inspection_detail::validateParameters(
                    request, entry.toObject().value("parameters").toObject());
        if (request.value("documentId") != QString::fromStdString(document_.identity()))
            fail("WRONG_DOCUMENT", "Native commit targets another document");
        const auto id = request.value("requestId").toString();
        const auto hash = request.value("payloadHash").toString();
        if (actor_.status(id, hash).value("status") == "pending") {
            try {
                validatePolicy(*actor_.previewEdit(id, hash));
            } catch (const InspectionError &error) {
                if (error.code() == "LOCKED_ENTITY")
                    actor_.cancel(id, hash);
                throw;
            }
        }
    }
    auto result = transactions_.execute(request);
    if (operation == "transaction.preview") {
        const auto id = result.value("requestId").toString();
        const auto hash = result.value("payloadHash").toString();
        try {
            validatePolicy(*actor_.previewEdit(id, hash));
        } catch (const InspectionError &error) {
            if (error.code() == "LOCKED_ENTITY")
                actor_.cancel(id, hash);
            throw;
        }
    }
    return result;
}
std::shared_ptr<const Document::PreparedEdit>
NativeAssistantSession::previewEdit(const QJsonObject &sealed) {
    checkScope();
    if (active_)
        fail("REENTRANT_TRANSACTION", "Cannot acquire preview during native dispatch");
    auto edit = actor_.previewEdit(sealed.value("requestId").toString(),
                                   sealed.value("payloadHash").toString());
    validatePolicy(*edit);
    return edit;
}
void NativeAssistantSession::validatePolicy(const Document::PreparedEdit &edit) const {
    if (!selection_)
        return;
    const auto &after = edit.snapshot();
    for (const auto &[id, before] : document_.bodies()) {
        const bool exists = after.bodies().contains(id);
        const bool changed = !exists || *before != *after.bodies().at(id) ||
                             document_.worldTransform(id) != after.worldTransform(id);
        if (changed && selection_->locked(document_, id))
            fail("LOCKED_ENTITY", "The proposed change affects a locked native entity");
    }
    for (const auto &[id, body] : after.bodies())
        if (!document_.bodies().contains(id) && selection_->locked(after, id))
            fail("LOCKED_ENTITY", "The proposed change adds geometry under a locked native entity");
}
void NativeAssistantSession::close() {
    if (std::this_thread::get_id() != owner_)
        fail("WRONG_THREAD", "Native assistant requires the document owner thread");
    if (closed_)
        return;
    checkScope();
    if (active_)
        fail("REENTRANT_TRANSACTION", "Cannot close during native dispatch");
    if (actor_.uncertain())
        fail("OUTCOME_UNKNOWN", "Reconcile the native publication before closing its session");
    transactions_.clear();
    inspection_.clear();
    closed_ = true;
}
} // namespace sketchy
