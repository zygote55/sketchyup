#pragma once
#include "integrations/blender_job.hpp"
#include <QLockFile>
#include <map>
namespace sketchy {
enum class RenderJobState { Queued, Running, Canceling, Completed, Failed, Canceled, Interrupted };
QString renderJobStateName(RenderJobState state);
struct StoredRenderJob {
    QString id, documentId, revision, manifestHash, message;
    qint64 createdMs{}, updatedMs{}, queueSequence{1};
    RenderJobState state{RenderJobState::Queued};
    BlenderJob::Options options;
    int attempts{};
    QJsonObject report;
};
// Owns only its private job directory and holds a single-writer process lock.
class RenderJobStore {
  public:
    struct Limits {
        int pending{8}, retained{16};
        qint64 bytes{2LL * 1024 * 1024 * 1024};
    };
    explicit RenderJobStore(QString directory);
    RenderJobStore(QString directory, Limits limits);
    const std::map<QString, StoredRenderJob> &jobs() const { return jobs_; }
    QString enqueue(const PreparedRender &input, BlenderJob::Options options);
    std::shared_ptr<const PreparedRender> input(const QString &id) const;
    std::shared_ptr<const BlenderResult> result(const QString &id) const;
    void transition(const QString &id, RenderJobState state, QString message = {},
                    QJsonObject report = {});
    void complete(const QString &id, const BlenderResult &result, QJsonObject report);
    void retry(const QString &id);
    void remove(const QString &id);
    void clearFinished();
    QString directory() const { return directory_; }

  private:
    QString directory_;
    Limits limits_;
    std::unique_ptr<QLockFile> lock_;
    std::map<QString, StoredRenderJob> jobs_;
    QString jobDirectory(const QString &id) const;
    qint64 usedBytes() const;
    void requireCapacity(qint64 additional, int pendingAddition = 0) const;
    void save(const StoredRenderJob &job) const;
    void reconcile();
};
} // namespace sketchy
