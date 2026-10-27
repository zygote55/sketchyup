#pragma once
// R101.c (ADR 0175 §7, §8): saved report schedules. A schedule is a named §1 request kept in a
// user-level JSON file, keyed by document identity. The model file never holds one, so a save is
// bit-identical with or without schedules (V11) and the document keeps its schema. A corrupt or
// oversized store is refused with a located error; it is never reset.
#include "core/model.hpp"
#include <QByteArray>
#include <QJsonObject>
#include <QString>
#include <map>
#include <string>
#include <vector>

namespace sketchy {

struct ReportSchedule {
    QString name;
    QString createdAt; // ISO 8601 in UTC, written by saveReportSchedule.
    QJsonObject request;
};

// The user-level store: AppLocalDataLocation/report-schedules.json. The caller sets the
// organization and application names first, as the CLI and the desktop do (the presentation job
// store uses the same folder).
QString reportScheduleStorePath();

// The §1 request vocabulary of report.run, taken from the inspection registry so that the session
// and the schedule flags validate the same shape.
QJsonObject reportRequestSchema();

// The user-level store, read once at construction and written by commit. A missing file is an
// empty store. A file that is a link or folder, not valid JSON, not the version-1 layout, or over
// 1 MiB is refused with INPUT_ERROR, located by path and byte or entry.
class ReportScheduleStore {
  public:
    explicit ReportScheduleStore(QString path);
    const QString &path() const { return path_; }

    // The schedules saved for one document identity, in name order.
    std::vector<ReportSchedule> schedules(const std::string &documentId) const;

    // The identity guard (V11): a name saved for this document is returned; a name saved only for
    // another document is refused with WRONG_DOCUMENT, and a name saved nowhere with NOT_FOUND.
    const ReportSchedule &lookup(const std::string &documentId, const QString &name) const;

    // Names are unique per document. Each refusal names the bound it hit.
    void add(const std::string &documentId, ReportSchedule schedule);
    void remove(const std::string &documentId, const QString &name);

    // R101.e1 (ADR 0175 §7, gaps 2 and 3). Each is one commit, so a refusal or a failed write
    // leaves the file and this store as they were: the name and the request are never lost in
    // between. rename keeps the request and createdAt; update keeps the name and createdAt. Both
    // refuse a name saved for another document (WRONG_DOCUMENT) or nowhere (NOT_FOUND), and rename
    // refuses a name already in use for this document (INVALID_REQUEST).
    void rename(const std::string &documentId, const QString &name, const QString &newName);
    void update(const std::string &documentId, const QString &name, QJsonObject request);

  private:
    using Schedules = std::map<QString, ReportSchedule>;
    QString path_;
    QByteArray digest_; // Hash of the bytes read at construction; empty when the file was absent.
    std::map<std::string, Schedules> documents_;

    void commit(std::map<std::string, Schedules> replacement);
};

// A schedule's references must still resolve in the document. Refuses NOT_FOUND and names every
// missing tag, material and definition ID in its details (ADR 0175 §7: never run with fewer
// filters).
void requireResolvedScheduleIds(const Document &doc, const QJsonObject &request);

// The session's report.schedule operation (ADR 0175 §8): {apiVersion, operation, documentId,
// action, name, request}. It is a session operation, not an inspection query, because the
// inspection registry is read-only.
QJsonObject reportScheduleSchema();
QJsonObject reportScheduleOperation(const Document &doc, const QJsonObject &request,
                                    const QString &storePath);

// The actions, shared by the session and the CLI flags. Describe without a name lists the
// document's schedules. Save and update pin the precision: a request that names none takes the
// document's precision at that moment (R101.e1, owner choice 4).
QJsonObject saveReportSchedule(const Document &doc, const QString &storePath, const QString &name,
                               const QJsonObject &request);
QJsonObject describeReportSchedules(const Document &doc, const QString &storePath,
                                    const QString &name = {});
QJsonObject deleteReportSchedule(const Document &doc, const QString &storePath,
                                 const QString &name);
// R101.e1: rename keeps the request and its creation time; update replaces the request and keeps
// the name and its creation time (how a stale schedule is fixed).
QJsonObject renameReportSchedule(const Document &doc, const QString &storePath, const QString &name,
                                 const QString &newName);
QJsonObject updateReportSchedule(const Document &doc, const QString &storePath, const QString &name,
                                 const QJsonObject &request);

} // namespace sketchy
