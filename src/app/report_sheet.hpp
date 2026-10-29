#pragma once
#include <QByteArray>
#include <QDialog>
#include <QDialogButtonBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QSaveFile>
#include <QString>
#include <QWidget>
#include <QVBoxLayout>
#include <stdexcept>
namespace sketchy {
// Writes a report's bytes to `path` atomically, for Save report… (UX_DESIGN 16.5, 18.5). The file
// dialog that chose `path` has already confirmed any overwrite. Throws if the file cannot be written.
inline void writeReportBytes(const QString &path, const QByteArray &bytes) {
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly))
        throw std::runtime_error(file.errorString().toStdString());
    if (file.write(bytes) != bytes.size() || !file.commit())
        throw std::runtime_error(file.errorString().toStdString());
}
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
        footer = new QWidget;
        auto *footerLayout = new QHBoxLayout(footer);
        footerLayout->setContentsMargins(0, 0, 0, 0);
        layout->addWidget(footer);
        buttons = new QDialogButtonBox(QDialogButtonBox::Close);
        connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
        layout->addWidget(buttons);
    }
    QLabel *summary;
    QVBoxLayout *body;
    // A footer row for explicit actions (an OK that accepts); hide buttons() for a sheet that
    // owns its own footer button.
    QWidget *footer;
    QDialogButtonBox *buttons;
};
} // namespace sketchy
