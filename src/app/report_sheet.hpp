#pragma once
#include <QDialog>
#include <QDialogButtonBox>
#include <QLabel>
#include <QVBoxLayout>
namespace sketchy {
// Common plain-text report presentation; callers provide report-specific rows
// and explicit actions without making the report itself an editing operation.
class ReportSheet : public QDialog {
  public:
    ReportSheet(const QString &name, const QString &title, QWidget *parent = nullptr)
        : QDialog(parent) {
        setObjectName(name);
        setWindowTitle(title);
        resize(650, 480);
        auto *layout = new QVBoxLayout(this);
        summary = new QLabel;
        summary->setTextFormat(Qt::PlainText);
        summary->setWordWrap(true);
        layout->addWidget(summary);
        body = new QVBoxLayout;
        layout->addLayout(body, 1);
        buttons = new QDialogButtonBox(QDialogButtonBox::Close);
        connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
        layout->addWidget(buttons);
    }
    QLabel *summary;
    QVBoxLayout *body;
    QDialogButtonBox *buttons;
};
} // namespace sketchy
