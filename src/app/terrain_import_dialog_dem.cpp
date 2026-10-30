#include "app/terrain_import_dialog.hpp"
#include "app/terrain_dem_window.hpp"
#include "app/terrain_import_dry_run.hpp"
#include "geo/geographic.hpp"
#include "io/geotiff_terrain.hpp"
#include <QButtonGroup>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFileInfo>
#include <QFrame>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QLocale>
#include <QPushButton>
#include <QRadioButton>
#include <QStringList>
#include <QVBoxLayout>
#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace sketchy {

namespace {
// The text helpers the page shares with the settings page (the same forms as there).
QString count(double value) { return QLocale().toString(qulonglong(std::llround(value))); }
QString plural(double value, const QString &one, const QString &many) {
    return count(value) + ' ' + (std::llround(value) == 1 ? one : many);
}
QString decimal(double value, int places = 2) { return QLocale().toString(value, 'f', places); }

// The captions and values of the page, as the rest of the dialog builds them.
QLabel *captionLabel(const QString &text) {
    auto *label = new QLabel(text);
    label->setObjectName("terrainImportFieldLabel");
    return label;
}
QLabel *headingLabel(const QString &text) {
    auto *label = new QLabel(text);
    label->setObjectName("terrainImportHeading");
    return label;
}
QLabel *valueLabel(const QString &name) {
    auto *label = new QLabel(QStringLiteral("—"));
    label->setObjectName(name);
    label->setTextFormat(Qt::PlainText);
    label->setWordWrap(true);
    label->setTextInteractionFlags(Qt::NoTextInteraction);
    return label;
}
// A metres field (easting, northing, width, height, spacing) or a degrees field, named for a screen
// reader; its value commits on Enter or focus out, as the origin fields do.
QDoubleSpinBox *geoField(const QString &name, const QString &accessible, const QString &suffix,
                         int decimals, double minimum, double maximum) {
    auto *field = new QDoubleSpinBox;
    field->setObjectName(name);
    field->setAccessibleName(accessible);
    field->setRange(minimum, maximum);
    field->setDecimals(decimals);
    field->setSuffix(suffix);
    field->setGroupSeparatorShown(true);
    field->setKeyboardTracking(false);
    return field;
}

} // namespace

// ---- GeoTIFF page (R107.b4, UX_DESIGN §20) -------------------------------------------------
namespace {
// Whether the file's posts fit a product (the reader's own rule, as geoTiffFacts reports it).
bool fitsProduct(const GeoTiffFacts &file, DemProduct product) {
    return std::find(file.products.begin(), file.products.end(), product) != file.products.end();
}
// The zone and hemisphere a zone choice holds ("11N").
std::pair<int, bool> zoneChoice(const QString &label) {
    if (label.size() < 2)
        return {0, true};
    return {label.chopped(1).toInt(), label.endsWith(QLatin1Char('N'))};
}
// The zone and hemisphere of a site's UTM CRS label (EPSG:32601 to 32660 north, 32701 to 32760 south).
std::optional<std::pair<int, bool>> utmOfCrs(const std::string &crs) {
    if (crs.rfind("EPSG:", 0) != 0)
        return std::nullopt;
    bool ok{};
    const int code = QString::fromStdString(crs.substr(5)).toInt(&ok);
    if (!ok)
        return std::nullopt;
    if (code >= 32601 && code <= 32660)
        return std::pair{code - 32600, true};
    if (code >= 32701 && code <= 32760)
        return std::pair{code - 32700, false};
    return std::nullopt;
}
} // namespace

void TerrainImportDialog::buildGeoTiff(QVBoxLayout *column) {
    demBox_ = new QWidget;
    demBox_->setObjectName("terrainImportDemBox");
    auto *box = new QVBoxLayout(demBox_);
    box->setContentsMargins(0, 0, 0, 0);
    box->setSpacing(8);

    // The source: the product, the file, the horizontal and the vertical reference (UX §20.1, §20.3).
    auto *source = new QGridLayout;
    source->setHorizontalSpacing(10);
    source->setVerticalSpacing(8);
    source->setColumnStretch(1, 1);
    dep3_ = new QRadioButton("USGS 3DEP");
    dep3_->setObjectName("terrainImportDep3");
    dep3_->setAccessibleName("Source product: USGS 3DEP");
    glo30_ = new QRadioButton("Copernicus GLO-30");
    glo30_->setObjectName("terrainImportGlo30");
    glo30_->setAccessibleName("Source product: Copernicus GLO-30");
    auto *products = new QButtonGroup(this);
    products->addButton(dep3_, 0);
    products->addButton(glo30_, 1);
    auto *productLine = new QHBoxLayout;
    productLine->setSpacing(12);
    productLine->addWidget(dep3_);
    productLine->addWidget(glo30_);
    productLine->addStretch(1);
    demProductCaption_ = valueLabel("terrainImportDemProductCaption");
    auto *productColumn = new QVBoxLayout;
    productColumn->setSpacing(4);
    productColumn->addLayout(productLine);
    productColumn->addWidget(demProductCaption_);
    source->addWidget(captionLabel("Product"), 0, 0, Qt::AlignTop);
    source->addLayout(productColumn, 0, 1);

    demFile_ = valueLabel("terrainImportDemFile");
    source->addWidget(captionLabel("File"), 1, 0, Qt::AlignTop);
    source->addWidget(demFile_, 1, 1);

    demDatum_ = valueLabel("terrainImportDemDatum");
    demDatumNote_ = valueLabel("terrainImportDemDatumNote");
    auto *datumColumn = new QVBoxLayout;
    datumColumn->setSpacing(4);
    datumColumn->addWidget(demDatum_);
    datumColumn->addWidget(demDatumNote_);
    source->addWidget(captionLabel("Horizontal"), 2, 0, Qt::AlignTop);
    source->addLayout(datumColumn, 2, 1);

    verticalChoice_ = new QComboBox;
    verticalChoice_->setObjectName("terrainImportVertical");
    verticalChoice_->setAccessibleName("Vertical reference of the heights, choose one");
    demVerticalCaption_ = valueLabel("terrainImportDemVerticalCaption");
    auto *verticalColumn = new QVBoxLayout;
    verticalColumn->setSpacing(4);
    verticalColumn->addWidget(verticalChoice_);
    verticalColumn->addWidget(demVerticalCaption_);
    // The vertical reference sits below the spacing, where §20.6 puts it in the Tab order.
    auto *verticalRow = new QGridLayout;
    verticalRow->setHorizontalSpacing(10);
    verticalRow->setColumnStretch(1, 1);
    verticalRow->addWidget(captionLabel("Vertical"), 0, 0, Qt::AlignTop);
    verticalRow->addLayout(verticalColumn, 0, 1);
    box->addLayout(source);

    auto *affiliation = captionLabel("Provider names identify the sources only. SketchyUp is not "
                                     "affiliated with USGS or Copernicus.");
    affiliation->setTextFormat(Qt::PlainText);
    affiliation->setWordWrap(true);
    box->addWidget(affiliation);

    // The window (UX §20.2): around the site origin, centre and size, or corners. Latitude and
    // longitude sit beside each UTM point, converted in the window's zone.
    box->addWidget(headingLabel("Window"));
    windowOrigin_ = new QRadioButton("Around the site origin");
    windowOrigin_->setObjectName("terrainImportWindowOrigin");
    windowOrigin_->setAccessibleName("Window: around the site origin");
    windowCentre_ = new QRadioButton("Centre and size");
    windowCentre_->setObjectName("terrainImportWindowCentre");
    windowCentre_->setAccessibleName("Window: centre and size");
    windowCorners_ = new QRadioButton("Corners");
    windowCorners_->setObjectName("terrainImportWindowCorners");
    windowCorners_->setAccessibleName("Window: corners");
    windowModes_ = new QButtonGroup(this);
    windowModes_->addButton(windowOrigin_, 0);
    windowModes_->addButton(windowCentre_, 1);
    windowModes_->addButton(windowCorners_, 2);
    box->addWidget(windowOrigin_);
    box->addWidget(windowCentre_);

    auto positionHeader = [](QGridLayout *grid) {
        grid->addWidget(captionLabel("East, metres"), 0, 1);
        grid->addWidget(captionLabel("North, metres"), 0, 2);
        grid->addWidget(captionLabel("Latitude"), 0, 3);
        grid->addWidget(captionLabel("Longitude"), 0, 4);
    };
    auto degreeField = [](const QString &name, const QString &accessible, bool latitude) {
        auto *field = geoField(name, accessible, "°", 6, latitude ? -90 : -180, latitude ? 90 : 180);
        field->setSpecialValueText("—");
        return field;
    };
    centreBox_ = new QWidget;
    centreBox_->setObjectName("terrainImportDemCentre");
    auto *centre = new QGridLayout(centreBox_);
    centre->setContentsMargins(22, 0, 0, 0);
    centre->setHorizontalSpacing(6);
    centre->setVerticalSpacing(4);
    positionHeader(centre);
    centre->addWidget(captionLabel("Centre"), 1, 0);
    centreE_ = geoField("terrainImportCentreE", "Centre east, metres", " m", 1, 0, 2e7);
    centreN_ = geoField("terrainImportCentreN", "Centre north, metres", " m", 1, -2e7, 3e7);
    centreLat_ = degreeField("terrainImportCentreLat", "Centre latitude, degrees", true);
    centreLon_ = degreeField("terrainImportCentreLon", "Centre longitude, degrees", false);
    centre->addWidget(centreE_, 1, 1);
    centre->addWidget(centreN_, 1, 2);
    centre->addWidget(centreLat_, 1, 3);
    centre->addWidget(centreLon_, 1, 4);
    box->addWidget(centreBox_);

    box->addWidget(windowCorners_);
    cornersBox_ = new QWidget;
    cornersBox_->setObjectName("terrainImportDemCorners");
    auto *corners = new QGridLayout(cornersBox_);
    corners->setContentsMargins(22, 0, 0, 0);
    corners->setHorizontalSpacing(6);
    corners->setVerticalSpacing(4);
    positionHeader(corners);
    corners->addWidget(captionLabel("South-west"), 1, 0);
    corners->addWidget(captionLabel("North-east"), 2, 0);
    swE_ = geoField("terrainImportSwE", "South-west corner east, metres", " m", 1, 0, 2e7);
    swN_ = geoField("terrainImportSwN", "South-west corner north, metres", " m", 1, -2e7, 3e7);
    swLat_ = degreeField("terrainImportSwLat", "South-west corner latitude, degrees", true);
    swLon_ = degreeField("terrainImportSwLon", "South-west corner longitude, degrees", false);
    neE_ = geoField("terrainImportNeE", "North-east corner east, metres", " m", 1, 0, 2e7);
    neN_ = geoField("terrainImportNeN", "North-east corner north, metres", " m", 1, -2e7, 3e7);
    neLat_ = degreeField("terrainImportNeLat", "North-east corner latitude, degrees", true);
    neLon_ = degreeField("terrainImportNeLon", "North-east corner longitude, degrees", false);
    corners->addWidget(swE_, 1, 1);
    corners->addWidget(swN_, 1, 2);
    corners->addWidget(swLat_, 1, 3);
    corners->addWidget(swLon_, 1, 4);
    corners->addWidget(neE_, 2, 1);
    corners->addWidget(neN_, 2, 2);
    corners->addWidget(neLat_, 2, 3);
    corners->addWidget(neLon_, 2, 4);
    box->addWidget(cornersBox_);

    // Size, for the two modes that have a centre: the window is square unless changed.
    sizeBox_ = new QWidget;
    sizeBox_->setObjectName("terrainImportDemSize");
    auto *size = new QHBoxLayout(sizeBox_);
    size->setContentsMargins(22, 0, 0, 0);
    size->setSpacing(6);
    width_ = geoField("terrainImportWidth", "Width, metres", " m", 1, 1, 1e7);
    height_ = geoField("terrainImportHeight", "Height, metres", " m", 1, 1, 1e7);
    width_->setValue(1000);
    height_->setValue(1000);
    size->addWidget(captionLabel("Width"));
    size->addWidget(width_);
    size->addSpacing(8);
    size->addWidget(captionLabel("Height"));
    size->addWidget(height_);
    size->addStretch(1);
    box->addWidget(sizeBox_);

    // Zone and Fit to the file (the window and the grid share one zone).
    zoneRow_ = new QWidget;
    zoneRow_->setObjectName("terrainImportDemZone");
    auto *zoneLine = new QHBoxLayout(zoneRow_);
    zoneLine->setContentsMargins(0, 0, 0, 0);
    zoneLine->setSpacing(8);
    zone_ = new QComboBox;
    zone_->setObjectName("terrainImportZone");
    zone_->setAccessibleName("UTM zone and hemisphere of the window");
    for (const char hemisphere : {'N', 'S'})
        for (int zone = 1; zone <= 60; ++zone) {
            const auto label = QString::number(zone) + QLatin1Char(hemisphere);
            zone_->addItem(label, label);
        }
    demFit_ = new QPushButton("Fit to the file");
    demFit_->setObjectName("terrainImportFitFile");
    demFit_->setAccessibleName("Fit the window to the file");
    demFit_->setAutoDefault(false);
    zoneLine->addWidget(captionLabel("Zone"));
    zoneLine->addWidget(zone_);
    zoneLine->addSpacing(12);
    zoneLine->addWidget(demFit_);
    zoneLine->addStretch(1);
    box->addWidget(zoneRow_);

    demWindowCaption_ = valueLabel("terrainImportDemWindowCaption");
    box->addWidget(demWindowCaption_);

    // Spacing: whole metres, the nominal post size by default (UX §20.2).
    auto *spacingLine = new QHBoxLayout;
    spacingLine->setSpacing(8);
    spacing_ = geoField("terrainImportSpacing", "Post spacing, metres, at least the file's post size", " m", 0, 1, 1e6);
    spacingLine->addWidget(captionLabel("Post spacing"));
    spacingLine->addWidget(spacing_);
    spacingLine->addStretch(1);
    box->addLayout(spacingLine);
    demSpacingCaption_ = valueLabel("terrainImportDemSpacingCaption");
    box->addWidget(demSpacingCaption_);

    // The live counts of the dry run (UX §20.2).
    demGrid_ = valueLabel("terrainImportDemGrid");
    demGrid_->setObjectName("terrainImportDemGrid");
    box->addWidget(demGrid_);
    box->addLayout(verticalRow);

    // The mismatch with the site's vertical reference: the offset and the note (UX §20.3).
    mismatchBox_ = new QFrame;
    mismatchBox_->setObjectName("terrainImportMismatch");
    auto *mismatch = new QVBoxLayout(mismatchBox_);
    mismatch->setContentsMargins(12, 8, 12, 8);
    mismatch->setSpacing(6);
    mismatchTitle_ = headingLabel("⚠ The vertical references differ");
    mismatchTitle_->setObjectName("terrainImportMismatchTitle");
    mismatch->addWidget(mismatchTitle_);
    mismatchText_ = valueLabel("terrainImportMismatchText");
    mismatch->addWidget(mismatchText_);
    auto *offsetGrid = new QGridLayout;
    offsetGrid->setHorizontalSpacing(10);
    offsetGrid->setVerticalSpacing(6);
    offset_ = geoField("terrainImportOffset", "Offset to add to the heights, metres", " m", 2, -10000, 10000);
    offset_->setValue(0);
    why_ = new QLineEdit;
    why_->setObjectName("terrainImportWhy");
    why_->setAccessibleName("Why the heights are offset, required");
    why_->setMaxLength(int(TerrainDemSource::textBytes));
    why_->setPlaceholderText("Required, for example the datum shift from a source you trust");
    offsetGrid->addWidget(captionLabel("Offset to add to the heights"), 0, 0);
    offsetGrid->addWidget(offset_, 0, 1);
    offsetGrid->addWidget(captionLabel("Why"), 1, 0);
    offsetGrid->addWidget(why_, 1, 1);
    offsetGrid->setColumnStretch(1, 1);
    mismatch->addLayout(offsetGrid);
    mismatchCaption_ = valueLabel("terrainImportMismatchCaption");
    mismatch->addWidget(mismatchCaption_);
    mismatchBox_->hide();
    box->addWidget(mismatchBox_);

    // The site (UX §20.3): its zone and vertical reference, and Site settings….
    auto *siteLine = new QGridLayout;
    siteLine->setHorizontalSpacing(10);
    siteLine->setVerticalSpacing(6);
    siteLine->setColumnStretch(1, 1);
    demSite_ = valueLabel("terrainImportDemSite");
    siteSettings_ = new QPushButton("Site settings…");
    siteSettings_->setObjectName("terrainImportSiteSettings");
    siteSettings_->setAccessibleName("Open Site settings");
    siteSettings_->setAutoDefault(false);
    siteSettings_->setEnabled(bool(setup_.openSiteSettings));
    siteLine->addWidget(captionLabel("Site"), 0, 0, Qt::AlignTop);
    siteLine->addWidget(demSite_, 0, 1);
    siteLine->addWidget(siteSettings_, 1, 1, Qt::AlignLeft);
    box->addLayout(siteLine);
    column->addWidget(demBox_);

    // The page's own Tab order (UX §20.6); the units and the point settings sit around it.
    setTabOrder(dep3_, glo30_);
    setTabOrder(glo30_, windowOrigin_);
    setTabOrder(windowOrigin_, windowCentre_);
    setTabOrder(windowCentre_, centreE_);
    setTabOrder(centreE_, centreN_);
    setTabOrder(centreN_, centreLat_);
    setTabOrder(centreLat_, centreLon_);
    setTabOrder(centreLon_, windowCorners_);
    setTabOrder(windowCorners_, swE_);
    setTabOrder(swE_, swN_);
    setTabOrder(swN_, swLat_);
    setTabOrder(swLat_, swLon_);
    setTabOrder(swLon_, neE_);
    setTabOrder(neE_, neN_);
    setTabOrder(neN_, neLat_);
    setTabOrder(neLat_, neLon_);
    setTabOrder(neLon_, width_);
    setTabOrder(width_, height_);
    setTabOrder(height_, zone_);
    setTabOrder(zone_, demFit_);
    setTabOrder(demFit_, spacing_);
    setTabOrder(spacing_, verticalChoice_);
    setTabOrder(verticalChoice_, offset_);
    setTabOrder(offset_, why_);
    setTabOrder(why_, siteSettings_);

    // ---- Behaviour -----------------------------------------------------------------------------
    connect(products, &QButtonGroup::idToggled, this, [this](int, bool checked) {
        if (checked && !updating_) {
            refresh();
            schedule(0);
        }
    });
    connect(windowModes_, &QButtonGroup::idToggled, this, [this](int, bool checked) {
        if (checked && !updating_) {
            refresh();
            schedule(0);
        }
    });
    connect(zone_, &QComboBox::currentIndexChanged, this, [this] {
        if (updating_)
            return;
        positionProblem_.clear();
        refresh();
        schedule(0);
    });
    for (auto *field : {centreE_, centreN_, swE_, swN_, neE_, neN_, width_, height_})
        connect(field, &QDoubleSpinBox::valueChanged, this, [this] {
            if (updating_)
                return;
            positionProblem_.clear();
            refresh();
            schedule(250);
        });
    // A latitude or longitude typed converts to UTM in the window's zone (UX §20.8 question 1).
    auto latLon = [this](QDoubleSpinBox *easting, QDoubleSpinBox *northing,
                         QDoubleSpinBox *latitude, QDoubleSpinBox *longitude) {
        auto changed = [this, easting, northing, latitude, longitude] {
            if (updating_)
                return;
            updateFromLatLon(easting, northing, latitude, longitude);
            refresh();
            schedule(250);
        };
        connect(latitude, &QDoubleSpinBox::valueChanged, this, changed);
        connect(longitude, &QDoubleSpinBox::valueChanged, this, changed);
    };
    latLon(centreE_, centreN_, centreLat_, centreLon_);
    latLon(swE_, swN_, swLat_, swLon_);
    latLon(neE_, neN_, neLat_, neLon_);
    connect(spacing_, &QDoubleSpinBox::valueChanged, this, [this] {
        if (updating_)
            return;
        spacingEdited_ = true;
        refresh();
        schedule(250);
    });
    connect(verticalChoice_, &QComboBox::currentIndexChanged, this, [this] {
        if (updating_)
            return;
        refresh();
        schedule(0);
    });
    connect(offset_, &QDoubleSpinBox::valueChanged, this, [this] {
        if (updating_)
            return;
        refresh();
        schedule(250);
    });
    connect(why_, &QLineEdit::textEdited, this, [this] {
        if (updating_)
            return;
        refresh();
        schedule(400);
    });
    connect(demFit_, &QPushButton::clicked, this, [this] { fitToFile(); });
    connect(siteSettings_, &QPushButton::clicked, this, [this] {
        if (!setup_.openSiteSettings)
            return;
        setup_.openSiteSettings();
        documentChanged();
    });
}

void TerrainImportDialog::initGeoTiff() {
    if (!geotiff())
        return;
    // The file's facts, from its header alone (the posts are not read). A file the reader cannot use
    // is refused before any window is chosen (UX §20.5).
    try {
        facts_ = geoTiffFacts(setup_.path);
    } catch (const TerrainError &error) {
        factsRefusal_ = error.refusal().empty() ? std::string("unreadable") : error.refusal();
        factsSentence_ = QString::fromStdString(error.sentence());
        if (factsSentence_.isEmpty())
            factsSentence_ = "The file cannot be read.";
    } catch (const std::exception &) {
        factsRefusal_ = "unreadable";
        factsSentence_ = "The file cannot be read.";
    }
    if (facts_ && facts_->products.empty()) {
        // The posts fit neither product: the reader's own refusal, in the dialog's words.
        factsRefusal_ = "post-size";
        factsSentence_ = QStringLiteral("The posts are %1 by %2 arc-seconds. 3DEP is read at 1/3 or "
                                        "1 arc-second, and GLO-30 at 1 arc-second.")
                             .arg(QString::number(facts_->postArcSecondsEastWest, 'g', 4),
                                  QString::number(facts_->postArcSecondsNorthSouth, 'g', 4));
    }
    if (!facts_)
        return;
    const auto &file = *facts_;
    const bool wasUpdating = updating_;
    updating_ = true;
    nominal_ = demNominalSpacing(file);
    // The product: the one the posts fit, or neither when both fit (UX §20.1).
    dep3_->setEnabled(fitsProduct(file, DemProduct::Dep3));
    glo30_->setEnabled(fitsProduct(file, DemProduct::Glo30));
    if (file.products.size() == 1)
        (fitsProduct(file, DemProduct::Dep3) ? dep3_ : glo30_)->setChecked(true);
    const auto ns = demArcSecondLabel(file.postArcSecondsNorthSouth);
    const auto ew = demArcSecondLabel(file.postArcSecondsEastWest);
    const auto step = std::abs(file.postArcSecondsEastWest - file.postArcSecondsNorthSouth) < 1e-6
                          ? ns
                          : ns + " north-south, " + ew + " east-west";
    if (file.products.size() == 1)
        demProductCaption_->setText(QStringLiteral("%1 posts are %2 only.")
                                        .arg(step, demProductLabel(file.products.front())));
    else
        demProductCaption_->setText(QStringLiteral("%1 posts fit both products, so choose one.")
                                        .arg(step));
    // The file's line: its name, size, post, compression (UX §20.3).
    demFile_->setText(QFileInfo(setup_.fileName).fileName() + " · " +
                      QLocale().formattedDataSize(QFileInfo(setup_.path).size(), 1) + " · " + step +
                      " · " + QString::fromStdString(file.compression));
    // The vertical reference: the file's own, or the person's choice of three (UX §20.3).
    verticalChoice_->clear();
    if (!file.verticalReference.empty()) {
        verticalChoice_->addItem("From the file: " + QString::fromStdString(file.verticalReference),
                                 QString());
        verticalChoice_->setEnabled(false);
    } else {
        verticalChoice_->addItem("Choose…", QString());
        for (const char *name : {"NAVD88", "EGM2008", "EGM96"})
            verticalChoice_->addItem(name, name);
    }
    verticalChoice_->setCurrentIndex(0);
    // The window starts around the site origin when the site has one (in its record's zone, or its
    // UTM label's zone), and otherwise centred on the file (UX §20.2, as the brief sets it).
    const auto centre = demCentre(file);
    const auto &site = doc_.site();
    std::pair<int, bool> zone{centre.zone, centre.north};
    if (site && site->geographic)
        zone = {site->geographic->utmZone, site->geographic->utmNorth};
    else if (site && utmOfCrs(site->crs))
        zone = *utmOfCrs(site->crs);
    zone_->setCurrentIndex(zone_->findData(demZoneLabel(zone.first, zone.second)));
    width_->setValue(1000);
    height_->setValue(1000);
    if (site) {
        centreE_->setValue(site->easting);
        centreN_->setValue(site->northing);
        setWindowMode(0);
    } else {
        centreE_->setValue(centre.easting);
        centreN_->setValue(centre.northing);
        setWindowMode(1);
    }
    swE_->setValue(centreE_->value() - 500);
    swN_->setValue(centreN_->value() - 500);
    neE_->setValue(centreE_->value() + 500);
    neN_->setValue(centreN_->value() + 500);
    updating_ = wasUpdating;
}

void TerrainImportDialog::setWindowMode(int mode) {
    const bool wasUpdating = updating_;
    updating_ = true;
    (mode == 0 ? windowOrigin_ : mode == 1 ? windowCentre_ : windowCorners_)->setChecked(true);
    updating_ = wasUpdating;
    refreshGeoTiffWindow();
}

std::optional<DemWindow> TerrainImportDialog::demWindow() const {
    const auto [zone, north] = zoneChoice(zone_->currentData().toString());
    if (zone < 1 || zone > 60)
        return std::nullopt;
    DemWindow window{zone, north, 0, 0, 0, 0};
    const int mode = windowModes_->checkedId();
    if (mode == 2) {
        window.e0 = swE_->value();
        window.n0 = swN_->value();
        window.e1 = neE_->value();
        window.n1 = neN_->value();
        return window;
    }
    double easting = 0, northing = 0;
    if (mode == 0) {
        const auto &site = doc_.site();
        if (!site)
            return std::nullopt;
        easting = site->easting;
        northing = site->northing;
    } else {
        easting = centreE_->value();
        northing = centreN_->value();
    }
    window.e0 = easting - width_->value() / 2;
    window.e1 = easting + width_->value() / 2;
    window.n0 = northing - height_->value() / 2;
    window.n1 = northing + height_->value() / 2;
    return window;
}

void TerrainImportDialog::updateLatLon(QDoubleSpinBox *easting, QDoubleSpinBox *northing,
                                       QDoubleSpinBox *latitude, QDoubleSpinBox *longitude) {
    const auto [zone, north] = zoneChoice(zone_->currentData().toString());
    const bool wasUpdating = updating_;
    updating_ = true;
    try {
        const auto position = utmToGeo(UtmPosition{zone, north, easting->value(), northing->value()});
        latitude->setValue(position.latitude);
        longitude->setValue(position.longitude);
    } catch (const std::exception &) {
        // Not a position in this zone: the fields show a dash (the minimum is their special value).
        latitude->setValue(latitude->minimum());
        longitude->setValue(longitude->minimum());
    }
    updating_ = wasUpdating;
}

void TerrainImportDialog::updateFromLatLon(QDoubleSpinBox *easting, QDoubleSpinBox *northing,
                                           QDoubleSpinBox *latitude, QDoubleSpinBox *longitude) {
    const auto [zone, north] = zoneChoice(zone_->currentData().toString());
    const bool wasUpdating = updating_;
    updating_ = true;
    try {
        const auto utm = geoToUtm(GeoPosition{latitude->value(), longitude->value()}, zone);
        if (utm.north != north)
            throw std::runtime_error("the other hemisphere");
        easting->setValue(utm.easting);
        northing->setValue(utm.northing);
        positionProblem_.clear();
    } catch (const std::exception &) {
        positionProblem_ = QStringLiteral("This latitude and longitude are not in zone %1.")
                               .arg(demZoneLabel(zone, north));
    }
    updating_ = wasUpdating;
}

void TerrainImportDialog::fitToFile() {
    if (!facts_)
        return;
    const auto centre = demCentre(*facts_);
    const auto fit = demFitWindow(*facts_, centre.zone, centre.north);
    const bool wasUpdating = updating_;
    updating_ = true;
    if (fit) {
        zone_->setCurrentIndex(zone_->findData(demZoneLabel(fit->zone, fit->north)));
        swE_->setValue(fit->e0);
        swN_->setValue(fit->n0);
        neE_->setValue(fit->e1);
        neN_->setValue(fit->n1);
        positionProblem_.clear();
    } else
        positionProblem_ = QStringLiteral("The file has no part in zone %1.")
                               .arg(demZoneLabel(centre.zone, centre.north));
    updating_ = wasUpdating;
    if (fit)
        setWindowMode(2);
    refresh();
    schedule(0);
}

void TerrainImportDialog::refreshGeoTiffWindow() {
    const bool wasUpdating = updating_;
    updating_ = true;
    const int mode = windowModes_->checkedId();
    windowOrigin_->setEnabled(bool(doc_.site()));
    demFit_->setEnabled(bool(facts_));
    centreBox_->setVisible(mode == 1);
    cornersBox_->setVisible(mode == 2);
    sizeBox_->setVisible(mode == 0 || mode == 1);
    updateLatLon(centreE_, centreN_, centreLat_, centreLon_);
    updateLatLon(swE_, swN_, swLat_, swLon_);
    updateLatLon(neE_, neN_, neLat_, neLon_);
    updating_ = wasUpdating;
}

void TerrainImportDialog::refreshGeoTiff() {
    refreshGeoTiffWindow();
    refreshGeoTiffLabels();
    refreshGeoTiffOverlays();
}

QString TerrainImportDialog::siteVertical() const {
    const auto &site = doc_.site();
    return site && site->geographic ? QString::fromStdString(site->geographic->verticalReference)
                                    : QString();
}

QString TerrainImportDialog::effectiveVertical() const {
    if (facts_ && !facts_->verticalReference.empty())
        return QString::fromStdString(facts_->verticalReference);
    return verticalChoice_ ? verticalChoice_->currentData().toString() : QString();
}

bool TerrainImportDialog::fileBlocked() const {
    return !factsRefusal_.empty() || (dryRun_ && demFileRefusal(dryRun_->refusal));
}

// The site's vertical label and the terrain's differ, so the offset and the note are needed. A site
// labelled "unknown" has nothing to mix with: the notice says so instead (UX §20.3).
bool TerrainImportDialog::mismatch() const {
    const auto site = siteVertical();
    const auto vertical = effectiveVertical();
    return !site.isEmpty() && site != "unknown" && !vertical.isEmpty() && site != vertical;
}

double TerrainImportDialog::demNominal() const {
    if (dryRun_ && dryRun_->geotiff) {
        const auto dem = dryRun_->sourceReport["dem"].toObject();
        if (dem.contains("nominalSpacing"))
            return dem["nominalSpacing"].toDouble();
    }
    return nominal_;
}

// Why there is no dry run yet, and so no Import: the choices the file needs. Empty when a dry run
// may go. A refused file is the banner's, not a blocker.
QString TerrainImportDialog::demBlocker() const {
    if (!factsRefusal_.empty())
        return "The file cannot be imported.";
    if (!dep3_->isChecked() && !glo30_->isChecked())
        return "Choose the source product: USGS 3DEP or Copernicus GLO-30.";
    if (effectiveVertical().isEmpty())
        return "Choose the vertical reference to import.";
    if (!demWindow())
        return "Choose a window: a window around the site origin needs an origin.";
    if (!positionProblem_.isEmpty())
        return positionProblem_;
    if (spacingEdited_ && spacing_->value() < demNominal())
        return QStringLiteral("Finer than the file's posts (%1 m) would only interpolate. Use %1 m or "
                              "more.")
            .arg(QString::number(std::llround(demNominal())));
    return {};
}

// The refusal the dialog shows for the file, the window or the import: the reader's or the
// engine's plain sentence, mapped by its code (never the command line's text). `code` receives it.
QString TerrainImportDialog::demRefusalSentence(std::string &code) const {
    if (!factsRefusal_.empty()) {
        code = factsRefusal_;
        return factsSentence_;
    }
    if (dryRun_ && dryRun_->geotiff && !dryRun_->error.isEmpty()) {
        code = dryRun_->refusal;
        return dryRun_->sentence.isEmpty() ? demPlainMessage(dryRun_->error) : dryRun_->sentence;
    }
    if (dryRun_ && dryRun_->result && !dryRun_->result->ok) {
        code = dryRun_->result->refusal;
        auto sentence = QString::fromStdString(dryRun_->result->sentence);
        if (sentence.isEmpty()) {
            const auto refusals = dryRun_->result->report["terrain"].toObject()["refusals"].toArray();
            sentence = refusals.isEmpty() ? QString("The import is refused.")
                                          : demPlainMessage(refusals.first().toObject()["message"].toString());
        }
        return sentence;
    }
    return {};
}

QString TerrainImportDialog::refusalNextSteps(const std::string &code) const {
    QStringList lines;
    int number = 1;
    for (const auto &step : demNextSteps(code, demNominal()))
        lines << QString::number(number++) + ". " + step + ".";
    return lines.join('\n');
}

QString TerrainImportDialog::demRefusalBanner() const {
    std::string code;
    const auto sentence = demRefusalSentence(code);
    if (sentence.isEmpty())
        return {};
    const auto steps = refusalNextSteps(code);
    return "✕ Refused. Nothing was imported. Your model is unchanged.\n" + demPlainMessage(sentence) +
           (steps.isEmpty() ? QString() : "\n" + steps);
}

QString TerrainImportDialog::demReadyText() const {
    const auto terrain = report()["terrain"].toObject();
    const auto notices = terrain["notices"].toArray().size();
    auto text = QString("Ready: %1 · %2")
                    .arg(plural(terrain["triangles"].toDouble(), "triangle", "triangles"),
                         plural(double(notices), "notice", "notices"));
    if (dryRun_ && dryRun_->result && dryRun_->result->establish &&
        dryRun_->result->establish->geographic)
        text += " · writes the site's geographic record";
    return text;
}

void TerrainImportDialog::setDemSpacingNominal(double nominal) {
    if (spacingEdited_)
        return;
    const bool wasUpdating = updating_;
    updating_ = true;
    spacing_->setValue(nominal);
    updating_ = wasUpdating;
}

void TerrainImportDialog::refreshGeoTiffLabels() {
    const bool wasUpdating = updating_;
    updating_ = true;
    const bool blocked = fileBlocked();
    dep3_->setEnabled(!blocked && facts_ && fitsProduct(*facts_, DemProduct::Dep3));
    glo30_->setEnabled(!blocked && facts_ && fitsProduct(*facts_, DemProduct::Glo30));
    verticalChoice_->setEnabled(!blocked && facts_ && facts_->verticalReference.empty());
    if (facts_) {
        const bool nad83 = facts_->horizontalDatum == "NAD83";
        demDatum_->setText(nad83 ? QStringLiteral("NAD83, labelled approximation  ⚠ Approximation")
                                 : QStringLiteral("WGS84"));
        demDatumNote_->setVisible(nad83);
        demDatumNote_->setText("NAD83 is taken as WGS84. Sources disagree on the size of the difference, "
                               "from about a metre upward, so positions are approximate. The terrain "
                               "keeps the NAD83 label.");
    }
    // The vertical caption: the file's reference, or the product's (UX §20.3).
    if (facts_ && !facts_->verticalReference.empty())
        demVerticalCaption_->setText("The file names its reference, so its heights are " +
                                     QString::fromStdString(facts_->verticalReference) + ".");
    else if (dep3_->isChecked())
        demVerticalCaption_->setText("3DEP heights are NAVD88 over the conterminous US. The sources read do "
                                     "not cover other areas, so check the reference there.");
    else if (glo30_->isChecked())
        demVerticalCaption_->setText("GLO-30 heights are EGM2008, and a surface: buildings and trees are "
                                     "included.");
    else
        demVerticalCaption_->setText("Choose the source product first.");
    // The mismatch block, when the site's reference differs (UX §20.3).
    const bool showMismatch = mismatch() && !blocked;
    mismatchBox_->setVisible(showMismatch);
    if (showMismatch) {
        mismatchText_->setText(QStringLiteral("The heights are %1. The site's elevation is %2.")
                                   .arg(effectiveVertical(), siteVertical()));
        mismatchCaption_->setText("Mixing them needs an offset and a note. SketchyUp adds no geoid model, so "
                                  "enter the difference at this place from a source you trust. To change "
                                  "the site's reference instead, use Site settings.");
    }
    // The site row.
    const auto &site = doc_.site();
    if (!site || !site->geographic)
        demSite_->setText("This site has no geographic location, so the zone and vertical reference are not "
                          "checked. Set one in Site settings.");
    else
        demSite_->setText(QStringLiteral("Zone %1 · vertical %2 · %3")
                              .arg(demZoneLabel(site->geographic->utmZone, site->geographic->utmNorth),
                                   siteVertical(), QString::fromStdString(site->geographic->datum)));
    // The window's captions: the origin it is centred on, a zone the file crosses, a leaving window.
    QStringList notes;
    if (windowModes_->checkedId() == 0 && site)
        notes << QStringLiteral("Centred on the site origin, E %1, N %2.")
                     .arg(decimal(site->easting, 1), decimal(site->northing, 1));
    if (!positionProblem_.isEmpty())
        notes << positionProblem_;
    if (facts_ && facts_->zones.size() > 1) {
        QStringList crossed;
        for (const int zone : facts_->zones)
            crossed << QString::number(zone);
        notes << QStringLiteral("The file crosses into zones %1. The window and the grid are in one zone, and "
                                "the preview clips the file to its band.")
                     .arg(crossed.join(", "));
    }
    const auto window = demWindow();
    if (facts_ && window && !demWindowInside(*facts_, *window, 8))
        notes << "The window leaves the file: move it inside the dashed outline, or choose Fit to the file.";
    demWindowCaption_->setText(notes.join(' '));
    // The live counts (UX §20.2): the dry run's, when it is for these settings.
    const auto terrain = report()["terrain"].toObject();
    const bool current = dryRun_ && dryRun_->command == command() && !checking();
    const auto dem = dryRun_ ? dryRun_->sourceReport["dem"].toObject() : QJsonObject();
    const auto grid = dem["grid"].toObject();
    if (current && dryRun_->result && !terrain.isEmpty() && !grid.isEmpty()) {
        const auto columns = grid["columns"].toDouble(), rows = grid["rows"].toDouble();
        const auto spacing = dem["spacing"].toDouble(), nominal = dem["nominalSpacing"].toDouble();
        const auto posts = dem["posts"].toObject();
        const double limit = double(TerrainLimits::triangles);
        const double room = limit - double(doc_.materializedTotals().terrainTriangles) -
                            terrain["triangles"].toDouble();
        const QString voids = posts["voids"].toDouble() > 0 ? QStringLiteral("refused")
                                                             : QStringLiteral("none (a window with a void is refused)");
        const QString first = QStringLiteral("Grid %1 × %2 = %3 posts · spacing %4 m (nominal %5 m) · ")
                                  .arg(count(columns), count(rows), count(columns * rows), count(spacing),
                                       count(nominal));
        const QString second = QStringLiteral("%1 triangles · room left in this model: %2 of %3")
                                   .arg(count(terrain["triangles"].toDouble()), count(room), count(limit));
        demGrid_->setText(first + (dem["decimated"].toBool() ? QStringLiteral("decimated") : QStringLiteral("not decimated")) +
                          QStringLiteral("\n") + second + QStringLiteral("\nVoids: ") + voids);
    } else if (dryRun_ && dryRun_->geotiff && !dryRun_->error.isEmpty())
        demGrid_->setText("The grid is not made: the refusal above says why.");
    else
        demGrid_->setText("The grid is counted when the window is checked.");
    // The spacing: the nominal post size by default, and its caption. A grid the limits would pass
    // is decimated, and an amber notice says so (UX §20.2).
    setDemSpacingNominal(demNominal());
    const auto spacing = spacing_->value();
    const auto nominal = QString::number(std::llround(demNominal()));
    const auto decimatedTo = dem["spacing"].toDouble();
    if (spacingEdited_ && spacing < demNominal())
        demSpacingCaption_->setText(QStringLiteral("✕ Finer than the file's posts (%1 m) would only interpolate. "
                                                   "Use %1 m or more.")
                                        .arg(nominal));
    else if (current && dem["decimated"].toBool() && decimatedTo > 0)
        demSpacingCaption_->setText(QStringLiteral("⚠ %1").arg(
            decimatedNotice(QString::number(std::llround(spacingEdited_ ? spacing : demNominal())),
                            QString::number(std::llround(decimatedTo)))));
    else
        demSpacingCaption_->setText(QStringLiteral("Nominal %1 m, the file's post size. A coarser spacing is "
                                                   "accepted; the engine decimates a grid that passes the limits.")
                                        .arg(nominal));
    updating_ = wasUpdating;
}

void TerrainImportDialog::refreshGeoTiffOverlays() {
    // The window, the file and the grid in the preview (UX §20.2, §20.7 item 12), in the frame the
    // preview's points use: the site origin, else the dry run's origin, else the window's centre
    // rounded to 100 m, as the engine proposes.
    const auto window = demWindow();
    std::optional<std::pair<double, double>> origin;
    if (const auto &site = doc_.site(); site)
        origin = std::pair{site->easting, site->northing};
    else if (dryRun_ && dryRun_->result)
        origin = std::pair{dryRun_->result->origin.easting, dryRun_->result->origin.northing};
    else if (window)
        origin = std::pair{std::round((window->e0 + window->width() / 2) / 100) * 100,
                           std::round((window->n0 + window->height() / 2) / 100) * 100};
    if (!origin || (!facts_ && !window)) {
        preview_->setOverlays({});
        preview_->setSummary({});
        return;
    }
    auto local = [&](double easting, double northing) {
        return QPointF(easting - origin->first, northing - origin->second);
    };
    TerrainPreviewOverlays overlays;
    overlays.origin = QPointF(0, 0);
    // The accessible description, in words (UX §20.6): the window, the file, the grid.
    QStringList summary;
    if (facts_) {
        const auto [zone, north] = window ? std::pair{window->zone, window->north}
                                          : std::pair{demCentre(*facts_).zone, demCentre(*facts_).north};
        for (const auto &point : demFootprintUtm(*facts_, zone, north, 32))
            overlays.file.push_back(local(point[0], point[1]));
        overlays.fileLabel = "File " + QFileInfo(setup_.fileName).fileName();
        summary << QStringLiteral("File %1 by %2 degrees")
                       .arg(decimal(facts_->maxLongitude - facts_->minLongitude, 2),
                            decimal(facts_->maxLatitude - facts_->minLatitude, 2));
    }
    if (window) {
        const double e0 = window->e0, n0 = window->n0, e1 = window->e1, n1 = window->n1;
        overlays.window = {local(e0, n0), local(e1, n0), local(e1, n1), local(e0, n1)};
        overlays.windowLabel = QStringLiteral("Window %1 by %2 m")
                                   .arg(count(window->width()), count(window->height()));
        overlays.windowOutside = facts_ && !demWindowInside(*facts_, *window, 8);
        summary.prepend(QStringLiteral("Window %1 by %2 metres in zone %3")
                            .arg(count(window->width()), count(window->height()),
                                 demZoneLabel(window->zone, window->north)));
        // The grid: the spacing the dry run used, else the one the person chose, else the nominal.
        const auto dem = dryRun_ ? dryRun_->sourceReport["dem"].toObject() : QJsonObject();
        const double spacing = dem.contains("spacing") ? dem["spacing"].toDouble()
                               : spacingEdited_        ? spacing_->value()
                                                       : demNominal();
        const double columns = std::floor(window->width() / spacing + 1e-9) + 1;
        const double rows = std::floor(window->height() / spacing + 1e-9) + 1;
        if (spacing > 0 && columns >= 2 && rows >= 2) {
            const double east = e0 + (columns - 1) * spacing, north = n0 + (rows - 1) * spacing;
            overlays.grid = {local(e0, n0), local(east, n0), local(east, north), local(e0, north)};
            overlays.gridLabel = QStringLiteral("Grid %1 × %2 posts")
                                     .arg(count(columns), count(rows));
            summary << QStringLiteral("Grid %1 by %2 posts, %3 metres apart")
                           .arg(count(columns), count(rows), count(spacing));
        }
    }
    // The void posts of a refused window, located (UX §20.5, §20.7 item 11).
    if (dryRun_ && !dryRun_->voids.empty()) {
        for (const auto &point : dryRun_->voids)
            overlays.voids.push_back(local(point[0], point[1]));
        overlays.voidLabel = QStringLiteral("%1 void posts").arg(count(double(dryRun_->voidCount)));
    }
    preview_->setOverlays(overlays);
    preview_->setSummary(summary.join(QStringLiteral(". ")));
}

QString TerrainImportDialog::demReportText() const {
    QStringList out;
    out << "Terrain import report: " + QFileInfo(setup_.fileName).fileName();
    std::string code;
    const auto refusal = demRefusalSentence(code);
    const auto full = report();
    const auto terrain = full["terrain"].toObject();
    const auto dem = full["dem"].toObject();
    // The void posts a refused window lists (UX §20.7 item 11), in the report as on the preview.
    auto listVoids = [&] {
        if (!dryRun_ || dryRun_->voids.empty())
            return;
        out << QStringLiteral("Void posts, E and N in metres, the first %1 of %2:")
                   .arg(count(double(dryRun_->voids.size())), count(double(dryRun_->voidCount)));
        for (const auto &point : dryRun_->voids)
            out << "  E " + decimal(point[0], 1) + " · N " + decimal(point[1], 1);
    };
    if (dem.isEmpty()) {
        // No import report: the file or the settings were refused before the window was read.
        if (!refusal.isEmpty()) {
            out << "Outcome: refused. Nothing was imported.";
            out << demPlainMessage(refusal);
            const auto steps = refusalNextSteps(code);
            if (!steps.isEmpty())
                out << steps;
            listVoids();
        } else
            out << (demBlocker().isEmpty() ? QString("No report yet: the source is being checked.")
                                           : demBlocker());
        return out.join('\n') + '\n';
    }
    const bool refused = !refusal.isEmpty();
    const auto outcome = terrain["outcome"].toString();
    out << "Outcome: " + (refused || outcome == "refused"    ? QString("refused. Nothing was imported.")
                          : outcome == "importedWithNotices" ? QString("ready, with notices")
                                                             : QString("ready"));
    if (refused) {
        out << demPlainMessage(refusal);
        const auto steps = refusalNextSteps(code);
        if (!steps.isEmpty())
            out << steps;
    }
    // The source: the product, the post and the compression, the size and its exact count.
    QStringList source{dem["product"].toString()};
    if (facts_) {
        source << demArcSecondLabel(facts_->postArcSecondsNorthSouth);
        source << QString::fromStdString(facts_->compression);
    }
    const auto bytes = full["bytes"].toDouble();
    out << "Source: " + source.join(", ") + QStringLiteral(" · ") +
               QLocale().formattedDataSize(qint64(bytes), 1) + QStringLiteral(" (") + count(bytes) +
               QStringLiteral(" bytes)");
    out << "Licence: " + dem["licence"].toString();
    out << "SHA-256: " + full["sha256"].toString();
    const auto window = dem["window"].toObject();
    if (!window.isEmpty()) {
        const double width = window["easting1"].toDouble() - window["easting0"].toDouble();
        const double height = window["northing1"].toDouble() - window["northing0"].toDouble();
        out << QStringLiteral("Window: zone %1 · E %2 to %3 · N %4 to %5 · %6")
                   .arg(demZoneLabel(window["zone"].toInt(), window["north"].toBool()),
                        decimal(window["easting0"].toDouble(), 1), decimal(window["easting1"].toDouble(), 1))
                   .arg(decimal(window["northing0"].toDouble(), 1), decimal(window["northing1"].toDouble(), 1),
                        width == height ? count(width) + QStringLiteral(" m square")
                                        : count(width) + QStringLiteral(" by ") + count(height) + QStringLiteral(" m"));
    }
    const bool decimated = dem["decimated"].toBool();
    out << QStringLiteral("Spacing: %1 m (nominal %2 m) · %3")
               .arg(count(dem["spacing"].toDouble()), count(dem["nominalSpacing"].toDouble()),
                    decimated ? QStringLiteral("decimated") : QStringLiteral("not decimated"));
    const auto posts = dem["posts"].toObject(), grid = dem["grid"].toObject();
    out << QStringLiteral("Grid: %1 × %2 = %3 posts · read %4 · outside %5 · voids %6 (a window with a void is "
                          "refused)")
               .arg(count(grid["columns"].toDouble()), count(grid["rows"].toDouble()),
                    count(posts["grid"].toDouble()), count(posts["read"].toDouble()),
                    count(posts["outside"].toDouble()), count(posts["voids"].toDouble()));
    listVoids();
    const auto datum = dem["datum"].toString();
    out << (datum == "NAD83" ? QString("Datum: NAD83, taken as WGS84 (labelled approximation)")
                             : "Datum: " + datum);
    const bool fromFile = facts_ && !facts_->verticalReference.empty();
    const auto note = dem["verticalNote"].toString();
    const auto offset = dem["verticalOffset"].toDouble();
    out << QStringLiteral("Heights: %1, %2 · offset %3 m")
               .arg(dem["vertical"].toString(), fromFile ? QStringLiteral("from the file")
                                                         : QStringLiteral("chosen (the file names none)"),
                    decimal(offset));
    const auto other = doc_.materializedTotals().terrainTriangles;
    out << QStringLiteral("Triangles: %1 · %2")
               .arg(count(terrain["triangles"].toDouble()),
                    other ? QStringLiteral("the model's other terrains hold %1").arg(count(double(other)))
                          : QStringLiteral("the model had none before this import"));
    // The engine's credit reads "Credit: U.S. Geological Survey" for 3DEP; the line gives the label once.
    auto credit = dem["credit"].toString();
    if (credit.startsWith(QStringLiteral("Credit: ")))
        credit.remove(0, int(QStringLiteral("Credit: ").size()));
    out << "Credit: " + credit;
    if (const auto disclaimer = dem["disclaimer"].toString(); !disclaimer.isEmpty())
        out << disclaimer + ".";
    QString record = "not written";
    if (dryRun_ && dryRun_->result && dryRun_->result->establish && dryRun_->result->establish->geographic)
        record = "written at the origin, labelled imported, with the file's name and SHA-256";
    else if (const auto &site = doc_.site(); site && site->geographic)
        record = "not written: the site keeps its own record";
    else if (doc_.site())
        record = "not written: the site already has its origin";
    out << "Geographic record: " + record;
    // The notices, once each, in the dialog's words (UX §20.4).
    const auto notices = terrain["notices"].toArray();
    if (!notices.isEmpty()) {
        out << "" << "Notices";
        const auto requested = QString::number(std::llround(spacingEdited_ ? spacing_->value() : demNominal()));
        for (const auto &value : notices) {
            const auto kind = value.toObject();
            out << "  " + demNoticeText(kind["kind"].toString().toStdString(), kind["message"].toString(),
                                        dem["vertical"].toString(), decimal(offset), note, requested,
                                        QString::number(std::llround(dem["spacing"].toDouble())));
        }
    }
    return out.join('\n') + '\n';
}

} // namespace sketchy
