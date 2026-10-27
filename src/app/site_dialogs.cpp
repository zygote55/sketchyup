#include "app/site_dialogs.hpp"
#include "app/accessible_names.hpp"
#include "app/viewport.hpp"
#include "automation/commands.hpp"
#include "core/terrain.hpp"
#include "geo/geographic.hpp"
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QFrame>
#include <QHBoxLayout>
#include <QJsonArray>
#include <QLabel>
#include <QLineEdit>
#include <QLocale>
#include <QPushButton>
#include <QSpinBox>
#include <QVBoxLayout>
#include <cmath>
namespace sketchy {
namespace {
QLabel *text(const QString &value, const QString &name) {
    auto *label = new QLabel(value);
    label->setObjectName(name);
    label->setTextFormat(Qt::PlainText);
    label->setWordWrap(true);
    return label;
}
QLabel *title(const QString &value) {
    // The terrain dialogs' heading style (ADR 0168 dialogs share it).
    auto *label = text(value, "terrainDialogTitle");
    label->setAccessibleName(value);
    return label;
}
QString metres(double value) { return QLocale().toString(value, 'f', 3) + " m"; }
QLabel *field(QFormLayout *form, const QString &caption, const QString &name) {
    auto *label = text({}, name);
    label->setTextInteractionFlags(Qt::TextSelectableByMouse | Qt::TextSelectableByKeyboard);
    label->setFocusPolicy(Qt::StrongFocus); // Read-only values are reachable with Tab.
    label->setAccessibleName("Site " + caption.toLower());
    form->addRow(caption, label);
    return label;
}
QDoubleSpinBox *coordinate(const QString &name, const QString &accessible, double value) {
    auto *spin = new QDoubleSpinBox;
    spin->setObjectName(name);
    spin->setAccessibleName(accessible);
    spin->setRange(-siteCoordinateLimit, siteCoordinateLimit);
    spin->setDecimals(3);
    spin->setSuffix(" m");
    spin->setGroupSeparatorShown(true);
    spin->setKeyboardTracking(false);
    spin->setValue(value);
    return spin;
}
} // namespace

SiteOriginDialog::SiteOriginDialog(Mode mode, Document &doc, Viewport &view, QWidget *parent)
    : Settled<QDialog>(parent), mode_(mode), doc_(doc), view_(view) {
    const bool reanchor = mode == Mode::Reanchor;
    if (reanchor && !doc.site())
        throw std::runtime_error("The document has no site origin to re-anchor");
    if (!reanchor && doc.site())
        throw std::runtime_error("The site origin is already set; re-anchor to change it");
    setObjectName(reanchor ? "siteReanchorDialog" : "siteSetOriginDialog");
    setWindowTitle(reanchor ? "Re-anchor site origin" : "Set site origin");
    setModal(true);
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(20, 16, 20, 16);
    layout->setSpacing(10);
    layout->addWidget(title(windowTitle()));
    auto *explanation = text(
        reanchor ? QString("Enter the new site coordinates of the model origin. Re-anchor moves "
                           "all geometry, section planes and saved views by the difference, so "
                           "every site coordinate stays the same; only model coordinates change. "
                           "One edit: Undo restores it.")
                 : QString("Enter the site coordinates (projected metres) of the model origin. "
                           "Geometry does not move: model coordinates stay as they are and gain "
                           "these site coordinates. Later terrain imports use this origin."),
        "siteOriginExplanation");
    layout->addWidget(explanation);
    const auto current = doc.site().value_or(SiteOrigin{});
    if (reanchor)
        layout->addWidget(text(QString("Current origin: E %1 · N %2 · Z %3")
                                   .arg(metres(current.easting), metres(current.northing),
                                        metres(current.elevation)),
                               "siteOriginCurrent"));
    auto *form = new QFormLayout;
    form->setRowWrapPolicy(QFormLayout::WrapLongRows);
    const QString prefix = reanchor ? "New site " : "Site ";
    easting_ = coordinate("siteOriginEasting", prefix + "easting", current.easting);
    northing_ = coordinate("siteOriginNorthing", prefix + "northing", current.northing);
    elevation_ = coordinate("siteOriginElevation", prefix + "elevation", current.elevation);
    form->addRow("Easting (E)", easting_);
    form->addRow("Northing (N)", northing_);
    form->addRow("Elevation (Z)", elevation_);
    if (!reanchor) {
        crs_ = new QLineEdit;
        crs_->setObjectName("siteOriginCrs");
        crs_->setAccessibleName("CRS label");
        crs_->setMaxLength(int(siteCrsBytes));
        crs_->setPlaceholderText("Optional, for example EPSG:32633");
        form->addRow("CRS label", crs_);
    }
    layout->addLayout(form);
    if (reanchor) {
        delta_ = text({}, "siteReanchorDelta");
        layout->addWidget(delta_);
    }
    banner_ = new QFrame;
    banner_->setObjectName("terrainDialogBanner");
    banner_->setProperty("tone", "failure");
    auto *bannerRow = new QHBoxLayout(banner_);
    bannerRow->setContentsMargins(12, 8, 12, 8);
    bannerText_ = text({}, "siteOriginProblem");
    bannerRow->addWidget(bannerText_, 1);
    layout->addWidget(banner_);
    banner_->hide();
    layout->addStretch(1);
    auto *buttons = new QHBoxLayout;
    buttons->addStretch(1);
    auto *cancel = new QPushButton("Cancel");
    cancel->setObjectName("siteOriginCancel");
    cancel->setAutoDefault(false);
    apply_ = new QPushButton(reanchor ? "Re-anchor" : "Set origin");
    apply_->setObjectName("siteOriginApply");
    apply_->setAccessibleName(reanchor ? "Re-anchor site origin" : "Set site origin");
    apply_->setAutoDefault(false);
    apply_->setDefault(true);
    buttons->addWidget(cancel);
    buttons->addWidget(apply_);
    layout->addLayout(buttons);
    nameInternalEditors(this);
    // Wrapped text and the coordinate fields keep their full height at any text size.
    // (The layout's minimum, not setMinimumWidth, which the constraint would override.)
    layout->setSizeConstraint(QLayout::SetMinimumSize);
    explanation->setMinimumWidth(480);
    connect(cancel, &QPushButton::clicked, this, &QDialog::reject);
    connect(apply_, &QPushButton::clicked, this, [this] { apply(); });
    recheck_.setSingleShot(true);
    recheck_.setInterval(200);
    connect(&recheck_, &QTimer::timeout, this, [this] { check(); });
    for (auto *spin : {easting_, northing_, elevation_})
        connect(spin, &QDoubleSpinBox::valueChanged, this, [this] { recheck_.start(); });
    check();
}
QJsonObject SiteOriginDialog::command() const {
    QJsonObject command{{"command", mode_ == Mode::Reanchor ? "site.reanchor" : "site.set"},
                        {"easting", easting_->value()},
                        {"northing", northing_->value()},
                        {"elevation", elevation_->value()}};
    if (crs_ && !crs_->text().trimmed().isEmpty())
        command["crs"] = crs_->text().trimmed();
    return command;
}
bool SiteOriginDialog::canApply() const { return problem_.isEmpty(); }
void SiteOriginDialog::check() {
    recheck_.stop();
    problem_.clear();
    if (mode_ == Mode::Reanchor && doc_.site()) {
        const auto &site = *doc_.site();
        // local = site - origin: everything moves by old origin - new origin.
        const Vec3 move{site.easting - easting_->value(), site.northing - northing_->value(),
                        site.elevation - elevation_->value()};
        delta_->setText(QString("Geometry moves by X %1 · Y %2 · Z %3 in model coordinates.")
                            .arg(metres(move.x), metres(move.y), metres(move.z)));
        if (move.x == 0 && move.y == 0 && move.z == 0)
            problem_ = "Enter a different origin to re-anchor.";
        // Locks are authoritative: a locked top-level body cannot move, so it blocks the edit.
        for (const auto &[id, body] : doc_.bodies())
            if (problem_.isEmpty() && !body->parent && body->locked)
                problem_ = QString("Re-anchor is not available: “%1” is locked, and re-anchoring "
                                   "moves every top-level body. Unlock it first.")
                               .arg(QString::fromStdString(body->name));
    }
    if (problem_.isEmpty()) {
        // The command's own refusal (the ±1,000 km coordinate limit, invalid values), from a
        // dry run on a copy of the document.
        Document candidate = doc_;
        try {
            executeBatch(candidate,
                         {{"apiVersion", 1},
                          {"documentId", QString::fromStdString(candidate.identity())},
                          {"expectedRevision", QString::number(candidate.revision())},
                          {"commands", QJsonArray{command()}}});
        } catch (const std::exception &error) {
            problem_ = (mode_ == Mode::Reanchor ? "Re-anchor is not available: "
                                                : "The origin cannot be set: ") +
                       QString::fromUtf8(error.what());
            if (!problem_.endsWith('.'))
                problem_ += '.';
        }
    }
    const bool blocked = !problem_.isEmpty() &&
                         problem_ != "Enter a different origin to re-anchor.";
    banner_->setVisible(blocked);
    bannerText_->setText(problem_);
    bannerText_->setAccessibleName(problem_);
    apply_->setEnabled(problem_.isEmpty());
    apply_->setToolTip(problem_);
}
void SiteOriginDialog::apply() {
    check();
    if (!canApply())
        return;
    try {
        view_.commitTerrainCommand(command());
    } catch (const std::exception &error) {
        problem_ = QString::fromUtf8(error.what());
        banner_->show();
        bannerText_->setText(problem_);
        apply_->setEnabled(false);
        return;
    }
    accept();
}

SiteGeographicDialog::SiteGeographicDialog(Document &doc, Viewport &view, QWidget *parent)
    : Settled<QDialog>(parent), doc_(doc), view_(view) {
    setObjectName("siteGeographicDialog");
    setWindowTitle("Site location");
    setModal(true);
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(20, 16, 20, 16);
    layout->setSpacing(10);
    layout->addWidget(title("Site location"));
    layout->addWidget(text("Enter where the site origin is, in WGS84 latitude and longitude. The "
                           "origin keeps its projected position, so this location must be its UTM "
                           "position in the zone shown, to within 1 m. Use the origin's position "
                           "computes the location from the origin in the zone and hemisphere you "
                           "choose.",
                           "siteGeographicExplanation"));
    const auto current = doc.site().value_or(SiteOrigin{}).geographic.value_or(SiteGeographic{});
    latitude_ = new QDoubleSpinBox;
    latitude_->setObjectName("siteGeoLatitudeField");
    latitude_->setAccessibleName("Latitude, north positive");
    latitude_->setRange(utmMinLatitude, utmMaxLatitude);
    latitude_->setDecimals(6);
    latitude_->setSuffix("°");
    latitude_->setValue(current.latitude);
    longitude_ = new QDoubleSpinBox;
    longitude_->setObjectName("siteGeoLongitudeField");
    longitude_->setAccessibleName("Longitude, east positive");
    longitude_->setRange(-180, 180);
    longitude_->setDecimals(6);
    longitude_->setSuffix("°");
    longitude_->setValue(current.longitude);
    zone_ = new QSpinBox;
    zone_->setObjectName("siteGeoZoneField");
    zone_->setAccessibleName("UTM zone");
    zone_->setRange(0, 60);
    zone_->setSpecialValueText("From longitude");
    zone_->setValue(current.utmZone);
    hemisphere_ = new QComboBox;
    hemisphere_->setObjectName("siteGeoHemisphere");
    hemisphere_->setAccessibleName("Hemisphere of the UTM zone");
    hemisphere_->addItems({"North", "South"});
    hemisphere_->setCurrentIndex(current.utmNorth ? 0 : 1);
    datum_ = new QComboBox;
    datum_->setObjectName("siteGeoDatumField");
    datum_->setAccessibleName("Datum");
    datum_->addItem("WGS84", "WGS84");
    datum_->addItem("NAD83 (labelled approximation)", "NAD83");
    datum_->setCurrentIndex(current.datum == "NAD83" ? 1 : 0);
    vertical_ = new QComboBox;
    vertical_->setObjectName("siteGeoVerticalField");
    vertical_->setAccessibleName("Vertical reference of the origin's elevation");
    for (const auto name : siteVerticalReferences)
        vertical_->addItem(QString::fromLatin1(name.data(), qsizetype(name.size())));
    vertical_->setCurrentText(current.verticalReference.empty()
                                  ? QString("unknown")
                                  : QString::fromStdString(current.verticalReference));
    source_ = new QLineEdit;
    source_->setObjectName("siteGeoSourceField");
    source_->setAccessibleName("Source note");
    source_->setMaxLength(int(siteSourceBytes));
    source_->setText(QString::fromStdString(current.provenance.source));
    source_->setPlaceholderText("For example: phone GPS, or a map click");
    auto *form = new QFormLayout;
    form->setRowWrapPolicy(QFormLayout::WrapLongRows);
    form->addRow("Latitude (north positive)", latitude_);
    form->addRow("Longitude (east positive)", longitude_);
    form->addRow("UTM zone (0: from longitude)", zone_);
    form->addRow("Hemisphere", hemisphere_);
    form->addRow("Datum", datum_);
    form->addRow("Vertical reference of the elevation", vertical_);
    form->addRow("Source note", source_);
    layout->addLayout(form);
    auto *useOriginButton = new QPushButton("Use the origin's position");
    useOriginButton->setObjectName("siteGeoUseOrigin");
    useOriginButton->setAccessibleName("Use the origin's position");
    useOriginButton->setAutoDefault(false);
    layout->addWidget(useOriginButton);
    banner_ = new QFrame;
    banner_->setObjectName("terrainDialogBanner");
    banner_->setProperty("tone", "failure");
    auto *bannerRow = new QHBoxLayout(banner_);
    bannerRow->setContentsMargins(12, 8, 12, 8);
    bannerText_ = text({}, "siteGeoProblem");
    bannerRow->addWidget(bannerText_, 1);
    layout->addWidget(banner_);
    banner_->hide();
    layout->addStretch(1);
    auto *buttons = new QHBoxLayout;
    buttons->addStretch(1);
    auto *cancel = new QPushButton("Cancel");
    cancel->setObjectName("siteGeoCancel");
    cancel->setAutoDefault(false);
    apply_ = new QPushButton("Save location");
    apply_->setObjectName("siteGeoApply");
    apply_->setAccessibleName("Save site geographic location");
    apply_->setAutoDefault(false);
    apply_->setDefault(true);
    buttons->addWidget(cancel);
    buttons->addWidget(apply_);
    layout->addLayout(buttons);
    nameInternalEditors(this);
    layout->setSizeConstraint(QLayout::SetMinimumSize);
    connect(cancel, &QPushButton::clicked, this, &QDialog::reject);
    connect(apply_, &QPushButton::clicked, this, [this] { apply(); });
    connect(useOriginButton, &QPushButton::clicked, this, [this] { useOrigin(); });
    recheck_.setSingleShot(true);
    recheck_.setInterval(200);
    connect(&recheck_, &QTimer::timeout, this, [this] { check(); });
    for (auto *spin : {latitude_, longitude_})
        connect(spin, &QDoubleSpinBox::valueChanged, this, [this] { recheck_.start(); });
    connect(zone_, &QSpinBox::valueChanged, this, [this] { recheck_.start(); });
    check();
}
QJsonObject SiteGeographicDialog::command() const {
    QJsonObject command{{"command", "site.set_geographic"},
                        {"latitude", latitude_->value()},
                        {"longitude", longitude_->value()},
                        {"datum", datum_->currentData().toString()},
                        {"verticalReference", vertical_->currentText()},
                        {"provenance", "typed"},
                        {"source", source_->text().trimmed()}};
    if (zone_->value() > 0)
        command["zone"] = zone_->value();
    return command;
}
void SiteGeographicDialog::check() {
    recheck_.stop();
    problem_.clear();
    // The command's own refusal (the datum, the band, the zone, the 1 m match to the origin), from
    // a dry run on a copy of the document, as the origin dialogs do.
    Document candidate = doc_;
    try {
        executeBatch(candidate, {{"apiVersion", 1},
                                 {"documentId", QString::fromStdString(candidate.identity())},
                                 {"expectedRevision", QString::number(candidate.revision())},
                                 {"commands", QJsonArray{command()}}});
    } catch (const std::exception &error) {
        problem_ = QString::fromUtf8(error.what());
    }
    banner_->setVisible(!problem_.isEmpty());
    bannerText_->setText(problem_);
    bannerText_->setAccessibleName(problem_);
    apply_->setEnabled(problem_.isEmpty());
    apply_->setToolTip(problem_);
}
void SiteGeographicDialog::useOrigin() {
    // The origin's own position needs its zone and hemisphere; the zone is the user's to choose.
    try {
        if (zone_->value() == 0)
            throw std::runtime_error("Choose the UTM zone of the origin to use its position");
        const auto &site = *doc_.site();
        const auto position = utmToGeo(
            {zone_->value(), hemisphere_->currentIndex() == 0, site.easting, site.northing});
        latitude_->setValue(position.latitude);
        longitude_->setValue(position.longitude);
        source_->setText(QString("Computed from the site origin, UTM zone %1 %2")
                             .arg(zone_->value())
                             .arg(hemisphere_->currentIndex() == 0 ? "North" : "South"));
        check();
    } catch (const std::exception &error) {
        problem_ = QString::fromUtf8(error.what());
        banner_->show();
        bannerText_->setText(problem_);
        apply_->setEnabled(false);
    }
}
void SiteGeographicDialog::apply() {
    check();
    if (!canApply())
        return;
    try {
        view_.commitTerrainCommand(command());
    } catch (const std::exception &error) {
        problem_ = QString::fromUtf8(error.what());
        banner_->show();
        bannerText_->setText(problem_);
        apply_->setEnabled(false);
        return;
    }
    accept();
}

SiteSettingsDialog::SiteSettingsDialog(Document &doc, Viewport &view, QWidget *parent)
    : Settled<QDialog>(parent), doc_(doc), view_(view) {
    setObjectName("siteSettingsDialog");
    setWindowTitle("Site settings");
    setModal(true);
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(20, 16, 20, 16);
    layout->setSpacing(10);
    auto *heading = title("Site settings");
    layout->addWidget(heading);
    // Without an origin: how one is established, and Set origin….
    none_ = new QFrame;
    none_->setObjectName("siteNoOrigin");
    auto *noneLayout = new QVBoxLayout(none_);
    noneLayout->setContentsMargins(0, 0, 0, 0);
    noneLayout->setSpacing(8);
    auto *explain = text(
        "This document has no site origin. The first terrain import (File → Import terrain…) "
        "establishes it from the source: the proposed origin is the centre of the source "
        "extents, rounded to 100 m, and can be edited in the import dialog. To give the model "
        "site coordinates without importing, set the origin here.",
        "siteNoOriginText");
    explain->setFocusPolicy(Qt::StrongFocus);
    explain->setTextInteractionFlags(Qt::TextSelectableByMouse | Qt::TextSelectableByKeyboard);
    explain->setAccessibleName("No site origin");
    explain->setAccessibleDescription(explain->text());
    noneLayout->addWidget(explain);
    auto *setRow = new QHBoxLayout;
    set_ = new QPushButton("Set origin…");
    set_->setObjectName("siteSetOrigin");
    set_->setAccessibleName("Set site origin");
    set_->setAutoDefault(false);
    setRow->addWidget(set_);
    setRow->addStretch(1);
    noneLayout->addLayout(setRow);
    layout->addWidget(none_);
    // With an origin: read-only fields and Re-anchor….
    origin_ = new QFrame;
    origin_->setObjectName("siteOrigin");
    auto *originLayout = new QVBoxLayout(origin_);
    originLayout->setContentsMargins(0, 0, 0, 0);
    originLayout->setSpacing(8);
    auto *form = new QFormLayout;
    form->setRowWrapPolicy(QFormLayout::WrapLongRows);
    easting_ = field(form, "Easting", "siteEasting");
    northing_ = field(form, "Northing", "siteNorthing");
    elevation_ = field(form, "Elevation", "siteElevation");
    crs_ = field(form, "CRS label", "siteCrs");
    source_ = field(form, "Source", "siteSource");
    sha256_ = field(form, "SHA-256", "siteSha256");
    sha256_->setWordWrap(false); // Two fixed lines; a wrapped label's height lags in the form.
    originLayout->addLayout(form);
    originLayout->addWidget(text("Site coordinates are model coordinates plus this origin. The CRS "
                                 "is a label: coordinates are never reprojected. To move the "
                                 "origin, re-anchor; geometry then moves so its site coordinates "
                                 "stay the same.",
                                 "siteOriginNote"));
    // R107.a (ADR 0181 §2.1): the geographic location, read only, or none with Set location….
    geoNone_ = new QFrame;
    geoNone_->setObjectName("siteNoGeographic");
    auto *geoNoneLayout = new QVBoxLayout(geoNone_);
    geoNoneLayout->setContentsMargins(0, 0, 0, 0);
    geoNoneLayout->setSpacing(8);
    geoNoneLayout->addWidget(text("No geographic location. Set the origin's WGS84 latitude and "
                                  "longitude to place the site on the Earth.",
                                  "siteNoGeographicText"));
    auto *geoSetRow = new QHBoxLayout;
    geoSet_ = new QPushButton("Set location…");
    geoSet_->setObjectName("siteSetGeographic");
    geoSet_->setAccessibleName("Set site geographic location");
    geoSet_->setAutoDefault(false);
    geoSetRow->addWidget(geoSet_);
    geoSetRow->addStretch(1);
    geoNoneLayout->addLayout(geoSetRow);
    originLayout->addWidget(geoNone_);
    geo_ = new QFrame;
    geo_->setObjectName("siteGeographic");
    auto *geoLayout = new QVBoxLayout(geo_);
    geoLayout->setContentsMargins(0, 0, 0, 0);
    geoLayout->setSpacing(8);
    auto *geoForm = new QFormLayout;
    geoForm->setRowWrapPolicy(QFormLayout::WrapLongRows);
    geoLatitude_ = field(geoForm, "Latitude", "siteGeoLatitude");
    geoLongitude_ = field(geoForm, "Longitude", "siteGeoLongitude");
    geoZone_ = field(geoForm, "UTM zone", "siteGeoZone");
    geoDatum_ = field(geoForm, "Datum", "siteGeoDatum");
    geoVertical_ = field(geoForm, "Vertical reference", "siteGeoVertical");
    geoConvergence_ = field(geoForm, "Grid convergence", "siteGeoConvergence");
    geoSource_ = field(geoForm, "Source", "siteGeoSource");
    geoLayout->addLayout(geoForm);
    geoWarning_ = text({}, "siteGeoWarning");
    geoLayout->addWidget(geoWarning_);
    auto *geoRow = new QHBoxLayout;
    geoChange_ = new QPushButton("Change location…");
    geoChange_->setObjectName("siteChangeGeographic");
    geoChange_->setAccessibleName("Change site geographic location");
    geoChange_->setAutoDefault(false);
    geoClear_ = new QPushButton("Clear location");
    geoClear_->setObjectName("siteClearGeographic");
    geoClear_->setAccessibleName("Clear site geographic location");
    geoClear_->setAutoDefault(false);
    geoRow->addWidget(geoChange_);
    geoRow->addWidget(geoClear_);
    geoRow->addStretch(1);
    geoLayout->addLayout(geoRow);
    originLayout->addWidget(geo_);
    connect(geoSet_, &QPushButton::clicked, this, [this] { openGeographic(); });
    connect(geoChange_, &QPushButton::clicked, this, [this] { openGeographic(); });
    connect(geoClear_, &QPushButton::clicked, this, [this] {
        try {
            view_.commitTerrainCommand(QJsonObject{{"command", "site.clear_geographic"}});
        } catch (const std::exception &error) {
            refresh();
            geoWarning_->setText(QString::fromUtf8(error.what()));
            geoWarning_->setVisible(true);
            return;
        }
        refresh();
    });
    auto *reanchorRow = new QHBoxLayout;
    reanchor_ = new QPushButton("Re-anchor…");
    reanchor_->setObjectName("siteReanchor");
    reanchor_->setAccessibleName("Re-anchor site origin");
    reanchor_->setAutoDefault(false);
    reanchorRow->addWidget(reanchor_);
    reanchorRow->addStretch(1);
    originLayout->addLayout(reanchorRow);
    layout->addWidget(origin_);
    layout->addStretch(1);
    auto *buttons = new QHBoxLayout;
    buttons->addStretch(1);
    close_ = new QPushButton("Close");
    close_->setObjectName("siteSettingsClose");
    close_->setAutoDefault(false);
    close_->setDefault(true);
    buttons->addWidget(close_);
    layout->addLayout(buttons);
    connect(close_, &QPushButton::clicked, this, &QDialog::accept);
    connect(set_, &QPushButton::clicked, this, [this] { open(SiteOriginDialog::Mode::Set); });
    connect(reanchor_, &QPushButton::clicked, this,
            [this] { open(SiteOriginDialog::Mode::Reanchor); });
    layout->setSizeConstraint(QLayout::SetMinimumSize);
    heading->setMinimumWidth(440);
    refresh();
    view_.setSiteSettingsOpen(true);
}
SiteSettingsDialog::~SiteSettingsDialog() { view_.setSiteSettingsOpen(false); }
void SiteSettingsDialog::open(SiteOriginDialog::Mode mode) {
    SiteOriginDialog dialog(mode, doc_, view_, this);
    dialog.exec();
    refresh();
    (doc_.site() ? reanchor_ : set_)->setFocus(Qt::OtherFocusReason);
}
void SiteSettingsDialog::openGeographic() {
    SiteGeographicDialog dialog(doc_, view_, this);
    dialog.exec();
    refresh();
}
void SiteSettingsDialog::refresh() {
    const auto &site = doc_.site();
    none_->setVisible(!site);
    origin_->setVisible(site.has_value());
    if (!site) {
        setTabOrder(set_, close_);
        return;
    }
    easting_->setText(metres(site->easting));
    northing_->setText(metres(site->northing));
    elevation_->setText(metres(site->elevation));
    crs_->setText(site->crs.empty() ? QString("None") : QString::fromStdString(site->crs));
    crs_->setToolTip("The CRS label is set with the origin (Set origin or the first terrain "
                     "import) and kept by Re-anchor; it is informational only.");
    source_->setText(site->established.source.empty()
                         ? QString("Not recorded (set by command)")
                         : QString::fromStdString(site->established.source));
    // The digest as two lines of two 16-digit groups; the tooltip and description hold it whole.
    const auto digest = QString::fromStdString(site->established.sha256);
    QStringList groups;
    for (int i = 0; i < digest.size(); i += 16)
        groups << digest.mid(i, 16);
    sha256_->setText(digest.isEmpty() ? QString("None")
                                      : groups.mid(0, 2).join(' ') + '\n' +
                                            groups.mid(2).join(' '));
    sha256_->setToolTip(digest);
    // QFormLayout's height-for-width pass gives a multi-line, non-wrapping label one line.
    sha256_->setMinimumHeight(sha256_->sizeHint().height());
    for (auto *label : {easting_, northing_, elevation_, crs_, source_})
        label->setAccessibleDescription(label->text());
    sha256_->setAccessibleDescription(digest.isEmpty() ? QString("None") : digest);
    // ADR 0181 §2.1 and §2.6: the geographic location, read only, with the sun study's warning.
    const auto &geographic = site->geographic;
    geoNone_->setVisible(!geographic);
    geo_->setVisible(geographic.has_value());
    if (geographic) {
        const auto &record = *geographic;
        geoLatitude_->setText(QLocale().toString(record.latitude, 'f', 6) + "°");
        geoLongitude_->setText(QLocale().toString(record.longitude, 'f', 6) + "°");
        geoZone_->setText(
            QString("%1 %2").arg(record.utmZone).arg(record.utmNorth ? "North" : "South"));
        geoDatum_->setText(QString::fromStdString(record.datum) +
                           (record.datum == "NAD83" ? " (labelled approximation)" : ""));
        geoVertical_->setText(QString::fromStdString(record.verticalReference));
        geoConvergence_->setText(QLocale().toString(record.convergence, 'f', 4) +
                                 "° (read only: grid north is this far clockwise of true north; "
                                 "the model is not rotated)");
        geoSource_->setText(QString::fromStdString(record.provenance.kind) +
                            (record.provenance.source.empty()
                                 ? QString()
                                 : ": " + QString::fromStdString(record.provenance.source)));
        QStringList warnings;
        if (record.datum == "NAD83")
            warnings << "NAD83 is treated as WGS84: a labelled approximation (ADR 0181 §2.3).";
        const auto distance = solarSiteDistance(doc_);
        if (distance && *distance > siteGeographicToleranceMetres)
            warnings << QString("The sun study's location is %1 m from this site location. Use "
                                "site location in the sun editor to match them.")
                            .arg(QLocale().toString(*distance, 'f', 1));
        geoWarning_->setText(warnings.join('\n'));
        geoWarning_->setVisible(!warnings.isEmpty());
    }
    setTabOrder(easting_, northing_);
    setTabOrder(northing_, elevation_);
    setTabOrder(elevation_, crs_);
    setTabOrder(crs_, source_);
    setTabOrder(source_, sha256_);
    setTabOrder(sha256_, reanchor_);
    setTabOrder(reanchor_, close_);
}
} // namespace sketchy
