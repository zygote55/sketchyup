#pragma once
#include "app/settled_dialog.hpp"
#include "app/terrain_dem_window.hpp"
#include "app/terrain_preview.hpp"
#include "app/terrain_run.hpp"
#include "app/theme.hpp"
#include "core/model.hpp"
#include "io/geotiff_terrain.hpp"
#include <QByteArray>
#include <QDialog>
#include <QJsonObject>
#include <QThreadPool>
#include <QTimer>
#include <array>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>
class QButtonGroup;
class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QFrame;
class QGridLayout;
class QLabel;
class QLineEdit;
class QProgressBar;
class QPushButton;
class QRadioButton;
class QTableWidget;
class QTreeWidget;
class QTreeWidgetItem;
class QVBoxLayout;
class QWidget;
namespace sketchy {
class TerrainRoleTable;
struct TerrainImportDryRun;
// R095.d: File > Import terrain… (ADR 0168 "Decisions > Interaction design"). Settings on the
// left (format, units, layer or code roles, point-file columns, site coordinates and
// triangulation), the live preview on the right (Sources / Surface, elevation shading), a
// footer status with Show report, and the bounded report with locations that zoom the
// preview.
//
// Every change runs the core's terrain.import dry run again (parseTerrainSourceRequest and
// runTerrainImport) on a worker, debounced, one run at a time, with stale results dropped.
// D-044: the footer shows the run's stage and a determinate progress bar, and the preview the
// stage while it has nothing to show; a change cancels the running dry run, and Cancel or
// closing the dialog cancels it, so the destructor waits only until its next checkpoint.
// Import applies the current dry run through applyTerrainImport: ONE labelled "Import terrain"
// edit on the open document (owner decision 11), the same core path as terrain.import. A
// refusal shows a banner that nothing was imported and keeps Import disabled.
//
// R107.b4 (UX_DESIGN §20): a .tif or .tiff file is the GeoTIFF elevation format, the third source of
// this dialog. Its file is read in windows from its path (`Setup::path`), so `bytes` stays empty.
// Its settings are the product, the window (around the site origin, centre and size, or corners,
// in UTM with latitude and longitude beside each point), the spacing, and the vertical reference
// with the offset and note a mismatch needs. Every dry run is the engine's (parseGeoTiffTerrainRequest
// and runTerrainImport); the dialog chooses the window and words the refusals.
class TerrainImportDialog final : public Settled<QDialog> {
  public:
    struct Setup {
        QString fileName; // shown in the title and kept as the site provenance
        QByteArray bytes; // the source, at most 64 MiB (not used for a GeoTIFF)
        ThemeColors colors;
        QString path; // R107.b4: a GeoTIFF's file, read from its path in windows
        // R107.b4: Site settings…, opened by the window (it holds the viewport the dialog does not).
        std::function<void()> openSiteSettings;
    };
    TerrainImportDialog(Document &doc, Setup setup, QWidget *parent);
    ~TerrainImportDialog() override;

    // The window calls this after every change to the document.
    void documentChanged();
    void setTheme(const ThemeColors &colors);

    // The terrain.import fields the settings stand for (without `data`).
    QJsonObject command() const;
    bool checking() const;
    // D-044: whether a dry run is on the worker (checking() also covers the debounce).
    bool running() const { return running_; }
    QProgressBar *progressBar() const { return progress_; }
    void done(int result) override;
    bool canImport() const;
    // The latest dry run's bounded report (empty before the first one) and its text form.
    QJsonObject report() const;
    QString reportText() const;
    void saveReport(const QString &path) const;
    // After Import: the operation record (terrain, tiles, siteEstablished, importReport).
    const QJsonObject &operation() const { return operation_; }
    TerrainPreview *preview() const { return preview_; }
    QTreeWidget *reportList() const { return reportList_; }
    // R107.b4: the file is refused, and the person chose another file (Window opens the chooser).
    bool chooseAnotherFile() const { return chooseAnother_; }
    bool geotiff() const;

  private:
    void build();
    void buildGeoTiff(QVBoxLayout *column);
    void initGeoTiff();
    void refreshGeoTiff();
    void refreshGeoTiffWindow();
    void refreshGeoTiffLabels();
    void refreshGeoTiffOverlays();
    void updateLatLon(QDoubleSpinBox *easting, QDoubleSpinBox *northing, QDoubleSpinBox *latitude,
                      QDoubleSpinBox *longitude);
    void updateFromLatLon(QDoubleSpinBox *easting, QDoubleSpinBox *northing,
                          QDoubleSpinBox *latitude, QDoubleSpinBox *longitude);
    void fitToFile();
    void setWindowMode(int mode);
    std::optional<DemWindow> demWindow() const;
    QString siteVertical() const; // the site's vertical label; empty when it has no location
    QString effectiveVertical() const;
    bool fileBlocked() const;
    bool mismatch() const;
    QString demBlocker() const;
    QString demRefusalBanner() const;
    QString demRefusalSentence(std::string &code) const;
    QString demReportText() const;
    QString demReadyText() const;
    double demNominal() const;
    QString refusalNextSteps(const std::string &code) const;
    void setDemSpacingNominal(double nominal);
    void schedule(int delayMs = 250);
    void start();
    void finished(std::uint64_t generation, std::shared_ptr<TerrainImportDryRun> result);
    void refresh();
    void refreshFormat();
    void refreshUnits();
    void refreshRoles();
    void refreshSample();
    void refreshSite();
    void refreshTriangulation();
    void refreshReport();
    void refreshFooter();
    void refreshProgress();
    bool showsRun() const;
    void cancelRun();
    void applyPreset();
    void chooseLocation(QTreeWidgetItem *item);
    void importNow();
    QString settingsProblem() const;
    QString styleSheetFor(const ThemeColors &colors) const;
    bool points() const;

    Document &doc_;
    Setup setup_;
    QThreadPool pool_;
    QTimer rerun_;
    QTimer progressPoll_;
    std::shared_ptr<TerrainRunControl> control_; // the running dry run's, while running_
    bool running_{}, pending_{};
    bool closed_{}; // done() was called: no further dry runs start
    int polledStage_{-1}; // the stage the footer last showed
    std::uint64_t generation_{}, revision_{};
    std::shared_ptr<TerrainImportDryRun> dryRun_;
    QJsonObject operation_;
    bool originEdited_{};
    bool unitsCleared_{}; // the user chose no units over the DXF's declared ones
    bool updating_{};
    // R107.b4: the GeoTIFF page. The file's facts are read once, from its header; a file the
    // reader cannot use (its header or its post size) is refused before any window is chosen.
    std::optional<GeoTiffFacts> facts_;
    std::string factsRefusal_;   // the refusal code of the header, when it has one
    QString factsSentence_;      // its plain sentence
    bool chooseAnother_{};
    bool spacingEdited_{};       // the person typed a spacing; otherwise the nominal post size
    double nominal_{};           // the file's nominal post size, metres (the reader's rounding)
    QString positionProblem_;    // a latitude and longitude outside the window's zone, said under the window

    QLabel *subline_{};
    QFrame *banner_{};
    QLabel *bannerText_{};
    QPushButton *chooseOther_{};
    QComboBox *format_{}, *units_{};
    QLabel *unitsCaption_{}, *crsCaption_{};
    QWidget *triangulationBox_{};
    QWidget *pointsBox_{};
    QComboBox *preset_{}, *delimiter_{}, *header_{};
    QGridLayout *columnsGrid_{};
    std::vector<QComboBox *> columns_;
    QTableWidget *sample_{};
    QLabel *rolesHeading_{};
    TerrainRoleTable *roleTable_{};
    // R107.b4: the GeoTIFF page (shown for a GeoTIFF only).
    QWidget *demBox_{};
    QLabel *demFile_{}, *demDatum_{}, *demDatumNote_{}, *demProductCaption_{}, *demVerticalCaption_{};
    QLabel *demSpacingCaption_{}, *demWindowCaption_{}, *demGrid_{}, *demSite_{};
    QRadioButton *dep3_{}, *glo30_{};
    QRadioButton *windowOrigin_{}, *windowCentre_{}, *windowCorners_{};
    QButtonGroup *windowModes_{};
    QWidget *sizeBox_{}, *centreBox_{}, *cornersBox_{}, *zoneRow_{};
    QComboBox *verticalChoice_{}, *zone_{};
    QDoubleSpinBox *width_{}, *height_{};
    QDoubleSpinBox *centreE_{}, *centreN_{}, *centreLat_{}, *centreLon_{};
    QDoubleSpinBox *swE_{}, *swN_{}, *swLat_{}, *swLon_{};
    QDoubleSpinBox *neE_{}, *neN_{}, *neLat_{}, *neLon_{};
    QPushButton *demFit_{}, *siteSettings_{};
    QDoubleSpinBox *spacing_{};
    QFrame *mismatchBox_{};
    QLabel *mismatchTitle_{}, *mismatchText_{}, *mismatchCaption_{};
    QDoubleSpinBox *offset_{};
    QLineEdit *why_{};
    QLabel *sourceExtents_{}, *documentOrigin_{}, *formula_{}, *localExtents_{};
    QWidget *originBox_{};
    QDoubleSpinBox *originE_{}, *originN_{}, *originZ_{};
    QPushButton *originReset_{};
    QLineEdit *crs_{};
    QComboBox *boundary_{};
    QDoubleSpinBox *maxEdge_{};
    QCheckBox *flip_{}, *steiner_{};
    QRadioButton *sources_{}, *surface_{};
    QCheckBox *shading_{};
    QPushButton *fit_{};
    TerrainPreview *preview_{};
    QWidget *reportPanel_{};
    QLabel *reportSummary_{};
    QTreeWidget *reportList_{};
    QPushButton *copyReport_{}, *saveReport_{};
    QLabel *status_{};
    QProgressBar *progress_{};
    QPushButton *showReport_{}, *cancel_{}, *import_{};
};
} // namespace sketchy
