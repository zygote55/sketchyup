#include "app/render_jobs.hpp"
#include <QDateTime>
#include <QDesktopServices>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLabel>
#include <QPlainTextEdit>
#include <QProgressBar>
#include <QPushButton>
#include <QSignalBlocker>
#include <QTimer>
#include <QTreeWidget>
#include <QUrl>
#include <QVBoxLayout>
#include <algorithm>

namespace sketchy {
namespace {

// The role of a row: a render, a batch, or one preset of a batch.
constexpr int kindRole = Qt::UserRole + 1;

bool pending(RenderJobState state) {
    return state == RenderJobState::Queued || state == RenderJobState::Running ||
           state == RenderJobState::Canceling;
}

bool terminal(PresentationJobState state) {
    return state == PresentationJobState::Completed || state == PresentationJobState::Failed ||
           state == PresentationJobState::Canceled || state == PresentationJobState::Interrupted;
}

QString elapsedText(qint64 createdMs, qint64 endMs) {
    return QString::number(std::max(qint64(0), endMs - createdMs) / 1000) + " s";
}

} // namespace

RenderJobsDialog::RenderJobsDialog(
    RenderQueue &queue, std::function<QString(const StoredRenderJob &)> provenance,
    std::function<void(const QString &)> open, QWidget *parent, PresentationBatches *batches,
    std::function<QString(const StoredPresentationJob &)> batchProvenance)
    : QDialog(parent), queue_(queue), provenance_(std::move(provenance)), open_(std::move(open)),
      batches_(batches), batchProvenance_(std::move(batchProvenance)) {
    setObjectName("renderJobs");
    setWindowTitle("Jobs");
    resize(760, 620);
    auto *layout = new QVBoxLayout(this);
    auto *intro = new QLabel(
        "Renders keep their captured model and settings. Retry uses that capture. Presentation "
        "batches keep what they captured, and Resume continues one at its first unfinished frame. "
        "Up to one batch and two renders run at once.");
    intro->setWordWrap(true);
    layout->addWidget(intro);
    list_ = new QTreeWidget;
    list_->setObjectName("renderJobList");
    list_->setAccessibleName("Retained render jobs and presentation batches");
    list_->setHeaderLabels({"State / progress", "Captured model", "Attempts", "Elapsed"});
    list_->setRootIsDecorated(true);
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
    folder_ = button("Show folder", "showBatchFolder");
    folder_->hide();
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
    clear_->setToolTip(
        "Renders: deletes captures, logs and images. Batches: deletes records and logs; "
        "output folders stay.");
    footer->addWidget(clear_);
    footer->addStretch();
    auto *close = new QPushButton("Close");
    footer->addWidget(close);
    layout->addLayout(footer);
    connect(close, &QPushButton::clicked, this, &QDialog::hide);
    connect(list_, &QTreeWidget::itemSelectionChanged, this, [this] { selection(); });
    connect(&queue_, &RenderQueue::changed, this, [this] { refresh(); });
    if (batches_)
        connect(batches_, &PresentationBatches::changed, this, [this] { refresh(); });
    connect(openButton_, &QPushButton::clicked, this, [this] {
        if (selectedKind() == "batch")
            emit batchResults(selected());
        else
            perform([&] { open_(selected()); });
    });
    connect(cancel_, &QPushButton::clicked, this, [this] {
        if (selectedKind() == "batch")
            perform([&] { batches_->cancel(); });
        else
            perform([&] { queue_.cancel(selected()); });
    });
    connect(retry_, &QPushButton::clicked, this, [this] {
        if (selectedKind() == "batch")
            perform([&] { batches_->resume(selected()); });
        else
            perform([&] { queue_.retry(selected()); });
    });
    connect(remove_, &QPushButton::clicked, this, [this] {
        if (selectedKind() == "batch")
            perform([&] { batches_->remove(selected()); });
        else
            perform([&] { queue_.remove(selected()); });
    });
    connect(folder_, &QPushButton::clicked, this, [this] {
        if (!batches_)
            return;
        for (const auto &job : batches_->jobs())
            if (job.id == selected())
                QDesktopServices::openUrl(QUrl::fromLocalFile(job.output));
    });
    connect(clear_, &QPushButton::clicked, this, [this] {
        perform([&] {
            queue_.clearFinished();
            if (batches_)
                batches_->clearFinished();
        });
    });
    auto *timer = new QTimer(this);
    connect(timer, &QTimer::timeout, this, [this] {
        if (isVisible())
            refresh();
    });
    timer->start(1000);
    refresh();
}

void RenderJobsDialog::selectJob(const QString &id) {
    refresh();
    for (int i = 0; i < list_->topLevelItemCount(); ++i) {
        auto *row = list_->topLevelItem(i);
        if (row->data(0, Qt::UserRole).toString() == id &&
            row->data(0, kindRole).toString() == "batch") {
            list_->setCurrentItem(row);
            break;
        }
    }
    selection();
}

QString RenderJobsDialog::selected() const {
    const auto *item = list_->currentItem();
    if (!item)
        return {};
    return (item->parent() ? item->parent() : item)->data(0, Qt::UserRole).toString();
}

QString RenderJobsDialog::selectedKind() const {
    const auto *item = list_->currentItem();
    if (!item)
        return {};
    return (item->parent() ? item->parent() : item)->data(0, kindRole).toString();
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
    const auto selectedRow = selectedKind();
    const auto jobs = queue_.jobs();
    std::vector<StoredPresentationJob> batchJobs;
    if (batches_) {
        try {
            batchJobs = batches_->jobs();
        } catch (const std::exception &error) {
            status_->setText(QString::fromUtf8(error.what()));
        }
    }
    QSignalBlocker blocker(list_);
    list_->clear();
    bool finished{};
    for (const auto &job : jobs) {
        auto *row = new QTreeWidgetItem(list_);
        row->setData(0, Qt::UserRole, job.id);
        row->setData(0, kindRole, "render");
        auto state = renderJobStateName(job.state);
        if (job.state == RenderJobState::Running)
            state += " · " + queue_.progress(job.id);
        row->setText(0, state);
        row->setText(1, provenance_(job));
        row->setToolTip(1, job.documentId + "\n" + job.id);
        row->setText(2, QString::number(job.attempts));
        const auto end = pending(job.state) ? QDateTime::currentMSecsSinceEpoch() : job.updatedMs;
        row->setText(3, elapsedText(job.createdMs, end));
        finished |= !pending(job.state);
    }
    for (const auto &job : batchJobs) {
        addBatch(job);
        finished |= terminal(job.state);
    }
    // The selection survives a rebuild: the same row, or its batch, is current again.
    for (int i = 0; i < list_->topLevelItemCount(); ++i) {
        auto *row = list_->topLevelItem(i);
        if (row->data(0, Qt::UserRole).toString() == selectedId &&
            row->data(0, kindRole).toString() == selectedRow) {
            list_->setCurrentItem(row);
            break;
        }
    }
    if (!list_->currentItem() && list_->topLevelItemCount() > 0)
        list_->setCurrentItem(list_->topLevelItem(0));
    clear_->setEnabled(finished);
    selection();
}

void RenderJobsDialog::addBatch(const StoredPresentationJob &job) {
    const auto manifest = presentationManifest(job.output);
    const auto presets = manifest.value("presets").toArray();
    const int done = manifest.value("completedFrames").toInt();
    const int total = manifest.value("totalFrames").toInt();
    const bool running = job.state == PresentationJobState::Running;
    QString state;
    switch (job.state) {
    case PresentationJobState::Queued:
        state = QString("Queued · %1 frames").arg(total);
        break;
    case PresentationJobState::Running:
        state = QString("● Running · %1 of %2 frames").arg(done).arg(total);
        break;
    case PresentationJobState::Canceling:
        state = QString("● Canceling · discarding frame %1 of %2").arg(done + 1).arg(total);
        break;
    case PresentationJobState::Completed:
        state =
            QString("✓ Completed · %1 presets · %2 frames").arg(job.plan.presets.size()).arg(total);
        break;
    case PresentationJobState::Canceled:
        state = QString("■ Canceled · %1 of %2 kept, no video").arg(done).arg(total);
        break;
    case PresentationJobState::Failed:
        state = "✕ Failed · " + job.message;
        break;
    case PresentationJobState::Interrupted:
        state = "⚠ Interrupted · " + job.message;
        break;
    }
    auto *parent = new QTreeWidgetItem(list_);
    parent->setData(0, Qt::UserRole, job.id);
    parent->setData(0, kindRole, "batch");
    parent->setText(0, state);
    const auto provenance = batchProvenance_ ? batchProvenance_(job) : QString();
    parent->setText(1, job.plan.path + " · " + provenance);
    parent->setToolTip(1, job.documentId + "\n" + job.id);
    parent->setText(2, QString::number(job.attempts));
    const auto end = terminal(job.state) ? job.updatedMs : QDateTime::currentMSecsSinceEpoch();
    parent->setText(3, elapsedText(job.createdMs, end));
    parent->setExpanded(true);
    // One child row per preset, in plan order. Presets render one after another (UX_DESIGN 17.6).
    const int fps = std::max(1, job.plan.timing.framesPerSecond);
    // Presets render in order, and the manifest names a preset's state only once a frame verifies.
    // So while the batch runs, the first preset not yet complete is the one rendering.
    const bool live =
        job.state == PresentationJobState::Running || job.state == PresentationJobState::Canceling;
    int active = -1;
    for (int p = 0; live && p < int(job.plan.presets.size()) && p < presets.size(); ++p) {
        const auto entry = presets.at(p).toObject();
        if (entry.value("state").toString() != "completed") {
            active = p;
            break;
        }
    }
    for (int p = 0; p < int(job.plan.presets.size()); ++p) {
        const auto &preset = job.plan.presets[size_t(p)];
        const auto name = preset.name;
        const auto entry = p < presets.size() ? presets.at(p).toObject() : QJsonObject{};
        const auto kept = entry.value("frames").toArray().size();
        const auto planned = manifest.value("framesPerPreset").toInt();
        const auto stateName = entry.value("state").toString();
        QString text;
        bool bar = false;
        if (entry.contains("video")) {
            text = QString("%1 · video written, %2 s").arg(name).arg(double(kept) / fps, 0, 'f', 1);
        } else if (p == active) {
            text = QString("%1 · frame %2 of %3 · Running").arg(name).arg(kept + 1).arg(planned);
            bar = true;
        } else if (stateName == "completed") {
            if (!job.plan.video)
                text = QString("%1 · %2 frames finished, no video").arg(name).arg(kept);
            else if (running || job.state == PresentationJobState::Canceling)
                text = QString("%1 · encoding video (15:00 limit)").arg(name);
            else
                text = QString("%1 · video not written").arg(name);
        } else if (stateName == "queued" && !terminal(job.state)) {
            const auto before = p > 0 ? job.plan.presets[size_t(p - 1)].name : QString("start");
            text = QString("%1 · waiting for %2").arg(name, before);
        } else if (stateName == "queued") {
            text = QString("%1 · not started").arg(name);
        } else {
            text = QString("%1 · %2 of %3 frames kept").arg(name).arg(kept).arg(planned);
        }
        auto *child = new QTreeWidgetItem(parent);
        child->setData(0, Qt::UserRole, job.id);
        child->setData(0, kindRole, "preset");
        child->setText(0, text);
        if (bar) {
            auto *progress = new QProgressBar;
            progress->setRange(0, std::max(1, planned));
            progress->setValue(kept);
            progress->setTextVisible(false);
            progress->setAccessibleName(
                QString("%1, frame %2 of %3").arg(name).arg(kept + 1).arg(planned));
            list_->setItemWidget(child, 1, progress);
        } else {
            const auto device = entry.value("device").toObject();
            child->setText(1,
                           QString("%1 × %2 · %3 samples%4")
                               .arg(preset.settings.width)
                               .arg(preset.settings.height)
                               .arg(preset.settings.samples)
                               .arg(device.isEmpty() ? QString()
                                                     : " · " + device.value("backend").toString()));
        }
    }
}

void RenderJobsDialog::selection() {
    const auto id = selected();
    const auto kind = selectedKind();
    const bool batch = kind == "batch";
    openButton_->setText(batch ? "Show results…" : "Open image");
    cancel_->setText(batch ? "Cancel batch" : "Cancel");
    retry_->setText(batch ? "Resume" : "Retry capture");
    cancel_->setToolTip(batch ? "Stops the frame in progress and discards it. Finished frames stay."
                              : QString());
    retry_->setToolTip(
        batch
            ? "Continues at the first unfinished frame. Frames already kept are not rendered again."
            : QString());
    remove_->setToolTip(
        batch
            ? "Removes this job's record and log. Your frames and videos stay in the output folder."
            : QString());
    folder_->setVisible(batch);
    openButton_->setEnabled(false);
    cancel_->setEnabled(false);
    retry_->setEnabled(false);
    remove_->setEnabled(false);
    folder_->setEnabled(false);
    if (batch) {
        selectionBatch(id);
        return;
    }
    selectionRender(id);
}

void RenderJobsDialog::selectionBatch(const QString &id) {
    if (!batches_)
        return;
    for (const auto &job : batches_->jobs()) {
        if (job.id != id)
            continue;
        const bool folderExists = QFileInfo(job.output).isDir();
        openButton_->setEnabled(folderExists);
        folder_->setEnabled(folderExists);
        cancel_->setEnabled(job.state == PresentationJobState::Running);
        retry_->setEnabled(job.state == PresentationJobState::Failed ||
                           job.state == PresentationJobState::Canceled ||
                           job.state == PresentationJobState::Interrupted);
        remove_->setEnabled(terminal(job.state));
        const auto manifest = presentationManifest(job.output);
        QStringList presets;
        QStringList kept;
        QString device = "not recorded";
        for (const auto &value : manifest.value("presets").toArray()) {
            const auto entry = value.toObject();
            kept << QString("%1 %2")
                        .arg(entry.value("directory").toString())
                        .arg(entry.value("frames").toArray().size());
            if (device == "not recorded" && entry.contains("device"))
                device = entry.value("device").toObject().value("backend").toString() + " / " +
                         entry.value("device").toObject().value("id").toString();
        }
        for (const auto &preset : job.plan.presets)
            presets << QString("%1 %2 × %3 · %4 samples")
                           .arg(preset.name)
                           .arg(preset.settings.width)
                           .arg(preset.settings.height)
                           .arg(preset.settings.samples);
        const auto captured =
            QDateTime::fromMSecsSinceEpoch(job.createdMs).toLocalTime().toString(Qt::ISODate);
        const auto text =
            (batchProvenance_ ? batchProvenance_(job) : QString()) + "\nCaptured " + captured +
            "\nOutput: " + job.output + "\nPresets: " + presets.join("; ") + "\nDevice: " + device +
            "\nFrames kept: " + kept.join(", ") + "\nCapture: " + job.captureSha256.left(12) +
            "… (the model as it was at revision " + job.revision + ")\n" + job.message + "\n\n" +
            QString::fromUtf8(QJsonDocument(manifest).toJson());
        if (details_->toPlainText() != text)
            details_->setPlainText(text);
        return;
    }
    details_->clear();
}

void RenderJobsDialog::selectionRender(const QString &id) {
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
