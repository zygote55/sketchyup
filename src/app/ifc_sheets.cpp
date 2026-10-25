#include "app/ifc_sheets.hpp"
#include <QFileInfo>
#include <QFrame>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QJsonArray>
#include <QJsonDocument>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QTreeWidget>
#include <QTreeWidgetItem>
#include <algorithm>
namespace sketchy {
namespace {
QString humanSize(qint64 bytes) {
    if (bytes >= 1024 * 1024)
        return QString::asprintf("%.1f MB", double(bytes) / (1024.0 * 1024.0));
    if (bytes >= 1024)
        return QString::asprintf("%d KB", int(bytes / 1024));
    return QString::number(bytes) + " B";
}
QString humanMs(qint64 ms) {
    if (ms >= 1000)
        return QString::asprintf("%.1f s", double(ms) / 1000.0);
    return QString::number(ms) + " ms";
}
QString georeferenceLine(const QJsonObject &report) {
    const QJsonObject geo = report["georeference"].toObject();
    const QString result = geo["result"].toString();
    if (result == "siteOrigin") {
        const QJsonObject crs = geo["crs"].toObject();
        const QString crsName = crs["name"].toString();
        return crsName.isEmpty() ? QStringLiteral("from the IFC") : QStringLiteral("from the IFC (%1)").arg(crsName);
    }
    if (result == "gridAligned")
        return QStringLiteral("aligned to the grid");
    if (result == "notRepresentable")
        return QStringLiteral("not representable; the project axes were kept");
    return QStringLiteral("the project axes were kept");
}
// One proportional segment bar (the accounting identity), 100 units total.
void addSegmentBar(QVBoxLayout *into, qint64 a, qint64 b, qint64 c) {
    const qint64 total = a + b + c;
    if (total <= 0)
        return;
    auto *bar = new QFrame;
    bar->setObjectName("ifcReportBar");
    auto *layout = new QHBoxLayout(bar);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(1);
    const std::array<QColor, 3> colors{QColor("#3b6ea5"), QColor("#8a6d3b"), QColor("#777")};
    const std::array<qint64, 3> parts{a, b, c};
    for (int i = 0; i < 3; ++i) {
        if (parts[i] <= 0)
            continue;
        auto *segment = new QFrame;
        segment->setFixedHeight(10);
        segment->setStyleSheet(QString("background: %1;").arg(colors[i].name()));
        layout->addWidget(segment, int(100.0 * parts[i] / total));
    }
    into->addWidget(bar);
}
QString unitsLine(const QJsonObject &report) {
    const QJsonObject units = report["units"].toObject();
    const QString name = units["lengthUnit"].toString();
    if (units["override"].toBool(false))
        return QStringLiteral("%1 (chosen; the file declared none)").arg(name);
    return QStringLiteral("%1 (the file declares it)").arg(name);
}
// R098.a3: the STEP units line. The helper's units block (ADR 0172 section 2): the declared
// unit is authoritative ("declared"); the option stands only for a file that declares none
// ("option"); a mismatch is reported, never silently rescaled.
QString stepUnitsLine(const QJsonObject &report) {
    const QJsonObject units = report["units"].toObject();
    const QString name = units["length"].toString().isEmpty() ? QStringLiteral("none") : units["length"].toString();
    const QString applied = units["applied"].toString();
    if (applied == "option")
        return QStringLiteral("%1 (chosen; the file declared none)").arg(name);
    if (applied == "declared")
        return QStringLiteral("%1 (the file declares it)").arg(name);
    return QStringLiteral("%1").arg(name);
}
// The tessellation policy (ADR 0172 section 3) the application chose; it rides the report's own
// `tessellation` block (the application's choice, not the helper's).
QString stepTessellationLine(const QJsonObject &report) {
    const QJsonObject tessellation = report["tessellation"].toObject();
    return QStringLiteral("%1 mm deviation · %2° crease angle · %3 per element · %4 total")
        .arg(QString::number(tessellation["deflectionMm"].toDouble(1.0), 'f', 2))
        .arg(QString::number(tessellation["angleDeg"].toDouble(30.0), 'f', 1))
        .arg(QString::number(tessellation["maxTrianglesPerElement"].toInteger(100000)))
        .arg(QString::number(tessellation["maxTotalTriangles"].toInteger(1000000)));
}
} // namespace
void showIfcImportReport(QWidget *parent, const QString &fileName, const QJsonObject &report,
                          qint64 elapsedMs, int restarts) {
    ReportSheet sheet("ifcImportReport", "IFC import report", parent);
    sheet.resize(720, 560);
    // The banner.
    auto *banner = new QLabel;
    banner->setObjectName("ifcReportBanner");
    const QJsonObject engine = report["engine"].toObject();
    const int engineRestarts = engine["restarts"].toInt(0);
    const int shownRestarts = std::max(restarts, engineRestarts);
    banner->setText(QString("✓ Import finished · %1 · %2 restart%3")
                       .arg(humanMs(elapsedMs))
                       .arg(shownRestarts)
                       .arg(shownRestarts == 1 ? QString() : QString("s")));
    banner->setStyleSheet("font-size: 16px; font-weight: 600; color: #2e7d32;");
    sheet.summary->setObjectName("ifcReportSummary");
    const QJsonObject source = report["source"].toObject();
    const QJsonObject accounting = report["accounting"].toObject();
    const qint64 entities = accounting["entities"].toInt(0);
    sheet.summary->setText(
        QString("%1\n%2 · %3\nEntities read: %4")
            .arg(QFileInfo(fileName).fileName())
            .arg(source["schema"].toString())
            .arg(humanSize(source["bytes"].toInteger(0)))
            .arg(entities));
    sheet.summary->setTextFormat(Qt::PlainText);
    sheet.summary->setWordWrap(true);
    sheet.body->addWidget(banner);
    sheet.body->addWidget(sheet.summary);
    // The accounting identity with its bar.
    auto *identity = new QLabel;
    identity->setObjectName("ifcReportIdentity");
    identity->setText(
        QString("entities read %1 = converted to geometry %2 + structure and metadata %3 + omitted %4")
            .arg(accounting["entities"].toInt(0))
            .arg(accounting["converted"].toInt(0))
            .arg(accounting["supporting"].toInt(0))
            .arg(accounting["omitted"].toInt(0)));
    sheet.body->addWidget(identity);
    addSegmentBar(sheet.body, accounting["converted"].toInt(0), accounting["supporting"].toInt(0),
                   accounting["omitted"].toInt(0));
    // How it was read.
    auto *how = new QPlainTextEdit;
    how->setObjectName("ifcReportHow");
    how->setAccessibleName("how it was read");
    how->setReadOnly(true);
    how->setMaximumHeight(96);
    how->setPlainText(QString("Schema: %1\nUnits: %2\nGeoreference: %3\nMaximum deviation: %4 mm")
                         .arg(source["schema"].toString())
                         .arg(unitsLine(report))
                         .arg(georeferenceLine(report))
                         .arg(QString::number(report["precision"].toObject().value("chordDeviationMm").toDouble(0.5), 'f', 2)));
    sheet.body->addWidget(how);
    // The grouped list.
    auto *list = new QTreeWidget;
    list->setObjectName("ifcReportList");
    list->setAccessibleName("import report details");
    list->setColumnCount(2);
    list->setHeaderLabels({"What", "Count"});
    list->setRootIsDecorated(true);
    auto *spatialItem = new QTreeWidgetItem(list, {"Spatial structure", QString()});
    const QJsonObject spatialCounts = report["spatial"].toObject();
    for (const char *key : {"projects", "sites", "buildings", "storeys", "spaces"})
        new QTreeWidgetItem(spatialItem, {key, QString::number(spatialCounts[key].toInt(0))});
    auto *geometry = new QTreeWidgetItem(list, {"Geometry", QString()});
    const QJsonObject geometryReport = report["geometry"].toObject();
    for (const char *key : {"bodies", "meshes", "triangles", "instances"})
        new QTreeWidgetItem(geometry, {key, QString::number(geometryReport[key].toInteger(0))});
    auto *notices = new QTreeWidgetItem(list, {"Notices", QString()});
    const auto noticeList = report["notices"].toArray();
    for (const auto &value : noticeList) {
        const QJsonObject notice = value.toObject();
        new QTreeWidgetItem(notices, {notice["message"].toString(),
                                       QString::number(notice["count"].toInteger(0))});
    }
    if (noticeList.isEmpty())
        new QTreeWidgetItem(notices, {"None", QString()});
    sheet.body->addWidget(list, 1);
    // The footer.
    auto *ok = new QPushButton("OK");
    ok->setObjectName("ifcReportOK");
    QObject::connect(ok, &QPushButton::clicked, &sheet, &QDialog::accept);
    auto *footerLayout = qobject_cast<QHBoxLayout *>(sheet.footer->layout());
    footerLayout->addStretch();
    footerLayout->addWidget(ok);
    sheet.buttons->hide();
    sheet.exec();
}
void showIfcRefusal(QWidget *parent, const QString &fileName, const std::string &code,
                      const QString &message, const QJsonObject &report) {
    ReportSheet sheet("ifcRefusal", "IFC import refused", parent);
    sheet.resize(640, 420);
    sheet.summary->setObjectName("ifcRefusalSummary");
    sheet.summary->setWordWrap(true);
    sheet.summary->setTextFormat(Qt::PlainText);
    sheet.summary->setText(
        QString("✗ %1\n\nThe model was not changed. Nothing was written.\n\n"
                "The check that failed: %2\nThe words the helper used: %3\n\n"
                "Not affected: the open model, the sheets, the assistant, your settings and "
                "recovery.")
            .arg(QFileInfo(fileName).fileName())
            .arg(QString::fromStdString(code))
            .arg(message));
    sheet.body->addWidget(sheet.summary);
    if (!report.isEmpty()) {
        auto *details = new QPlainTextEdit;
        details->setObjectName("ifcRefusalDetails");
        details->setAccessibleName("refusal details");
        details->setReadOnly(true);
        details->setPlainText(QString::fromUtf8(QJsonDocument(report).toJson(QJsonDocument::Indented)));
        sheet.body->addWidget(details, 1);
    }
    auto *ok = new QPushButton("OK");
    ok->setObjectName("ifcRefusalOK");
    QObject::connect(ok, &QPushButton::clicked, &sheet, &QDialog::accept);
    auto *footerLayout = qobject_cast<QHBoxLayout *>(sheet.footer->layout());
    footerLayout->addStretch();
    footerLayout->addWidget(ok);
    sheet.buttons->hide();
    sheet.exec();
}
void showIfcUnavailable(QWidget *parent, const IfcProbe &probe, bool exportDirection) {
    // "Not installed" and "cannot start" are different states with different sheet names; the
    // broken state shows the helper's own last log line at the bottom of the message, so a
    // person can act on it (ADR 0171 §16.6).
    const QString sheetName =
        probe.state == IfcState::Missing  ? "ifcUnavailable"
        : probe.state == IfcState::Broken ? "ifcBroken"
                                          : "ifcLimited";
    ReportSheet sheet(sheetName, exportDirection ? "IFC export" : "IFC import", parent);
    sheet.resize(600, 380);
    sheet.summary->setObjectName("ifcUnavailableSummary");
    sheet.summary->setWordWrap(true);
    sheet.summary->setTextFormat(Qt::PlainText);
    if (probe.state == IfcState::Missing) {
        sheet.summary->setText(
            QString("IFC %1 is not installed.\n\n"
                    "The sketchyup-ifc package is not installed on this machine.\n"
                    "Everything else is unaffected: the open model, the sheets, the assistant, "
                    "your settings and recovery all work as before.\n\n"
                    "To enable IFC %1, install the sketchyup-ifc package.")
                .arg(exportDirection ? QStringLiteral("export") : QStringLiteral("import")));
    } else if (probe.state == IfcState::Broken) {
        sheet.summary->setText(
            QString("IFC %1 cannot start.\n\nThe helper could not be started.\n\n%2")
            .arg(exportDirection ? QStringLiteral("export") : QStringLiteral("import"))
            .arg(probe.logTail));
    } else {
        sheet.summary->setText(
            QString("IFC %1 is limited.\n\nThe OpenCASCADE kernel is not installed, so only "
                    "tessellated geometry will be converted.\n\n%2")
                .arg(exportDirection ? QStringLiteral("export") : QStringLiteral("import"))
                .arg(probe.kernelReason));
    }
    sheet.body->addWidget(sheet.summary);
    auto *ok = new QPushButton("OK");
    ok->setObjectName("ifcUnavailableOK");
    QObject::connect(ok, &QPushButton::clicked, &sheet, &QDialog::accept);
    auto *footerLayout = qobject_cast<QHBoxLayout *>(sheet.footer->layout());
    footerLayout->addStretch();
    footerLayout->addWidget(ok);
    sheet.buttons->hide();
    sheet.exec();
}
// R098.a3: the STEP import report (ADR 0172 section 8) — the IFC report's own screen for
// STEP: the banner, what came in (the helper's own counts, which ride the report as `step`),
// the accounting identity with its bar, how it was read (the schema, the units, the
// application's own tessellation policy), the grouped list, the footer.
void showStepImportReport(QWidget *parent, const QString &fileName, const QJsonObject &report,
                          qint64 elapsedMs, int restarts) {
    ReportSheet sheet("stepImportReport", "STEP import report", parent);
    sheet.resize(720, 560);
    auto *banner = new QLabel;
    banner->setObjectName("stepReportBanner");
    const QJsonObject engine = report["engine"].toObject();
    const int engineRestarts = engine["restarts"].toInt(0);
    const int shownRestarts = std::max(restarts, engineRestarts);
    banner->setText(QString("✓ Import finished · %1 · %2 restart%3")
                       .arg(humanMs(elapsedMs))
                       .arg(shownRestarts)
                       .arg(shownRestarts == 1 ? QString() : QString("s")));
    banner->setStyleSheet("font-size: 16px; font-weight: 600; color: #2e7d32;");
    sheet.summary->setObjectName("stepReportSummary");
    const QJsonObject source = report["source"].toObject();
    const QJsonObject step = report["step"].toObject(); // the helper's own line (the application's runner attached it)
    sheet.summary->setText(
        QString("%1\n%2 · %3\nConverted: %4 · Triangles: %5")
            .arg(QFileInfo(fileName).fileName())
            .arg(source["schema"].toString())
            .arg(humanSize(source["bytes"].toInteger(0)))
            .arg(QString::number(step["converted"].toInteger(0)))
            .arg(QString::number(step["triangles"].toInteger(0))));
    sheet.summary->setTextFormat(Qt::PlainText);
    sheet.summary->setWordWrap(true);
    sheet.body->addWidget(banner);
    sheet.body->addWidget(sheet.summary);
    // The accounting identity with its bar. The application's runner computes it for the
    // STEP package (the helper's package carries the converted nodes and the root; the
    // conversion omits nothing — the helper refuses before writing anything it cannot convert).
    const QJsonObject accounting = report["accounting"].toObject();
    auto *identity = new QLabel;
    identity->setObjectName("stepReportIdentity");
    identity->setText(
        QString("nodes read %1 = converted to geometry %2 + structure and metadata %3 + omitted %4")
            .arg(accounting["entities"].toInteger(0))
            .arg(accounting["converted"].toInteger(0))
            .arg(accounting["supporting"].toInteger(0))
            .arg(accounting["omitted"].toInteger(0)));
    sheet.body->addWidget(identity);
    addSegmentBar(sheet.body, accounting["converted"].toInteger(0), accounting["supporting"].toInteger(0),
                   accounting["omitted"].toInteger(0));
    // How it was read.
    auto *how = new QPlainTextEdit;
    how->setObjectName("stepReportHow");
    how->setAccessibleName("how it was read");
    how->setReadOnly(true);
    how->setMaximumHeight(96);
    how->setPlainText(QString("Schema: %1\nUnits: %2\nTessellation: %3\nNo georeference: the STEP import keeps the helper's axes")
                         .arg(source["schema"].toString())
                         .arg(stepUnitsLine(report))
                         .arg(stepTessellationLine(report)));
    sheet.body->addWidget(how);
    // The grouped list: the helper's own counts, the application's conversion, the notices.
    auto *list = new QTreeWidget;
    list->setObjectName("stepReportList");
    list->setAccessibleName("import report details");
    list->setColumnCount(2);
    list->setHeaderLabels({"What", "Count"});
    list->setRootIsDecorated(true);
    auto *helper = new QTreeWidgetItem(list, {"The STEP helper", QString()});
    for (const char *key : {"nodes", "meshes", "converted", "omitted", "unsupported", "triangles"})
        new QTreeWidgetItem(helper, {key, QString::number(step[key].toInteger(0))});
    auto *geometry = new QTreeWidgetItem(list, {"Geometry", QString()});
    const QJsonObject geometryReport = report["geometry"].toObject();
    for (const char *key : {"bodies", "vertices", "faces", "edges"})
        new QTreeWidgetItem(geometry, {key, QString::number(geometryReport[key].toInteger(0))});
    auto *notices = new QTreeWidgetItem(list, {"Notices", QString()});
    const auto noticeList = report["notices"].toArray();
    for (const auto &value : noticeList) {
        const QJsonObject notice = value.toObject();
        new QTreeWidgetItem(notices, {notice["message"].toString(),
                                       QString::number(notice["count"].toInteger(0))});
    }
    if (noticeList.isEmpty())
        new QTreeWidgetItem(notices, {"None", QString()});
    sheet.body->addWidget(list, 1);
    auto *ok = new QPushButton("OK");
    ok->setObjectName("stepReportOK");
    QObject::connect(ok, &QPushButton::clicked, &sheet, &QDialog::accept);
    auto *footerLayout = qobject_cast<QHBoxLayout *>(sheet.footer->layout());
    footerLayout->addStretch();
    footerLayout->addWidget(ok);
    sheet.buttons->hide();
    sheet.exec();
}
// R098.a3: the STEP refusal — the IFC refusal's own words. The check that failed is named
// (the application's own, or the helper's as its words come back); the model is unchanged
// and nothing was written.
void showStepRefusal(QWidget *parent, const QString &fileName, const std::string &code,
                      const QString &message, const QJsonObject &report) {
    ReportSheet sheet("stepRefusal", "STEP import refused", parent);
    sheet.resize(640, 420);
    sheet.summary->setObjectName("stepRefusalSummary");
    sheet.summary->setWordWrap(true);
    sheet.summary->setTextFormat(Qt::PlainText);
    sheet.summary->setText(
        QString("✗ %1\n\nThe model was not changed. Nothing was written.\n\n"
                "The check that failed: %2\nThe words used: %3\n\n"
                "Not affected: the open model, the sheets, the assistant, your settings and "
                "recovery.")
            .arg(QFileInfo(fileName).fileName())
            .arg(QString::fromStdString(code))
            .arg(message));
    sheet.body->addWidget(sheet.summary);
    if (!report.isEmpty()) {
        auto *details = new QPlainTextEdit;
        details->setObjectName("stepRefusalDetails");
        details->setAccessibleName("refusal details");
        details->setReadOnly(true);
        details->setPlainText(QString::fromUtf8(QJsonDocument(report).toJson(QJsonDocument::Indented)));
        sheet.body->addWidget(details, 1);
    }
    auto *ok = new QPushButton("OK");
    ok->setObjectName("stepRefusalOK");
    QObject::connect(ok, &QPushButton::clicked, &sheet, &QDialog::accept);
    auto *footerLayout = qobject_cast<QHBoxLayout *>(sheet.footer->layout());
    footerLayout->addStretch();
    footerLayout->addWidget(ok);
    sheet.buttons->hide();
    sheet.exec();
}
// R098.a3: the STEP import's availability — the IFC helper's own states. The STEP import
// needs none of the IFC kernel, so the helper's Limited state is not shown; the window
// calls this only for Missing and Broken.
void showStepUnavailable(QWidget *parent, const IfcProbe &probe) {
    const QString sheetName = probe.state == IfcState::Missing ? "stepUnavailable" : "stepBroken";
    ReportSheet sheet(sheetName, "STEP import", parent);
    sheet.resize(600, 380);
    sheet.summary->setObjectName("stepUnavailableSummary");
    sheet.summary->setWordWrap(true);
    sheet.summary->setTextFormat(Qt::PlainText);
    if (probe.state == IfcState::Missing) {
        sheet.summary->setText(
            QString("STEP import is not installed.\n\n"
                    "The sketchyup-ifc package is not installed on this machine.\n"
                    "Everything else is unaffected: the open model, the sheets, the assistant, "
                    "your settings and recovery all work as before.\n\n"
                    "To enable STEP import, install the sketchyup-ifc package."));
    } else {
        sheet.summary->setText(
            QString("STEP import cannot start.\n\nThe helper could not be started.\n\n%1").arg(probe.logTail));
    }
    sheet.body->addWidget(sheet.summary);
    auto *ok = new QPushButton("OK");
    ok->setObjectName("stepUnavailableOK");
    QObject::connect(ok, &QPushButton::clicked, &sheet, &QDialog::accept);
    auto *footerLayout = qobject_cast<QHBoxLayout *>(sheet.footer->layout());
    footerLayout->addStretch();
    footerLayout->addWidget(ok);
    sheet.buttons->hide();
    sheet.exec();
}
} // namespace sketchy
