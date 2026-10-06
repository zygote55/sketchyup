#include "app/solar_panel.hpp"
#include <QCheckBox>
#include <QDateEdit>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QLineEdit>
#include <QPushButton>
#include <QRegularExpression>
#include <QTimeEdit>
#include <QVBoxLayout>
namespace sketchy {
namespace {
QString offsetText(int minutes) {
    return QString("%1%2:%3")
        .arg(minutes < 0 ? "-" : "+")
        .arg(std::abs(minutes) / 60, 2, 10, QChar('0'))
        .arg(std::abs(minutes) % 60, 2, 10, QChar('0'));
}
int parseOffset(const QString &text) {
    const auto match = QRegularExpression("^([+-])(\\d{2}):(\\d{2})$").match(text.trimmed());
    if (!match.hasMatch() || match.captured(3).toInt() > 59)
        throw std::runtime_error("Enter a UTC offset such as -07:00 or +05:45");
    const auto minutes = match.captured(2).toInt() * 60 + match.captured(3).toInt();
    if (minutes > 840)
        throw std::runtime_error("UTC offset must be between -14:00 and +14:00");
    return match.captured(1) == "-" ? -minutes : minutes;
}
} // namespace
SolarPanel::SolarPanel(Document &document, Viewport &viewport, QWidget *parent)
    : QWidget(parent), doc_(document), view_(viewport) {
    setObjectName("solarPanel");
    auto *layout = new QVBoxLayout(this);
    auto *note = new QLabel("Study the sun using an explicit location and time. North is model +Y "
                            "unless rotated; +X is east. All calculations work offline. Shadows "
                            "appear in textured, shaded and monochrome modes.");
    note->setWordWrap(true);
    layout->addWidget(note);
    details_ = new QLabel;
    details_->setObjectName("solarDetails");
    details_->setWordWrap(true);
    details_->setTextFormat(Qt::PlainText);
    layout->addWidget(details_);
    auto *button = new QPushButton("Edit sun study…");
    button->setObjectName("solarEditButton");
    layout->addWidget(button);
    layout->addStretch();
    setFocusProxy(button);
    connect(button, &QPushButton::clicked, this, [this] { edit(); });
    connect(&view_, &Viewport::changed, this, [this] { refresh(); });
    refresh();
}
void SolarPanel::refresh() {
    const auto &s = doc_.solar();
    const auto sun = solarPosition(s);
    details_->setText(
        QString("%1\nLatitude %2°, longitude %3°\n%4 %5 UTC%6\nNorth rotation %7°\nSun elevation "
                "%8°, azimuth %9\n%10\nShadows: %11")
            .arg(s.enabled ? "Sun lighting enabled" : "Sun lighting disabled")
            .arg(s.latitude, 0, 'f', 6)
            .arg(s.longitude, 0, 'f', 6)
            .arg(QDate(s.time.year, s.time.month, s.time.day).toString(Qt::ISODate))
            .arg(QTime(s.time.hour, s.time.minute, s.time.second).toString("HH:mm:ss"))
            .arg(offsetText(s.time.utcOffsetMinutes))
            .arg(s.northDegrees, 0, 'f', 3)
            .arg(sun.elevationDegrees, 0, 'f', 2)
            .arg(sun.azimuthDefined ? QString::number(sun.azimuthDegrees, 'f', 2) + "°"
                                    : "undefined at vertical")
            .arg(sun.aboveHorizon ? "Sun center above the geometric horizon"
                                  : "Sun center below the geometric horizon")
            .arg(s.shadows ? "enabled when sun lighting is on and above the horizon" : "off"));
}
void SolarPanel::edit() {
    const auto original = doc_.solar();
    const auto stamp = doc_.saveStamp();
    const auto revision = doc_.revision();
    QDialog dialog(this);
    dialog.setObjectName("solarDialog");
    dialog.setWindowTitle("Sun and shadows");
    dialog.resize(500, 480);
    auto *form = new QFormLayout(&dialog);
    auto flag = [&](const char *name, const char *label, bool checked) {
        auto *field = new QCheckBox(label);
        field->setObjectName(name);
        field->setChecked(checked);
        form->addRow(field);
        return field;
    };
    auto *enabled = flag("solarEnabled", "Use sun lighting", original.enabled);
    auto *shadows = flag("solarShadows", "Cast shadows", original.shadows);
    auto number = [&](const char *name, const char *label, double value) {
        auto *field = new QLineEdit(QLocale().toString(value, 'g', 15));
        field->setObjectName(name);
        form->addRow(label, field);
        return field;
    };
    auto *latitude = number("solarLatitude", "Latitude (north positive)", original.latitude);
    auto *longitude = number("solarLongitude", "Longitude (east positive)", original.longitude);
    auto *north = number("solarNorth", "North rotation (clockwise degrees)", original.northDegrees);
    const auto oldLatitude = latitude->text(), oldLongitude = longitude->text(),
               oldNorth = north->text();
    auto *date = new QDateEdit(QDate(original.time.year, original.time.month, original.time.day));
    date->setObjectName("solarDate");
    date->setCalendarPopup(true);
    date->setDisplayFormat("yyyy-MM-dd");
    date->setDateRange(QDate(1901, 1, 1), QDate(2099, 12, 31));
    form->addRow("Local date", date);
    auto *time =
        new QTimeEdit(QTime(original.time.hour, original.time.minute, original.time.second));
    time->setObjectName("solarTime");
    time->setDisplayFormat("HH:mm:ss");
    form->addRow("Local time", time);
    auto *offset = new QLineEdit(offsetText(original.time.utcOffsetMinutes));
    offset->setObjectName("solarOffset");
    form->addRow("UTC offset (±HH:MM)", offset);
    auto *hint =
        new QLabel("Enter the UTC offset in effect on this date, including daylight saving time. "
                   "For example, -07:00 or +05:45. Model files keep this exact offset.");
    hint->setWordWrap(true);
    form->addRow(hint);
    auto *error = new QLabel;
    error->setObjectName("solarDialogError");
    error->setWordWrap(true);
    error->setTextFormat(Qt::PlainText);
    form->addRow(error);
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Cancel);
    form->addRow(buttons);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, [&] {
        try {
            if (doc_.revision() != revision || !doc_.isCurrentSnapshot(stamp))
                throw std::runtime_error("The model changed. Reopen the sun editor to continue.");
            auto value = [](QLineEdit *field, const QString &before, double exact) {
                if (field->text() == before)
                    return exact;
                bool okay{};
                auto result = QLocale().toDouble(field->text(), &okay);
                if (!okay)
                    throw std::runtime_error(
                        "Latitude, longitude and north rotation require numbers");
                return result;
            };
            auto changed = original;
            changed.enabled = enabled->isChecked();
            changed.shadows = shadows->isChecked();
            changed.latitude = value(latitude, oldLatitude, original.latitude);
            changed.longitude = value(longitude, oldLongitude, original.longitude);
            changed.northDegrees = value(north, oldNorth, original.northDegrees);
            const auto d = date->date();
            const auto t = time->time();
            changed.time = {d.year(),
                            d.month(),
                            d.day(),
                            t.hour(),
                            t.minute(),
                            t.second(),
                            parseOffset(offset->text())};
            changed.validate();
            view_.applyModelSolar(changed);
            refresh();
            dialog.accept();
        } catch (const std::exception &failure) {
            error->setText(QString::fromUtf8(failure.what()));
        }
    });
    dialog.exec();
}
} // namespace sketchy
