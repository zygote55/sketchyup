#pragma once
#include <QCloseEvent>
#include <QDialog>
#include <QEventLoop>
#include <QLabel>
#include <QProgressBar>
#include <QTimer>
#include <QVBoxLayout>
#include <chrono>
#include <future>
namespace sketchy {
// The caller fences document replacement/reentrant file actions. Workers own
// immutable snapshots and never access widgets or the live Document.
template <class Operation>
auto runFileOperation(QWidget *parent, const QString &label, Operation operation) {
    class Progress final : public QDialog {
      public:
        explicit Progress(QWidget *parent) : QDialog(parent) {}
        void reject() override {} // Publication cannot be cancelled after commit begins.
      protected:
        void closeEvent(QCloseEvent *event) override { event->ignore(); }
    } progress(parent);
    progress.setObjectName("fileOperationDialog");
    progress.setWindowTitle(label);
    progress.setWindowModality(Qt::ApplicationModal);
    auto *layout = new QVBoxLayout(&progress);
    auto *description = new QLabel(label);
    description->setTextFormat(Qt::PlainText);
    layout->addWidget(description);
    auto *bar = new QProgressBar;
    bar->setRange(0, 0);
    bar->setAccessibleName(label);
    layout->addWidget(bar);
    // Keep short operations quiet, but continue delivering timers and paint events.
    QTimer::singleShot(150, &progress, [&] { progress.show(); });
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
