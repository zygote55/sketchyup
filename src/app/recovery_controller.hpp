#pragma once
#include "io/recovery.hpp"
#include <QObject>
#include <QTimer>
#include <functional>
#include <future>
namespace sketchy {
class RecoveryController : public QObject {
    Q_OBJECT
  public:
    RecoveryController(Document &document, QString root, std::function<RecoveryContext()> context,
                       QObject *parent = nullptr);
    ~RecoveryController() override;
    void setInterval(int seconds);
    int interval() const { return interval_; }
    void checkpoint();
    QString discard();
    void adopt(const RecoveryInfo &info);
    const QString &root() const { return root_; }
    const std::optional<RecoveryInfo> &durable() const { return durable_; }
    const QString &error() const { return error_; }
    bool busy() const { return future_.valid(); }
  signals:
    void changed();

  private:
    struct Worker;
    struct Outcome {
        std::optional<RecoveryInfo> info;
        QString error;
    };
    Document &document_;
    QString root_, error_;
    std::function<RecoveryContext()> context_;
    std::shared_ptr<Worker> worker_;
    std::optional<RecoveryInfo> durable_;
    std::optional<Document::SaveStamp> session_;
    Document::SaveStamp pendingStamp_;
    std::future<Outcome> future_;
    QTimer timer_, poll_;
    int interval_{30};
    void finish();
    void tick();
};
} // namespace sketchy
