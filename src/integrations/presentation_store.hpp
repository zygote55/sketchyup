#pragma once
#include "core/presentation_records.hpp"
#include <QJsonObject>
#include <QLockFile>
#include <map>
#include <memory>
#include <vector>
namespace sketchy {
// The batch states follow the R070 render job machine (ADR 0173 §4). Only one batch may be
// Running or Canceling at a time.
enum class PresentationJobState { Queued, Running, Canceling, Completed, Failed, Canceled, Interrupted };
QString presentationJobStateName(PresentationJobState state);
// One output of a batch: a name and the render settings. The preset record's ID is not part
// of the job, so a stored job does not depend on the document's allocator.
struct PresentationPreset {
    QString name;
    RenderSettings settings;
    bool operator==(const PresentationPreset &) const = default;
};
// What a batch renders: the keyframes and timing of one camera path, and the ordered presets.
struct PresentationPlan {
    QString path; // The camera path's name, reported in the manifest.
    std::vector<Id> keyframes;
    CameraPathTiming timing;
    std::vector<PresentationPreset> presets;
    bool operator==(const PresentationPlan &) const = default;
};
// The plan rules a batch can be created with: a valid path name, two to thirty-two keyframes,
// timing and planned frames within the R099.a limits, and one to four unique presets.
void validatePresentationPlan(const PresentationPlan &plan);
struct StoredPresentationJob {
    QString id, documentId, revision, captureSha256, output, message;
    qint64 createdMs{}, updatedMs{}, captureBytes{};
    PresentationJobState state{PresentationJobState::Queued};
    int attempts{};
    PresentationPlan plan;
    QJsonObject report;
};
// Owns only its private directory, <directory>/<id>/, and holds a single-writer process lock.
// The frozen capture is stored beside job.json and is hash-verified before every use. The
// output directory belongs to the user: the store never creates or removes it.
class PresentationJobStore {
  public:
    struct Limits {
        int pending{8}, retained{16};
        qint64 bytes{2LL * 1024 * 1024 * 1024};
    };
    explicit PresentationJobStore(QString directory);
    PresentationJobStore(QString directory, Limits limits);
    const std::map<QString, StoredPresentationJob> &jobs() const { return jobs_; }
    QString enqueue(const QByteArray &capture, QString documentId, QString revision,
                    PresentationPlan plan, QString output);
    // The frozen document bytes, refused when they no longer match the job's hash.
    QByteArray capture(const QString &id) const;
    // Worker scratch for a run of this job lives inside the job's own directory.
    QString scratchDirectory(const QString &id) const;
    void transition(const QString &id, PresentationJobState state, QString message = {},
                    QJsonObject report = {});
    void complete(const QString &id, QJsonObject report);
    void retry(const QString &id);
    void remove(const QString &id);
    void clearFinished();
    QString directory() const { return directory_; }

  private:
    QString directory_;
    Limits limits_;
    std::unique_ptr<QLockFile> lock_;
    std::map<QString, StoredPresentationJob> jobs_;
    QString jobDirectory(const QString &id) const;
    qint64 usedBytes() const;
    void requireCapacity(qint64 additional, int pendingAddition = 0) const;
    void save(const StoredPresentationJob &job) const;
    void reconcile();
};
} // namespace sketchy
