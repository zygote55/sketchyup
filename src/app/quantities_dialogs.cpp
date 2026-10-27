#include "app/quantities_dialogs.hpp"
#include <QApplication>
#include <QClipboard>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QRegularExpression>
#include <QVBoxLayout>

namespace sketchy {

QString scheduleNameProblem(const QString &name) {
    if (name.isEmpty() || name.size() > 64)
        return "Use 1 to 64 letters, digits, spaces or . _ ( ) -, starting and ending with a "
               "letter or a digit.";
    if (!QRegularExpression(R"(\A[A-Za-z0-9](?:[A-Za-z0-9 ._()-]{0,62}[A-Za-z0-9])?\z)")
             .match(name)
             .hasMatch())
        return "Use 1 to 64 letters, digits, spaces or . _ ( ) -, starting and ending with a "
               "letter or a digit.";
    return {};
}

ScheduleNameDialog::ScheduleNameDialog(QWidget *parent, bool rename, const QString &current,
                                       const std::set<QString> &taken, const QStringList &stored,
                                       const QString &limitNote)
    : QDialog(parent), taken_(taken) {
    setObjectName("scheduleNameDialog");
    setWindowTitle(rename ? "Rename schedule" : "Save as schedule");
    setModal(true);
    auto *layout = new QVBoxLayout(this);
    auto *title = new QLabel(rename ? "Rename schedule" : "Save as schedule");
    title->setStyleSheet("font-weight: bold;");
    layout->addWidget(title);
    auto *row = new QFormLayout;
    name_ = new QLineEdit(current);
    name_->setObjectName("scheduleName");
    name_->setAccessibleName("Schedule name");
    name_->setMaxLength(64);
    row->addRow("Name", name_);
    layout->addLayout(row);
    count_ = new QLabel;
    count_->setObjectName("scheduleNameCount");
    layout->addWidget(count_);
    problem_ = new QLabel;
    problem_->setObjectName("scheduleNameProblem");
    problem_->setWordWrap(true);
    layout->addWidget(problem_);
    if (!stored.isEmpty()) {
        auto *heading = new QLabel("Stored with the name");
        heading->setStyleSheet("font-weight: bold;");
        layout->addWidget(heading);
        auto *list = new QLabel(stored.join("\n"));
        list->setObjectName("scheduleStored");
        list->setWordWrap(true);
        layout->addWidget(list);
    }
    auto *notStored = new QLabel("Not stored: the selection. A schedule runs without it, from the "
                                 "file and the command line alike.");
    notStored->setObjectName("scheduleNotStored");
    notStored->setWordWrap(true);
    layout->addWidget(notStored);
    if (!limitNote.isEmpty()) {
        auto *limit = new QLabel(limitNote);
        limit->setObjectName("scheduleLimit");
        limit->setWordWrap(true);
        layout->addWidget(limit);
    }
    error_ = new QLabel;
    error_->setObjectName("scheduleDialogError");
    error_->setWordWrap(true);
    layout->addWidget(error_);
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Cancel);
    // Cancel is never the Enter button: with Save disabled, Enter does nothing (§19.10).
    auto *cancel = buttons->button(QDialogButtonBox::Cancel);
    cancel->setAutoDefault(false);
    cancel->setFocusPolicy(Qt::StrongFocus);
    save_ = buttons->addButton(rename ? "Rename" : "Save", QDialogButtonBox::AcceptRole);
    save_->setObjectName("scheduleSave");
    save_->setDefault(true);
    save_->setAutoDefault(true);
    layout->addWidget(buttons);
    // Tab walks the name, Cancel, then Save, in the order the buttons are read.
    setTabOrder(name_, cancel);
    setTabOrder(cancel, save_);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(save_, &QPushButton::clicked, this, &QDialog::accept);
    connect(name_, &QLineEdit::textChanged, this, [this] { check(); });
    check();
    name_->setFocus();
    name_->selectAll();
}

QString ScheduleNameDialog::name() const { return name_->text(); }

void ScheduleNameDialog::showError(const QString &text) { error_->setText(text); }

void ScheduleNameDialog::check() {
    const auto name = this->name();
    count_->setText(QString("%1 of 64 characters").arg(name.size()));
    count_->setAccessibleName(QString("Schedule name, %1 of 64 characters").arg(name.size()));
    auto problem = scheduleNameProblem(name);
    // taken_ holds the other schedules' names: a rename keeps the name it already has.
    if (problem.isEmpty() && taken_.contains(name))
        problem =
            QString("A schedule named \"%1\" already exists for this model. Choose another name.")
                .arg(name);
    problem_->setText(problem);
    save_->setEnabled(problem.isEmpty());
    error_->clear();
}

bool confirmScheduleDelete(QWidget *parent, const QString &name) {
    QMessageBox box(parent);
    box.setObjectName("scheduleDeleteDialog");
    box.setIcon(QMessageBox::Question);
    box.setWindowTitle("Delete schedule");
    box.setText(QString("Delete the schedule %1?").arg(name));
    box.setInformativeText("Its saved filters are removed from this computer. The model is not "
                           "changed. Reports you exported earlier are not changed.");
    auto *remove = box.addButton("Delete", QMessageBox::DestructiveRole);
    remove->setObjectName("scheduleDeleteConfirm");
    auto *cancel = box.addButton("Cancel", QMessageBox::RejectRole);
    box.setDefaultButton(cancel);
    box.setEscapeButton(cancel);
    box.exec();
    return box.clickedButton() == remove;
}

ExportReportDialog::ExportReportDialog(QWidget *parent, const QString &path,
                                       const QStringList &written,
                                       const std::vector<ExportAttention> &attention,
                                       const QStringList &notices)
    : QDialog(parent) {
    setObjectName("exportReportDialog");
    setWindowTitle("Export report");
    auto *layout = new QVBoxLayout(this);
    auto *banner = new QLabel("✓ Written. The model is unchanged.");
    banner->setObjectName("exportBanner");
    banner->setStyleSheet("font-weight: bold;");
    layout->addWidget(banner);
    auto *columns = new QHBoxLayout;
    auto *left = new QLabel(written.join("\n"));
    left->setObjectName("exportWritten");
    left->setWordWrap(true);
    left->setAccessibleName("What was written");
    columns->addWidget(left, 1);
    auto *right = new QVBoxLayout;
    auto *heading = new QLabel("Needs your attention");
    heading->setStyleSheet("font-weight: bold;");
    right->addWidget(heading);
    if (attention.empty()) {
        auto *none = new QLabel("No not-solids in this report.");
        none->setObjectName("exportNoAttention");
        right->addWidget(none);
    }
    for (std::size_t i = 0; i < attention.size(); ++i) {
        const auto &item = attention[i];
        auto *words = new QLabel(QString("⚠ %1   %2").arg(item.words).arg(item.count));
        words->setObjectName(QString("exportAttention%1").arg(i));
        words->setWordWrap(true);
        words->setAccessibleName(QString("Warning: %1, %2").arg(item.words).arg(item.count));
        right->addWidget(words);
        auto *detail = new QLabel(item.detail);
        detail->setWordWrap(true);
        right->addWidget(detail);
        if (!item.bodies.empty()) {
            auto *select = new QPushButton("Select in model");
            select->setObjectName(QString("exportSelect%1").arg(i));
            right->addWidget(select, 0, Qt::AlignLeft);
            connect(select, &QPushButton::clicked, this, [this, bodies = item.bodies] {
                selected_ = bodies;
                accept();
            });
        }
    }
    if (!notices.isEmpty()) {
        auto *notice = new QLabel("Notices");
        notice->setStyleSheet("font-weight: bold;");
        right->addWidget(notice);
        for (const auto &line : notices) {
            auto *label = new QLabel(line);
            label->setWordWrap(true);
            label->setObjectName("exportNotice");
            right->addWidget(label);
        }
    }
    right->addStretch(1);
    columns->addLayout(right, 1);
    layout->addLayout(columns);
    auto *buttons = new QDialogButtonBox;
    auto *copy = buttons->addButton("Copy path", QDialogButtonBox::ActionRole);
    copy->setObjectName("exportCopyPath");
    connect(copy, &QPushButton::clicked, this,
            [path] { QApplication::clipboard()->setText(path); });
    auto *close = buttons->addButton("Close", QDialogButtonBox::AcceptRole);
    close->setObjectName("exportClose");
    close->setDefault(true);
    connect(close, &QPushButton::clicked, this, &QDialog::accept);
    layout->addWidget(buttons);
}

} // namespace sketchy
