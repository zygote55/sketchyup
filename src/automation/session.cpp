#include "automation/session.hpp"
#include "automation/inspection_validation.hpp"
#include "geometry/offset.hpp"
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
namespace sketchy {
namespace {
[[noreturn]] void fail(const char *code, const char *message) {
    throw InspectionError(code, message);
}
QByteArray hash(const QByteArray &bytes) {
    return QCryptographicHash::hash(bytes, QCryptographicHash::Sha256);
}
QByteArray read(const QString &path) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        fail("INPUT_ERROR", "Cannot read the explicitly selected model");
    const auto bytes = file.read(128 * 1024 * 1024 + 1);
    if (file.error() != QFileDevice::NoError)
        fail("INPUT_ERROR", "Could not read model bytes");
    if (bytes.size() > 128 * 1024 * 1024)
        fail("LIMIT_EXCEEDED", "Model exceeds 128 MiB");
    return bytes;
}
QString path(const QString &value) {
    if (value.isEmpty())
        return {};
    QFileInfo info(value);
    if (info.isSymLink() && !info.exists())
        fail("INVALID_PATH", "Broken symbolic link is not a model target");
    if (info.exists()) {
        if (!info.isFile())
            fail("INVALID_PATH", "Model target must be a regular file");
        return info.canonicalFilePath();
    }
    const auto parent = QFileInfo(info.absolutePath()).canonicalFilePath();
    if (parent.isEmpty())
        fail("INVALID_PATH", "Model target directory must already exist");
    return QDir(parent).filePath(info.fileName());
}
std::optional<QByteArray> fileHash(const QString &target) {
    QFileInfo info(target);
    if (info.isSymLink())
        fail("FILE_CHANGED", "Model destination became a symbolic link");
    if (!info.exists())
        return {};
    if (!info.isFile())
        fail("FILE_CHANGED", "Model destination is no longer a regular file");
    return hash(read(target));
}
QJsonObject schema(QString name, bool document = false) {
    QJsonObject fields{{"apiVersion", QJsonObject{{"const", 1}}},
                       {"operation", QJsonObject{{"const", name}}}};
    QJsonArray required{"apiVersion", "operation"};
    if (document) {
        fields["documentId"] =
            QJsonObject{{"type", "string"}, {"minLength", 1}, {"maxLength", 128}};
        fields["expectedRevision"] =
            QJsonObject{{"type", "string"}, {"pattern", "^(0|[1-9][0-9]*)$"}, {"maxLength", 20}};
        required.append("documentId");
        required.append("expectedRevision");
    }
    return {{"$schema", "https://json-schema.org/draft/2020-12/schema"},
            {"type", "object"},
            {"properties", fields},
            {"required", required},
            {"additionalProperties", false}};
}
void boundedDepth(const QByteArray &bytes) {
    int depth = 0;
    bool quoted = false, escaped = false;
    for (const auto c : bytes) {
        if (quoted) {
            if (escaped)
                escaped = false;
            else if (c == '\\')
                escaped = true;
            else if (c == '"')
                quoted = false;
        } else if (c == '"')
            quoted = true;
        else if (c == '{' || c == '[') {
            if (++depth > 64)
                fail("LIMIT_EXCEEDED", "Request nesting exceeds 64 levels");
        } else if (c == '}' || c == ']')
            --depth;
    }
}
void write(QIODevice &output, const QJsonObject &object) {
    auto bytes = QJsonDocument(object).toJson(QJsonDocument::Compact);
    if (bytes.size() > sessionResponseBytes)
        fail("LIMIT_EXCEEDED", "Session response exceeds 1 MiB");
    bytes.append('\n');
    qint64 offset = 0;
    while (offset < bytes.size()) {
        const auto written = output.write(bytes.constData() + offset, bytes.size() - offset);
        if (written <= 0)
            fail("OUTPUT_ERROR",
                 "Cannot write automation response; reconcile accepted commits on reconnect");
        offset += written;
    }
    if (auto file = dynamic_cast<QFile *>(&output); file && !file->flush())
        fail("OUTPUT_ERROR",
             "Cannot flush automation response; reconcile accepted commits on reconnect");
}
} // namespace
void checkAutomationDepth(const QByteArray &bytes) { boundedDepth(bytes); }
void writeAutomationResponse(QIODevice &output, const QJsonObject &response) {
    write(output, response);
}
QJsonObject sessionCapabilities() {
    QJsonArray operations;
    for (const auto *name : {"session.describe", "session.capabilities", "document.save"})
        operations.append(QJsonObject{
            {"name", name},
            {"parameters", schema(name, QString(name) == "document.save")},
            {"sideEffects", QString(name) == "document.save"
                                ? "Explicitly bound model destination and its native backup"
                                : "none"}});
    return {{"apiVersion", 1},
            {"transport", "Bounded JSON lines over stdin/stdout"},
            {"operations", operations},
            {"inspection", inspectionSessionCapabilities()},
            {"transactions", transactionCapabilities()},
            {"limits", QJsonObject{{"wireBytes", sessionWireBytes},
                                   {"responseBytes", sessionResponseBytes},
                                   {"correlationIdBytes", 128},
                                   {"requestDepth", 64}}},
            {"selection", "Headless session has no desktop selection or view"},
            {"save", "Only the destination explicitly bound at process startup; external effects "
                     "are separate from geometry commit"},
            {"filesystemTools", false},
            {"shell", false}};
}
AutomationSession::AutomationSession(Options options) : recovered_(options.recoverLatest) {
    if ((options.create ? !options.input.isEmpty() : options.input.isEmpty()) ||
        options.outcomes.isEmpty() ||
        (options.create && (options.output.isEmpty() || options.recoverLatest)))
        fail("INVALID_REQUEST",
             "Choose an explicit input model or create with an output, and an outcome directory");
    const auto input = path(options.input);
    output_ = path(options.output);
    QFileInfo rootInfo(options.outcomes);
    if (rootInfo.exists() && !rootInfo.isDir())
        fail("INVALID_PATH", "Outcome storage must be a directory");
    // Resolve the existing ancestor as well as the final directory to prevent a
    // symlinked parent from hiding a destination inside private outcome storage.
    auto ancestor = rootInfo.absoluteFilePath();
    QStringList tail;
    while (!QFileInfo::exists(ancestor)) {
        const QFileInfo info(ancestor);
        tail.prepend(info.fileName());
        ancestor = info.absolutePath();
    }
    if (!QFileInfo(ancestor).isDir())
        fail("INVALID_PATH", "Outcome parent must be a directory");
    auto root = QFileInfo(ancestor).canonicalFilePath();
    for (const auto &part : tail)
        root = QDir(root).filePath(part);
    root = QDir::cleanPath(root);
    for (const auto &target : {input, output_})
        if (!target.isEmpty() && (root == "/" || target == root || target.startsWith(root + "/")))
            fail("INVALID_PATH", "Model paths must be outside private outcome storage");
    if (options.recoverLatest && !output_.isEmpty() && input == output_)
        fail("INVALID_PATH", "Recovered work must save to a separately selected copy");
    if (!output_.isEmpty()) {
        outputLock_ = std::make_unique<QLockFile>(output_ + ".automation.lock");
        if (!outputLock_->tryLock(0))
            fail("FILE_BUSY", "Another automation session owns the save destination");
        outputHash_ = fileHash(output_);
        if (options.create && outputHash_)
            fail("FILE_EXISTS", "Creating a model requires a new destination");
    }
    Document document;
    if (!options.create) {
        const auto bytes = read(input);
        document = decodeContainer(bytes);
        if (input == output_ && outputHash_ != hash(bytes))
            fail("FILE_CHANGED", "Input changed while opening the session");
    } else {
        // Persist the initial identity before any durable transaction. A restart
        // opens this file and resolves the same document's outcome journal.
        try {
            saveDocument(document, output_);
        } catch (const std::exception &) {
            fail("SAVE_OUTCOME_UNKNOWN",
                 "Initial model save failed; inspect its destination before retrying create");
        }
        outputHash_ = hash(encodeContainer(document));
    }
    TransactionCoordinator::Options coordinator;
    if (options.recoverLatest)
        coordinator.mode = TransactionCoordinator::OpenMode::RecoverLatest;
    actor_ = std::make_unique<TransactionCoordinator>(std::move(document), root, coordinator);
    transactions_ = std::make_unique<TransactionDispatcher>(*actor_);
}
AutomationSession::~AutomationSession() {
    try {
        close();
    } catch (...) { /* Opening the same journal retires lost staging. */
    }
}
QJsonObject AutomationSession::describe() const {
    const auto &doc = actor_->document();
    return {{"apiVersion", 1},
            {"documentId", QString::fromStdString(doc.identity())},
            {"revision", QString::number(doc.revision())},
            {"dirty", doc.dirty()},
            {"recoveryRequested", recovered_},
            {"outcomeUncertain", actor_->uncertain()},
            {"saveAvailable", !output_.isEmpty()},
            {"saveState", saveUnknown_ ? "unknown" : "ready"},
            {"selectionAvailable", false},
            {"viewAvailable", false}};
}
QJsonObject AutomationSession::save(const QJsonObject &request) {
    const auto &doc = actor_->document();
    if (request["documentId"].toString().toStdString() != doc.identity())
        fail("WRONG_DOCUMENT", "Save targets another document");
    if (request["expectedRevision"] != QString::number(doc.revision()))
        fail("STALE_REVISION", "Save requires the current revision");
    if (output_.isEmpty())
        fail("UNSUPPORTED_CAPABILITY", "No save destination was bound at startup");
    if (actor_->uncertain())
        fail("OUTCOME_UNKNOWN", "Reconcile transaction publication before saving");
    if (fileHash(output_) != outputHash_)
        fail("FILE_CHANGED",
             "Save destination changed outside this session; open and inspect it before replacing");
    const auto snapshot = captureSave(doc);
    try {
        actor_->edit([&](Document &candidate) { saveSnapshot(candidate, snapshot, output_); });
    } catch (const std::exception &) {
        saveUnknown_ = true;
        fail("SAVE_OUTCOME_UNKNOWN", "Save did not confirm durability; reopen and inspect the "
                                     "selected destination before dependent work");
    }
    outputHash_ = hash(snapshot.bytes());
    auto result = describe();
    result["status"] = "saved";
    result["sha256"] = QString::fromLatin1(outputHash_->toHex());
    return result;
}
QJsonObject AutomationSession::execute(const QJsonObject &request) {
    (void)actor_->document();
    if (closed_)
        fail("SESSION_CLOSED", "Session has closed");
    if (active_)
        fail("REENTRANT_TRANSACTION", "Session cannot reenter itself");
    struct Guard {
        bool &active;
        ~Guard() { active = false; }
    } guard{active_};
    active_ = true;
    if (QJsonDocument(request).toJson(QJsonDocument::Compact).size() > transactionRequestBytes)
        fail("LIMIT_EXCEEDED", "Request exceeds 64 KiB");
    if (request["apiVersion"] != 1)
        fail("UNSUPPORTED_VERSION", "Session requires API version 1");
    if (request.contains("operation") == request.contains("query"))
        fail("INVALID_REQUEST", "Choose exactly one registered operation or query");
    if (request.contains("query"))
        return inspection_.execute(actor_->document(), request);
    const auto operation = request["operation"].toString();
    if (operation == "session.describe" || operation == "session.capabilities") {
        inspection_detail::validateParameters(request, schema(operation));
        return operation == "session.describe" ? describe() : sessionCapabilities();
    }
    if (saveUnknown_ && operation != "transaction.status")
        fail("SAVE_OUTCOME_UNKNOWN",
             "Reopen and inspect the save destination before dependent operations");
    if (operation == "document.save") {
        inspection_detail::validateParameters(request, schema(operation, true));
        return save(request);
    }
    return transactions_->execute(request);
}
QJsonObject automationFailure(const std::exception &error) {
    QString code = "INVALID_OPERATION";
    if (auto typed = dynamic_cast<const InspectionError *>(&error))
        code = QString::fromStdString(typed->code());
    else if (auto typed = dynamic_cast<const OutcomeStoreError *>(&error))
        code = QString::fromStdString(typed->code());
    else if (auto typed = dynamic_cast<const PlanarError *>(&error))
        code = QString::fromStdString(typed->code());
    else if (auto typed = dynamic_cast<const OffsetError *>(&error))
        code = QString::fromStdString(typed->code());
    return {{"code", code}, {"message", QString::fromUtf8(error.what()).left(2048)}};
}
QJsonObject AutomationSession::respond(const QJsonObject &envelope) {
    QJsonValue id = QJsonValue::Null;
    try {
        const auto correlation = envelope["id"].toString();
        if (!envelope["id"].isString() || correlation.isEmpty() ||
            correlation.toUtf8().size() > 128)
            fail("INVALID_REQUEST",
                 "Envelope requires a string correlation ID of 1–128 UTF-8 bytes");
        id = correlation;
        if (envelope.size() != 2 || !envelope["request"].isObject())
            fail("INVALID_REQUEST", "Envelope contains only id and request object");
        return {{"id", id}, {"ok", true}, {"result", execute(envelope["request"].toObject())}};
    } catch (const std::exception &error) {
        return {{"id", id}, {"ok", false}, {"error", automationFailure(error)}};
    }
}
void AutomationSession::close() {
    if (closed_ || !actor_)
        return;
    (void)actor_->document();
    if (active_)
        fail("REENTRANT_TRANSACTION", "Cannot close during session dispatch");
    inspection_.clear();
    transactions_->clear();
    closed_ = true;
}
int runAutomationStream(AutomationSession &session, QIODevice &input, QIODevice &output) {
    bool failed = false;
    try {
        for (;;) {
            const auto line = input.readLine(sessionWireBytes + 2);
            if (line.isEmpty()) {
                if (auto file = dynamic_cast<QFile *>(&input);
                    file && file->error() != QFileDevice::NoError)
                    fail("INPUT_ERROR", "Cannot read automation stream");
                break;
            }
            if (line.size() > sessionWireBytes)
                fail("LIMIT_EXCEEDED", "JSON line exceeds 66 KiB; stream closed");
            boundedDepth(line);
            QJsonParseError error;
            const auto json = QJsonDocument::fromJson(line, &error);
            QJsonObject response;
            if (error.error != QJsonParseError::NoError || !json.isObject()) {
                failed = true;
                response = {
                    {"id", QJsonValue::Null},
                    {"ok", false},
                    {"error",
                     QJsonObject{{"code", "INVALID_REQUEST"},
                                 {"message", "Expected one JSON request envelope per line"}}}};
            } else {
                response = session.respond(json.object());
                failed |= response["ok"] != true;
            }
            write(output, response);
        }
        session.close();
    } catch (const std::exception &error) {
        // Writing may itself have failed. Return a nonzero process status;
        // accepted commits remain queryable through durable outcomes.
        try {
            write(output,
                  {{"id", QJsonValue::Null}, {"ok", false}, {"error", automationFailure(error)}});
        } catch (...) {
        }
        try {
            session.close();
        } catch (...) {
        }
        return 1;
    }
    return failed ? 1 : 0;
}
} // namespace sketchy
