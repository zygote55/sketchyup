#include "app/unit_display.hpp"
#include "app/window.hpp"
#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QSettings>
#include <algorithm>
namespace sketchy {
DisplayUnit Window::preferredUnits() {
    try {
        return parseDisplayUnit(QSettings("SketchyUp", "SketchyUp")
                                    .value("defaultUnits", "m")
                                    .toString()
                                    .toStdString());
    } catch (const std::exception &) {
        return DisplayUnit::Meters;
    }
}
int Window::preferredPrecision() {
    // Stored as "full" or a decimal-place count valid for the preferred units.
    const auto stored = QSettings("SketchyUp", "SketchyUp").value("defaultPrecision", "full");
    bool valid = false;
    const auto precision = stored.toString().toInt(&valid);
    return valid && validDisplayPrecision(preferredUnits(), precision) ? precision
                                                                       : fullDisplayPrecision;
}
void Window::startUnits() {
    QSettings preferences("SketchyUp", "SketchyUp");
    bool chosen = false;
    try {
        parseDisplayUnit(preferences.value("defaultUnits").toString().toStdString());
        chosen = true;
    } catch (const std::exception &) {
    }
    if (!chosen)
        unitsSettings(true);
}
void Window::unitsSettings(bool firstRun) {
    QDialog dialog(this);
    dialog.setObjectName("documentUnitsDialog");
    dialog.setWindowTitle(firstRun ? "Welcome to SketchyUp" : "Document units");
    QFormLayout form(&dialog);
    auto *hint = new QLabel(firstRun ? "Choose default units for new models."
                                     : "Choose how lengths are displayed and entered.");
    hint->setWordWrap(true);
    form.addRow(hint);
    auto *units = new QComboBox;
    units->setObjectName("documentUnitsChoice");
    for (auto unit : {DisplayUnit::Meters, DisplayUnit::Millimeters, DisplayUnit::FeetInches})
        units->addItem(unitName(unit), QString::fromLatin1(unitCode(unit).data()));
    units->setCurrentIndex(int(firstRun ? preferredUnits() : doc_.displayUnits()));
    form.addRow("Units", units);
    // Display only: precision never rounds geometry or entered values. First-run
    // setup asks only for units, so new models start at Full.
    auto *precision = new QComboBox;
    precision->setObjectName("documentPrecisionChoice");
    precision->setAccessibleName("Display precision");
    const auto populate = [precision](DisplayUnit unit, int selected) {
        precision->clear();
        precision->addItem(precisionSample(unit, fullDisplayPrecision), fullDisplayPrecision);
        for (int places = 0; places <= maxDisplayPrecision(unit); ++places)
            precision->addItem(precisionSample(unit, places), places);
        precision->setCurrentIndex(std::max(0, precision->findData(selected)));
    };
    populate(firstRun ? preferredUnits() : doc_.displayUnits(),
             firstRun ? fullDisplayPrecision : doc_.displayPrecision());
    connect(units, &QComboBox::currentIndexChanged, precision, [units, populate] {
        populate(parseDisplayUnit(units->currentData().toString().toStdString()),
                 fullDisplayPrecision);
    });
    form.addRow("Precision", precision);
    form.setRowVisible(precision, !firstRun);
    QWidget::setTabOrder(units, precision);
    auto *makeDefault = new QCheckBox("Use for new documents");
    makeDefault->setObjectName("documentUnitsDefault");
    makeDefault->setChecked(firstRun);
    makeDefault->setVisible(!firstRun);
    form.addRow(makeDefault);
    auto *note =
        new QLabel("Bare values use the chosen units (feet for feet and inches). Explicit suffixes "
                   "such as 25mm or 3' 6\" override them. Existing geometry keeps its size.");
    note->setWordWrap(true);
    note->setMaximumWidth(410);
    form.addRow(note);
    auto *error = new QLabel;
    error->setObjectName("documentUnitsError");
    error->setWordWrap(true);
    error->setTextFormat(Qt::PlainText);
    form.addRow(error);
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    form.addRow(buttons);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    const auto stamp = doc_.saveStamp();
    const auto revision = doc_.revision();
    connect(buttons, &QDialogButtonBox::accepted, &dialog, [&] {
        try {
            if (!doc_.isCurrentSnapshot(stamp) || doc_.revision() != revision)
                throw std::runtime_error("The document changed. Reopen units to try again.");
            const auto selected = parseDisplayUnit(units->currentData().toString().toStdString());
            const auto places = firstRun ? fullDisplayPrecision : precision->currentData().toInt();
            viewport_->cancel();
            // First-run defaults affect only a pristine blank document, never an opened model.
            if (firstRun && path_.isEmpty() && !doc_.dirty() && !doc_.canUndo() &&
                doc_.bodies().empty() && doc_.revision() == 0)
                doc_ = Document(selected, places);
            else if (!firstRun)
                doc_.setDisplayUnits(selected, places);
            if (makeDefault->isChecked()) {
                QSettings preferences("SketchyUp", "SketchyUp");
                preferences.setValue("defaultUnits", QString::fromLatin1(unitCode(selected).data()));
                preferences.setValue("defaultPrecision", places == fullDisplayPrecision
                                                             ? QString("full")
                                                             : QString::number(places));
            }
            viewport_->refresh();
            sync();
            dialog.accept();
        } catch (const std::exception &failure) {
            error->setText(QString::fromUtf8(failure.what()));
        }
    });
    dialog.exec();
    viewport_->setFocus();
}
} // namespace sketchy
