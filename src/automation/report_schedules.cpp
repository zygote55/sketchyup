#include "automation/report_schedules.hpp"
#include "automation/inspection.hpp"
#include "automation/report_commands.hpp"
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLockFile>
#include <QRegularExpression>
#include <QSaveFile>
#include <QStandardPaths>
#include <QStringList>
#include <stdexcept>

namespace sketchy {
namespace {

// The bounds of ADR 0175 §7. A document holds a few dozen named requests and a store a few
// documents; each refusal names the bound it reached.
constexpr qsizetype storeBytes = 1024 * 1024;
constexpr int documentLimit = 64;
constexpr int scheduleLimit = 32;
constexpr int nameLimit = 64;
constexpr int identityLimit = 128;
constexpr int timestampLimit = 32;

const char *storeFile = "report-schedules.json";
const char *lockFile = "report-schedules.lock";

using Schedules = std::map<QString, ReportSchedule>;
using Documents = std::map<std::string, Schedules>;

[[noreturn]] void fail(const char *code, const QString &message, QJsonObject details = {}) {
    throw InspectionError(code, message.toStdString(), std::move(details));
}

// A name is 1 to 64 characters: letters, digits, spaces and . _ ( ) -, starting and ending with a
// letter or digit. It never holds a comma, quote or line break, so the CSV's #schedule= line stays
// a plain value (ADR 0175 §6.1).
void requireName(const QString &name) {
    if (name.isEmpty() || name.size() > nameLimit)
        fail("INVALID_REQUEST", "A report schedule name is 1 to 64 characters");
    if (!QRegularExpression(R"(\A[A-Za-z0-9](?:[A-Za-z0-9 ._()-]{0,62}[A-Za-z0-9])?\z)")
             .match(name)
             .hasMatch())
        fail("INVALID_REQUEST", "A report schedule name starts and ends with a letter or digit and "
                                "uses only letters, digits, spaces and . _ ( ) -");
}

bool usableIdentity(const QString &documentId) {
    return documentId.size() <= identityLimit &&
           QRegularExpression(R"(\A[A-Za-z0-9_.-]{1,128}\z)").match(documentId).hasMatch();
}

void requireIdentity(const std::string &documentId) {
    if (!usableIdentity(QString::fromStdString(documentId)))
        fail("INVALID_REQUEST", "This document has no usable identity; save the document before "
                                "scheduling reports");
}

QJsonObject encodeSchedule(const ReportSchedule &schedule) {
    return {
        {"name", schedule.name}, {"createdAt", schedule.createdAt}, {"request", schedule.request}};
}

// The store's own layout: an object with exactly these fields.
bool exactly(const QJsonObject &object, const QStringList &names) {
    if (object.size() != names.size())
        return false;
    for (const auto &name : names)
        if (!object.contains(name))
            return false;
    return true;
}

// Decodes the store's bytes. Every refusal is located: the store path, then the entry at fault.
Documents decodeStore(const QString &path, const QByteArray &bytes) {
    const auto refuse = [&](const QString &where, const char *reason) {
        fail("INPUT_ERROR", "Report schedule store " + path + ": " + where + " " + reason + ".");
    };
    QJsonParseError error;
    const auto json = QJsonDocument::fromJson(bytes, &error);
    if (error.error != QJsonParseError::NoError)
        fail("INPUT_ERROR", QStringLiteral("Report schedule store %1 is not valid JSON (byte %2: "
                                           "%3)")
                                .arg(path)
                                .arg(error.offset)
                                .arg(error.errorString()));
    if (!json.isObject())
        fail("INPUT_ERROR", "Report schedule store " + path + " is not a JSON object");
    const auto root = json.object();
    if (!exactly(root, {"storeVersion", "documents"}) || root["storeVersion"] != 1 ||
        !root["documents"].isArray())
        refuse("the store", "has an unsupported layout or version");

    const auto entries = root["documents"].toArray();
    if (entries.size() > documentLimit)
        refuse("the store", "holds more than 64 documents");
    Documents documents;
    int document = 0;
    for (const auto &value : entries) {
        const auto where = QStringLiteral("documents[%1]").arg(document++);
        const auto entry = value.toObject();
        if (!value.isObject() || !exactly(entry, {"documentId", "schedules"}) ||
            !entry["documentId"].isString() || !usableIdentity(entry["documentId"].toString()) ||
            !entry["schedules"].isArray())
            refuse(where, "is not a document entry");
        const auto id = entry["documentId"].toString().toStdString();
        if (documents.contains(id))
            refuse(where, "repeats a document identity");
        const auto items = entry["schedules"].toArray();
        if (items.isEmpty() || items.size() > scheduleLimit)
            refuse(where, "must hold between 1 and 32 schedules");
        Schedules schedules;
        int schedule = 0;
        for (const auto &item : items) {
            const auto at = where + QStringLiteral(".schedules[%1]").arg(schedule++);
            const auto fields = item.toObject();
            if (!item.isObject() || !exactly(fields, {"name", "createdAt", "request"}) ||
                !fields["name"].isString() || !fields["createdAt"].isString() ||
                !fields["request"].isObject())
                refuse(at, "is not a schedule entry");
            ReportSchedule decoded{fields["name"].toString(), fields["createdAt"].toString(),
                                   fields["request"].toObject()};
            if (decoded.name.size() > nameLimit ||
                !QRegularExpression(R"(\A[A-Za-z0-9](?:[A-Za-z0-9 ._()-]{0,62}[A-Za-z0-9])?\z)")
                     .match(decoded.name)
                     .hasMatch())
                refuse(at, "has an invalid name");
            if (decoded.createdAt.size() > timestampLimit ||
                !QDateTime::fromString(decoded.createdAt, Qt::ISODate).isValid())
                refuse(at, "has an invalid creation time");
            if (QJsonDocument(decoded.request).toJson(QJsonDocument::Compact).size() >
                inspectionRequestBytes)
                refuse(at, "has a request over 16 KiB");
            if (schedules.contains(decoded.name))
                refuse(at, "repeats a schedule name");
            schedules.emplace(decoded.name, std::move(decoded));
        }
        documents.emplace(id, std::move(schedules));
    }
    return documents;
}

} // namespace

QString reportScheduleStorePath() {
    if (QCoreApplication::organizationName().isEmpty() ||
        QCoreApplication::applicationName().isEmpty())
        fail("INVALID_OPERATION",
             "The application names must be set before the report schedule store is located");
    const auto folder = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
    if (folder.isEmpty())
        fail("INVALID_OPERATION", "This platform has no user data folder for report schedules");
    return QDir(folder).filePath(storeFile);
}

QJsonObject reportRequestSchema() {
    for (const auto &entry : inspectionCatalog()) {
        const auto query = entry.toObject();
        if (query["name"].toString() == "report.run")
            return query["parameters"].toObject()["properties"].toObject()["request"].toObject();
    }
    throw std::runtime_error("report.run is missing from the inspection registry");
}

ReportScheduleStore::ReportScheduleStore(QString path) : path_(QFileInfo(path).absoluteFilePath()) {
    const QFileInfo info(path_);
    if (info.isSymLink() || (info.exists() && !info.isFile()))
        fail("INPUT_ERROR",
             "Report schedule store " + path_ + " must be a regular file, not a link or folder");
    if (!info.exists())
        return;
    QFile file(path_);
    if (info.size() > storeBytes)
        fail("INPUT_ERROR", "Report schedule store " + path_ + " exceeds 1 MiB");
    if (!file.open(QIODevice::ReadOnly))
        fail("INPUT_ERROR", "Cannot read report schedule store " + path_);
    const auto bytes = file.read(storeBytes + 1);
    if (bytes.size() > storeBytes || file.error() != QFileDevice::NoError)
        fail("INPUT_ERROR", "Report schedule store " + path_ + " exceeds 1 MiB or cannot be read");
    digest_ = QCryptographicHash::hash(bytes, QCryptographicHash::Sha256);
    documents_ = decodeStore(path_, bytes);
}

std::vector<ReportSchedule> ReportScheduleStore::schedules(const std::string &documentId) const {
    std::vector<ReportSchedule> result;
    if (const auto document = documents_.find(documentId); document != documents_.end())
        for (const auto &entry : document->second)
            result.push_back(entry.second);
    return result;
}

const ReportSchedule &ReportScheduleStore::lookup(const std::string &documentId,
                                                  const QString &name) const {
    if (const auto document = documents_.find(documentId); document != documents_.end())
        if (const auto found = document->second.find(name); found != document->second.end())
            return found->second;
    for (const auto &[other, schedules] : documents_)
        if (schedules.contains(name))
            fail("WRONG_DOCUMENT",
                 "Report schedule '" + name +
                     "' is saved for another document; a schedule runs only on the document it "
                     "was saved for",
                 QJsonObject{{"documentId", QString::fromStdString(documentId)},
                             {"savedFor", QString::fromStdString(other)}});
    fail("NOT_FOUND", "No report schedule named '" + name + "' is saved for this document");
}

void ReportScheduleStore::add(const std::string &documentId, ReportSchedule schedule) {
    requireIdentity(documentId);
    requireName(schedule.name);
    if (QJsonDocument(schedule.request).toJson(QJsonDocument::Compact).size() >
        inspectionRequestBytes)
        fail("LIMIT_EXCEEDED", "A saved report request is at most 16 KiB");
    auto replacement = documents_;
    auto &schedules = replacement[documentId];
    if (schedules.contains(schedule.name))
        fail("INVALID_REQUEST", "A report schedule named '" + schedule.name +
                                    "' already exists for this document; delete it first");
    if (int(schedules.size()) >= scheduleLimit)
        fail("LIMIT_EXCEEDED", "A document holds at most 32 report schedules; delete one first");
    if (replacement.size() > documentLimit)
        fail("LIMIT_EXCEEDED", "The report schedule store holds at most 64 documents; delete the "
                               "schedules of another document first");
    schedules.emplace(schedule.name, std::move(schedule));
    commit(std::move(replacement));
}

void ReportScheduleStore::remove(const std::string &documentId, const QString &name) {
    lookup(documentId, name); // Refuses a name saved for another document, or saved nowhere.
    auto replacement = documents_;
    auto &schedules = replacement[documentId];
    schedules.erase(name);
    if (schedules.empty())
        replacement.erase(documentId);
    commit(std::move(replacement));
}

void ReportScheduleStore::commit(Documents replacement) {
    QJsonArray entries;
    for (const auto &[id, schedules] : replacement) {
        QJsonArray items;
        for (const auto &entry : schedules)
            items.append(encodeSchedule(entry.second));
        entries.append(
            QJsonObject{{"documentId", QString::fromStdString(id)}, {"schedules", items}});
    }
    const auto bytes = QJsonDocument(QJsonObject{{"storeVersion", 1}, {"documents", entries}})
                           .toJson(QJsonDocument::Compact);
    if (bytes.size() > storeBytes)
        fail("LIMIT_EXCEEDED", "The report schedule store would exceed 1 MiB; delete saved "
                               "schedules first");
    const QFileInfo info(path_);
    if (info.isSymLink() || (info.exists() && !info.isFile()))
        fail("INPUT_ERROR",
             "Report schedule store " + path_ + " must be a regular file, not a link or folder");
    const QDir folder = info.absoluteDir();
    if (!QDir().mkpath(folder.absolutePath()))
        fail("OUTPUT_ERROR", "Cannot create the report schedule folder");
    QLockFile lock(folder.filePath(lockFile));
    if (!lock.tryLock(0)) {
        if (lock.error() == QLockFile::LockFailedError)
            fail("OUTPUT_ERROR", "Another report schedule update is in progress");
        fail("OUTPUT_ERROR", "Cannot write the report schedule folder " + folder.absolutePath());
    }
    // The file must still be the one this store read. A change by another process is refused,
    // never merged, and so is a file that appeared or vanished.
    if (info.exists()) {
        QFile current(path_);
        if (!current.open(QIODevice::ReadOnly))
            fail("FILE_CHANGED", "Cannot verify the report schedule store; run the command again");
        const auto previous = current.read(storeBytes + 1);
        if (previous.size() > storeBytes ||
            QCryptographicHash::hash(previous, QCryptographicHash::Sha256) != digest_)
            fail("FILE_CHANGED",
                 "The report schedule store changed on disk; run the command again");
    } else if (!digest_.isEmpty())
        fail("FILE_CHANGED", "The report schedule store was removed; run the command again");
    // QSaveFile publishes the whole file or leaves the previous contents in place.
    QSaveFile file(path_);
    file.setDirectWriteFallback(false);
    if (!file.open(QIODevice::WriteOnly) ||
        !file.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner) ||
        file.write(bytes) != bytes.size() || !file.commit())
        fail("OUTPUT_ERROR",
             "Cannot write the report schedule store; its previous contents are unchanged");
    digest_ = QCryptographicHash::hash(bytes, QCryptographicHash::Sha256);
    documents_ = std::move(replacement);
}

void requireResolvedScheduleIds(const Document &doc, const QJsonObject &request) {
    const auto filters = request["filters"].toObject();
    QJsonObject missing;
    QStringList parts;
    const auto absent = [&](const char *field, const auto &exists) {
        QStringList ids;
        for (const auto &value : filters[field].toArray()) {
            bool ok{};
            const auto id = value.toString().toULongLong(&ok);
            if (ok && !exists(id))
                ids.append(value.toString());
        }
        if (ids.isEmpty())
            return;
        missing[field] = QJsonArray::fromStringList(ids);
        parts.append(QString::fromLatin1(field) + " " + ids.join(", "));
    };
    absent("tags", [&](Id id) { return doc.tags().contains(id); });
    absent("materials", [&](Id id) { return doc.materials().contains(id); });
    absent("definitions", [&](Id id) { return doc.definitions().contains(id); });
    if (!parts.isEmpty())
        fail("NOT_FOUND",
             "The schedule refers to IDs that no longer exist in this document: " +
                 parts.join("; "),
             QJsonObject{{"missing", missing}});
}

QJsonObject reportScheduleSchema() {
    return {{"$schema", "https://json-schema.org/draft/2020-12/schema"},
            {"type", "object"},
            {"properties",
             QJsonObject{
                 {"apiVersion", QJsonObject{{"const", 1}}},
                 {"operation", QJsonObject{{"const", "report.schedule"}}},
                 {"documentId",
                  QJsonObject{{"type", "string"}, {"minLength", 1}, {"maxLength", identityLimit}}},
                 {"action", QJsonObject{{"type", "string"},
                                        {"enum", QJsonArray{"save", "describe", "delete"}}}},
                 {"name", QJsonObject{{"type", "string"}, {"maxLength", nameLimit}}},
                 {"request", reportRequestSchema()}}},
            {"required", QJsonArray{"apiVersion", "operation", "documentId", "action"}},
            {"additionalProperties", false}};
}

QJsonObject reportScheduleOperation(const Document &doc, const QJsonObject &request,
                                    const QString &storePath) {
    if (request["documentId"].toString() != QString::fromStdString(doc.identity()))
        fail("WRONG_DOCUMENT",
             "This report schedule request names another document; a session "
             "saves and deletes schedules of its own document only",
             QJsonObject{{"sessionDocumentId", QString::fromStdString(doc.identity())}});
    const auto action = request["action"].toString();
    const auto name = request["name"].toString();
    const bool hasRequest = request.contains("request");
    if (action == "save") {
        if (name.isEmpty() || !hasRequest)
            fail("INVALID_REQUEST", "Save needs a name and a request");
        return saveReportSchedule(doc, storePath, name, request["request"].toObject());
    }
    if (hasRequest)
        fail("INVALID_REQUEST", "Only save takes a request");
    if (action == "delete") {
        if (name.isEmpty())
            fail("INVALID_REQUEST", "Delete needs the schedule's name");
        return deleteReportSchedule(doc, storePath, name);
    }
    if (action != "describe")
        fail("INVALID_REQUEST", "Unknown report schedule action");
    return describeReportSchedules(doc, storePath, name);
}

QJsonObject saveReportSchedule(const Document &doc, const QString &storePath, const QString &name,
                               const QJsonObject &request) {
    const auto documentId = doc.identity();
    requireIdentity(documentId);
    requireName(name);
    if (request["filters"].toObject()["selection"].toBool())
        fail("INVALID_REQUEST", "A saved schedule cannot use the session selection; name its "
                                "tags, materials or definitions instead");
    requireResolvedScheduleIds(doc, request);
    reportRequestFrom(doc, request, false); // The same validation a run applies.
    ReportScheduleStore store(storePath);
    const ReportSchedule schedule{name, QDateTime::currentDateTimeUtc().toString(Qt::ISODate),
                                  request};
    store.add(documentId, schedule);
    return {{"status", "saved"},
            {"documentId", QString::fromStdString(documentId)},
            {"schedule", encodeSchedule(schedule)}};
}

QJsonObject describeReportSchedules(const Document &doc, const QString &storePath,
                                    const QString &name) {
    const auto documentId = doc.identity();
    requireIdentity(documentId);
    const ReportScheduleStore store(storePath);
    QJsonObject result{{"documentId", QString::fromStdString(documentId)}};
    if (name.isEmpty()) {
        QJsonArray schedules;
        for (const auto &schedule : store.schedules(documentId))
            schedules.append(encodeSchedule(schedule));
        result["schedules"] = schedules;
        return result;
    }
    requireName(name);
    result["schedule"] = encodeSchedule(store.lookup(documentId, name));
    return result;
}

QJsonObject deleteReportSchedule(const Document &doc, const QString &storePath,
                                 const QString &name) {
    const auto documentId = doc.identity();
    requireIdentity(documentId);
    requireName(name);
    ReportScheduleStore store(storePath);
    store.remove(documentId, name);
    return {
        {"status", "deleted"}, {"documentId", QString::fromStdString(documentId)}, {"name", name}};
}

} // namespace sketchy
