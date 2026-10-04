#include "app/history_panel.hpp"
#include "automation/commands.hpp"
#include <QHBoxLayout>
#include <QHeaderView>
#include <QTimer>
#include <QVBoxLayout>
namespace sketchy {
HistoryPanel::HistoryPanel(Document &doc, Viewport &view, QWidget *parent)
    : QWidget(parent), doc_(doc), view_(view) {
    setObjectName("historyPanel");
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(4, 4, 4, 4);
    summary_ = new QLabel;
    summary_->setObjectName("historySummary");
    summary_->setWordWrap(true);
    summary_->setTextFormat(Qt::PlainText);
    layout->addWidget(summary_);
    auto *controls = new QHBoxLayout;
    undo_ = new QPushButton("Undo");
    undo_->setObjectName("historyUndo");
    redo_ = new QPushButton("Redo");
    redo_->setObjectName("historyRedo");
    controls->addWidget(undo_);
    controls->addWidget(redo_);
    layout->addLayout(controls);
    connect(undo_, &QPushButton::clicked, this, [this] {
        if (page_.position)
            navigate(page_.position - 1);
    });
    connect(redo_, &QPushButton::clicked, this, [this] {
        if (page_.position < page_.total)
            navigate(page_.position + 1);
    });
    steps_ = new QTreeWidget;
    steps_->setObjectName("historySteps");
    steps_->setRootIsDecorated(false);
    steps_->setHeaderLabels({"Action", "State"});
    steps_->setSelectionMode(QAbstractItemView::SingleSelection);
    steps_->header()->setSectionResizeMode(0, QHeaderView::Stretch);
    steps_->header()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    layout->addWidget(steps_, 1);
    setFocusProxy(steps_);
    connect(steps_, &QTreeWidget::itemClicked, this, [this](QTreeWidgetItem *item) {
        if (!syncing_)
            navigate(item->data(0, Qt::UserRole).toULongLong());
    });
    connect(steps_, &QTreeWidget::itemActivated, this, [this](QTreeWidgetItem *item) {
        if (!syncing_)
            navigate(item->data(0, Qt::UserRole).toULongLong());
    });
    connect(steps_, &QTreeWidget::itemSelectionChanged, this, [this] {
        if (!syncing_)
            details();
    });
    auto *pages = new QHBoxLayout;
    older_ = new QPushButton("Older steps");
    older_->setObjectName("historyOlder");
    newer_ = new QPushButton("Newer steps");
    newer_->setObjectName("historyNewer");
    pages->addWidget(older_);
    pages->addWidget(newer_);
    layout->addLayout(pages);
    connect(older_, &QPushButton::clicked, this, [this] {
        offset_ -= std::min(offset_, size_t(200));
        refresh(true);
    });
    connect(newer_, &QPushButton::clicked, this, [this] {
        offset_ = std::min(offset_ + 200, page_.total);
        refresh(true);
    });
    details_ = new QPlainTextEdit;
    details_->setObjectName("historyDetails");
    details_->setReadOnly(true);
    details_->setMaximumHeight(130);
    layout->addWidget(details_);
    error_ = new QLabel;
    error_->setObjectName("historyError");
    error_->setWordWrap(true);
    error_->setTextFormat(Qt::PlainText);
    layout->addWidget(error_);
    error_->hide();
    refresh();
}
void HistoryPanel::refresh(bool force) {
    if (!force && doc_.owns(stamp_) && revision_ == doc_.revision())
        return;
    const auto summary = doc_.history(0, 1);
    if (!force || !doc_.owns(stamp_) || doc_.revision() != revision_) {
        if (summary.position < offset_ || summary.position > offset_ + 200 || !doc_.owns(stamp_))
            offset_ = summary.position > 150 ? summary.position - 150 : 0;
    }
    offset_ = std::min(offset_, summary.total);
    page_ = doc_.history(offset_, 200);
    stamp_ = doc_.saveStamp();
    revision_ = doc_.revision();
    syncing_ = true;
    steps_->clear();
    auto row = [&](size_t position, const QString &label, bool applied, bool saved) {
        auto *item = new QTreeWidgetItem(steps_);
        item->setData(0, Qt::UserRole, qulonglong(position));
        item->setText(0, label);
        item->setToolTip(0, label);
        item->setText(1, position == page_.position ? (saved ? "Current · Saved" : "Current")
                         : saved                    ? "Saved"
                         : applied                  ? "Applied"
                                                    : "Redo");
        if (!applied)
            item->setForeground(0, palette().brush(QPalette::Disabled, QPalette::Text));
        if (position == page_.position) {
            auto font = item->font(0);
            font.setBold(true);
            item->setFont(0, font);
            steps_->setCurrentItem(item);
        }
        return item;
    };
    row(0, page_.pruned ? "Retained baseline (older steps unavailable)" : "Starting state", true,
        page_.baseSaved);
    for (const auto &entry : page_.entries)
        row(entry.position,
            (entry.metadata.assistant ? "AI · " : "") + QString::fromStdString(entry.label),
            entry.applied, entry.saved);
    if (!steps_->currentItem() && steps_->topLevelItemCount() > 1)
        steps_->setCurrentItem(steps_->topLevelItem(1));
    summary_->setText(
        QString("Step %1 of %2 · %3 MiB retained\nClick a step, or select it and press Enter.")
            .arg(page_.position)
            .arg(page_.total)
            .arg(double(page_.bytes) / (1024 * 1024), 0, 'f', 1));
    undo_->setEnabled(page_.position > 0);
    redo_->setEnabled(page_.position < page_.total);
    older_->setEnabled(offset_ > 0);
    newer_->setEnabled(offset_ + page_.entries.size() < page_.total);
    syncing_ = false;
    details();
}
void HistoryPanel::details() {
    const auto *item = steps_->currentItem();
    if (!item) {
        details_->clear();
        return;
    }
    const auto position = item->data(0, Qt::UserRole).toULongLong();
    if (!position) {
        details_->setPlainText(
            page_.pruned ? "Earlier steps were discarded to keep history within its limits."
                         : "History begins at this document's starting or opened state.");
        return;
    }
    for (const auto &entry : page_.entries)
        if (entry.position == position) {
            QString text = QString::fromStdString(entry.label);
            if (!entry.metadata.taskId.empty())
                text += "\nTask: " + QString::fromStdString(entry.metadata.taskId);
            if (!entry.metadata.request.empty())
                text += "\n\nRequest:\n" + QString::fromStdString(entry.metadata.request);
            details_->setPlainText(text);
            return;
        }
}
void HistoryPanel::navigate(size_t position) {
    if (pending_)
        return;
    const auto stamp = stamp_;
    const auto revision = revision_;
    pending_ = true;
    // Qt may still be dispatching an item event. Refresh only after its delegate returns.
    QTimer::singleShot(0, this, [this, position, stamp, revision] {
        pending_ = false;
        try {
            if (!doc_.isCurrentSnapshot(stamp) || doc_.revision() != revision)
                throw std::runtime_error("History changed. Choose a step from the refreshed list.");
            view_.cancel();
            executeHistory(doc_, {{"apiVersion", 1},
                                  {"documentId", QString::fromStdString(doc_.identity())},
                                  {"expectedRevision", QString::number(revision)},
                                  {"position", QString::number(position)}});
            error_->clear();
            error_->hide();
            view_.refresh();
            QMetaObject::invokeMethod(&view_, "changed");
            refresh();
            view_.setFocus(Qt::OtherFocusReason);
        } catch (const std::exception &error) {
            refresh();
            error_->setText(QString::fromUtf8(error.what()));
            error_->show();
        }
    });
}
} // namespace sketchy
