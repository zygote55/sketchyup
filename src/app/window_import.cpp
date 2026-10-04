#include "app/window.hpp"
#include "io/formline.hpp"
#include <QDialog>
#include <QDialogButtonBox>
#include <QLabel>
#include <QMap>
#include <QPlainTextEdit>
#include <QVBoxLayout>
namespace sketchy {
void Window::importFormlinePath(const QString &path) {
    // Parse and validate privately before asking to replace the current document.
    auto imported = loadFormline(path);
    if (!canReplace())
        return;
    doc_ = std::move(imported.document);
    path_.clear(); // Save must choose a new native file, never overwrite the source.
    viewport_->cancel();
    viewport_->setSelection(0);
    viewport_->fit();
    sync();
    status_->setText("Imported an unsaved native model · Source file unchanged");
    QDialog report(this);
    report.setObjectName("formlineImportReport");
    report.setWindowTitle("Formline import report");
    report.resize(560, 420);
    auto *layout = new QVBoxLayout(&report);
    auto *summary =
        new QLabel(QString("Imported %1 objects into a new unsaved model.\n"
                           "Meters; Y-up converted to Z-up. Cylinders retain 48 sides.\n"
                           "Open the imported group to edit faces. Save to a new .sketchyup file; "
                           "the source is unchanged.")
                       .arg(imported.report.value("objects").toInt()));
    summary->setObjectName("formlineImportSummary");
    summary->setTextFormat(Qt::PlainText);
    summary->setWordWrap(true);
    layout->addWidget(summary);
    auto *details = new QPlainTextEdit;
    details->setObjectName("formlineImportDetails");
    details->setReadOnly(true);
    QString text = QString("Boxes: %1\nCylinders: %2\nIn-model colors: %3\n\n"
                           "Preserved: names, visibility, position, rotation and source identity.")
                       .arg(imported.report.value("boxes").toInt())
                       .arg(imported.report.value("cylinders").toInt())
                       .arg(doc_.materials().size());
    QMap<QString, QString> names;
    for (const auto &[id, body] : doc_.bodies())
        if (body->properties.contains("formline.sourceId"))
            names[QString::fromStdString(std::get<std::string>(
                body->properties.at("formline.sourceId")))] = QString::fromStdString(body->name);
    const auto warnings = imported.report.value("warnings").toArray();
    text += warnings.isEmpty() ? "\n\nNo conversion notices." : "\n\nConversion notices:";
    for (const auto value : warnings) {
        const auto warning = value.toObject();
        const auto name = names.value(warning.value("sourceId").toString());
        text += "\n• " + (name.isEmpty() ? QString{} : name + ": ") +
                warning.value("message").toString();
        if (warning.contains("unusedDepth"))
            text += QString(" Depth: %1 m; diameter: %2 m.")
                        .arg(warning.value("unusedDepth").toDouble())
                        .arg(warning.value("diameter").toDouble());
        if (warning.contains("count"))
            text += QString(" Count: %1.").arg(warning.value("count").toInt());
    }
    details->setPlainText(text);
    layout->addWidget(details, 1);
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Close);
    connect(buttons, &QDialogButtonBox::rejected, &report, &QDialog::reject);
    layout->addWidget(buttons);
    report.exec();
}
} // namespace sketchy
