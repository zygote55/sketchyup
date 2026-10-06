#pragma once
#include "integrations/render_store.hpp"
#include <QObject>
#include <QPointer>
#include <set>
#include <vector>
namespace sketchy {
class RenderQueue : public QObject {
    Q_OBJECT
  public:
    explicit RenderQueue(QString directory, QObject *parent = nullptr);
    RenderQueue(QString directory, RenderJobStore::Limits limits, int concurrency,
                QObject *parent = nullptr);
    ~RenderQueue() override;
    std::vector<StoredRenderJob> jobs() const;
    QString enqueue(std::shared_ptr<const PreparedRender> input, BlenderJob::Options options);
    void cancel(const QString &id);
    void retry(const QString &id);
    void remove(const QString &id);
    void clearFinished();
    std::shared_ptr<const BlenderResult> result(const QString &id);
    std::shared_ptr<const PreparedRender> input(const QString &id) const;
    QString progress(const QString &id) const;
    qint64 workerProcessId(const QString &id) const;
    int running() const { return int(active_.size()); }
  signals:
    void changed();
    void resultReady(QString id);
    void error(QString message);

  private:
    RenderJobStore store_;
    int concurrency_;
    bool shuttingDown_{};
    std::map<QString, QPointer<BlenderJob>> active_;
    std::map<QString, QString> errors_;
    std::set<QString> finishing_;
    void checkOwner() const;
    void schedule();
    void pump();
    void finish(const QString &id);
    void failed(const QString &id, const QString &message, const QJsonObject &report = {});
};
} // namespace sketchy
