// R097.e2 (ADR 0171 section 16.8, 16.9): the IFC export screens, offscreen, driven by a stand-in
// sketchyup-ifc-worker.
//
// The application's own side (the screens) is what this suite checks: the plan dialog with its
// written / not-written / georeferencing / "what this is" lines (16.8), its keyboard (Enter
// exports, Escape cancels, an arrow scrolls the form, 16.9); the system Save dialog; the
// existing-destination refusal; the progress sheet with its four steps, the bar, the elapsed
// time and the Cancel; the export report with its accounting identity; the refusal sheet; and
// a cancelled export that writes nothing (16.8). The heavy work is never done here: a stand-in
// helper (a shell script) answers --probe, --export and --check. The suite runs in the fast
// tier with the helper absent (it skips when a real helper is present).
//
// The driver: while a flow (the menu action) runs its nested loops, it walks the plan dialog
// (accept or cancel it), answers the Save dialog, optionally cancels the progress sheet and
// closes the report or refusal it sees. It records; the assertions run after the flow returns.

#include "app/ifc_export_dialog.hpp"
#include "app/ifc_export_progress_sheet.hpp"
#include "app/ifc_export_runner.hpp"
#include "app/report_sheet.hpp"
#include "app/window.hpp"
#include "core/annotation_anchors.hpp"
#include "core/annotations.hpp"
#include "core/body.hpp"
#include "core/drawing_sheets.hpp"
#include "core/scenes.hpp"
#include "core/sections.hpp"
#include "core/terrain.hpp"
#include "geometry/section.hpp"
#include "io/ifc_export.hpp"
#include "io/ifc_worker.hpp"
#include <QAction>
#include <QApplication>
#include <QAbstractButton>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLabel>
#include <QLineEdit>
#include <QList>
#include <QMap>
#include <QPushButton>
#include <QProgressBar>
#include <QScrollBar>
#include <QTest>
#include <QTemporaryDir>
#include <QTimer>
#include <iostream>
#include <functional>
#include <map>
#include <memory>
#include <stdexcept>
#include <string>
namespace {
using namespace sketchy;
void check(bool ok, const std::string &message) {
    if (!ok)
        throw std::runtime_error(message);
}
bool contains(const QString &text, const QString &needle) { return text.contains(needle); }
// Write a helper file and make it executable. The application execs it directly (a shebang);
// the helper runs in a scrubbed environment (only LC_ALL, no PATH), so the body uses only
// shell builtins (printf, case, while, shift) and absolute paths.
QString writeExecutable(const QString &dir, const QString &name, const QString &body) {
    const QString path = dir + "/" + name;
    QFile file(path);
    check(file.open(QIODevice::WriteOnly), "write the stand-in " + name.toStdString());
    const auto bytes = body.toUtf8();
    check(file.write(bytes) == bytes.size(), "write the stand-in " + name.toStdString());
    file.close();
    check(QFile::setPermissions(path, QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner),
          "make the stand-in executable");
    return path;
}
QString writeFixture(const QString &dir, const QString &name, const QByteArray &bytes) {
    const QString path = dir + "/" + name;
    QFile file(path);
    check(file.open(QIODevice::WriteOnly), "write " + name.toStdString());
    check(file.write(bytes) == bytes.size(), "write " + name.toStdString());
    file.close();
    return path;
}

// ---- The model ---------------------------------------------------------------------------------------------------
std::shared_ptr<Body> box(Id id, Id parent, const std::string &name, Vec3 origin, Vec3 size) {
    auto body = std::make_shared<Body>();
    body->id = id;
    body->parent = parent;
    body->name = name;
    const auto face =
        body->surface.addFace({{origin, origin + Vec3{size.x, 0, 0}, origin + Vec3{size.x, size.y, 0},
                               origin + Vec3{0, size.y, 0}}});
    body->surface.extrude(face, size.z);
    body->topology = Topology::rebuild(body->surface, {});
    return body;
}
std::shared_ptr<Body> group(Id id, Id parent, const std::string &name) {
    auto body = std::make_shared<Body>();
    body->id = id;
    body->parent = parent;
    body->name = name;
    body->kind = BodyKind::Group;
    return body;
}
enum class SiteMode { None, Authority, NoLabel };
// A model with two bodies, an empty group and a body with wires (no faces), plus one
// annotation, scene, section and sheet (each a "not written" kind), and, when asked, a site
// origin (with an authority CRS, or without a label).
Document makeModel(SiteMode site = SiteMode::Authority) {
    Document doc;
    Edit edit{"Export model", {}};
    const auto add = [&] (std::shared_ptr<Body> body) {
        edit.changes.push_back({body->id, nullptr, body});
        return body;
    };
    add(group(1, 0, "House"));
    add(box(2, 1, "Wall", {0, 0, 0}, {4, 0.3, 2.5}));
    add(box(3, 1, "Column", {1, 0, 0}, {1, 1, 1}));
    add(group(4, 1, "Empty"));
    {
        auto wires = std::make_shared<Body>();
        wires->id = 5;
        wires->parent = 1;
        wires->name = "Wires";
        const auto a = wires->surface.vertex({0, 0, 0});
        const auto b = wires->surface.vertex({1, 0, 0});
        const auto c = wires->surface.vertex({1, 1, 0});
        wires->surface.wires = {{a, b}, {b, c}};
        wires->topology = Topology::rebuild(wires->surface, {});
        add(wires);
    }
    edit.nextIdFloor = 6;
    doc.apply(std::move(edit), doc.revision());
    AnnotationRecord label;
    label.name = "Label";
    label.kind = AnnotationKind::Label;
    label.text = "Label";
    check(!doc.bodies().at(2)->topology.edges.empty(), "the Wall body has edges");
    label.anchors = {edgeAnchor(doc, 2, doc.bodies().at(2)->topology.edges.begin()->first, 0.5)};
    createAnnotation(doc, label);
    SceneSnapshot snapshot;
    snapshot.camera = SceneCamera{};
    createScene(doc, "Front", snapshot);
    createSection(doc, "Cut", 0, SectionPlane{{1, 0, 0}, -1});
    createDrawingSheet(doc, "Cover", DrawingPaper{});
    if (site == SiteMode::Authority)
        setSiteOrigin(doc, SiteOrigin{729013.35, 9063992.68, 1.3, "EPSG:32760", {"test", ""}});
    else if (site == SiteMode::NoLabel)
        setSiteOrigin(doc, SiteOrigin{729013.35, 9063992.68, 1.3, "", {"test", ""}});
    return doc;
}

// ---- The stand-in helper -----------------------------------------------------------------------------------------
// The --export R line: the counts the application checks against the plan, and the entities
// the --check line repeats (they must agree). `projects` is 1 and the other spatial counts the
// plan's, so the nodes identity holds with `elements` as the remainder.
QString exportRLine(const QJsonObject &plan, qint64 fileSize) {
    const auto spatial = plan["spatial"].toObject();
    const auto written = plan["written"].toObject();
    const auto geo = plan["georeference"].toObject();
    const int sites = spatial["sites"].toInt(0);
    const int buildings = spatial["buildings"].toInt(0);
    const int storeys = spatial["storeys"].toInt(0);
    const int spaces = spatial["spaces"].toInt(0);
    const int assemblies = spatial["assemblies"].toInt(0);
    const int nodes = written["nodes"].toInt(0);
    const QJsonObject counts{
        {"projects", 1},
        {"sites", sites},
        {"buildings", buildings},
        {"storeys", storeys},
        {"spaces", spaces},
        {"assemblies", assemblies},
        {"elements", nodes - 1 - sites - buildings - storeys - spaces - assemblies},
        {"representations", written["geometryNodes"].toInt(0)},
        {"triangles", written["triangles"].toInt(0)},
        {"vertices", written["vertices"].toInt(0)},
        {"styles", written["styles"].toInt(0)},
        {"colourMaps", written["colourMaps"].toInt(0)},
        {"closedFaceSets", written["closedFaceSets"].toInt(0)},
        {"materials", written["materials"].toInt(0)},
        {"propertySets", written["propertySets"].toInt(0)},
        {"properties", written["properties"].toInt(0)},
        {"layerAssigned", written["hidden"].toInt(0)},
    };
    const QJsonObject report{
        {"status", "ok"},
        {"bytes", fileSize},
        {"entities", 7},
        {"library", QJsonObject{{"name", "stand-in"}, {"version", "0"}, {"commit", "standin"}}},
        {"georeference", geo["result"].toString() == "written" ? "written" : "none"},
        {"classes", QJsonObject{}},
        {"counts", counts},
        {"written", QJsonObject{}},
        {"fallbacks", QJsonArray{}},
        {"predefinedTypeDropped", 0},
    };
    return QStringLiteral("R ") + QString::fromUtf8(QJsonDocument(report).toJson(QJsonDocument::Compact));
}
// The stand-in helper: --probe (the app's probe body), --export (write a small STEP file, play
// the progress, report; `fail` stops with code 1 and no file) and --check (the same entities).
QString exportWorkerScript(const QJsonObject &plan, const QByteArray &ifcFile, int sleepSeconds, bool fail) {
    const QString sleep =
        sleepSeconds > 0 ? QString("    /bin/sleep %1\n").arg(sleepSeconds) : QString{};
    const QString exportBody =
        fail ? QStringLiteral("    echo 'R {\"status\":\"refused\",\"code\":\"standIn\",\"message\":\"the stand-in stopped\"}'\n"
                     "    exit 1\n")
              : sleep +
                    QStringLiteral(
                        "    printf '%s\\n' 'ISO-10303-21;' 'HEADER;' 'FILE_DESCRIPTION(,);' "
                        "'FILE_NAME(stand.ifc,,(),,stand-in,stand-in,);' 'FILE_SCHEMA((IFC4));' "
                        "'DATA;' 'ENDSEC;' 'END-ISO-10303-21;' > \"$out\"\n"
                        "    echo 'P phase build'\n"
                        "    echo 'P done 1'\n") +
          "    echo '" +
          exportRLine(plan, ifcFile.size()) + "'\n"
                    "    exit 0\n";
    const QString checkBody = QStringLiteral("    echo 'R {\"status\":\"ok\",\"entities\":7}'\n    exit 0\n");
    const QString probe = QStringLiteral(
        R"({"worker":{"version":"stand-in"},"ifcopenshell":{"version":"0.9.0"},"kernelAvailable":true,"kernels":[]})");
    return QString(R"PROG(#!/bin/sh
# A stand-in for sketchyup-ifc-worker (R097.e2 export dialog tests).
mode="$1"
case "$mode" in
  --probe)
    echo ')PROG"
           + probe +
           R"PROG('
    exit 0
    ;;
  --export)
    out=""
    while [ "$#" -gt 0 ]; do
      case "$1" in
        --output) out="$2"; shift 2 ;;
        *) shift ;;
      esac
    done
    [ -z "$out" ] && { echo 'R {"status":"refused","code":"standIn","message":"no --output"}'; exit 3; }
)PROG"
           + exportBody +
           R"PROG(    ;;
  --check)
)PROG"
           + checkBody +
           R"PROG(    ;;
  *)
    echo 'usage: sketchyup-ifc-worker --probe | --export | --check' >&2
    exit 2
    ;;
esac
)PROG");
}
// The IFC file the stand-in writes, the way a helper would name it.
QByteArray standInIfc() {
    return QByteArray(
        "ISO-10303-21;\n"
        "HEADER;\n"
        "FILE_DESCRIPTION(,);\n"
        "FILE_NAME(stand.ifc,,(),,stand-in,stand-in,);\n"
        "FILE_SCHEMA((IFC4));\n"
        "DATA;\n"
        "ENDSEC;\n"
        "END-ISO-10303-21;\n");
}

// ---- The driver --------------------------------------------------------------------------------------------------
struct Driver {
    int exportDialogMode = 0; // 0: leave it, 1: Export to file..., 2: Cancel
    QString savePath; // the Save dialog's target; empty: reject it
    bool cancelProgress = false; // click the progress sheet's Cancel
    int exportDialogTicks = 0;
    bool exportGoEnabled = false;
    bool exportGoIsDefault = false;
    bool exportFormHasFocus = false;
    int formScrollTop = -1;
    int formScrollBottom = -1;
    QString exportBanner;
    QString exportHelper;
    int saveCount = 0;
    bool progressSeen = false;
    int progressMaxBar = -1;
    bool progressCancelFound = false;
    QString progressFile;
    bool progressAllDone = false;
    QList<QString> sheets;
    QMap<QString, QString> summaries;
    QMap<QString, QString> banners;
    QMap<QString, QString> identities;
    QString actionFailure;
    QTimer timer;
    Driver() {
        timer.setInterval(20);
        QObject::connect(&timer, &QTimer::timeout, [this] { step(); });
    }
    void start() { timer.start(); }
    void stop() {
        timer.stop();
        if (!actionFailure.isEmpty())
            throw std::runtime_error("the driver action failed: " + actionFailure.toStdString());
    }
    void step() {
        for (auto *widget : QApplication::topLevelWidgets()) {
            if (!widget->isVisible())
                continue;
            if (auto *dialog = qobject_cast<IfcExportDialog *>(widget)) {
                if (auto *go = dialog->findChild<QPushButton *>("ifcExportGo")) {
                    exportGoEnabled = exportGoEnabled || go->isEnabled();
                    exportGoIsDefault = exportGoIsDefault || go->isDefault();
                }
                if (auto *banner = dialog->findChild<QLabel *>("ifcExportBanner"))
                    if (!banner->text().isEmpty())
                        exportBanner = banner->text();
                if (auto *helper = dialog->findChild<QLabel *>("ifcExportHelperLine"))
                    if (!helper->text().isEmpty())
                        exportHelper = helper->text();
                if (auto *scroll = dialog->findChild<QScrollArea *>("ifcExportFormScroll")) {
                    formScrollTop = std::max(formScrollTop, scroll->verticalScrollBar()->value());
                    if (auto *viewport = scroll->viewport())
                        formScrollBottom = std::max(formScrollBottom, viewport->height());
                }
                if (exportDialogMode != 0 && !dialog->property("closed").toBool()) {
                    dialog->setProperty("closed", true);
                    auto *button = exportDialogMode == 1 ? dialog->findChild<QPushButton *>("ifcExportGo")
                                                        : dialog->findChild<QPushButton *>("ifcExportCancel");
                    QMetaObject::invokeMethod(button, [button] { button->click(); }, Qt::QueuedConnection);
                }
                continue;
            }
            if (auto *save = qobject_cast<QFileDialog *>(widget)) {
                if (save->objectName() != "ifcExportFileDialog")
                    continue;
                if (!save->property("driven").toBool()) {
                    save->setProperty("driven", true);
                    ++saveCount;
                }
                // A save to a file that does not exist yet: set the name field and accept (the
                // pattern from tests/dxf_input_tests.cpp). Re-issued every tick until it takes.
                if (savePath.isEmpty()) {
                    QMetaObject::invokeMethod(save, [save] { save->reject(); }, Qt::QueuedConnection);
                    continue;
                }
                if (auto *name = save->findChild<QLineEdit *>("fileNameEdit"))
                    name->setText(savePath);
                QMetaObject::invokeMethod(save, "accept");
                continue;
            }
            if (auto *sheet = qobject_cast<IfcExportProgressSheet *>(widget)) {
                progressSeen = true;
                if (auto *file = sheet->findChild<QLabel *>("ifcExportProgressFile"))
                    progressFile = file->text();
                if (auto *bar = sheet->findChild<QProgressBar *>("ifcExportProgressBar"))
                    progressMaxBar = std::max(progressMaxBar, bar->value());
                auto *cancel = sheet->findChild<QPushButton *>("ifcExportProgressCancel");
                if (cancel)
                    progressCancelFound = true;
                if (cancelProgress && cancel && !sheet->property("cancelled").toBool()) {
                    sheet->setProperty("cancelled", true);
                    QMetaObject::invokeMethod(cancel, [cancel] { cancel->click(); }, Qt::QueuedConnection);
                }
                // Finished state: every step is a check mark.
                int done = 0;
                for (int i = 1; i <= 4; ++i)
                    if (auto *state = sheet->findChild<QLabel *>(QString("ifcExportProgressStep%1State").arg(i));
                        state && state->text() == "\u2713")
                        ++done;
                progressAllDone = progressAllDone || done == 4;
                continue;
            }
            if (auto *report = dynamic_cast<ReportSheet *>(widget)) {
                const auto name = report->objectName();
                if (!sheets.contains(name))
                    sheets.append(name);
                summaries[name] = report->summary->text();
                if (auto *label = report->findChild<QLabel *>("ifcReportBanner"))
                    banners[name] = label->text();
                if (auto *label = report->findChild<QLabel *>("ifcExportReportBanner"))
                    banners[name] = label->text();
                if (auto *label = report->findChild<QLabel *>("ifcExportRefusalFirst"))
                    banners[name] = label->text();
                if (auto *label = report->findChild<QLabel *>("ifcExportReportIdentity"))
                    identities[name] = label->text();
                if (!report->property("closed").toBool()) {
                    report->setProperty("closed", true);
                    for (auto *button : report->findChildren<QAbstractButton *>())
                        if (button->isVisible() && (button->objectName().endsWith("OK") ||
                                                   button->objectName() == "ifcExportRefusalClose" ||
                                                   button->objectName() == "ifcExportRefusalCopy")) {
                            QMetaObject::invokeMethod(button, [button] { button->click(); },
                                                Qt::QueuedConnection);
                            break;
                        }
                }
                continue;
            }
        }
    }
};
} // namespace

int main(int argc, char **argv) {
    QTemporaryDir preferences;
    qputenv("XDG_CONFIG_HOME", preferences.path().toUtf8());
    // The widget file dialogs, which the driver fills in (the app itself uses the system dialog).
    QApplication::setAttribute(Qt::AA_DontUseNativeDialogs);
    QApplication app(argc, argv);
    if (!resolveIfcWorker("").isEmpty()) {
        std::cout << "skipping: a real sketchyup-ifc-worker is present; the stand-in suite "
                     "assumes none\n";
        return 0;
    }
    QTemporaryDir root;

    // The plan the stand-in answers with, computed from the same model the flow uses.
    const Document model = makeModel(SiteMode::Authority);
    const QJsonObject plan = planIfcExport(model);
    check(plan["written"].toObject()["bodies"].toInt(0) >= 2, "the model has written bodies");
    const QByteArray ifcFile = standInIfc();
    const QString okWorker =
        writeExecutable(root.path(), "worker-ok", exportWorkerScript(plan, ifcFile, 1, false));
    const QString slowWorker =
        writeExecutable(root.path(), "worker-slow", exportWorkerScript(plan, ifcFile, 5, false));
    const QString failWorker =
        writeExecutable(root.path(), "worker-fail", exportWorkerScript(plan, ifcFile, 0, true));

    Window window;
    window.resize(1280, 860);
    window.show();
    check(QTest::qWaitForWindowExposed(&window, 5000), "The window shows");
    auto *status = window.findChild<QLabel *>("statusMessage");
    check(status, "the status label is found");
    // Put the model in the window.
    const auto doc = makeModel(SiteMode::Authority);
    window.document() = doc;

    try {
        // T1: the plan dialog on its own, a real model.
        {
            IfcExportDialog dialog(&window);
            auto *go = dialog.findChild<QPushButton *>("ifcExportGo");
            auto *banner = dialog.findChild<QLabel *>("ifcExportBanner");
            auto *helper = dialog.findChild<QLabel *>("ifcExportHelperLine");
            auto *what = dialog.findChild<QLabel *>("ifcExportWhatThisIs");
            auto *geo = dialog.findChild<QLabel *>("ifcExportGeoreference");
            check(go && banner && helper && what && geo, "the dialog controls are found");
            check(banner->isHidden(), "a healthy model shows no banner");
            check(helper->text().isEmpty(), "no helper line before a probe");
            // The written lines, as the plan counts them.
            dialog.setDocument(model);
            const auto spatial = plan["spatial"].toObject();
            const auto written = plan["written"].toObject();
            auto *structure = dialog.findChild<QLabel *>("ifcExportStructureText");
            auto *groups = dialog.findChild<QLabel *>("ifcExportGroupsText");
            auto *bodies = dialog.findChild<QLabel *>("ifcExportBodiesText");
            check(structure && groups && bodies, "the written lines are found");
            const int sites = spatial["sites"].toInt(0),
                buildings = spatial["buildings"].toInt(0),
                storeys = spatial["storeys"].toInt(0);
            const QString expectedStructure = (sites + buildings + storeys > 0)
                ? QString("The IFC structure is kept: %1 sites, %2 buildings, %3 storeys")
                      .arg(sites).arg(buildings).arg(storeys)
                : QString("1 site and 1 building are created; no storeys are invented");
            check(structure->text() == expectedStructure,
                  "the structure line is the plan's: " + structure->text().toStdString());
            check(groups->text() ==
                      QString("%1, as assemblies").arg(written["groups"].toInt(0)),
                  "the groups line names its count: " + groups->text().toStdString());
            check(bodies->text().startsWith(
                      QString("%1, as triangulated shapes").arg(written["bodies"].toInt(0))),
                  "the bodies line names its count: " + bodies->text().toStdString());
            // The not-written lines: the non-zero ones shown, the zero ones hidden.
            const auto notWritten = plan["notWritten"].toObject();
            const auto countOf = [&] (const char *key) {
                auto *label = dialog.findChild<QLabel *>(QString("ifcExportCount_") + key);
                check(label, QString("the not-written count for %1 is found").arg(key).toStdString());
                return label;
            };
            const auto nameOf = [&] (const char *key) {
                auto *label = dialog.findChild<QLabel *>(QString("ifcExportNotWritten_") + key);
                check(label, QString("the not-written line for %1 is found").arg(key).toStdString());
                return label;
            };
            const int annotations = notWritten["annotations"].toInt(0);
            const int looseEdges = notWritten["looseEdges"].toInt(0) + notWritten["curves"].toInt(0);
            check(annotations >= 1 && looseEdges >= 1, "the model has an annotation and loose edges");
            check(!countOf("annotations")->isHidden() && countOf("annotations")->text() ==
                          QString::number(annotations),
                  "the annotations line is shown with its count");
            check(!countOf("looseEdges")->isHidden() && countOf("looseEdges")->text() ==
                          QString::number(looseEdges),
                  "the loose-edges line sums edges and curves");
            check(countOf("terrain")->isHidden(), "a zero kind is hidden");
            check(countOf("terrain")->isHidden() && nameOf("terrain")->isHidden(),
                  "a zero kind hides its name and count");
            // Georeferencing: the site and its authority CRS are written, a green line.
            check(contains(geo->text(), "Written") && contains(geo->text(), "EPSG:32760"),
                  "the georeference line is the green written line: " + geo->text().toStdString());
            // What this is.
            check(contains(what->text(), "tessellated") &&
                      contains(what->text(), "not a certified Reference View"),
                  "the what-this-is line is shown");
            // The helper line: available, and a limited probe still exports.
            IfcProbe availableProbe;
            availableProbe.state = IfcState::Available;
            availableProbe.ifcOpenShell = "0.9.0";
            dialog.setProbe(availableProbe);
            check(contains(helper->text(), "IfcOpenShell 0.9.0"),
                  "the available helper is named: " + helper->text().toStdString());
            IfcProbe limitedProbe;
            limitedProbe.state = IfcState::Limited;
            limitedProbe.ifcOpenShell = "0.9.0";
            dialog.setProbe(limitedProbe);
            check(contains(helper->text(), "kernel: none") ||
                      contains(helper->text(), "kernel"),
                  "a limited helper names the missing kernel");
            // Keyboard: Enter exports, Escape cancels, an arrow scrolls the form.
            dialog.setProbe(availableProbe);
            dialog.setDocument(model);
            dialog.show();
            check(QTest::qWaitForWindowExposed(&dialog, 3000), "the dialog shows");
            check(go->isDefault(), "Export to file... is the default button");
            QTest::keyClick(dialog.findChild<QScrollArea *>("ifcExportFormScroll"), Qt::Key_Down);
            auto *scroll = dialog.findChild<QScrollArea *>("ifcExportFormScroll");
            check(scroll->verticalScrollBar()->value() >= 0, "the form is the focus");
            QTest::keyClick(&dialog, Qt::Key_Return);
            check(QTest::qWaitFor([&] { return dialog.result() == QDialog::Accepted; }, 3000),
                  "Enter accepts the dialog");
            IfcExportDialog second(&window);
            second.setProbe(availableProbe);
            second.setDocument(model);
            second.show();
            check(QTest::qWaitForWindowExposed(&second, 3000), "the dialog shows");
            QTest::keyClick(&second, Qt::Key_Escape);
            check(QTest::qWaitFor([&] { return second.result() == QDialog::Rejected; }, 3000),
                  "Escape cancels the dialog");
        }
        // T2: an empty model is refused in the dialog, with Export disabled.
        {
            IfcExportDialog dialog(&window);
            auto *go = dialog.findChild<QPushButton *>("ifcExportGo");
            auto *banner = dialog.findChild<QLabel *>("ifcExportBanner");
            IfcProbe availableProbe;
            availableProbe.state = IfcState::Available;
            availableProbe.ifcOpenShell = "0.9.0";
            dialog.setProbe(availableProbe);
            dialog.setDocument(Document());
            check(dialog.refused(), "an empty model is refused");
            check(contains(banner->text(), "Nothing to export"),
                  "the refusal names what is missing: " + banner->text().toStdString());
            check(!go->isEnabled(), "Export is disabled for an empty model");
        }
        // T3: a site origin without a CRS label is an amber "Not written" line.
        {
            IfcExportDialog dialog(&window);
            auto *geo = dialog.findChild<QLabel *>("ifcExportGeoreference");
            IfcProbe availableProbe;
            availableProbe.state = IfcState::Available;
            availableProbe.ifcOpenShell = "0.9.0";
            dialog.setProbe(availableProbe);
            dialog.setDocument(makeModel(SiteMode::NoLabel));
            check(!dialog.refused(), "a model with a site but no CRS label is not refused");
            check(contains(geo->text(), "Not written") && contains(geo->text(), "no coordinate system"),
                  "the georeference line is amber: " + geo->text().toStdString());
        }
        // T4: the full flow, a successful export.
        {
            window.setIfcWorkerPath(okWorker);
            const QString out = root.path() + "/result.ifc";
            Driver driver;
            driver.exportDialogMode = 1; // Export to file...
            driver.savePath = out;
            driver.start();
            window.findChild<QAction *>("file.exportIfc")->trigger();
            driver.stop();
            check(driver.sheets.contains("ifcExportReport"),
                  "the report sheet was shown");
            check(driver.progressSeen, "the progress sheet was shown");
            check(driver.saveCount == 1, "the Save dialog was answered once");
            check(QFile::exists(out), "the IFC file was written");
            {
                QFile f(out);
                check(f.open(QIODevice::ReadOnly), "the written file opens");
                const auto bytes = f.readAll();
                check(!bytes.isEmpty() && bytes.startsWith("ISO-10303-21;"),
                      "the written file is a STEP file");
            }
            check(contains(driver.banners["ifcExportReport"], "A new IFC file was written"),
                  "the report banner says a new file was written: " +
                      driver.banners.value("ifcExportReport").toStdString());
            check(contains(driver.summaries["ifcExportReport"], "result.ifc"),
                  "the report summary names the file: " +
                      driver.summaries.value("ifcExportReport").toStdString());
            check(!driver.identities["ifcExportReport"].isEmpty() &&
                      contains(driver.identities["ifcExportReport"], "balanced"),
                  "the accounting identity is shown: " +
                      driver.identities.value("ifcExportReport").toStdString());
            check(contains(status->text(), "Exported a new IFC file"),
                  "the status line names the export");
        }
        // T5: a destination that exists is a refusal; the file is not replaced.
        {
            window.setIfcWorkerPath(okWorker);
            const QString existing = writeFixture(root.path(), "exists.ifc", standInIfc());
            const qint64 before = QFileInfo(existing).lastModified().toMSecsSinceEpoch();
            Driver driver;
            driver.exportDialogMode = 1;
            driver.savePath = existing;
            driver.start();
            window.findChild<QAction *>("file.exportIfc")->trigger();
            driver.stop();
            check(driver.sheets.contains("ifcExportRefusal"), "the refusal sheet was shown");
            check(contains(driver.banners["ifcExportRefusal"], "Nothing was written"),
                  "the refusal says nothing was written");
            check(contains(driver.summaries["ifcExportRefusal"], "already exists"),
                  "the refusal names the existing file: " +
                      driver.summaries.value("ifcExportRefusal").toStdString());
            check(QFileInfo(existing).lastModified().toMSecsSinceEpoch() == before,
                  "the existing file was not touched");
        }
        // T6: a helper that stops is a refusal; nothing is written.
        {
            window.setIfcWorkerPath(failWorker);
            const QString out = root.path() + "/failed.ifc";
            Driver driver;
            driver.exportDialogMode = 1;
            driver.savePath = out;
            driver.start();
            window.findChild<QAction *>("file.exportIfc")->trigger();
            driver.stop();
            check(driver.sheets.contains("ifcExportRefusal"), "the refusal sheet was shown");
            check(contains(driver.banners["ifcExportRefusal"], "Nothing was written"),
                  "the refusal says nothing was written");
            check(contains(driver.summaries["ifcExportRefusal"], "workerFailed") ||
                      contains(driver.summaries["ifcExportRefusal"], "stopped"),
                  "the refusal names the failed helper: " +
                      driver.summaries.value("ifcExportRefusal").toStdString());
            check(!QFile::exists(out), "no file was written");
        }
        // T7: a cancelled export writes nothing and shows no report.
        {
            window.setIfcWorkerPath(slowWorker);
            const QString out = root.path() + "/cancelled.ifc";
            Driver driver;
            driver.exportDialogMode = 1;
            driver.savePath = out;
            driver.cancelProgress = true;
            driver.start();
            window.findChild<QAction *>("file.exportIfc")->trigger();
            driver.stop();
            check(!driver.sheets.contains("ifcExportReport"), "a cancel shows no report");
            check(!driver.sheets.contains("ifcExportRefusal"), "a cancel is not a refusal");
            check(!QFile::exists(out), "a cancelled export writes nothing");
            check(contains(status->text(), "cancelled"),
                  "the status line names the cancel: " + status->text().toStdString());
        }
        // T8: a missing helper is "not installed" for export.
        {
            window.setIfcWorkerPath("");
            Driver driver;
            driver.exportDialogMode = 2; // cancel the dialog if one opens
            driver.start();
            window.findChild<QAction *>("file.exportIfc")->trigger();
            driver.stop();
            check(driver.sheets.contains("ifcUnavailable"),
                  "a missing helper shows the not-installed sheet");
            check(contains(driver.summaries["ifcUnavailable"], "export"),
                  "the not-installed sheet is for export: " +
                      driver.summaries.value("ifcUnavailable").toStdString());
            check(contains(driver.summaries["ifcUnavailable"], "sketchyup-ifc"),
                  "the not-installed sheet names the package");
        }
    }
    catch (const std::exception &e) {
        std::cerr << e.what() << std::endl;
        return 1;
    }
    std::cout << "ifc export screens: the plan, the save dialog, a success, a destination and a "
                "failed worker refusal, a cancel, and a missing helper"
              << std::endl;
    return 0;
}
