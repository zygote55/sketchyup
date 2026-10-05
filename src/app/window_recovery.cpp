#include "app/inspection_service.hpp"
#include "app/render_panel.hpp"
#include "app/window.hpp"
#include <QAction>
#include <QCheckBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QFileInfo>
#include <QHeaderView>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSettings>
#include <QSpinBox>
#include <QStandardPaths>
#include <QVBoxLayout>
namespace sketchy {
Window::~Window() {
    delete render_;
    delete recovery_;
}
void Window::resetRecoveryContext() {
    recoveryContext_ = {};
    recoveredName_.clear();
    saveFailure_.clear();
    saveBanner_->hide();
}
void Window::clearRecovery() {
    if (!recovery_)
        return;
    const auto error = recovery_->discard();
    if (!error.isEmpty())
        status_->setText("Recovery cleanup failed: " + error);
}
void Window::startRecovery(const QString &root) {
    if (recovery_)
        return;
    const auto directory =
        root.isEmpty() ? QDir(QStandardPaths::writableLocation(QStandardPaths::AppDataLocation))
                             .filePath("recovery")
                       : root;
    recovery_ = new RecoveryController(doc_, directory, [this] { return recoveryContext_; }, this);
    recovery_->setObjectName("recoveryController");
    connect(recovery_, &RecoveryController::changed, this, &Window::syncRecovery);
    recovery_->setInterval(
        QSettings("SketchyUp", "SketchyUp").value("recoverySeconds", 30).toInt());
    recoveryStatus_->show();
    syncRecovery();
}
void Window::syncRecovery() {
    if (!recovery_)
        return;
    const auto &durable = recovery_->durable();
    QString text;
    if (!recovery_->error().isEmpty())
        text = "Recovery failed · ";
    else if (recovery_->busy())
        text = "Saving recovery… · ";
    else if (!recovery_->interval())
        text = "Automatic recovery off · ";
    if (!doc_.dirty()) {
        text += "No unsaved edits";
        recoveryStatus_->setToolTip(recovery_->error());
    } else if (durable && durable->documentId == QString::fromStdString(doc_.identity())) {
        text += "Last verified " + durable->capturedAt.toLocalTime().toString("HH:mm:ss");
        const auto newer =
            doc_.revision() >= durable->revision ? doc_.revision() - durable->revision : 0;
        if (newer)
            text += QString(" · %1 newer changes in memory").arg(newer);
        recoveryStatus_->setToolTip(
            QString("Verified recovery revision %1 of current revision %2.\n%3")
                .arg(durable->revision)
                .arg(doc_.revision())
                .arg(recovery_->error()));
    } else {
        text += doc_.dirty() ? "Edits not yet protected by recovery" : "No unsaved edits";
        recoveryStatus_->setToolTip(recovery_->error());
    }
    if (!recovery_->error().isEmpty())
        text += " · Retry with File → Save recovery now";
    recoveryStatus_->setText(text);
    if (auto *action = findChild<QAction *>("file.recoveryNow"))
        action->setEnabled(!recovery_->busy());
}
void Window::recoverySettings() {
    if (!recovery_)
        startRecovery();
    QDialog dialog(this);
    dialog.setObjectName("recoverySettings");
    dialog.setWindowTitle("Recovery settings");
    auto *layout = new QVBoxLayout(&dialog);
    auto *enabled = new QCheckBox("Keep automatic recovery copies");
    enabled->setObjectName("recoveryEnabled");
    enabled->setChecked(recovery_->interval() != 0);
    layout->addWidget(enabled);
    auto *interval = new QSpinBox;
    interval->setObjectName("recoveryInterval");
    interval->setRange(5, 3600);
    interval->setSuffix(" seconds");
    interval->setValue(recovery_->interval() ? recovery_->interval() : 30);
    interval->setEnabled(enabled->isChecked());
    connect(enabled, &QCheckBox::toggled, interval, &QSpinBox::setEnabled);
    layout->addWidget(interval);
    auto *hint = new QLabel(
        "Recovery never overwrites your saved file. New edits remain in memory until the next "
        "successful recovery copy. A write failure pauses automatic attempts until you retry.");
    hint->setWordWrap(true);
    layout->addWidget(hint);
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    layout->addWidget(buttons);
    if (dialog.exec() == QDialog::Accepted) {
        const int seconds = enabled->isChecked() ? interval->value() : 0;
        QSettings("SketchyUp", "SketchyUp").setValue("recoverySeconds", seconds);
        recovery_->setInterval(seconds);
    }
}
void Window::showRecovery(bool onlyIfPresent) {
    if (!recovery_)
        startRecovery();
    auto candidates = listRecoveries(recovery_->root());
    if (onlyIfPresent && candidates.empty())
        return;
    QDialog dialog(this);
    dialog.setObjectName("recoveryDialog");
    dialog.setWindowTitle("Recover work");
    dialog.resize(760, 430);
    auto *layout = new QVBoxLayout(&dialog);
    auto *summary = new QLabel("Choose a verified recovery copy. Recovered work opens as Edited "
                               "and saves to a new file. Your saved file stays untouched.");
    summary->setWordWrap(true);
    layout->addWidget(summary);
    auto *list = new QTreeWidget;
    list->setObjectName("recoveryList");
    list->setHeaderLabels({"Model", "Last explicit save", "Recovery copy", "Changes"});
    list->setRootIsDecorated(false);
    list->setSelectionMode(QAbstractItemView::SingleSelection);
    list->header()->setSectionResizeMode(QHeaderView::ResizeToContents);
    layout->addWidget(list, 1);
    auto *details = new QPlainTextEdit;
    details->setObjectName("recoveryDetails");
    details->setReadOnly(true);
    details->setMaximumHeight(130);
    layout->addWidget(details);
    auto *buttons = new QDialogButtonBox;
    auto *open = buttons->addButton("Open recovered version", QDialogButtonBox::ActionRole);
    auto *saved = buttons->addButton("Open last saved", QDialogButtonBox::ActionRole);
    auto *discard = buttons->addButton("Discard recovery data", QDialogButtonBox::DestructiveRole);
    open->setObjectName("openRecovered");
    saved->setObjectName("openLastSaved");
    discard->setObjectName("discardRecovery");
    buttons->addButton(QDialogButtonBox::Close);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    layout->addWidget(buttons);
    auto selection = [&]() -> RecoveryRead * {
        const auto *item = list->currentItem();
        if (!item)
            return nullptr;
        const auto index = item->data(0, Qt::UserRole).toUInt();
        return index < candidates.size() ? &candidates[index] : nullptr;
    };
    auto update = [&] {
        auto *candidate = selection();
        open->setEnabled(candidate && candidate->verified);
        saved->setEnabled(candidate && candidate->verified &&
                          !candidate->info.sourcePath.isEmpty());
        discard->setEnabled(candidate != nullptr);
        if (!candidate) {
            details->setPlainText("No inactive recovery copies found.");
            return;
        }
        QString text = candidate->verified
                           ? QString("Verified revision %1. ").arg(candidate->info.revision)
                           : "No verified model could be recovered. ";
        if (candidate->incompleteTail)
            text += "An interrupted journal tail was ignored. ";
        if (!candidate->issue.isEmpty())
            text +=
                "Recovery stopped: " + candidate->issue + ". Original recovery data is preserved.";
        if (!candidate->missingAssets.isEmpty())
            text += "\nMissing resources: " + candidate->missingAssets.join(", ");
        if (!candidate->info.sourcePath.isEmpty())
            text += "\nSaved file: " + candidate->info.sourcePath;
        details->setPlainText(text);
    };
    auto populate = [&] {
        list->clear();
        for (size_t i = 0; i < candidates.size(); ++i) {
            const auto &candidate = candidates[i];
            const auto &meta = candidate.info;
            auto *item = new QTreeWidgetItem(list);
            item->setData(0, Qt::UserRole, unsigned(i));
            item->setText(0, meta.sourcePath.isEmpty()
                                 ? (candidate.verified ? "Untitled" : "Unreadable recovery")
                                 : QFileInfo(meta.sourcePath).fileName());
            item->setText(1, meta.savedAt.isValid()
                                 ? meta.savedAt.toLocalTime().toString("yyyy-MM-dd HH:mm:ss")
                                 : "Never saved");
            item->setText(2, candidate.verified
                                 ? meta.capturedAt.toLocalTime().toString("yyyy-MM-dd HH:mm:ss")
                                 : "Unverified");
            item->setText(3, candidate.verified
                                 ? QString::number(meta.revision - meta.savedRevision.value_or(0))
                                 : "—");
        }
        if (list->topLevelItemCount())
            list->setCurrentItem(list->topLevelItem(0));
        update();
    };
    connect(list, &QTreeWidget::itemSelectionChanged, &dialog, update);
    connect(open, &QPushButton::clicked, &dialog, [&] {
        auto *candidate = selection();
        if (!candidate)
            return;
        auto current = readRecovery(recovery_->root(), candidate->info.key);
        if (!current.document || !current.verified) {
            details->setPlainText("Cannot open recovery: " + current.issue);
            return;
        }
        if (!canReplace())
            return;
        doc_ = std::move(*current.document);
        path_.clear();
        resetRecoveryContext();
        recoveredName_ = current.info.sourcePath.isEmpty()
                             ? "Recovered Untitled"
                             : "Recovered " + QFileInfo(current.info.sourcePath).fileName();
        recoveryContext_ = {current.info.sourcePath, current.info.savedRevision,
                            current.info.savedAt};
        recovery_->adopt(current.info);
        viewport_->cancel();
        viewport_->setSelection(0);
        viewport_->fit();
        sync();
        status_->setText("Recovered verified work · Save to a new native file");
        dialog.accept();
    });
    connect(saved, &QPushButton::clicked, &dialog, [&] {
        auto *candidate = selection();
        if (!candidate)
            return;
        try {
            auto loaded = loadDocument(candidate->info.sourcePath);
            if (QString::fromStdString(loaded.identity()) != candidate->info.documentId)
                throw std::runtime_error("The saved path now belongs to a different model");
            if (!canReplace())
                return;
            doc_ = std::move(loaded);
            resetRecoveryContext();
            path_ = candidate->info.sourcePath;
            recoveryContext_ = {path_, doc_.revision(), QFileInfo(path_).lastModified().toUTC()};
            rememberPath(path_);
            viewport_->cancel();
            viewport_->setSelection(0);
            viewport_->fit();
            sync();
            dialog.accept();
        } catch (const std::exception &error) {
            details->setPlainText("Cannot open saved file: " + QString::fromUtf8(error.what()));
        }
    });
    connect(discard, &QPushButton::clicked, &dialog, [&] {
        auto *candidate = selection();
        if (!candidate)
            return;
        try {
            discardRecovery(recovery_->root(), candidate->info.key);
            candidates = listRecoveries(recovery_->root());
            populate();
        } catch (const std::exception &error) {
            details->setPlainText("Cannot discard recovery: " + QString::fromUtf8(error.what()));
        }
    });
    populate();
    dialog.exec();
}
} // namespace sketchy
