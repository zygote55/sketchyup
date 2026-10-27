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

// The three actions, shared by the session and the CLI flags. Describe without a name lists the
// document's schedules.
QJsonObject saveReportSchedule(const Document &doc, const QString &storePath, const QString &name,
                               const QJsonObject &request);
QJsonObject describeReportSchedules(const Document &doc, const QString &storePath,
                                    const QString &name = {});
QJsonObject deleteReportSchedule(const Document &doc, const QString &storePath,
                                 const QString &name);

} // namespace sketchy
