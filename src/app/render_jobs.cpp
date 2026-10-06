#include "app/render_jobs.hpp"
#include <QDateTime>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QJsonDocument>
#include <QLabel>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSignalBlocker>
#include <QTimer>
#include <QTreeWidget>
#include <QVBoxLayout>
namespace sketchy {
namespace {
bool pending(RenderJobState state) {
    return state == RenderJobState::Queued || state == RenderJobState::Running ||
           state == RenderJobState::Canceling;
}
} // namespace
RenderJobsDialog::RenderJobsDialog(RenderQueue &queue,
                                   std::function<QString(const StoredRenderJob &)> provenance,
                                   std::function<void(const QString &)> open, QWidget *parent)
    : QDialog(parent), queue_(queue), provenance_(std::move(provenance)), open_(std::move(open)) {
    setObjectName("renderJobs");
    setWindowTitle("Render jobs");
    resize(760, 620);
    auto *layout = new QVBoxLayout(this);
    auto *intro =
        new QLabel("Renders keep their captured model and settings. Retry uses that capture. "
                   "Up to two jobs run at once; finished images stay here until removed.");
    intro->setWordWrap(true);
    layout->addWidget(intro);
    list_ = new QTreeWidget;
    list_->setObjectName("renderJobList");
    list_->setAccessibleName("Retained render jobs");
    list_->setHeaderLabels({"State / progress", "Captured model", "Attempts", "Elapsed"});
    list_->setRootIsDecorated(false);
    list_->setSelectionMode(QAbstractItemView::SingleSelection);
    list_->header()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    list_->header()->setSectionResizeMode(1, QHeaderView::Stretch);
    layout->addWidget(list_, 2);
    auto *buttons = new QHBoxLayout;
    auto button = [&](const QString &text, const QString &name) {
        auto *result = new QPushButton(text);
        result->setObjectName(name);
        buttons->addWidget(result);
        return result;
    };
    openButton_ = button("Open image", "openStoredRender");
    cancel_ = button("Cancel", "cancelStoredRender");
    retry_ = button("Retry capture", "retryStoredRender");
    remove_ = button("Remove", "removeStoredRender");
    layout->addLayout(buttons);
    details_ = new QPlainTextEdit;
    details_->setReadOnly(true);
    details_->setObjectName("renderJobDetails");
    details_->setAccessibleName("Captured settings and worker logs");
    layout->addWidget(details_, 1);
    status_ = new QLabel;
    status_->setObjectName("renderJobsStatus");
    status_->setWordWrap(true);
    status_->setTextFormat(Qt::PlainText);
    layout->addWidget(status_);
    auto *footer = new QHBoxLayout;
    clear_ = new QPushButton("Remove all finished jobs");
    clear_->setObjectName("clearStoredRenders");
    clear_->setToolTip("Deletes retained captures, logs and images for finished jobs.");
    footer->addWidget(clear_);
    footer->addStretch();
    auto *close = new QPushButton("Close");
    footer->addWidget(close);
    layout->addLayout(footer);
    connect(close, &QPushButton::clicked, this, &QDialog::hide);
    connect(list_, &QTreeWidget::itemSelectionChanged, this, [this] { selection(); });
    connect(&queue_, &RenderQueue::changed, this, [this] { refresh(); });
    connect(openButton_, &QPushButton::clicked, this,
            [this] { perform([&] { open_(selected()); }); });
    connect(cancel_, &QPushButton::clicked, this,
            [this] { perform([&] { queue_.cancel(selected()); }); });
    connect(retry_, &QPushButton::clicked, this,
            [this] { perform([&] { queue_.retry(selected()); }); });
    connect(remove_, &QPushButton::clicked, this,
            [this] { perform([&] { queue_.remove(selected()); }); });
    connect(clear_, &QPushButton::clicked, this,
            [this] { perform([&] { queue_.clearFinished(); }); });
    auto *timer = new QTimer(this);
    connect(timer, &QTimer::timeout, this, [this] {
        if (isVisible())
            refresh();
    });
    timer->start(1000);
    refresh();
}
QString RenderJobsDialog::selected() const {
    return list_->currentItem() ? list_->currentItem()->data(0, Qt::UserRole).toString()
                                : QString{};
}
void RenderJobsDialog::perform(const std::function<void()> &action) {
    try {
        action();
        status_->clear();
    } catch (const std::exception &error) {
        status_->setText(QString::fromUtf8(error.what()));
    }
    refresh();
}
void RenderJobsDialog::refresh() {
    const auto selectedId = selected();
    const auto jobs = queue_.jobs();
    QSignalBlocker blocker(list_);
    bool finished{};
    for (int i = 0; i < int(jobs.size()); ++i) {
        const auto &job = jobs[size_t(i)];
        auto *row = list_->topLevelItem(i);
        if (!row) {
            row = new QTreeWidgetItem;
            list_->addTopLevelItem(row);
        }
        row->setData(0, Qt::UserRole, job.id);
        auto state = renderJobStateName(job.state);
        if (job.state == RenderJobState::Running)
            state += " · " + queue_.progress(job.id);
        row->setText(0, state);
        row->setText(1, provenance_(job));
        row->setToolTip(1, job.documentId + "\n" + job.id);
        row->setText(2, QString::number(job.attempts));
        const auto end = pending(job.state) ? QDateTime::currentMSecsSinceEpoch() : job.updatedMs;
        row->setText(3, QString::number(std::max(qint64(0), end - job.createdMs) / 1000) + " s");
        if (job.id == selectedId)
            list_->setCurrentItem(row);
        finished |= !pending(job.state);
    }
    while (list_->topLevelItemCount() > int(jobs.size()))
        delete list_->takeTopLevelItem(list_->topLevelItemCount() - 1);
    if (!list_->currentItem() && !jobs.empty())
        list_->setCurrentItem(list_->topLevelItem(0));
    clear_->setEnabled(finished);
    selection();
}
void RenderJobsDialog::selection() {
    const auto id = selected();
    openButton_->setEnabled(false);
    cancel_->setEnabled(false);
    retry_->setEnabled(false);
    remove_->setEnabled(false);
    for (const auto &job : queue_.jobs()) {
        if (job.id != id)
            continue;
        openButton_->setEnabled(job.state == RenderJobState::Completed);
        cancel_->setEnabled(pending(job.state) && job.state != RenderJobState::Canceling);
        retry_->setEnabled(job.state == RenderJobState::Failed ||
                           job.state == RenderJobState::Canceled ||
                           job.state == RenderJobState::Interrupted);
        remove_->setEnabled(!pending(job.state));
        const auto captured =
            QDateTime::fromMSecsSinceEpoch(job.createdMs).toLocalTime().toString(Qt::ISODate);
        const auto text = provenance_(job) + "\nCaptured " + captured + "\n" + job.message +
                          "\nDevice: " + job.options.backend + " / " + job.options.deviceId +
                          (job.options.backend == "CPU"
                               ? QString{}
                               : (job.options.backend != "OPENGL" && job.options.allowCpuFallback
                                      ? " (CPU fallback allowed)"
                                      : " (no CPU fallback)")) +
                          "\n\nSource manifest SHA-256: " + job.manifestHash + "\n\n" +
                          QString::fromUtf8(QJsonDocument(queue_.diagnostics(id)).toJson());
        if (details_->toPlainText() != text)
            details_->setPlainText(text);
        return;
    }
    details_->clear();
}
} // namespace sketchy
