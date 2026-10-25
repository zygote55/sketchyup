#include "app/ifc_sheets.hpp"
#include <QApplication>
#include <QClipboard>
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
// The IFC import's face (ADR 0171 section 16.5): the banner, the accounting identity with its
// bar, the notices and the footer are the shared builder's; the face supplies the strings,
// the summary, how it was read and the grouped list. The next format (R102.c, the .skp
// import) plugs in here: a face and a state, nothing else.
ImportReportFace ifcImportReportFace() {
    ImportReportFace face;
    face.sheetName = "ifcImportReport";
    face.title = "IFC import report";
    face.prefix = "ifcReport";
    face.identitySubject = "entities";
    face.chipWord = "IFC";
    face.attention = [](const ImportReportState &state) {
        // The import report has no `attention` member (only the export report does); what
        // needs attention after an import is what the helper did not convert.
        return state.report["accounting"].toObject()["omitted"].toInteger(0);
    };
    face.summary = [](const ImportReportState &state) {
        const QJsonObject source = state.report["source"].toObject();
        return QString("%1\n%2 · %3\nEntities read: %4")
            .arg(QFileInfo(state.fileName).fileName())
            .arg(source["schema"].toString())
            .arg(humanSize(source["bytes"].toInteger(0)))
            .arg(state.report["accounting"].toObject()["entities"].toInteger(0));
    };
    face.how = [](const ImportReportState &state) {
        const QJsonObject report = state.report;
        const QJsonObject source = report["source"].toObject();
        return QString("Schema: %1\nUnits: %2\nGeoreference: %3\nMaximum deviation: %4 mm")
            .arg(source["schema"].toString())
            .arg(unitsLine(report))
            .arg(georeferenceLine(report))
            .arg(QString::number(report["precision"].toObject().value("chordDeviationMm").toDouble(0.5), 'f', 2));
    };
    face.groups = [](const ImportReportState &state) {
        const QJsonObject report = state.report;
        std::vector<std::pair<QString, QStringList>> groups;
        auto &spatial = groups.emplace_back("Spatial structure", QStringList{});
        const QJsonObject spatialCounts = report["spatial"].toObject();
        for (const char *key : {"projects", "sites", "buildings", "storeys", "spaces"})
            spatial.second << QStringList{key, QString::number(spatialCounts[key].toInteger(0))};
        auto &geometry = groups.emplace_back("Geometry", QStringList{});
        const QJsonObject geometryReport = report["geometry"].toObject();
        for (const char *key : {"bodies", "meshes", "triangles", "instances"})
            geometry.second << QStringList{key, QString::number(geometryReport[key].toInteger(0))};
        return groups;
    };
    return face;
}
// One "show the last report" path (the screens refactor): the IFC and STEP import reports
// differ only in the face. The banner, the identity with its bar, the notices and the
// footer are the shared behaviour.
void showImportReport(QWidget *parent, const ImportReportFace &face, const ImportReportState &state) {
    const QJsonObject &report = state.report;
    ReportSheet sheet(face.sheetName, face.title, parent);
    sheet.resize(720, 560);
    // The banner.
    auto *banner = new QLabel;
    banner->setObjectName(face.prefix + "Banner");
    const QJsonObject engine = report["engine"].toObject();
    const int engineRestarts = engine["restarts"].toInteger(0);
    const int shownRestarts = std::max(state.restarts, engineRestarts);
    banner->setText(QString("✓ Import finished · %1 · %2 restart%3")
                       .arg(humanMs(state.elapsedMs))
                       .arg(shownRestarts)
                       .arg(shownRestarts == 1 ? QString() : QString("s")));
    banner->setStyleSheet("font-size: 16px; font-weight: 600; color: #2e7d32;");
    sheet.summary->setObjectName(face.prefix + "Summary");
    sheet.summary->setText(face.summary(state));
    sheet.summary->setTextFormat(Qt::PlainText);
    sheet.summary->setWordWrap(true);
    sheet.body->addWidget(banner);
    sheet.body->addWidget(sheet.summary);
    // The accounting identity with its bar.
    auto *identity = new QLabel;
    identity->setObjectName(face.prefix + "Identity");
    const QJsonObject accounting = report["accounting"].toObject();
    identity->setText(
        QString("%1 read %2 = converted to geometry %3 + structure and metadata %4 + omitted %5")
            .arg(face.identitySubject)
            .arg(accounting["entities"].toInteger(0))
            .arg(accounting["converted"].toInteger(0))
            .arg(accounting["supporting"].toInteger(0))
            .arg(accounting["omitted"].toInteger(0)));
    sheet.body->addWidget(identity);
    addSegmentBar(sheet.body, accounting["converted"].toInteger(0), accounting["supporting"].toInteger(0),
                   accounting["omitted"].toInteger(0));
    // How it was read.
    auto *how = new QPlainTextEdit;
    how->setObjectName(face.prefix + "How");
    how->setAccessibleName("how it was read");
    how->setReadOnly(true);
    how->setMaximumHeight(96);
    how->setPlainText(face.how(state));
    sheet.body->addWidget(how);
    // The grouped list (the face's groups, then the notices).
    auto *list = new QTreeWidget;
    list->setObjectName(face.prefix + "List");
    list->setAccessibleName("import report details");
    list->setColumnCount(2);
    list->setHeaderLabels({"What", "Count"});
    list->setRootIsDecorated(true);
    for (const auto &group : face.groups(state)) {
        auto *item = new QTreeWidgetItem(list, {group.first, QString()});
        // The face's rows are flat "label, value, label, value…" pairs.
        for (size_t i = 0; i + 1 < group.second.size(); i += 2)
            new QTreeWidgetItem(item, {group.second[i], group.second[i + 1]});
    }
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
    ok->setObjectName(face.prefix + "OK");
    QObject::connect(ok, &QPushButton::clicked, &sheet, &QDialog::accept);
    auto *footerLayout = qobject_cast<QHBoxLayout *>(sheet.footer->layout());
    footerLayout->addStretch();
    footerLayout->addWidget(ok);
    sheet.buttons->hide();
    sheet.exec();
}
// The one shared refusal sheet: a red top line, the summary, the helper's own details (when it
// has any, and the copy button when the face asks), the footer.
void showRefusalSheet(QWidget *parent, const RefusalSheet &face) {
    ReportSheet sheet(face.sheetName, face.title, parent);
    sheet.resize(face.width, face.height);
    if (!face.leadingLine.isEmpty()) {
        auto *first = new QLabel(face.leadingLine);
        first->setObjectName(face.prefix + "First");
        first->setStyleSheet("font-size: 18px; font-weight: 600; color: #c62828;");
        sheet.body->addWidget(first);
    }
    if (!face.refusedLine.isEmpty()) {
        auto *refused = new QLabel(face.refusedLine);
        refused->setObjectName(face.prefix + "Refused");
        refused->setStyleSheet("font-weight: 600; color: #c62828;");
        sheet.body->addWidget(refused);
    }
    sheet.summary->setObjectName(face.prefix + "Summary");
    sheet.summary->setWordWrap(true);
    sheet.summary->setTextFormat(Qt::PlainText);
    sheet.summary->setText(face.summary);
    sheet.body->addWidget(sheet.summary);
    if (!face.report.isEmpty()) {
        auto *details = new QPlainTextEdit;
        details->setObjectName(face.prefix + "Details");
        details->setAccessibleName("refusal details");
        details->setReadOnly(true);
        details->setPlainText(QString::fromUtf8(QJsonDocument(face.report).toJson(QJsonDocument::Indented)));
        sheet.body->addWidget(details, 1);
        if (face.copyDetails) {
            auto *copy = new QPushButton("Copy details");
            copy->setObjectName(face.prefix + "Copy");
            QObject::connect(copy, &QPushButton::clicked, &sheet, [details] {
                QApplication::clipboard()->setText(details->toPlainText());
            });
            sheet.body->addWidget(copy, 0, Qt::AlignLeft);
        }
    }
    auto *close = new QPushButton(face.closeButton);
    close->setObjectName(face.prefix + face.closeSuffix);
    if (face.closeIsDefault)
        close->setDefault(true);
    QObject::connect(close, &QPushButton::clicked, &sheet, &QDialog::accept);
    auto *footerLayout = qobject_cast<QHBoxLayout *>(sheet.footer->layout());
    footerLayout->addStretch();
    footerLayout->addWidget(close);
    sheet.buttons->hide();
    sheet.exec();
}
// The IFC import's refusal (16.6): what was checked and what failed, the words the helper used,
// what is unaffected. The model is unchanged.
void showIfcRefusal(QWidget *parent, const QString &fileName, const std::string &code,
                      const QString &message, const QJsonObject &report) {
    RefusalSheet face;
    face.sheetName = "ifcRefusal";
    face.title = "IFC import refused";
    face.prefix = "ifcRefusal";
    face.summary =
        QString("✗ %1\n\nThe model was not changed. Nothing was written.\n\n"
                "The check that failed: %2\nThe words the helper used: %3\n\n"
                "Not affected: the open model, the sheets, the assistant, your settings and "
                "recovery.")
            .arg(QFileInfo(fileName).fileName())
            .arg(QString::fromStdString(code))
            .arg(message);
    face.report = report;
    showRefusalSheet(parent, face);
}
// R097.e2 (16.8): the export's result sheet — the import's own component with the export's own
// words. The banner says "A new IFC file was written. The model is unchanged." The left column
// balances the model's contents the way the import balances entities (every item is written,
// written as a separate copy, or counted as not written). Needs your attention holds the
// coordinate-system row (and its fix) and the GlobalIds that had to be replaced (Select in
// model). Not written and Notices repeat the dialog's counts, now final, plus "numbers are
// written as plain reals; no unit meaning survives."
void showIfcExportReport(QWidget *parent, const QString &path, const QJsonObject &report,
                          qint64 elapsedMs, const std::function<void(qint64)> &selectInModel) {
    ReportSheet sheet("ifcExportReport", "IFC export report", parent);
    sheet.resize(760, 560);
    auto *banner = new QLabel("✓ A new IFC file was written. The model is unchanged.");
    banner->setObjectName("ifcExportReportBanner");
    banner->setStyleSheet("font-size: 16px; font-weight: 600; color: #2e7d32;");
    sheet.body->addWidget(banner);
    sheet.summary->setObjectName("ifcExportReportSummary");
    sheet.summary->setWordWrap(true);
    sheet.summary->setTextFormat(Qt::PlainText);
    const QJsonObject target = report["target"].toObject();
    sheet.summary->setText(
        QString("%1 · %2\n%3 · %4 · %5")
            .arg(QFileInfo(path).fileName())
            .arg(humanSize(target["bytes"].toInteger(0)))
            .arg(target["schema"].toString())
            .arg(target["geometry"].toString())
            .arg(humanMs(elapsedMs)));
    sheet.body->addWidget(sheet.summary);
    // The accounting identity (16.8): every item is written, written as a separate copy
    // (components), or counted as not written.
    const QJsonObject accounting = report["accounting"].toObject();
    auto *identity = new QLabel(
        QString("%1 = %2 + %3 + %4 %5")
            .arg(accounting["items"].toInt(0))
            .arg(accounting["written"].toInt(0))
            .arg(accounting["writtenAsCopies"].toInt(0))
            .arg(accounting["notWritten"].toInt(0))
            .arg(accounting["balanced"].toBool(true) ? "✓ balanced" : "✗ unbalanced"));
    identity->setObjectName("ifcExportReportIdentity");
    sheet.body->addWidget(identity);
    addSegmentBar(sheet.body, accounting["written"].toInt(0), accounting["writtenAsCopies"].toInt(0),
                   accounting["notWritten"].toInt(0));
    auto *list = new QTreeWidget;
    list->setObjectName("ifcExportReportList");
    list->setAccessibleName(
        "export report: what needs attention, what was written, what was not written, and the notices");
    list->setColumnCount(2);
    list->setHeaderLabels({"What", "Count"});
    list->setRootIsDecorated(true);
    // Needs your attention: the coordinate-system row (and its fix) and the replaced GlobalIds.
    const int attentionCount = report["attention"].toInt(0);
    auto *attentionItem = new QTreeWidgetItem(list, {"Needs your attention", QString::number(attentionCount)});
    const QJsonObject geo = report["georeference"].toObject();
    if (geo["result"].toString() == "notWritten")
        new QTreeWidgetItem(
            attentionItem,
            {QString("⚠ %1").arg(geo["reason"].toString() == "noCrsLabel"
                 ? "The site origin is set but has no coordinate system label, so neither the coordinate system nor the map conversion was written and the origin is lost in the file. Set the label to an authority code such as EPSG:32760 in File, Site settings."
                 : "The site origin's label is not an authority code such as EPSG:32760, so neither the coordinate system nor the map conversion was written and the origin is lost in the file. Change the label in File, Site settings."),
             "1"});
    for (const auto &value : report["locations"].toArray()) {
        const QJsonObject location = value.toObject();
        if (location["kind"].toString() != "globalIdReplaced")
            continue;
        auto *item = new QTreeWidgetItem(attentionItem, {location["name"].toString(), location["message"].toString()});
        auto *button = new QPushButton("Select in model");
        button->setObjectName("ifcExportSelectBody");
        const qint64 bodyId = location["bodyId"].toInteger(0);
        QObject::connect(button, &QPushButton::clicked, &sheet, [bodyId, selectInModel, &sheet] {
            if (selectInModel)
                selectInModel(bodyId);
            sheet.accept();
        });
        list->setItemWidget(item, 1, button);
    }
    if (attentionCount == 0)
        attentionItem->setHidden(true);
    // Written: the dialog's counts, now final.
    auto *writtenItem = new QTreeWidgetItem(list, {"Written", QString()});
    const QJsonObject written = report["written"].toObject();
    for (const char *key : {"groups", "bodies", "placements", "materials", "properties", "hidden", "triangles"})
        if (written[key].toInt(0) > 0)
            new QTreeWidgetItem(writtenItem, {key, QString::number(written[key].toInt(0))});
    // Not written: one line per kind, as the dialog had it.
    auto *notWrittenItem = new QTreeWidgetItem(list, {"Not written", QString()});
    const QJsonObject notWritten = report["notWritten"].toObject();
    for (const char *key : {"terrain", "terrainTiles", "referenceImages", "guides", "annotations",
                           "looseEdges", "curves", "sections", "scenes", "sheets",
                           "componentParameters", "textures", "emptyGroups", "bodiesWithoutFaces"})
        if (notWritten[key].toInteger(0) > 0)
            new QTreeWidgetItem(notWrittenItem, {key, QString::number(notWritten[key].toInteger(0))});
    // Notices, now final.
    auto *noticesItem = new QTreeWidgetItem(list, {"Notices", QString()});
    const auto noticeList = report["notices"].toArray();
    for (const auto &value : noticeList)
        new QTreeWidgetItem(noticesItem, {value.toObject()["message"].toString(),
                                            QString::number(value.toObject()["count"].toInteger(0))});
    if (noticeList.isEmpty())
        new QTreeWidgetItem(noticesItem, {"None", QString()});
    sheet.body->addWidget(list, 1);
    auto *ok = new QPushButton("OK");
    ok->setObjectName("ifcExportReportOK");
    ok->setDefault(true); // the close is the default; Esc closes (16.9)
    QObject::connect(ok, &QPushButton::clicked, &sheet, &QDialog::accept);
    auto *footerLayout = qobject_cast<QHBoxLayout *>(sheet.footer->layout());
    footerLayout->addStretch();
    footerLayout->addWidget(ok);
    sheet.buttons->hide();
    sheet.exec();
}
// R097.e2 (16.6, 16.8): the export's refusal — the import's own shape with the export's words.
// First, that nothing was written and the model is unchanged; then the reason with its number
// and limit; then numbered next steps. Red and the word "Refused", never colour alone. Cancel
// is not a refusal and has no sheet.
void showIfcExportRefusal(QWidget *parent, const QString &path, const std::string &code,
                          const QString &message, const QJsonObject &report) {
    RefusalSheet face;
    face.sheetName = "ifcExportRefusal";
    face.title = "IFC export refused";
    face.width = 680;
    face.height = 480;
    face.prefix = "ifcExportRefusal";
    face.leadingLine = "✗ Nothing was written.";
    const QString codeText = QString::fromStdString(code);
    face.refusedLine = "Refused: " + codeText;
    QString whatToDo;
    if (codeText == "destinationExists")
        whatToDo = "1. Choose a new name — an existing file is never replaced.\n2. Export again.";
    else if (codeText == "destination")
        whatToDo = "1. Choose another location.\n2. Export again.";
    else if (codeText == "outputTooLarge")
        whatToDo = "1. Export a smaller file (one building, storey or discipline).\n2. Export again.";
    else if (codeText == "packageTooLarge" || codeText == "documentTooLarge")
        whatToDo = "1. The model is larger than the export can carry; export a smaller one (one building, storey or discipline).";
    else if (codeText == "workerFailed")
        whatToDo = "1. Try again once.\n2. Then export a smaller or cleaned file.";
    else if (codeText == "emptyModel")
        whatToDo = "1. Add something to the model (a group or a body with faces).\n2. Export again.";
    else if (codeText == "workerMismatch")
        whatToDo = "1. Try again once.\n2. If it repeats, report it with the details.";
    else
        whatToDo = "1. Try again once.\n2. If it repeats, report it with the details.";
    face.summary =
        QString("%1\n\n%2\n\nWhat to do next:\n%3\n\nNot affected: the open model, the "
                "sheets, the assistant, your settings and recovery.")
            .arg(QFileInfo(path).fileName())
            .arg(message)
            .arg(whatToDo);
    face.report = report;
    face.copyDetails = true;
    face.closeButton = "Close";
    face.closeSuffix = "Close";
    face.closeIsDefault = true;
    showRefusalSheet(parent, face);
}
// The one shared availability sheet: the helper's own states (not installed, cannot start,
// limited) with the application's own words; the broken state shows the helper's own last log
// line at the bottom of the message, so a person can act on it (ADR 0171 §16.6).
void showAvailabilitySheet(QWidget *parent, const AvailabilitySheet &face) {
    ReportSheet sheet(face.sheetName, face.title, parent);
    sheet.resize(face.width, face.height);
    sheet.summary->setObjectName(face.prefix + "Summary");
    sheet.summary->setWordWrap(true);
    sheet.summary->setTextFormat(Qt::PlainText);
    sheet.summary->setText(face.text);
    sheet.body->addWidget(sheet.summary);
    auto *ok = new QPushButton("OK");
    ok->setObjectName(face.prefix + "OK");
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
    AvailabilitySheet face;
    face.sheetName = sheetName;
    face.title = exportDirection ? "IFC export" : "IFC import";
    face.prefix = "ifcUnavailable";
    if (probe.state == IfcState::Missing) {
        face.text =
            QString("IFC %1 is not installed.\n\n"
                    "The sketchyup-ifc package is not installed on this machine.\n"
                    "Everything else is unaffected: the open model, the sheets, the assistant, "
                    "your settings and recovery all work as before.\n\n"
                    "To enable IFC %1, install the sketchyup-ifc package.")
                .arg(exportDirection ? QStringLiteral("export") : QStringLiteral("import"));
    } else if (probe.state == IfcState::Broken) {
        face.text =
            QString("IFC %1 cannot start.\n\nThe helper could not be started.\n\n%2")
                .arg(exportDirection ? QStringLiteral("export") : QStringLiteral("import"))
                .arg(probe.logTail);
    } else {
        face.text =
            QString("IFC %1 is limited.\n\nThe OpenCASCADE kernel is not installed, so only "
                    "tessellated geometry will be converted.\n\n%2")
                .arg(exportDirection ? QStringLiteral("export") : QStringLiteral("import"))
                .arg(probe.kernelReason);
    }
    showAvailabilitySheet(parent, face);
}
// The STEP import's face (ADR 0172 section 8): the IFC report's own screen for STEP — the
// helper's own counts (they ride the report as `step`) beside the application's conversion;
// the tessellation policy and the triangle caps come from the application's own choice (the
// report's `tessellation`).
ImportReportFace stepImportReportFace() {
    ImportReportFace face;
    face.sheetName = "stepImportReport";
    face.title = "STEP import report";
    face.prefix = "stepReport";
    face.identitySubject = "nodes";
    face.chipWord = "STEP";
    face.attention = [](const ImportReportState &state) {
        // The attention is the helper's own: the solids it read but did not convert (the
        // budget and the tessellation failures); the conversion omits nothing.
        return state.report["step"].toObject()["omitted"].toInteger(0);
    };
    face.summary = [](const ImportReportState &state) {
        const QJsonObject source = state.report["source"].toObject();
        const QJsonObject step = state.report["step"].toObject(); // the helper's own line (the application's runner attached it)
        return QString("%1\n%2 · %3\nConverted: %4 · Triangles: %5")
            .arg(QFileInfo(state.fileName).fileName())
            .arg(source["schema"].toString())
            .arg(humanSize(source["bytes"].toInteger(0)))
            .arg(QString::number(step["converted"].toInteger(0)))
            .arg(QString::number(step["triangles"].toInteger(0)));
    };
    face.how = [](const ImportReportState &state) {
        const QJsonObject report = state.report;
        const QJsonObject source = report["source"].toObject();
        return QString("Schema: %1\nUnits: %2\nTessellation: %3\nNo georeference: the STEP import keeps the helper's axes")
            .arg(source["schema"].toString())
            .arg(stepUnitsLine(report))
            .arg(stepTessellationLine(report));
    };
    face.groups = [](const ImportReportState &state) {
        const QJsonObject report = state.report;
        std::vector<std::pair<QString, QStringList>> groups;
        auto &helper = groups.emplace_back("The STEP helper", QStringList{});
        const QJsonObject step = report["step"].toObject();
        for (const char *key : {"nodes", "meshes", "converted", "omitted", "unsupported", "triangles"})
            helper.second << QStringList{key, QString::number(step[key].toInteger(0))};
        auto &geometry = groups.emplace_back("Geometry", QStringList{});
        const QJsonObject geometryReport = report["geometry"].toObject();
        for (const char *key : {"bodies", "vertices", "faces", "edges"})
            geometry.second << QStringList{key, QString::number(geometryReport[key].toInteger(0))};
        return groups;
    };
    return face;
}
// R098.a3: the STEP refusal — the IFC refusal's own words. The check that failed is named
// (the application's own, or the helper's as its words come back); the model is unchanged
// and nothing was written.
// The STEP import's refusal (ADR 0172 section 8): the IFC refusal's own sheet with the STEP's
// own words.
void showStepRefusal(QWidget *parent, const QString &fileName, const std::string &code,
                      const QString &message, const QJsonObject &report) {
    RefusalSheet face;
    face.sheetName = "stepRefusal";
    face.title = "STEP import refused";
    face.prefix = "stepRefusal";
    face.summary =
        QString("✗ %1\n\nThe model was not changed. Nothing was written.\n\n"
                "The check that failed: %2\nThe words used: %3\n\n"
                "Not affected: the open model, the sheets, the assistant, your settings and "
                "recovery.")
            .arg(QFileInfo(fileName).fileName())
            .arg(QString::fromStdString(code))
            .arg(message);
    face.report = report;
    showRefusalSheet(parent, face);
}
// R098.a3: the STEP import's availability — the IFC helper's own states. The STEP import
// needs none of the IFC kernel, so the helper's Limited state is not shown; the window
// calls this only for Missing and Broken. R098.c2: the STEP export's, with its own words (the
// same helper, the same two states).
void showStepUnavailable(QWidget *parent, const IfcProbe &probe, bool exportDirection) {
    const QString direction = exportDirection ? QStringLiteral("export") : QStringLiteral("import");
    AvailabilitySheet face;
    face.sheetName = probe.state == IfcState::Missing ? (exportDirection ? "stepExportUnavailable" : "stepUnavailable")
                                                      : (exportDirection ? "stepExportBroken" : "stepBroken");
    face.title = exportDirection ? "STEP export" : "STEP import";
    face.prefix = exportDirection ? "stepExportUnavailable" : "stepUnavailable";
    if (probe.state == IfcState::Missing) {
        face.text =
            QString("STEP %1 is not installed.\n\n"
                    "The sketchyup-ifc package is not installed on this machine.\n"
                    "Everything else is unaffected: the open model, the sheets, the assistant, "
                    "your settings and recovery all work as before.\n\n"
                    "To enable STEP %1, install the sketchyup-ifc package.")
                .arg(direction);
    } else {
        face.text =
            QString("STEP %1 cannot start.\n\nThe helper could not be started.\n\n%2")
                .arg(direction)
                .arg(probe.logTail);
    }
    showAvailabilitySheet(parent, face);
}
// R098.c2 (ADR 0172 section 8): the STEP export's refusal — the IFC export refusal's shape with the
// STEP export's words: first, that nothing was written and the model is unchanged; then the reason;
// then the numbered next step for the check that failed. Red and the word "Refused", never colour
// alone. Cancel is not a refusal and has no sheet.
void showStepExportRefusal(QWidget *parent, const QString &path, const std::string &code,
                           const QString &message, const QJsonObject &report) {
    RefusalSheet face;
    face.sheetName = "stepExportRefusal";
    face.title = "STEP export refused";
    face.width = 680;
    face.height = 480;
    face.prefix = "stepExportRefusal";
    face.leadingLine = "✗ Nothing was written.";
    const QString codeText = QString::fromStdString(code);
    face.refusedLine = "Refused: " + codeText;
    QString whatToDo;
    if (codeText == "destinationExists")
        whatToDo = "1. Choose a new name — an existing file is never replaced.\n2. Export again.";
    else if (codeText == "destination")
        whatToDo = "1. Choose another location.\n2. Export again.";
    else if (codeText == "schema")
        whatToDo = "1. Choose AP214 (the default) or AP203; AP242 is not written.\n2. Export again.";
    else if (codeText == "outputTooLarge")
        whatToDo = "1. Export a smaller part of the model (one building, storey or group).\n2. Export again.";
    else if (codeText == "meshTooLarge")
        whatToDo = "1. Split the body that is too large into smaller bodies.\n2. Export again.";
    else if (codeText == "tooManyTriangles")
        whatToDo = "1. Export a smaller part of the model; the export takes at most 1,000,000 triangles.\n2. Export again.";
    else if (codeText == "packageTooLarge")
        whatToDo = "1. The model is larger than the export takes; export a smaller part of it.\n2. Export again.";
    else if (codeText == "emptyModel")
        whatToDo = "1. Add something to the model (a group or a body with faces).\n2. Export again.";
    else if (codeText == "workerMissing")
        whatToDo = "1. Install the sketchyup-ifc package.\n2. Export again; no restart is needed.";
    else if (codeText == "workerFailed")
        whatToDo = "1. Try again once.\n2. Then export a smaller part of the model.";
    else
        whatToDo = "1. Try again once.\n2. If it repeats, report it with the details.";
    face.summary =
        QString("%1\n\n%2\n\nWhat to do next:\n%3\n\nNot affected: the open model, the "
                "sheets, the assistant, your settings and recovery.")
            .arg(QFileInfo(path).fileName())
            .arg(message)
            .arg(whatToDo);
    face.report = report;
    face.copyDetails = true;
    face.closeButton = "Close";
    face.closeSuffix = "Close";
    face.closeIsDefault = true;
    showRefusalSheet(parent, face);
}
} // namespace sketchy
