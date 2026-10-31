// R095.d: File > Import terrain… (ADR 0168 "Decisions > Interaction design"). Offscreen: units
// gate Import, role mapping and point-column presets reach the import, a refusal shows the
// banner and changes nothing, a successful import is one undoable edit, the report copies as
// text, and choosing a location zooms the preview. `--screenshots DIR` also saves the dialog
// in its Sources, Surface and refusal states.
// R107.b4 (UX_DESIGN §20): the GeoTIFF page of the same dialog, on the committed fixtures: the
// product and the vertical reference, the default window, latitude and longitude entry, Fit to the
// file, the refusals (file and window), the located voids, the mismatch with an offset and a note,
// the report with Copy and Save, the import as one undoable edit that writes the site's record, the
// Credit line in Entity info, the keyboard order and accessible names, and the link to the user
// guide (UX §20.1) with its Tab order and the section it asks for.
#include "app/entity_info_panel.hpp"
#include "app/organization_panel.hpp"
#include "app/terrain_dem_window.hpp"
#include "app/terrain_import_dialog.hpp"
#include "app/terrain_section.hpp"
#include "app/window.hpp"
#include "core/terrain.hpp"
#include "geo/geographic.hpp"
#include "io/document_io.hpp"
#include "io/geotiff_terrain.hpp"
#include "native_dialog_audit.hpp"
#include "terrain_import_fixtures.hpp"
#include "terrain_run_hold.hpp"
#include <QAction>
#include <QApplication>
#include <QCheckBox>
#include <QClipboard>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QElapsedTimer>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLineEdit>
#include <QLocale>
#include <QFile>
#include <QFileDialog>
#include <QLabel>
#include <QProgressBar>
#include <QPushButton>
#include <QRadioButton>
#include <QTableWidget>
#include <QTemporaryDir>
#include <QTest>
#include <QTimer>
#include <QTreeWidget>
#include <QUrl>
#include <iostream>
using namespace sketchy;
namespace {
void check(bool ok, const std::string &message) {
    if (!ok)
        throw std::runtime_error(message);
}
template <class T> T *child(QWidget &dialog, const char *name) {
    auto *widget = dialog.findChild<T *>(name);
    check(widget != nullptr, std::string("Dialog has ") + name);
    return widget;
}
void settle(TerrainImportDialog &dialog) {
    check(QTest::qWaitFor([&] { return !dialog.checking(); }, 30000), "The dry run settles");
    QTest::qWait(5);
    check(!dialog.checking(), "The dry run stays settled");
}
void choose(QComboBox *combo, const QString &data) {
    const auto index = combo->findData(data);
    check(index >= 0, ("Choice offered: " + data).toStdString());
    combo->setCurrentIndex(index);
    emit combo->activated(index);
}

// A small DXF: three nested square contours on CONTOURS at 10, 11 and 12 m, a breakline on
// BREAKS, a 9 m square on SITE (a boundary once mapped) and a stray line on NOTES.
QByteArray smallDxf() {
    QStringList out;
    auto pair = [&](int code, const QString &value) { out << QString::number(code) << value; };
    pair(0, "SECTION");
    pair(2, "HEADER");
    pair(9, "$ACADVER");
    pair(1, "AC1015");
    pair(9, "$INSUNITS");
    pair(70, "6");
    pair(0, "ENDSEC");
    pair(0, "SECTION");
    pair(2, "ENTITIES");
    int handle = 0x100;
    auto square = [&](const QString &layer, double half, double z) {
        pair(0, "LWPOLYLINE");
        pair(5, QString::number(handle++, 16).toUpper());
        pair(8, layer);
        pair(90, "4");
        pair(70, "1");
        pair(38, QString::number(z));
        for (const auto &[x, y] : std::vector<std::pair<double, double>>{
                 {-half, -half}, {half, -half}, {half, half}, {-half, half}}) {
            pair(10, QString::number(x));
            pair(20, QString::number(y));
        }
    };
    square("SITE", 40, 9);
    square("CONTOURS", 30, 10);
    square("CONTOURS", 20, 11);
    square("CONTOURS", 10, 12);
    // A 3D POLYLINE breakline between the 9 m and 10 m squares.
    pair(0, "POLYLINE");
    pair(5, QString::number(handle++, 16).toUpper());
    pair(8, "BREAKS");
    pair(66, "1");
    pair(10, "0.0");
    pair(20, "0.0");
    pair(30, "0.0");
    pair(70, "8");
    int vertex = 0x800;
    for (const auto &[y, z] : std::vector<std::pair<int, double>>{{-35, 9.2}, {0, 9.5}, {35, 9.8}}) {
        pair(0, "VERTEX");
        pair(5, QString::number(vertex++, 16).toUpper());
        pair(8, "BREAKS");
        pair(10, "-38");
        pair(20, QString::number(y));
        pair(30, QString::number(z));
        pair(70, "32");
    }
    pair(0, "SEQEND");
    pair(5, QString::number(vertex++, 16).toUpper());
    pair(8, "BREAKS");
    pair(0, "LINE");
    pair(5, QString::number(handle++, 16).toUpper());
    pair(8, "NOTES");
    for (const auto &[code, value] : std::vector<std::pair<int, QString>>{
             {10, "32"}, {20, "-35"}, {30, "9.5"}, {11, "38"}, {21, "-35"}, {31, "9.5"}})
        pair(code, value);
    pair(0, "ENDSEC");
    pair(0, "EOF");
    return (out.join('\n') + '\n').toUtf8();
}
// A 5 x 5 grid of survey shots as PNEZD with a header: northing before easting, far from the
// origin, two codes (GND and TOP).
QByteArray smallPnezd() {
    QStringList lines{"Point,Northing,Easting,Elevation,Description"};
    int point = 1;
    for (int i = 0; i < 5; ++i)
        for (int j = 0; j < 5; ++j)
            lines << QString("%1,%2,%3,%4,%5")
                         .arg(point++)
                         .arg(5234500 + 10 * i)
                         .arg(512300 + 10 * j)
                         .arg(100 + i + 0.5 * j)
                         .arg(i == 2 && j == 2 ? "TOP" : "GND");
    return (lines.join('\n') + '\n').toUtf8();
}

// The committed GeoTIFF fixtures (tests/fixtures/geotiff, made by its generator).
QString geotiffFile(const char *name) {
    return QString::fromStdString(std::string(SOURCE_DIR) + "/tests/fixtures/geotiff/" + name);
}

struct Fixture {
    Window &window;
    std::unique_ptr<TerrainImportDialog> dialog;
    // `path` is a GeoTIFF's file (R107.b4); the text formats pass their bytes. `openGuide` takes
    // the user guide's request in place of the system's viewer, so a test never launches one.
    Fixture(Window &w, const QString &name, const QByteArray &bytes, const QString &path = {},
            std::function<bool(const QUrl &)> openGuide = [](const QUrl &) { return true; })
        : window(w) {
        dialog = std::make_unique<TerrainImportDialog>(
            window.document(),
            TerrainImportDialog::Setup{name, bytes, themeColors(false), path, {},
                                       std::move(openGuide)},
            &window);
        dialog->show();
        check(QTest::qWaitForWindowExposed(dialog.get(), 5000), "The dialog shows");
        settle(*dialog);
    }
};

void unitsGateImport(Window &window) {
    Fixture f(window, "survey.csv", smallPnezd());
    auto &dialog = *f.dialog;
    auto *import = child<QPushButton>(dialog, "terrainImportConfirm");
    auto *status = child<QLabel>(dialog, "terrainImportStatus");
    check(child<QComboBox>(dialog, "terrainImportFormat")->currentData() == "points",
          "A .csv is detected as a point file");
    check(!import->isEnabled(), "Import is disabled until units are chosen");
    check(status->text().contains("Choose units"), "The footer asks for units");
    check(!dialog.preview()->data(), "Nothing is previewed before units are chosen");
    // Enter on the disabled default button changes nothing.
    QTest::keyClick(child<QComboBox>(dialog, "terrainImportFormat"), Qt::Key_Return);
    check(dialog.isVisible(), "Enter does not import without units");
    choose(child<QComboBox>(dialog, "terrainImportUnits"), "usft");
    settle(dialog);
    check(import->isEnabled(), "Choosing units enables Import");
    check(status->text().startsWith("Ready: ") && status->text().contains("triangles"),
          "The footer reports the triangles: " + status->text().toStdString());
    const auto terrain = dialog.report()["terrain"].toObject();
    check(terrain["units"] == "usft" && terrain["conversion"].toString().contains("1200/3937"),
          "The conversion uses the chosen unit");
    check(child<QLabel>(dialog, "terrainImportFormula")->text() == terrain["conversion"].toString(),
          "The formula is shown");
    // The proposed origin, rounded to 100 m, fills the origin fields.
    const auto proposed = terrain["proposedOrigin"].toObject();
    check(std::fmod(proposed["easting"].toDouble(), 100) == 0 &&
              child<QDoubleSpinBox>(dialog, "terrainImportOriginE")->value() ==
                  proposed["easting"].toDouble(),
          "The proposed origin is shown and editable");
    choose(child<QComboBox>(dialog, "terrainImportUnits"), "");
    settle(dialog);
    check(!import->isEnabled(), "Clearing the units disables Import again");
    dialog.reject();
}

void pointColumnPresets(Window &window) {
    Fixture f(window, "survey.csv", smallPnezd());
    auto &dialog = *f.dialog;
    choose(child<QComboBox>(dialog, "terrainImportUnits"), "m");
    settle(dialog);
    auto *preset = child<QComboBox>(dialog, "terrainImportPreset");
    auto *delimiter = child<QComboBox>(dialog, "terrainImportDelimiter");
    check(delimiter->itemText(0) == "Detect (comma)", "The detected delimiter is named");
    check(preset->itemText(0) == "Detect (PNEZD)", "The detected preset is named");
    auto meaning = [&](int column) {
        return child<QComboBox>(dialog, QString("terrainImportColumn%1").arg(column).toUtf8())
            ->currentData()
            .toString();
    };
    check(meaning(1) == "point" && meaning(2) == "northing" && meaning(3) == "easting" &&
              meaning(4) == "elevation" && meaning(5) == "description",
          "Detect shows the detected PNEZD columns");
    auto *sample = child<QTableWidget>(dialog, "terrainImportSample");
    check(sample->rowCount() == 8 && sample->horizontalHeaderItem(1)->text() == "Northing",
          "The sample table shows the header and the first rows");
    const auto pnezd = dialog.report()["terrain"].toObject()["sourceExtents"].toObject();
    // PENZD swaps easting and northing.
    choose(preset, "PENZD");
    settle(dialog);
    check(dialog.command()["columns"] == "PENZD", "The preset reaches the import request");
    check(meaning(2) == "easting" && meaning(3) == "northing", "PENZD sets the column meanings");
    const auto penzd = dialog.report()["terrain"].toObject()["sourceExtents"].toObject();
    check(penzd["min"].toArray()[0] == pnezd["min"].toArray()[1] &&
              penzd["min"].toArray()[1] == pnezd["min"].toArray()[0],
          "PENZD reads easting and northing the other way round");
    // ENZ and XYZ read the first three columns.
    choose(preset, "XYZ");
    settle(dialog);
    check(meaning(1) == "easting" && meaning(2) == "northing" && meaning(3) == "elevation" &&
              meaning(4) == "ignore",
          "XYZ sets easting, northing and elevation");
    // Changing a column makes the preset Custom and sends the list.
    choose(preset, "PNEZD");
    settle(dialog);
    choose(child<QComboBox>(dialog, "terrainImportColumn5"), "ignore");
    settle(dialog);
    check(preset->currentData() == "custom", "Editing a column chooses Custom");
    const auto columns = dialog.command()["columns"].toArray();
    check(columns.size() == 5 && columns[0] == "point" && columns[4] == "ignore",
          "Custom columns reach the import request");
    check(dialog.canImport(), "Custom columns import");
    // Description codes map to roles: TOP as ignored drops one point.
    choose(preset, "PNEZD");
    settle(dialog);
    auto *roles = child<QTableWidget>(dialog, "terrainImportRoles");
    check(roles->rowCount() == 2, "Both description codes are listed");
    const auto before = dialog.report()["terrain"].toObject()["vertices"].toDouble();
    for (int row = 0; row < roles->rowCount(); ++row)
        if (roles->item(row, 0)->text() == "TOP")
            roles->selectRow(row);
    choose(child<QComboBox>(dialog, "terrainImportRole"), "ignore");
    settle(dialog);
    check(dialog.command()["roles"].toObject()["TOP"] == "ignore", "The code role is sent");
    check(dialog.report()["terrain"].toObject()["vertices"].toDouble() == before - 1,
          "The ignored code's point leaves the surface");
    dialog.reject();
}

void roleMappingReachesImport(Window &window) {
    window.demo();
    Fixture f(window, "site.dxf", smallDxf());
    auto &dialog = *f.dialog;
    auto *units = child<QComboBox>(dialog, "terrainImportUnits");
    check(units->currentData() == "header" && units->currentText().contains("metres"),
          "The DXF's declared unit is offered and chosen");
    auto *roles = child<QTableWidget>(dialog, "terrainImportRoles");
    check(roles->rowCount() == 4, "All four layers are listed");
    auto select = [&](const QString &layer) {
        roles->clearSelection();
        for (int row = 0; row < roles->rowCount(); ++row)
            if (roles->item(row, 0)->text() == layer) {
                roles->setCurrentCell(row, 0);
                roles->selectRow(row);
                return;
            }
        throw std::runtime_error("Layer listed: " + layer.toStdString());
    };
    select("SITE");
    check(roles->item(roles->currentRow(), 2)->text() == "Automatic: contour",
          "The inspection's default role is shown");
    choose(child<QComboBox>(dialog, "terrainImportRole"), "boundary");
    select("NOTES");
    choose(child<QComboBox>(dialog, "terrainImportRole"), "ignore");
    settle(dialog);
    check(child<QComboBox>(dialog, "terrainImportBoundary")->currentData() == "boundary",
          "A boundary layer chooses Boundary mode");
    const auto command = dialog.command();
    check(command["roles"].toObject()["SITE"] == "boundary" &&
              command["roles"].toObject()["NOTES"] == "ignore",
          "The roles reach the import request");
    check(dialog.canImport(), "The mapped source can be imported");
    // Surface mode draws the dry run's triangles.
    child<QRadioButton>(dialog, "terrainImportShowSurface")->setChecked(true);
    check(dialog.preview()->mode() == TerrainPreview::Mode::Surface &&
              !dialog.preview()->data()->triangles.empty(),
          "Surface shows the dry-run mesh");
    child<QCheckBox>(dialog, "terrainImportShading")->setChecked(true);
    check(dialog.preview()->shading(), "Elevation shading turns on");
    const auto revision = window.document().revision();
    const auto history = window.document().history().total;
    QTest::mouseClick(child<QPushButton>(dialog, "terrainImportConfirm"), Qt::LeftButton);
    check(QTest::qWaitFor([&] { return !dialog.isVisible(); }, 5000), "Import closes the dialog");
    check(dialog.result() == QDialog::Accepted, "Import accepts");
    auto &doc = window.document();
    check(doc.revision() == revision + 1 && doc.history().total == history + 1,
          "Import is one edit");
    check(doc.history().entries.back().label == "Import terrain", "The edit is labelled");
    const auto terrain = Id(dialog.operation()["terrain"].toString().toULongLong());
    const auto &record = *doc.bodies().at(terrain)->terrain;
    check(record.parameters.boundary == TerrainBoundaryMode::Boundary,
          "The boundary layer limits the terrain");
    std::size_t boundaries = 0, breaklines = 0;
    for (const auto &source : record.sources) {
        boundaries += source.role == TerrainSourceRole::Boundary;
        breaklines += source.role == TerrainSourceRole::Breakline;
        check(source.reference != "handle 105", "The ignored layer is not a source");
    }
    check(boundaries == 1 && breaklines == 1, "Roles reach the stored sources");
    check(doc.site() && doc.site()->established.source == "site.dxf",
          "The site origin is established with the file as provenance");
    doc.undo();
    check(!doc.bodies().contains(terrain) && !doc.site(), "Undo removes the terrain and origin");
    window.viewport()->refresh();
}

void refusalChangesNothing(Window &window, const QString &screenshots) {
    window.demo();
    auto &doc = window.document();
    const auto encoded = encodeContainer(doc);
    const auto revision = doc.revision();
    const auto history = doc.history().total;
    Fixture f(window, "invalid.dxf", terrain_fixture::terrainInvalid());
    auto &dialog = *f.dialog;
    auto *banner = child<QFrame>(dialog, "terrainImportBanner");
    check(banner->isVisible(), "A refusal shows the banner");
    check(child<QLabel>(dialog, "terrainImportBannerText")->text().contains("Nothing was imported"),
          "The banner says nothing was imported");
    auto *import = child<QPushButton>(dialog, "terrainImportConfirm");
    check(!import->isEnabled(), "A refusal keeps Import disabled");
    check(child<QLabel>(dialog, "terrainImportStatus")->text().startsWith("Refused"),
          "The footer says refused");
    QTest::mouseClick(import, Qt::LeftButton);
    QTest::keyClick(child<QLineEdit>(dialog, "terrainImportCrs"), Qt::Key_Return);
    check(dialog.isVisible(), "Import does nothing while refused");
    // The report: grouped, bounded, copyable.
    auto *show = child<QPushButton>(dialog, "terrainImportShowReport");
    QTest::mouseClick(show, Qt::LeftButton);
    QTest::qWait(20); // the layout gives the report its room
    auto *list = dialog.reportList();
    check(list->isVisible() && show->text() == "Hide report", "Show report opens the report");
    check(list->topLevelItem(0)->text(0).startsWith("Refusals"), "Refusals come first");
    QTreeWidgetItem *crossing = nullptr;
    for (int i = 0; i < list->topLevelItem(0)->childCount(); ++i)
        if (list->topLevelItem(0)->child(i)->data(0, Qt::UserRole + 2) == "crossing-constraints")
            crossing = list->topLevelItem(0)->child(i);
    check(crossing && crossing->childCount() <= 33, "Crossing pairs are listed, at most 32");
    QTest::mouseClick(child<QPushButton>(dialog, "terrainImportCopyReport"), Qt::LeftButton);
    const auto text = QGuiApplication::clipboard()->text();
    check(text == dialog.reportText(), "Copy report puts the text on the clipboard");
    check(text.contains("Outcome: refused. Nothing was imported.") && text.contains("Refusals") &&
              text.contains("[crossing-constraints]") && text.contains("more not listed") &&
              text.contains("Notices"),
          "The report text has the outcome, the groups and the truncation");
    QTemporaryDir directory;
    dialog.saveReport(directory.filePath("report.json"));
    QFile saved(directory.filePath("report.json"));
    check(saved.open(QIODevice::ReadOnly) &&
              QJsonDocument::fromJson(saved.readAll()).object() == dialog.report(),
          "Save report writes the bounded report");
    // Choosing a location zooms the preview and rings it.
    auto *preview = dialog.preview();
    const auto fitScale = preview->scale();
    auto *location = crossing->child(0);
    list->setFocus();
    list->setCurrentItem(location);
    const auto local = location->data(0, Qt::UserRole).toPointF();
    check(preview->marker() && *preview->marker() == local, "The location is ringed");
    check(std::abs(preview->centre().x() - local.x()) < 1e-9 &&
              std::abs(preview->centre().y() - local.y()) < 1e-9,
          "The preview centres the location");
    check(preview->scale() > fitScale, "The preview zooms in");
    if (!screenshots.isEmpty()) {
        dialog.resize(1120, 780);
        QTest::qWait(30);
        dialog.grab().save(screenshots + "/terrain-import-refusal.png");
    }
    const auto fitted = [&] {
        auto *fit = child<QPushButton>(dialog, "terrainImportFit");
        QTest::mouseClick(fit, Qt::LeftButton);
        const auto scale = preview->scale();
        list->setCurrentItem(location->parent());
        list->setCurrentItem(location);
        return scale;
    }();
    // Enter in the list chooses the row; it never reaches the default button.
    QTest::keyClick(list, Qt::Key_Down);
    QTest::keyClick(list, Qt::Key_Return);
    check(dialog.isVisible(), "Enter in the report stays in the dialog");
    check(*preview->marker() == list->currentItem()->data(0, Qt::UserRole).toPointF(),
          "The keyboard moves the ring");
    // The preview keys: arrows pan, minus zooms out, Shift+Z fits.
    const auto centre = preview->centre();
    QTest::keyClick(preview, Qt::Key_Right);
    check(preview->centre().x() > centre.x(), "Right pans");
    QTest::keyClick(preview, Qt::Key_Minus);
    QTest::keyClick(preview, Qt::Key_Z, Qt::ShiftModifier);
    check(std::abs(preview->scale() - fitted) < 1e-9, "Shift+Z fits");
    dialog.reject();
    check(encodeContainer(doc) == encoded && doc.revision() == revision &&
              doc.history().total == history,
          "A refusal leaves the document, revision and history unchanged");
}

// The dialog audit's checks (R084.u) on the open dialog, in its point-file (units not chosen) and
// refusal-with-report states: every Tab stop named, Tab and Shift+Tab reach every control in
// mirrored reading order, and Import is the default button.
void keyboardAudit(Window &window, const QString &name, const QByteArray &bytes, bool report,
                   const QString &path = {}) {
    window.demo();
    Fixture f(window, name, bytes, path);
    auto &dialog = *f.dialog;
    if (report)
        QTest::mouseClick(child<QPushButton>(dialog, "terrainImportShowReport"), Qt::LeftButton);
    dialog.activateWindow();
    check(QTest::qWaitForWindowFocused(&dialog), "The dialog has keyboard focus");
    child<QComboBox>(dialog, "terrainImportFormat")->setFocus(Qt::TabFocusReason);
    QStringList findings;
    const auto json = dialog_audit::auditOpen(dialog, findings);
    check(findings.isEmpty(), "Keyboard audit of " + name.toStdString() + ": " +
                                  findings.join("; ").toStdString());
    check(json["defaultButton"] == "Import", "Import is the default button");
    check(json["tabOrder"].toArray().size() >= 14, "The settings, preview and footer are Tab stops");
    dialog.reject();
}

// The menu path: File > Import terrain… opens the dialog for the file, Import adds one edit.
void menuImport(Window &window, const QString &screenshots) {
    window.demo();
    QTemporaryDir directory;
    const auto path = directory.filePath("site.dxf");
    QFile file(path);
    check(file.open(QIODevice::WriteOnly) && file.write(smallDxf()) > 0, "Write the fixture");
    file.close();
    auto &doc = window.document();
    const auto history = doc.history().total;
    std::exception_ptr failure;
    bool driven = false;
    QTimer driver;
    QObject::connect(&driver, &QTimer::timeout, [&] {
        for (auto *widget : QApplication::topLevelWidgets()) {
            if (!widget->isVisible())
                continue;
            if (auto *chooser = qobject_cast<QFileDialog *>(widget)) {
                chooser->selectFile(path);
                if (auto *field = chooser->findChild<QLineEdit *>("fileNameEdit");
                    field && field->isVisible())
                    field->setText(path);
                QMetaObject::invokeMethod(chooser, "accept", Qt::QueuedConnection);
            } else if (auto *dialog = dynamic_cast<TerrainImportDialog *>(widget);
                       dialog && !driven) {
                driven = true;
                try {
                    settle(*dialog);
                    if (!screenshots.isEmpty()) {
                        dialog->resize(1120, 780);
                        QTest::qWait(30);
                        dialog->grab().save(screenshots + "/terrain-import-sources.png");
                        dialog->findChild<QRadioButton *>("terrainImportShowSurface")
                            ->setChecked(true);
                        dialog->findChild<QCheckBox *>("terrainImportShading")->setChecked(true);
                        QTest::qWait(20);
                        dialog->grab().save(screenshots + "/terrain-import-surface.png");
                    }
                    check(dialog->canImport(), "The DXF with declared units can be imported");
                    QTest::keyClick(dialog->findChild<QLineEdit *>("terrainImportCrs"),
                                    Qt::Key_Return);
                } catch (...) {
                    failure = std::current_exception();
                    dialog->reject();
                }
            }
        }
    });
    driver.start(50);
    window.findChild<QAction *>("file.importTerrain")->trigger();
    driver.stop();
    if (failure)
        std::rethrow_exception(failure);
    check(driven, "The import dialog opened");
    check(doc.history().total == history + 1 &&
              doc.history().entries.back().label == "Import terrain",
          "Enter imports as one labelled edit");
    Id terrain{};
    for (const auto &[id, body] : doc.bodies())
        if (body->terrain)
            terrain = id;
    check(terrain && window.viewport()->selectedBody() == terrain, "The new terrain is selected");
    doc.undo();
    check(doc.history().position == history, "Undo takes the import back");
    window.viewport()->refresh();
}
// D-044: progress and cancel. A dry run held at the start of triangulation shows its stage in
// the footer and the preview and a determinate progress bar, without taking the focus; a changed
// setting cancels it and starts again; Cancel (and closing the dialog) during a run closes at
// once, the destructor does not wait for the run to finish, and the document is unchanged.
void progressAndCancel(Window &window) {
    window.demo();
    auto &doc = window.document();
    const auto encoded = encodeContainer(doc);
    const auto revision = doc.revision();
    const auto history = doc.history().total;
    auto open = [&] {
        auto dialog = std::make_unique<TerrainImportDialog>(
            doc, TerrainImportDialog::Setup{"small.dxf", smallDxf(), themeColors(false)}, &window);
        dialog->show();
        check(QTest::qWaitForWindowExposed(dialog.get(), 5000), "The dialog shows");
        return dialog;
    };
    {
        test::TerrainRunHold hold;
        auto dialog = open();
        check(QTest::qWaitFor([&] { return hold.holding == 1; }, 10000),
              "The dry run reaches triangulation");
        auto *status = child<QLabel>(*dialog, "terrainImportStatus");
        auto *bar = dialog->progressBar();
        check(QTest::qWaitFor([&] { return status->text() == "Checking: triangulating…"; }, 5000),
              "The footer names the stage: " + status->text().toStdString());
        check(bar->isVisible() && bar->objectName() == "terrainImportProgress" &&
                  !bar->accessibleName().isEmpty() && bar->focusPolicy() == Qt::NoFocus &&
                  bar->minimum() == 0 && bar->maximum() == 100 && bar->value() >= 30 &&
                  bar->value() < 95,
              "A determinate, named progress bar shows the run: " +
                  std::to_string(bar->value()));
        check(dialog->preview()->placeholder() == "Checking the source: triangulating…",
              "The preview names the stage: " + dialog->preview()->placeholder().toStdString());
        check(!dialog->canImport() && dialog->checking(), "Import waits for the run");
        auto *flip = child<QCheckBox>(*dialog, "terrainImportFlip");
        flip->setFocus(Qt::OtherFocusReason);
        QTest::qWait(150);
        check(dialog->focusWidget() == flip, "Progress does not take the focus");
        // A changed setting cancels the stale run and starts another.
        flip->toggle();
        check(QTest::qWaitFor([&] { return hold.cancelled == 1 && hold.held == 2; }, 10000),
              "A changed setting cancels the running dry run and starts again");
        hold.release = true;
        settle(*dialog);
        check(!bar->isVisible() && status->text().startsWith("Ready") && dialog->canImport() &&
                  dialog->command()["flatMode"] == QJsonValue(flip->isChecked() ? "flip" : "reportOnly"),
              "The new run finishes for the new setting: " + status->text().toStdString());
        dialog.reset();
    }
    // Cancel and closing the window, each during a held run.
    for (const bool close : {false, true}) {
        test::TerrainRunHold hold;
        auto dialog = open();
        check(QTest::qWaitFor([&] { return hold.holding == 1; }, 10000),
              "The dry run reaches triangulation");
        QElapsedTimer clock;
        clock.start();
        if (close)
            dialog->close();
        else
            QTest::mouseClick(child<QPushButton>(*dialog, "terrainImportCancel"), Qt::LeftButton);
        check(QTest::qWaitFor([&] { return !dialog->isVisible(); }, 2000), "The dialog closes");
        dialog.reset(); // waits for the worker
        const auto elapsed = clock.elapsed();
        std::cout << (close ? "close" : "Cancel") << " during a run: closed and destroyed in "
                  << elapsed << " ms\n";
        check(hold.cancelled == 1 && hold.holding == 0, "The run was cancelled, not finished");
        check(elapsed < 2000, "The dialog closes without waiting for the run");
        check(doc.revision() == revision && doc.history().total == history &&
                  encodeContainer(doc) == encoded,
              "Cancelling during a run changes nothing");
    }
}

// ---- R107.b4: the GeoTIFF page (UX_DESIGN §20) ----------------------------------------------

template <class T> T *widgetOf(Window &window) {
    for (auto *widget : window.findChildren<QWidget *>())
        if (auto *found = dynamic_cast<T *>(widget))
            return found;
    return nullptr;
}
QString number(int value) { return QLocale().toString(value); }

// The large plane (700 by 700 posts at 1 arc-second, around 0.1 W 51.5 N): the defaults, the
// latitude and longitude entry, the live counts, the report with Copy and Save, one undoable import
// that writes the site's record, and the Credit line under Source in Entity info.
void geotiffImport(Window &window) {
    window.demo();
    auto &doc = window.document();
    check(!doc.site(), "The demo model has no site origin");
    const auto history = doc.history().total;
    Fixture f(window, "plane-f32-tile-deflate-pred3-large-le.tif", {},
               geotiffFile("plane-f32-tile-deflate-pred3-large-le.tif"));
    auto &dialog = *f.dialog;
    auto *import = child<QPushButton>(dialog, "terrainImportConfirm");
    auto *status = child<QLabel>(dialog, "terrainImportStatus");
    check(child<QComboBox>(dialog, "terrainImportFormat")->currentData() == "geotiff",
          "A .tif opens the GeoTIFF format");
    check(!child<QComboBox>(dialog, "terrainImportUnits")->isVisible(), "A GeoTIFF offers no units");
    check(!child<QRadioButton>(dialog, "terrainImportDep3")->isChecked() &&
              !child<QRadioButton>(dialog, "terrainImportGlo30")->isChecked(),
          "A 1 arc-second file fits both products, so neither is chosen");
    check(child<QRadioButton>(dialog, "terrainImportDep3")->accessibleName() ==
              "Source product: USGS 3DEP",
          "The product radio has its accessible name");
    check(!import->isEnabled() && status->text() == "Choose the source product: USGS 3DEP or Copernicus GLO-30.",
          "Import waits for the product: " + status->text().toStdString());
    QTest::mouseClick(child<QRadioButton>(dialog, "terrainImportDep3"), Qt::LeftButton);
    settle(dialog);
    check(status->text() == "Choose the vertical reference to import.",
          "The file names no vertical reference, so one is asked for: " + status->text().toStdString());
    check(!dialog.preview()->overlays().file.empty() && !dialog.preview()->overlays().window.empty(),
          "The file's outline and the window are drawn before the first run");
    choose(child<QComboBox>(dialog, "terrainImportVertical"), "NAVD88");
    settle(dialog);
    check(import->isEnabled() && status->text().startsWith("Ready: ") && status->text().contains("triangles"),
          "A product and a vertical reference make it ready: " + status->text().toStdString());
    check(child<QRadioButton>(dialog, "terrainImportWindowCentre")->isChecked(),
          "With no site origin the window is centre and size");
    check(child<QDoubleSpinBox>(dialog, "terrainImportWidth")->value() == 1000 &&
              child<QDoubleSpinBox>(dialog, "terrainImportHeight")->value() == 1000,
          "The default window is 1,000 m square");
    const auto grid = child<QLabel>(dialog, "terrainImportDemGrid")->text();
    check(grid.startsWith("Grid 33 × 33 = " + number(1089) + " posts") && grid.contains("not decimated") &&
              grid.contains("room left in this model"),
          "The live counts: " + grid.toStdString());
    // Latitude and longitude convert to UTM in the window's zone (UX §20.8 question 1).
    const GeoPosition point{51.5, -0.1};
    child<QDoubleSpinBox>(dialog, "terrainImportCentreLat")->setValue(point.latitude);
    child<QDoubleSpinBox>(dialog, "terrainImportCentreLon")->setValue(point.longitude);
    settle(dialog);
    const auto utm = geoToUtm(point, 30);
    check(std::abs(child<QDoubleSpinBox>(dialog, "terrainImportCentreE")->value() - utm.easting) < 0.05 &&
              std::abs(child<QDoubleSpinBox>(dialog, "terrainImportCentreN")->value() - utm.northing) < 0.05,
          "A latitude and longitude set the UTM centre");
    check(import->isEnabled(), "The window at the new centre imports: " + status->text().toStdString());
    // The report: the source, the digest, the window, the spacing, the labels, the posts and the credit.
    const auto report = dialog.reportText();
    const auto digest = dialog.report()["sha256"].toString();
    check(report.startsWith("Terrain import report: plane-f32-tile-deflate-pred3-large-le.tif") &&
              report.contains("Outcome: ready") && report.contains("SHA-256: " + digest) &&
              report.contains("Window: zone 30N · E ") && report.contains(number(1000) + " m square") &&
              report.contains("Spacing: 31 m (nominal 31 m) · not decimated") &&
              report.contains("Grid: 33 × 33 = " + number(1089) + " posts") &&
              report.contains("Datum: WGS84") &&
              report.contains("Heights: NAVD88, chosen (the file names none) · offset 0.00 m") &&
              report.contains("Credit: U.S. Geological Survey"),
          "The report lists the GeoTIFF's facts:\n" + report.toStdString());
    check(report.contains("Geographic record: written at the origin"),
          "The report says the import writes the site's record");
    QTest::mouseClick(child<QPushButton>(dialog, "terrainImportCopyReport"), Qt::LeftButton);
    check(QGuiApplication::clipboard()->text() == dialog.reportText(),
          "Copy report puts the report on the clipboard");
    QTemporaryDir directory;
    dialog.saveReport(directory.filePath("report.txt"));
    QFile saved(directory.filePath("report.txt"));
    check(saved.open(QIODevice::ReadOnly) && QString::fromUtf8(saved.readAll()) == dialog.reportText(),
          "Save report writes the report text");
    const auto &overlays = dialog.preview()->overlays();
    check(overlays.window.size() == 4 && overlays.grid.size() == 4 && overlays.file.size() > 8 &&
              !overlays.windowOutside,
          "The preview draws the file, the window and the grid");
    check(dialog.preview()->accessibleDescription().startsWith(
              "Window " + number(1000) + " by " + number(1000) + " metres in zone 30N"),
          "The preview's description gives the window: " +
              dialog.preview()->accessibleDescription().toStdString());
    QTest::mouseClick(import, Qt::LeftButton);
    check(QTest::qWaitFor([&] { return !dialog.isVisible(); }, 5000), "Import closes the dialog");
    check(dialog.result() == QDialog::Accepted, "Import accepts");
    check(doc.history().total == history + 1 && doc.history().entries.back().label == "Import terrain",
          "Import is one labelled edit");
    const auto terrain = Id(dialog.operation()["terrain"].toString().toULongLong());
    const auto &site = doc.site();
    check(site && site->geographic && site->geographic->verticalReference == "NAVD88" &&
              site->geographic->provenance.kind == "imported" && site->geographic->utmZone == 30 &&
              site->geographic->utmNorth && site->geographic->provenance.sha256 == digest.toStdString(),
          "The import writes the site's geographic record, imported, with the file's digest");
    // The Credit line sits under Source in Entity info (UX §20.7 item 10).
    window.viewport()->setSelection(terrain);
    QTest::qWait(5);
    widgetOf<OrganizationPanel>(window)->showInfo();
    auto *panel = widgetOf<EntityInfoPanel>(window);
    check(panel && panel->terrain(), "Entity info has a Terrain section");
    QTest::qWait(10);
    panel->refresh();
    auto *section = panel->terrain();
    section->refresh();
    auto *credit = child<QLabel>(*section, "terrainCredit");
    check(section->isVisible() && credit->isVisible() &&
              credit->text().startsWith("plane-f32-tile-deflate-pred3-large-le.tif · Credit: U.S. Geological Survey"),
          "The Terrain section shows the Credit under Source: " + credit->text().toStdString());
    doc.undo();
    check(!doc.bodies().contains(terrain) && !doc.site(), "Undo removes the terrain and the site origin");
    window.viewport()->refresh();
}

// The 48 by 40 plane (1 arc-second, both products): a window wider than the file is refused by the
// window; Fit to the file makes it import, its corners lie inside the file, and a GLO-30 import
// carries its surface notice and its licence in the report.
void geotiffFit(Window &window) {
    window.demo();
    auto &doc = window.document();
    const auto history = doc.history().total;
    Fixture f(window, "plane-f32-strip-none-le.tif", {}, geotiffFile("plane-f32-strip-none-le.tif"));
    auto &dialog = *f.dialog;
    auto *import = child<QPushButton>(dialog, "terrainImportConfirm");
    auto *status = child<QLabel>(dialog, "terrainImportStatus");
    auto *banner = child<QLabel>(dialog, "terrainImportBannerText");
    QTest::mouseClick(child<QRadioButton>(dialog, "terrainImportGlo30"), Qt::LeftButton);
    choose(child<QComboBox>(dialog, "terrainImportVertical"), "EGM2008");
    settle(dialog);
    check(child<QFrame>(dialog, "terrainImportBanner")->isVisible() &&
              banner->text().startsWith("✕ Refused. Nothing was imported. Your model is unchanged.") &&
              banner->text().contains("outside the file"),
          "A window wider than the file is refused by name: " + banner->text().toStdString());
    check(!child<QPushButton>(dialog, "terrainImportChooseOther")->isVisible(),
          "A window refusal does not ask for another file");
    check(child<QWidget>(dialog, "terrainImportDemBox")->isEnabled() && !import->isEnabled() &&
              status->text().startsWith("Refused. Nothing was imported."),
          "The settings stay on, and Import stays off: " + status->text().toStdString());
    QTest::mouseClick(child<QPushButton>(dialog, "terrainImportFitFile"), Qt::LeftButton);
    settle(dialog);
    check(child<QRadioButton>(dialog, "terrainImportWindowCorners")->isChecked(),
          "Fit to the file sets the window to corners");
    check(!child<QFrame>(dialog, "terrainImportBanner")->isVisible() && import->isEnabled(),
          "The fitted window imports: " + status->text().toStdString());
    // Every post of the fitted window lies in the file (the same test the reader makes per post).
    const auto facts = geoTiffFacts(geotiffFile("plane-f32-strip-none-le.tif"));
    const DemWindow fitted{30, true, child<QDoubleSpinBox>(dialog, "terrainImportSwE")->value(),
                           child<QDoubleSpinBox>(dialog, "terrainImportSwN")->value(),
                           child<QDoubleSpinBox>(dialog, "terrainImportNeE")->value(),
                           child<QDoubleSpinBox>(dialog, "terrainImportNeN")->value()};
    check(demWindowInside(facts, fitted, 16), "The fitted window lies inside the file");
    const auto report = dialog.reportText();
    check(report.contains("Outcome: ready, with notices") &&
              report.contains("Credit: produced using Copernicus WorldDEM-30") &&
              report.contains("do not incur any liability") &&
              report.contains("GLO-30 heights are a surface model: buildings and trees are included.") &&
              report.contains("Heights: EGM2008, chosen (the file names none) · offset 0.00 m") &&
              report.contains("Datum: WGS84"),
          "The GLO-30 report carries its licence, notice and surface note:\n" + report.toStdString());
    check(!child<QFrame>(dialog, "terrainImportMismatch")->isVisible(),
          "No site, so no mismatch block");
    QTest::mouseClick(import, Qt::LeftButton);
    check(QTest::qWaitFor([&] { return !dialog.isVisible(); }, 5000), "Import closes the dialog");
    check(dialog.result() == QDialog::Accepted && doc.history().total == history + 1,
          "The fitted import is one edit");
    doc.undo();
    window.viewport()->refresh();
}

// The refusals of the file itself (the banner's first line, the settings grey out, Choose another
// file), and a window that covers a void post: refused, with its void posts located on the preview.
void geotiffRefusals(Window &window) {
    window.demo();
    auto &doc = window.document();
    const auto encoded = encodeContainer(doc);
    const auto revision = doc.revision();
    const auto history = doc.history().total;
    for (const char *name : {"refuse-projected.tif", "refuse-jpeg.tif", "refuse-bigtiff.tif"}) {
        Fixture f(window, name, {}, geotiffFile(name));
        auto &dialog = *f.dialog;
        auto *text = child<QLabel>(dialog, "terrainImportBannerText");
        check(child<QFrame>(dialog, "terrainImportBanner")->isVisible() &&
                  text->text().startsWith("✕ Refused. Nothing was imported. Your model is unchanged."),
              std::string(name) + " is refused, and its first line says so: " + text->text().toStdString());
        check(child<QPushButton>(dialog, "terrainImportChooseOther")->isVisible(),
              "A refused file offers Choose another file");
        check(!child<QWidget>(dialog, "terrainImportDemBox")->isEnabled() &&
                  !child<QPushButton>(dialog, "terrainImportConfirm")->isEnabled(),
              "The settings grey out and Import stays off");
        check(dialog.reportText().contains("Outcome: refused. Nothing was imported."),
              "The report says the file was refused");
        dialog.reject();
    }
    check(encodeContainer(doc) == encoded && doc.revision() == revision && doc.history().total == history,
          "Refused files change nothing");
    // A window over the void post (20, 15): its centre is 20.5 by 15.5 arc-seconds into the tile.
    Fixture f(window, "void-f32-tile-deflate-pred3-le.tif", {}, geotiffFile("void-f32-tile-deflate-pred3-le.tif"));
    auto &dialog = *f.dialog;
    QTest::mouseClick(child<QRadioButton>(dialog, "terrainImportDep3"), Qt::LeftButton);
    choose(child<QComboBox>(dialog, "terrainImportVertical"), "NAVD88");
    child<QDoubleSpinBox>(dialog, "terrainImportWidth")->setValue(200);
    child<QDoubleSpinBox>(dialog, "terrainImportHeight")->setValue(200);
    child<QDoubleSpinBox>(dialog, "terrainImportCentreLat")->setValue(51.6 - 15.5 / 3600);
    child<QDoubleSpinBox>(dialog, "terrainImportCentreLon")->setValue(-0.2 + 20.5 / 3600);
    settle(dialog);
    const auto banner = child<QLabel>(dialog, "terrainImportBannerText")->text();
    check(banner.contains("Nothing was imported.") && banner.contains("depend on no-data posts"),
          "A window over a void is refused by name: " + banner.toStdString());
    check(!dialog.preview()->overlays().voids.empty() &&
              dialog.preview()->overlays().voidLabel.contains("void posts"),
          "The void posts are located on the preview");
    check(dialog.reportText().contains("Void posts, E and N in metres"), "The report lists the void posts");
    dialog.reject();
    check(encodeContainer(doc) == encoded && doc.history().total == history,
          "A refused window changes nothing");
}

// The whole large tile, fitted: more posts than the terrain's limits allow at 31 m, so the grid is
// decimated. The grid line says so, the amber notice under the spacing names both spacings, and the
// report carries the decimation's notice (UX §20.2).
void geotiffDecimation(Window &window) {
    window.demo();
    Fixture f(window, "plane-f32-tile-deflate-pred3-large-le.tif", {},
              geotiffFile("plane-f32-tile-deflate-pred3-large-le.tif"));
    auto &dialog = *f.dialog;
    QTest::mouseClick(child<QRadioButton>(dialog, "terrainImportDep3"), Qt::LeftButton);
    choose(child<QComboBox>(dialog, "terrainImportVertical"), "NAVD88");
    QTest::mouseClick(child<QPushButton>(dialog, "terrainImportFitFile"), Qt::LeftButton);
    settle(dialog);
    const auto grid = child<QLabel>(dialog, "terrainImportDemGrid")->text();
    check(grid.contains("decimated") && !grid.contains("not decimated"),
          "The grid line says it is decimated: " + grid.toStdString());
    const auto caption = child<QLabel>(dialog, "terrainImportDemSpacingCaption")->text();
    check(caption.startsWith("⚠ Too many triangles at 31 m, so the spacing is "),
          "The amber notice names both spacings: " + caption.toStdString());
    const auto report = dialog.reportText();
    check(report.contains("Spacing: ") && report.contains("(nominal 31 m) · decimated") &&
              report.contains("Too many triangles at 31 m, so the spacing is "),
          "The report carries the decimation:\n" + report.toStdString());
    check(child<QPushButton>(dialog, "terrainImportConfirm")->isEnabled(),
          "A decimated window still imports: " +
              child<QLabel>(dialog, "terrainImportStatus")->text().toStdString());
    dialog.reject();
}

// The mismatch with the site's vertical reference: the terrain's EGM2008 against the site's NAVD88
// record. The refusal names both references, the block asks for an offset and a note, and with
// them the import is one edit that keeps the site's record (it is never overwritten).
void geotiffMismatch(Window &window) {
    window.demo();
    auto &doc = window.document();
    {
        Fixture f(window, "plane-f32-strip-none-le.tif", {}, geotiffFile("plane-f32-strip-none-le.tif"));
        auto &dialog = *f.dialog;
        QTest::mouseClick(child<QRadioButton>(dialog, "terrainImportDep3"), Qt::LeftButton);
        choose(child<QComboBox>(dialog, "terrainImportVertical"), "NAVD88");
        QTest::mouseClick(child<QPushButton>(dialog, "terrainImportFitFile"), Qt::LeftButton);
        settle(dialog);
        QTest::mouseClick(child<QPushButton>(dialog, "terrainImportConfirm"), Qt::LeftButton);
        check(QTest::qWaitFor([&] { return !dialog.isVisible(); }, 5000), "The first import closes");
    }
    check(doc.site() && doc.site()->geographic && doc.site()->geographic->verticalReference == "NAVD88",
          "The site's record is NAVD88");
    const auto history = doc.history().total;
    const auto position = doc.history().position;
    Fixture f(window, "plane-f32-strip-none-le.tif", {}, geotiffFile("plane-f32-strip-none-le.tif"));
    auto &dialog = *f.dialog;
    auto *import = child<QPushButton>(dialog, "terrainImportConfirm");
    auto *offset = child<QDoubleSpinBox>(dialog, "terrainImportOffset");
    QTest::mouseClick(child<QRadioButton>(dialog, "terrainImportDep3"), Qt::LeftButton);
    choose(child<QComboBox>(dialog, "terrainImportVertical"), "EGM2008");
    QTest::mouseClick(child<QPushButton>(dialog, "terrainImportFitFile"), Qt::LeftButton);
    settle(dialog);
    auto *mismatch = child<QFrame>(dialog, "terrainImportMismatch");
    check(mismatch->isVisible() &&
              child<QLabel>(dialog, "terrainImportMismatchText")->text() ==
                  "The heights are EGM2008. The site's elevation is NAVD88.",
          "The mismatch block names both references");
    check(!import->isEnabled() &&
              child<QLabel>(dialog, "terrainImportBannerText")->text().contains(
                  "The heights are EGM2008, and the site's elevation is NAVD88. Enter an offset and a note"),
          "The refusal names both references: " +
              child<QLabel>(dialog, "terrainImportBannerText")->text().toStdString());
    check(dialog.reportText().contains("Geographic record: not written: the site keeps its own record"),
          "The report says the site keeps its record");
    offset->setValue(0.5);
    QTest::keyClicks(child<QLineEdit>(dialog, "terrainImportWhy"), "Survey datum shift");
    settle(dialog);
    check(import->isEnabled(), "An offset and a note let the mixed heights in: " +
                                   child<QLabel>(dialog, "terrainImportStatus")->text().toStdString());
    QTest::mouseClick(import, Qt::LeftButton);
    check(QTest::qWaitFor([&] { return !dialog.isVisible(); }, 5000), "Import closes the dialog");
    check(dialog.result() == QDialog::Accepted && doc.history().total == history + 1,
          "The mixed import is one edit");
    const auto terrain = Id(dialog.operation()["terrain"].toString().toULongLong());
    const auto &record = *doc.bodies().at(terrain)->terrain;
    check(record.provenance.dem && record.provenance.dem->verticalOffset == 0.5 &&
              record.provenance.dem->verticalNote == "Survey datum shift" &&
              record.provenance.dem->vertical == "EGM2008",
          "The terrain keeps its offset, its note and its own reference");
    check(doc.site()->geographic->verticalReference == "NAVD88" &&
              doc.site()->geographic->provenance.kind == "imported",
          "The site's record is not overwritten");
    doc.undo();
    check(doc.history().position == position && !doc.bodies().contains(terrain),
          "Undo takes the mixed import back");
    window.viewport()->refresh();
}

// The GeoTIFF page's link to the user guide (UX §20.1, §20.6): the button is named and enabled, the
// Tab order runs from the product radios to it and on to the window's radios, and activating it,
// with the keyboard or the mouse, asks for the installed guide at the page's section. A refused
// open says so in the footer.
void geotiffUserGuide(Window &window) {
    window.demo();
    std::vector<QUrl> requested;
    bool refuse = false;
    Fixture f(window, "plane-f32-strip-none-le.tif", {}, geotiffFile("plane-f32-strip-none-le.tif"),
              [&](const QUrl &url) {
                  requested.push_back(url);
                  return !refuse;
              });
    auto &dialog = *f.dialog;
    auto *guide = child<QPushButton>(dialog, "terrainImportUserGuide");
    auto *status = child<QLabel>(dialog, "terrainImportStatus");
    check(guide->text() == "Open the user guide" &&
              guide->accessibleName() == "Open the user guide" && guide->isEnabled(),
          "The guide button is named and enabled");
    dialog.activateWindow();
    check(QTest::qWaitForWindowFocused(&dialog), "The dialog has keyboard focus");
    child<QRadioButton>(dialog, "terrainImportGlo30")->setFocus(Qt::TabFocusReason);
    QTest::keyClick(dialog.focusWidget(), Qt::Key_Tab);
    check(dialog.focusWidget() == guide, "Tab from the product reaches the guide button");
    // A radio group takes Tab on its checked button only: the demo site has no origin, so the window
    // is Centre and size, and Around the site origin is passed over.
    QTest::keyClick(dialog.focusWidget(), Qt::Key_Tab);
    check(dialog.focusWidget() == child<QRadioButton>(dialog, "terrainImportWindowCentre"),
          "Tab from the guide button reaches the window's checked radio: " +
              (dialog.focusWidget() ? dialog.focusWidget()->objectName() : QString()).toStdString());
    guide->setFocus(Qt::TabFocusReason);
    QTest::keyClick(guide, Qt::Key_Space);
    QTest::mouseClick(guide, Qt::LeftButton);
    check(requested.size() == 2, "Each activation asks once");
    for (const auto &url : requested)
        check(url.scheme() == "file" &&
                  url.toLocalFile().endsWith("/share/doc/sketchyup/USER_GUIDE.md") &&
                  url.fragment() == "terrain-from-a-downloaded-elevation-file",
              "The guide's file and section: " + url.toString().toStdString());
    refuse = true;
    QTest::mouseClick(guide, Qt::LeftButton);
    check(requested.size() == 3 && status->text().startsWith("Could not open the user guide"),
          "A refused open is said in the footer: " + status->text().toStdString());
}

} // namespace

int main(int argc, char **argv) {
    QTemporaryDir preferences;
    qputenv("XDG_CONFIG_HOME", preferences.path().toUtf8());
    QApplication app(argc, argv);
    QString screenshots;
    for (int i = 1; i + 1 < argc; ++i)
        if (QString(argv[i]) == "--screenshots")
            screenshots = argv[i + 1];
    Window window;
    window.resize(1280, 860);
    window.show();
    if (!QTest::qWaitForWindowExposed(&window, 5000))
        return 2;
    try {
        unitsGateImport(window);
        std::cout << "units gate import\n";
        pointColumnPresets(window);
        std::cout << "point column presets\n";
        roleMappingReachesImport(window);
        std::cout << "role mapping reaches the import\n";
        refusalChangesNothing(window, screenshots);
        std::cout << "refusal changes nothing; report copy; location zoom\n";
        keyboardAudit(window, "survey.csv", smallPnezd(), false);
        keyboardAudit(window, "invalid.dxf", terrain_fixture::terrainInvalid(), true);
        keyboardAudit(window, "plane-f32-strip-none-le.tif", {}, false, geotiffFile("plane-f32-strip-none-le.tif"));
        std::cout << "keyboard audit\n";
        menuImport(window, screenshots);
        std::cout << "menu import is one undoable edit\n";
        progressAndCancel(window);
        std::cout << "progress and cancel\n";
        geotiffImport(window);
        std::cout << "GeoTIFF page: defaults, latitude and longitude, the report, the import and the Credit\n";
        geotiffFit(window);
        std::cout << "GeoTIFF page: a window outside the file, Fit to the file, the GLO-30 report\n";
        geotiffRefusals(window);
        std::cout << "GeoTIFF refusals, and the located voids\n";
        geotiffDecimation(window);
        std::cout << "GeoTIFF decimation: the grid line, the amber notice and the report\n";
        geotiffMismatch(window);
        std::cout << "GeoTIFF mismatch: an offset and a note, the site's record kept\n";
        geotiffUserGuide(window);
        std::cout << "GeoTIFF user guide: the link, its Tab order, and the section it asks for\n";
    } catch (const std::exception &error) {
        std::cerr << "FAIL: " << error.what() << '\n';
        return 1;
    }
    return 0;
}
