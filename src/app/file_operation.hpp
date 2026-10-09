#pragma once
#include <QCloseEvent>
#include <QDialog>
#include <QEventLoop>
#include <QLabel>
#include <QPointer>
#include <QProgressBar>
#include <QTimer>
#include <QVBoxLayout>
#include <QVariant>
#include <chrono>
#include <future>
namespace sketchy {
// Retain one native progress window per owner. Recreating and destroying it
// during each operation can block the GUI thread in platform window teardown.
class FileOperationProgress final : public QDialog {
    QLabel *description_;
    QProgressBar *bar_;

  public:
    explicit FileOperationProgress(QWidget *parent) : QDialog(parent) {
        setObjectName("fileOperationDialog");
        setWindowModality(Qt::ApplicationModal);
        auto *layout = new QVBoxLayout(this);
        description_ = new QLabel;
        description_->setTextFormat(Qt::PlainText);
        layout->addWidget(description_);
        bar_ = new QProgressBar;
        bar_->setRange(0, 0);
        layout->addWidget(bar_);
    }
    void prepare(const QString &label) {
        setWindowTitle(label);
        description_->setText(label);
        bar_->setAccessibleName(label);
        setProperty("fileOperationActive", true);
    }
    void finish() {
        hide();
        setProperty("fileOperationActive", false);
    }
    void reject() override {} // Publication cannot be cancelled after commit begins.

  protected:
    void closeEvent(QCloseEvent *event) override { event->ignore(); }
};
// The caller fences document replacement/reentrant file actions. Workers own
// immutable snapshots and never access widgets or the live Document.
template <class Operation>
auto runFileOperation(QWidget *parent, const QString &label, Operation operation) {
    auto *progress = dynamic_cast<FileOperationProgress *>(
        parent->findChild<QDialog *>("fileOperationDialog", Qt::FindDirectChildrenOnly));
    if (!progress)
        progress = new FileOperationProgress(parent);
    progress->prepare(label);
    struct Finish {
        QPointer<FileOperationProgress> progress;
        ~Finish() {
            if (progress)
                progress->finish();
        }
    } finish{progress};
    // Keep short operations quiet, but continue delivering timers and paint events.
    // This timer belongs to the call, so an old short operation cannot show the
    // retained dialog during a later operation or after an exception.
    QTimer showProgress;
    showProgress.setSingleShot(true);
    showProgress.setTimerType(Qt::PreciseTimer);
    QObject::connect(&showProgress, &QTimer::timeout, progress, [progress] { progress->show(); });
    showProgress.start(150);
    auto future = std::async(std::launch::async, std::move(operation));
    QEventLoop loop;
    QTimer poll;
    QObject::connect(&poll, &QTimer::timeout, &loop, [&] {
        if (future.wait_for(std::chrono::seconds(0)) == std::future_status::ready)
            loop.quit();
    });
    poll.start(10);
    loop.exec();
    // If application shutdown ends the loop, join the owned operation before
    // releasing its state. Exceptions return to the existing file-error UI.
    return future.get();
}
} // namespace sketchy
