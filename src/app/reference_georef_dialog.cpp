#include "app/reference_georef_dialog.hpp"

#include "app/viewport.hpp"
#include "io/image_refusal.hpp"

#include <QAccessible>
#include <QAccessibleEvent>
#include <QClipboard>
#include <QComboBox>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLabel>
#include <QLineEdit>
#include <QLocale>
#include <QPlainTextEdit>
#include <QProgressBar>
#include <QPushButton>
#include <QRadioButton>
#include <QThread>
#include <QTimer>
#include <QVBoxLayout>

#include <atomic>
#include <cmath>
#include <cstdlib>

namespace sketchy {
namespace {

// The codes a placement can refuse. Each is about the zone, the elevation, the credit or the model, so the
// file itself is usable and the settings stay open. Every other code is about the file (UX §21.3).
bool placementCode(const std::string &code) {
    return code == "zone-mismatch" || code == "zone-band" || code == "zone-exception" || code == "latitude" ||
           code == "elevation" || code == "credit" || code == "reopen";
}

std::string zoneLabelOf(int zone, bool north) {
    return std::to_string(zone) + (north ? "N" : "S");
}

QString number(double value, int decimals) {
    return QLocale().toString(value, 'f', decimals);
}

// A length in metres: "0.500 m". A pixel smaller than a millimetre keeps four decimals.
QString metres(double value) {
    return number(value, value < 0.01 ? 4 : 3) + " m";
}

// Degrees and hemisphere: "35.0000° N, 119.0000° W".
QString latLon(const GeoPosition &position) {
    return QString::number(std::abs(position.latitude), 'f', 4) + "° " + (position.latitude < 0 ? "S" : "N") +
           ", " + QString::number(std::abs(position.longitude), 'f', 4) + "° " +
           (position.longitude < 0 ? "W" : "E");
}

// A grid corner: "E 361,789.4  N 3,819,654.4".
QString grid(double easting, double northing) {
    return "E " + number(easting, 1) + "  N " + number(northing, 1);
}

QString mebibytes(std::uint64_t bytes) {
    return QString::number(double(bytes) / (1024.0 * 1024.0), 'f', 1) + " MiB";
}

QString formatName(OrthophotoFormat format) {
    switch (format) {
    case OrthophotoFormat::Png:
        return "PNG";
    case OrthophotoFormat::Jpeg:
        return "JPEG";
    case OrthophotoFormat::GeoTiff:
        return "GeoTIFF";
    }
    return {};
}

} // namespace

struct ReferenceGeorefDialog::ReadState {
    std::atomic<int> percent{0};
    std::optional<OrthophotoFile> file;
    std::optional<Refusal> refusal;
};

ReferenceGeorefDialog::ReferenceGeorefDialog(Document &document, Viewport &view, const QString &path,
                                             QWidget *parent)
    : QDialog(parent), doc_(document), view_(view), path_(path) {
    setWindowTitle("Place georeferenced image");
    setObjectName("referenceGeoreferenceDialog");
    resize(640, 560);
    const auto fileName = QFileInfo(path).fileName();
    const auto stem = QFileInfo(path).completeBaseName();
    const auto &site = doc_.site();

    auto *layout = new QVBoxLayout(this);
    banner_ = new QLabel;
    banner_->setObjectName("referenceGeoBanner");
    banner_->setTextFormat(Qt::PlainText);
    banner_->setWordWrap(true);
    banner_->hide();
    layout->addWidget(banner_);

    // The settings, in the Tab order of UX §21.5 (Name first, so the reading order and the Tab order agree).
    auto *form = new QFormLayout;
    image_ = new QLabel("Reading " + fileName + "…");
    image_->setObjectName("referenceGeoImage");
    image_->setTextFormat(Qt::PlainText);
    image_->setWordWrap(true);
    form->addRow("Image", image_);
    position_ = new QLabel;
    position_->setObjectName("referenceGeoPosition");
    position_->setTextFormat(Qt::PlainText);
    position_->setWordWrap(true);
    form->addRow("Position", position_);

    name_ = new QLineEdit(stem);
    name_->setObjectName("referenceGeoName");
    name_->setAccessibleName("Name of the reference image");
    form->addRow("Name", name_);

    zone_ = new QComboBox;
    zone_->setObjectName("referenceGeoZone");
    zone_->setAccessibleName("Zone of the image, 1 to 60 with N or S");
    zone_->addItem("Choose the zone…", 0);
    for (int zone = 1; zone <= 60; ++zone)
        zone_->addItem(QString("%1N").arg(zone), zone);
    for (int zone = 1; zone <= 60; ++zone)
        zone_->addItem(QString("%1S").arg(zone), -zone);
    zoneFixed_ = new QLabel;
    zoneFixed_->setObjectName("referenceGeoZoneFixed");
    zoneFixed_->setTextFormat(Qt::PlainText);
    auto *zoneRow = new QHBoxLayout;
    zoneRow->addWidget(zone_);
    zoneRow->addWidget(zoneFixed_, 1);
    form->addRow("Zone", zoneRow);

    // The site's CRS label is the default zone when it has one. It is a default only: it is not used to place.
    if (site && site->crs.rfind("EPSG:32", 0) == 0) {
        const auto code = std::atoi(site->crs.c_str() + 5);
        if (code >= 32601 && code <= 32660)
            zone_->setCurrentIndex(zone_->findData(code - 32600));
        else if (code >= 32701 && code <= 32760)
            zone_->setCurrentIndex(zone_->findData(-(code - 32700)));
    }

    wgs84_ = new QRadioButton("WGS84");
    wgs84_->setObjectName("referenceGeoDatumWgs84");
    wgs84_->setAccessibleName("Datum, WGS84");
    wgs84_->setChecked(true);
    nad83_ = new QRadioButton("NAD83, labelled approximation");
    nad83_->setObjectName("referenceGeoDatumNad83");
    nad83_->setAccessibleName("Datum, NAD83 as a labelled approximation");
    datumFixed_ = new QLabel;
    datumFixed_->setObjectName("referenceGeoDatumFixed");
    datumFixed_->setTextFormat(Qt::PlainText);
    datumCaption_ = new QLabel;
    datumCaption_->setObjectName("referenceGeoDatumCaption");
    datumCaption_->setTextFormat(Qt::PlainText);
    datumCaption_->setWordWrap(true);
    auto *datumChoice = new QHBoxLayout;
    datumChoice->addWidget(wgs84_);
    datumChoice->addWidget(nad83_);
    datumChoice->addWidget(datumFixed_, 1);
    auto *datum = new QVBoxLayout;
    datum->addLayout(datumChoice);
    datum->addWidget(datumCaption_);
    form->addRow("Datum", datum);

    corners_ = new QLabel;
    corners_->setObjectName("referenceGeoCorners");
    corners_->setTextFormat(Qt::PlainText);
    corners_->setWordWrap(true);
    form->addRow("Corners", corners_);

    elevation_ = new QLineEdit(QLocale().toString(site ? site->elevation : 0.0, 'f', 2));
    elevation_->setObjectName("referenceGeoElevation");
    elevation_->setAccessibleName("Elevation of the image, metres");
    auto *elevationRow = new QHBoxLayout;
    elevationRow->addWidget(elevation_);
    elevationRow->addWidget(new QLabel("m"));
    elevationRow->addStretch(1);
    form->addRow("Elevation", elevationRow);

    credit_ = new QLineEdit;
    credit_->setObjectName("referenceGeoCredit");
    credit_->setAccessibleName("Credit line, optional, 256 bytes at most");
    credit_->setPlaceholderText("Optional, 256 bytes");
    form->addRow("Credit", credit_);

    origin_ = new QLabel;
    origin_->setObjectName("referenceGeoOrigin");
    origin_->setTextFormat(Qt::PlainText);
    origin_->setWordWrap(true);
    originNote_ = new QLabel;
    originNote_->setObjectName("referenceGeoOriginNote");
    originNote_->setTextFormat(Qt::PlainText);
    originNote_->setWordWrap(true);
    auto *origin = new QVBoxLayout;
    origin->addWidget(origin_);
    origin->addWidget(originNote_);
    form->addRow("Origin", origin);
    layout->addLayout(form);

    // The report: read-only, between the settings and the footer, so the Tab order follows the reading order.
    reportPane_ = new QWidget;
    reportPane_->setObjectName("referenceGeoReportPane");
    auto *reportLayout = new QVBoxLayout(reportPane_);
    reportLayout->setContentsMargins(0, 0, 0, 0);
    report_ = new QPlainTextEdit;
    report_->setObjectName("referenceGeoReport");
    report_->setAccessibleName("Georeferenced image report, read only");
    report_->setReadOnly(true);
    report_->setLineWrapMode(QPlainTextEdit::NoWrap);
    reportLayout->addWidget(report_);
    auto *reportButtons = new QHBoxLayout;
    copyReport_ = new QPushButton("Copy report");
    copyReport_->setObjectName("referenceGeoCopyReport");
    copyReport_->setAccessibleName("Copy report as text");
    copyReport_->setAutoDefault(false);
    saveReport_ = new QPushButton("Save report…");
    saveReport_->setObjectName("referenceGeoSaveReport");
    saveReport_->setAccessibleName("Save report");
    saveReport_->setAutoDefault(false);
    reportButtons->addWidget(copyReport_);
    reportButtons->addWidget(saveReport_);
    reportButtons->addStretch(1);
    reportLayout->addLayout(reportButtons);
    reportPane_->hide();
    layout->addWidget(reportPane_, 1);

    // The footer: the status line, the read's progress, Show report, Choose another file…, Cancel, Import.
    auto *footer = new QHBoxLayout;
    statusLabel_ = new QLabel("Reading…");
    statusLabel_->setObjectName("referenceGeoStatus");
    statusLabel_->setTextFormat(Qt::PlainText);
    statusLabel_->setWordWrap(true);
    footer->addWidget(statusLabel_, 1);
    progress_ = new QProgressBar;
    progress_->setObjectName("referenceGeoProgress");
    progress_->setAccessibleName("Reading the image, per cent");
    progress_->setRange(0, 100);
    progress_->setTextVisible(false);
    progress_->setFocusPolicy(Qt::NoFocus);
    progress_->setFixedWidth(progress_->fontMetrics().horizontalAdvance(QLatin1Char('M')) * 9);
    footer->addWidget(progress_);
    showReport_ = new QPushButton("Show report");
    showReport_->setObjectName("referenceGeoShowReport");
    showReport_->setAccessibleName("Show report");
    showReport_->setCheckable(true);
    showReport_->setAutoDefault(false);
    showReport_->setEnabled(false);
    footer->addWidget(showReport_);
    chooseAnother_button_ = new QPushButton("Choose another file…");
    chooseAnother_button_->setObjectName("referenceGeoChooseAnother");
    chooseAnother_button_->setAccessibleName("Choose another file");
    chooseAnother_button_->setAutoDefault(false);
    chooseAnother_button_->hide();
    footer->addWidget(chooseAnother_button_);
    cancel_ = new QPushButton("Cancel");
    cancel_->setObjectName("referenceGeoCancel");
    cancel_->setAccessibleName("Cancel");
    cancel_->setAutoDefault(false);
    footer->addWidget(cancel_);
    import_ = new QPushButton("Import");
    import_->setObjectName("referenceGeoImport");
    import_->setAccessibleName("Import georeferenced image");
    import_->setProperty("accentFill", true); // The shared focus rule (R091.h).
    import_->setAutoDefault(false);
    import_->setDefault(true);
    import_->setEnabled(false);
    footer->addWidget(import_);
    layout->addLayout(footer);

    setTabOrder(name_, zone_);
    setTabOrder(zone_, wgs84_);
    setTabOrder(wgs84_, nad83_);
    setTabOrder(nad83_, elevation_);
    setTabOrder(elevation_, credit_);
    setTabOrder(credit_, report_);
    setTabOrder(report_, copyReport_);
    setTabOrder(copyReport_, saveReport_);
    setTabOrder(saveReport_, showReport_);
    setTabOrder(showReport_, chooseAnother_button_);
    setTabOrder(chooseAnother_button_, cancel_);
    setTabOrder(cancel_, import_);

    connect(name_, &QLineEdit::textChanged, this, [this] { place(); });
    connect(elevation_, &QLineEdit::textChanged, this, [this] { place(); });
    connect(credit_, &QLineEdit::textChanged, this, [this] { place(); });
    connect(zone_, &QComboBox::currentIndexChanged, this, [this] { place(); });
    connect(wgs84_, &QRadioButton::toggled, this, [this] { place(); });
    connect(showReport_, &QPushButton::toggled, this, [this](bool shown) {
        reportPane_->setVisible(shown);
        report_->setPlainText(reportText(false));
    });
    connect(copyReport_, &QPushButton::clicked, this,
            [this] { QGuiApplication::clipboard()->setText(reportText(true)); });
    connect(saveReport_, &QPushButton::clicked, this, [this] {
        const auto stem = QFileInfo(path_).completeBaseName();
        const auto chosen = QFileDialog::getSaveFileName(this, "Save georeferenced image report",
                                                         stem + "-georeference-report.txt",
                                                         "Report text (*.txt);;Report JSON (*.json)");
        if (!chosen.isEmpty()) {
            try {
                saveReport(chosen);
            } catch (const std::exception &error) {
                statusLabel_->setText(QString("Could not save the report: ") + QString::fromUtf8(error.what()));
            }
        }
    });
    connect(chooseAnother_button_, &QPushButton::clicked, this, [this] {
        chooseAnother_ = true;
        reject();
    });
    connect(cancel_, &QPushButton::clicked, this, [this] { reject(); });
    connect(import_, &QPushButton::clicked, this, [this] { importNow(); });

    poll_ = new QTimer(this);
    poll_->setInterval(50);
    connect(poll_, &QTimer::timeout, this, [this] { updateProgress(); });
    refresh();
    startRead();
}

ReferenceGeorefDialog::~ReferenceGeorefDialog() = default;

// The file is read on a worker: it touches no document. The thread's result is taken in the dialog's thread
// when it finishes. The dialog's context ends the connection, so a dialog closed during the read is not
// touched by it, and the thread is deleted when it finishes.
void ReferenceGeorefDialog::startRead() {
    read_ = std::make_shared<ReadState>();
    auto state = read_;
    auto *thread = QThread::create([state, path = path_] {
        try {
            state->file = readOrthophotoFile(path, [state](int percent) { state->percent = percent; });
        } catch (const ImageRefusal &refusal) {
            state->refusal = Refusal{refusal.code(), QString::fromStdString(refusal.sentence())};
        } catch (const std::exception &error) {
            state->refusal = Refusal{"", QString::fromUtf8(error.what())};
        }
    });
    connect(thread, &QThread::finished, thread, &QObject::deleteLater);
    connect(thread, &QThread::finished, this, [this, state] { readFinished(*state); });
    poll_->start();
    thread->start();
}

void ReferenceGeorefDialog::updateProgress() {
    if (!reading_ || !read_)
        return;
    const auto percent = read_->percent.load();
    progress_->setValue(percent);
    statusLabel_->setText(QString("● Reading %1… %2 per cent").arg(QFileInfo(path_).fileName()).arg(percent));
}

void ReferenceGeorefDialog::readFinished(ReadState &state) {
    poll_->stop();
    reading_ = false;
    progress_->hide();
    if (state.file)
        file_ = std::move(state.file);
    else if (state.refusal)
        refusal_ = std::move(state.refusal);
    read_.reset();
    if (file_) {
        const auto &file = *file_;
        const bool geoTiff = file.format == OrthophotoFormat::GeoTiff;
        QString pixels = formatName(file.format);
        if (!file.pixels.empty())
            pixels += " " + QString::fromStdString(file.pixels);
        image_->setText(QString("%1 · %2 × %3 px · %4 · %5")
                            .arg(QString::fromStdString(file.fileName))
                            .arg(QLocale().toString(file.columns))
                            .arg(QLocale().toString(file.rows))
                            .arg(pixels)
                            .arg(mebibytes(file.sourceBytes)));
        if (geoTiff)
            position_->setText(QString("GeoTIFF tags · north up · %1 east × %2 north")
                                   .arg(metres(file.pixelWidth))
                                   .arg(metres(file.pixelHeight)));
        else
            position_->setText(QString("world file %1 · north up · %2 east × %3 north")
                                   .arg(QString::fromStdString(file.worldFile))
                                   .arg(metres(file.pixelWidth))
                                   .arg(metres(file.pixelHeight)));
    }
    place();
    if (fileRefused_)
        chooseAnother_button_->setFocus();
    else if (zoneNeeded_)
        zone_->setFocus();
    else
        name_->setFocus();
}

void ReferenceGeorefDialog::reject() {
    // Esc and Cancel end a read that is still running; the worker's result is then ignored.
    if (reading_ && !chooseAnother_)
        status_ = "Cancelled. Nothing was imported.";
    QDialog::reject();
}

bool ReferenceGeorefDialog::worldFile() const {
    return file_ && file_->format != OrthophotoFormat::GeoTiff;
}

// Whether the zone is taken from the file or the site's record, or the person has chosen one.
bool ReferenceGeorefDialog::zoneChosen() const {
    const auto &site = doc_.site();
    return !worldFile() || (site && site->geographic) || zone_->currentData().toInt() != 0;
}

OrthophotoOptions ReferenceGeorefDialog::options() const {
    OrthophotoOptions options;
    options.name = name_->text().toUtf8().toStdString();
    options.credit = credit_->text().toUtf8().toStdString();
    bool okay{};
    const auto elevation = QLocale().toDouble(elevation_->text().trimmed(), &okay);
    if (!okay || !std::isfinite(elevation))
        throw ImageRefusal("elevation");
    options.elevation = elevation;
    options.datum = nad83_->isChecked() ? "NAD83" : "WGS84";
    const int zone = zone_->currentData().toInt();
    if (zone != 0) {
        options.zone = std::abs(zone);
        options.north = zone > 0;
    }
    return options;
}

// The placement against the site as it is now. A change to any setting places again; the file is not read again.
void ReferenceGeorefDialog::place() {
    placement_.reset();
    placedRevision_.reset();
    placedStamp_.reset();
    zoneNeeded_ = false;
    // Without a file there is nothing to place, and a refusal from the read stays as it is.
    if (!file_) {
        refresh();
        return;
    }
    refusal_.reset();
    const auto site = doc_.site();
    if (orthophotoNeedsZone(*file_, site) && !zoneChosen()) {
        zoneNeeded_ = true;
    } else {
        try {
            placement_ = placeOrthophoto(*file_, options(), site);
            placedRevision_ = doc_.revision();
            placedStamp_ = doc_.saveStamp();
        } catch (const ImageRefusal &refusal) {
            refusal_ = Refusal{refusal.code(), QString::fromStdString(refusal.sentence())};
        } catch (const std::exception &error) {
            refusal_ = Refusal{"", QString::fromUtf8(error.what())};
        }
    }
    refresh();
}

void ReferenceGeorefDialog::refresh() {
    fileRefused_ = refusal_ && (!file_ || !placementCode(refusal_->code));
    const bool editable = file_ && !fileRefused_ && !reading_;
    const bool geoTiff = file_ && file_->format == OrthophotoFormat::GeoTiff;
    const auto &site = doc_.site();
    const bool siteRecord = site && site->geographic;

    name_->setEnabled(editable);
    elevation_->setEnabled(editable);
    credit_->setEnabled(editable);

    // The zone: the person's choice for a world file with no site record; the record's, or the file's,
    // read-only otherwise.
    const bool zoneChoice = file_ && worldFile() && !siteRecord;
    zone_->setVisible(zoneChoice);
    zone_->setEnabled(editable && zoneChoice);
    if (!file_)
        zoneFixed_->clear();
    else if (!worldFile())
        zoneFixed_->setText(
            QString("%1, from the file").arg(QString::fromStdString(zoneLabelOf(file_->zone, file_->north))));
    else if (siteRecord)
        zoneFixed_->setText(QString("%1, from the site's record")
                                .arg(QString::fromStdString(
                                    zoneLabelOf(site->geographic->utmZone, site->geographic->utmNorth))));
    else
        zoneFixed_->clear();
    zoneFixed_->setVisible(!zoneFixed_->text().isEmpty());

    // The datum: a choice for a world file; the file's own, read-only, for a GeoTIFF.
    const bool datumChoice = editable && worldFile();
    wgs84_->setVisible(!geoTiff);
    nad83_->setVisible(!geoTiff);
    wgs84_->setEnabled(datumChoice);
    nad83_->setEnabled(datumChoice);
    datumFixed_->setVisible(geoTiff);
    const bool nad83 = geoTiff ? file_->datum == "NAD83" : nad83_->isChecked();
    if (geoTiff)
        datumFixed_->setText(nad83 ? "NAD83, from the file" : "WGS84, from the file");
    datumCaption_->setText(nad83 ? "⚠ Approximation. NAD83 is taken as WGS84. Sources disagree on the size of the "
                                   "difference, from about a metre upward, so positions are approximate. The image "
                                   "keeps the NAD83 label."
                                 : QString());
    datumCaption_->setVisible(!datumCaption_->text().isEmpty());

    // The corners and the origin are the placement's, when there is one.
    if (placement_) {
        const auto &p = *placement_;
        const double east = p.georeference.easting, north = p.georeference.northing;
        corners_->setText("NW  " + grid(east, north) + "   " + latLon(p.northWest) + "\n" +
                          "SE  " + grid(east + p.width, north - p.height) + "   " + latLon(p.southEast));
        const double centreE = east + p.width / 2, centreN = north - p.height / 2;
        if (p.establishesOrigin)
            origin_->setText(QString("No site origin yet. The import sets one at the image's centre, rounded to "
                                     "100 m: E %1 N %2.")
                                 .arg(number(p.origin.easting, 0))
                                 .arg(number(p.origin.northing, 0)));
        else {
            const double distance = std::hypot(centreE - p.origin.easting, centreN - p.origin.northing);
            const QString away = distance >= 1000 ? number(distance / 1000, 1) + " km"
                                                  : number(std::round(distance), 0) + " m";
            origin_->setText(QString("Site origin E %1 N %2 · the image's centre is %3 away")
                                 .arg(number(p.origin.easting, 0))
                                 .arg(number(p.origin.northing, 0))
                                 .arg(away));
        }
    } else {
        corners_->clear();
        origin_->clear();
    }
    originNote_->setText(file_ && site && !siteRecord
                             ? QString("⚠ This site has no geographic location, so the zone is not checked. Set one "
                                       "in Site settings.")
                             : QString());
    originNote_->setVisible(!originNote_->text().isEmpty());

    // The banner: a refusal, with its sentence and one next step (UX §21.3).
    if (refusal_) {
        const auto text = QString("✕ Refused. Nothing was imported. Your model is unchanged.\n%1\n1. %2")
                              .arg(refusal_->sentence)
                              .arg(QString::fromStdString(nextStep()));
        banner_->setText(text);
        banner_->setAccessibleName(text);
        banner_->show();
    } else {
        banner_->clear();
        banner_->hide();
    }
    chooseAnother_button_->setVisible(fileRefused_);
    chooseAnother_button_->setEnabled(fileRefused_);
    showReport_->setEnabled(!reading_ && (file_ || refusal_));

    // The status line, the Import button and its description (UX §21.2 and §21.5).
    QString status, reason;
    if (reading_) {
        const int percent = read_ ? read_->percent.load() : 0;
        status = QString("● Reading %1… %2 per cent").arg(QFileInfo(path_).fileName()).arg(percent);
        reason = "Reading the image.";
    } else if (refusal_) {
        status = "Refused. Nothing was imported.";
        reason = refusal_->sentence;
    } else if (zoneNeeded_) {
        status = "Choose the zone the image's coordinates are in.";
        reason = status;
    } else if (placement_) {
        status = "Ready. Import places the image at the corners above. Undo removes it.";
    } else {
        status = QString();
    }
    statusLabel_->setText(status);
    progress_->setVisible(reading_);
    import_->setEnabled(placement_.has_value() && !refusal_ && !reading_);
    import_->setAccessibleDescription(import_->isEnabled() ? QString() : reason);
}

std::string ReferenceGeorefDialog::nextStep() const {
    const auto &code = refusal_->code;
    const auto &site = doc_.site();
    if (code == "image-type")
        return "Choose another file.";
    if (code == "image-size" || code == "stored-size" || code == "image-extent")
        return "Crop or reduce it in an image editor.";
    if (code == "geotiff-compression")
        return "Save it uncompressed, or as LZW or Deflate.";
    if (code == "geotiff-samples")
        return "Export 8-bit RGB.";
    if (code == "geotiff-crs" || code == "geotiff-units")
        return "Export in WGS84 UTM.";
    if (code == "geotiff-untagged" || code == "geotiff-transform")
        return "Add its georeference in a GIS.";
    if (code == "world-file" || code == "world-rotated" || code == "world-flipped")
        return "Use a north-up world file.";
    if (code == "zone-mismatch") {
        if (site && site->geographic)
            return "Choose an image in " +
                   zoneLabelOf(site->geographic->utmZone, site->geographic->utmNorth) + ".";
        return "Choose an image in the site's zone.";
    }
    if (code == "zone-band" || code == "zone-exception" || code == "latitude")
        return worldFile() ? "Choose the zone the image is in." : "Export the image in one UTM zone, from a GIS.";
    if (code == "elevation")
        return "Enter a number.";
    if (code == "credit")
        return "Shorten it.";
    if (code == "file-changed")
        return "Choose it again.";
    if (code == "damaged")
        return "Download or copy the file again.";
    if (code == "reopen")
        return "Close the dialog and choose the image again.";
    return "Choose another file.";
}

QJsonObject ReferenceGeorefDialog::reportJson() const {
    QJsonObject report{{"file", QFileInfo(path_).fileName()}};
    if (file_) {
        const auto &file = *file_;
        QJsonObject source{{"format", formatName(file.format)},
                           {"bytes", double(file.sourceBytes)},
                           {"sha256", QString::fromStdString(file.sha256)},
                           {"columns", file.columns},
                           {"rows", file.rows},
                           {"pixels", QString::fromStdString(file.pixels)}};
        if (!file.worldFile.empty())
            source["worldFile"] = QString::fromStdString(file.worldFile);
        report["source"] = source;
    }
    if (refusal_) {
        report["outcome"] = "refused";
        report["refusal"] = QJsonObject{{"code", QString::fromStdString(refusal_->code)},
                                        {"sentence", refusal_->sentence}};
    } else if (zoneNeeded_) {
        report["outcome"] = "zone needed";
    } else if (placement_) {
        const auto &p = *placement_;
        report["outcome"] = "ready";
        report["zone"] = QString::fromStdString(zoneLabelOf(p.georeference.zone, p.georeference.north));
        report["datum"] = QString::fromStdString(p.georeference.datum);
        report["pixelWidth"] = p.georeference.pixelWidth;
        report["pixelHeight"] = p.georeference.pixelHeight;
        report["widthMetres"] = p.width;
        report["heightMetres"] = p.height;
        report["elevation"] = p.elevation;
        report["northWest"] = QJsonObject{{"easting", p.georeference.easting},
                                          {"northing", p.georeference.northing},
                                          {"latitude", p.northWest.latitude},
                                          {"longitude", p.northWest.longitude}};
        report["southEast"] = QJsonObject{{"easting", p.georeference.easting + p.width},
                                          {"northing", p.georeference.northing - p.height},
                                          {"latitude", p.southEast.latitude},
                                          {"longitude", p.southEast.longitude}};
        report["origin"] = p.establishesOrigin ? "set" : "kept";
        report["credit"] = QString::fromStdString(p.georeference.credit);
        QJsonArray notices;
        for (const auto &notice : p.notices)
            notices.append(QString::fromStdString(notice));
        report["notices"] = notices;
    }
    return report;
}

QString ReferenceGeorefDialog::reportText(bool fullDigest) const {
    QStringList out;
    out << "Georeferenced image report: " + QFileInfo(path_).fileName();
    const auto report = reportJson();
    out << "Outcome: " + report.value("outcome").toString("reading");
    if (file_) {
        const auto &file = *file_;
        const auto digest = QString::fromStdString(file.sha256);
        out << "Source: " + QString::fromStdString(file.fileName) +
                   (file.worldFile.empty() ? QString() : " · world file " + QString::fromStdString(file.worldFile));
        out << "Format: " + formatName(file.format) +
                   (file.pixels.empty() ? QString() : ", " + QString::fromStdString(file.pixels)) + " · " +
                   mebibytes(file.sourceBytes) + " (" + QLocale().toString(qulonglong(file.sourceBytes)) + " bytes)";
        out << "SHA-256: " + (fullDigest ? digest : digest.left(12) + "…");
        out << "Size: " + QLocale().toString(file.columns) + " × " + QLocale().toString(file.rows) + " px";
    }
    if (placement_) {
        const auto &p = *placement_;
        out << "Pixel size: " + metres(p.georeference.pixelWidth) + " east × " + metres(p.georeference.pixelHeight) +
                   " north · " + number(p.width, 1) + " m × " + number(p.height, 1) + " m";
        out << "Zone: " + QString::fromStdString(zoneLabelOf(p.georeference.zone, p.georeference.north)) +
                   " · datum " +
                   (p.georeference.datum == "NAD83" ? "NAD83, labelled approximation" : QString("WGS84"));
        out << "Corners: NW " + grid(p.georeference.easting, p.georeference.northing) + " · " + latLon(p.northWest);
        out << "         SE " + grid(p.georeference.easting + p.width, p.georeference.northing - p.height) + " · " +
                   latLon(p.southEast);
        out << "Elevation: " + number(p.elevation, 2) + " m";
        if (p.establishesOrigin)
            out << "Origin: set at E " + number(p.origin.easting, 0) + " N " + number(p.origin.northing, 0);
        else
            out << "Origin: kept at E " + number(p.origin.easting, 0) + " N " + number(p.origin.northing, 0);
        out << "Credit: " + (p.georeference.credit.empty() ? QString("none entered")
                                                              : QString::fromStdString(p.georeference.credit));
    }
    if (refusal_)
        out << "Refused: " + refusal_->sentence;
    if (placement_ && !placement_->notices.empty()) {
        out << "";
        out << "⚠ Notices";
        for (const auto &notice : placement_->notices)
            out << "  " + QString::fromStdString(notice);
    }
    return out.join('\n');
}

void ReferenceGeorefDialog::saveReport(const QString &path) const {
    const auto bytes = path.endsWith(".json", Qt::CaseInsensitive)
                           ? QJsonDocument(reportJson()).toJson(QJsonDocument::Indented)
                           : reportText(true).toUtf8();
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size())
        throw std::runtime_error("The file cannot be written");
}

void ReferenceGeorefDialog::importNow() {
    try {
        if (!file_ || !placement_ || !placedRevision_ || !placedStamp_)
            throw std::runtime_error("Choose the zone the image's coordinates are in.");
        if (doc_.revision() != *placedRevision_ || !doc_.isCurrentSnapshot(*placedStamp_))
            throw std::runtime_error("The model changed. Reopen the image importer.");
        QJsonObject result;
        const auto edit = doc_.prepareEdit(
            [&](Document &draft) { result = applyOrthophoto(draft, *file_, *placement_); });
        view_.applyPreparedEdit(edit);
        body_ = Id(result["body"].toString().toULongLong());
        const auto &p = *placement_;
        const auto zone = QString::fromStdString(zoneLabelOf(p.georeference.zone, p.georeference.north));
        status_ = QString("Placed %1 as a georeferenced reference image: %2 × %3 px, zone %4. Undo removes it.")
                      .arg(QString::fromStdString(file_->fileName))
                      .arg(QLocale().toString(file_->columns))
                      .arg(QLocale().toString(file_->rows))
                      .arg(zone);
        note_ = QString("Georeferenced: zone %1, north-up, %2. %3")
                    .arg(zone)
                    .arg(metres(p.georeference.pixelWidth))
                    .arg(p.establishesOrigin ? QString("Origin set at E %1 N %2.")
                                                   .arg(number(p.origin.easting, 0))
                                                   .arg(number(p.origin.northing, 0))
                                             : QString("Origin unchanged."));
        accept();
    } catch (const std::exception &error) {
        placement_.reset();
        placedRevision_.reset();
        placedStamp_.reset();
        refusal_ = Refusal{"reopen", QString::fromUtf8(error.what())};
        refresh();
        announce(banner_, banner_->text());
    }
}

// UX §21.5: the refusal banner is announced as an alert, so the announcement is assertive.
void ReferenceGeorefDialog::announce(QWidget *widget, const QString &message) const {
    if (message.isEmpty())
        return;
    QAccessibleAnnouncementEvent event(widget, message);
    event.setPoliteness(QAccessible::AnnouncementPoliteness::Assertive);
    QAccessible::updateAccessibility(&event);
}

} // namespace sketchy
