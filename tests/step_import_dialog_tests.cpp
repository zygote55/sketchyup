// R098.a3 (ADR 0172 section 8): the STEP import screens, offscreen, driven by a stand-in
// sketchyup-ifc-worker.
//
// The application's own side (the screens) is what this suite checks: the options dialog in
// its STEP kind (the IFC screens' one code path: the file facts with the schema and the
// declared unit, the unit for a file that declares none, the crease angle, the IFC-only
// choices hidden, the helper line without a kernel line); the dirty-model prompt before the
// helper starts and the second one at the commit phase; the progress sheet with its seven
// steps, the bar and the Cancel; the import report with its accounting identity and its
// "how it was read" block (the schema, the units, the application's tessellation policy);
// the refusal sheet; and the "not installed" / "cannot start" sheets. The helper's Limited
// state is not a limitation for STEP (the import needs no kernel): a limited helper
// imports. A cancelled import leaves the model unchanged.
//
// The heavy work is never done here: a stand-in helper (a shell script) answers --probe with
// a small JSON and --import-step by copying a conversion package this suite writes with the
// package's own encoder (the same contract the real helper writes), playing the progress
// protocol along the way. The real worker and the IFC library belong to the IFC job; this
// suite runs in the fast tier with the helper absent (it skips when a real helper is
// present).
//
// The driver: while a flow (a menu action) runs its nested loops, it closes the prompts it
// sees (the file picker, the "Unsaved changes" boxes, the report OK) and records them. It
// only records; the assertions run after the flow returns.

#include "app/ifc_availability.hpp"
#include "app/ifc_import_dialog.hpp"
#include "app/report_sheet.hpp"
#include "app/step_import_runner.hpp"
#include "app/step_progress_sheet.hpp"
#include "app/window.hpp"
#include "io/document_io.hpp"
#include "io/step_worker.hpp"
#include <QAction>
#include <QApplication>
#include <QAbstractButton>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QProgressBar>
#include <QPushButton>
#include <QTest>
#include <QTemporaryDir>
#include <QTimer>
#include <cstddef>
#include <functional>
#include <iostream>
#include <map>
#include <stdexcept>
#include <string>
#include <vector>
namespace {
using namespace sketchy;
void check(bool ok, const std::string &message) {
    if (!ok)
        throw std::runtime_error(message);
}
bool contains(const QString &text, const QString &needle) { return text.contains(needle); }
// The size line as the sheets write it.
QString humanSize(qint64 bytes) {
    if (bytes >= 1024 * 1024)
        return QString::asprintf("%.1f MB", double(bytes) / (1024.0 * 1024.0));
    if (bytes >= 1024)
        return QString::asprintf("%d KB", int(bytes / 1024));
    return QString::number(bytes) + " B";
}
// A stand-in helper: a shell script the application can exec. The helper runs in a scrubbed
// environment (only LC_ALL is set; there is no PATH), so every command is an absolute path.
// `probe` is the --probe body, `importStep` the --import-step body.
QString standInScript(const QString &probeBody, const QString &importStepBody) {
    return QString(R"PROG(#!/bin/sh
# A stand-in for sketchyup-ifc-worker (R098.a3 dialog tests).
mode="$1"
case "$mode" in
  --probe)
)PROG"
           + probeBody
           + R"PROG(
    ;;
  --import-step)
)PROG"
           + importStepBody
           + R"PROG(
    ;;
  *)
    echo 'usage: sketchyup-ifc-worker --probe | --import-step <file> --output <file>' >&2
    exit 2
    ;;
esac
)PROG");
}
// The --import-step body: take the --output argument, write the package there, play the
// STEP progress protocol (read, tessellate, write; the bar the task counts drive), then
// report the helper's own line. `sleep` seconds of work, via /bin/sleep (the scrubbed
// environment has no PATH); `exec` makes the process die at once when the application
// cancels it. `refuse` reports the helper's refusal line and exits 3 instead.
QString importStepBody(const QString &package, const int sleepSeconds, const bool refuse, const QString &reportLine = QString()) {
    // The shell body of the stand-in: --output is read the helper's way (a flag and its value), the
    // progress protocol is played, and the package (or a refusal) is written. `exec` makes the
    // process die at once when the application cancels it. Named raw-string delimiters: the refusal
    // JSON's own ")"" would terminate a bare R"(...)" in the middle of its text.
    const QString args = QString(R"STEP(
    out=""
    while [ "$#" -gt 0 ]; do
      case "$1" in
        --output) out="$2"; shift 2 ;;
        *) shift ;;
      esac
    done
    [ -z "$out" ] && { echo 'R {"status":"refused","code":"standIn","message":"the stand-in got no --output"}'; exit 3; }
)STEP");
    if (refuse)
        return args +
               QString(R"STEP(
    echo 'P phase read'
    echo 'R {"status":"refused","code":"units","message":"The file declares no length unit and no --step-length-unit was given; it is refused (a unit is never guessed)"}'
    exit 3
)STEP");
    QString tail = QString(R"STEP(
    echo 'P phase tessellate'
    /bin/sleep 0.3
    echo 'P tasks 1'
    echo 'P at 0'
    echo 'P done 1'
    echo 'P phase write'
    echo 'P done 1'
)STEP");
    tail += reportLine.isEmpty() ? QString(R"STEP(
    echo 'R {"status":"ok","bytes":3000,"nodes":2,"meshes":1,"converted":1,"omitted":0,"unsupported":0,"triangles":12}'
    exit 0
)STEP")
                                     : (QString("    echo 'R ") + reportLine + QString("'\n    exit 0\n"));
    // The copy, the read phase, the sleep, and the tail.
    QString head = QString("    /usr/bin/cp '") + package + QString("' \"$out\" || exit 1\n");
    head += QString("    echo 'P phase read'\n    /bin/sleep ") + QString::number(sleepSeconds) + "\n";
    return args + head + tail;
}
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
// The --probe bodies: the JSON the app expects, as a shell command (the probe runs in a
// scrubbed environment, where a bare JSON line is a command name, not a value).
QString probeJson(const bool kernel, const QString &kernelReason) {
    const QString json = kernel ? QStringLiteral(R"({"worker":{"version":"stand-in"},"ifcopenshell":{"version":"0.9.0"},"kernelAvailable":true,"kernels":[]})")
                               : QStringLiteral(R"({"worker":{"version":"stand-in"},"ifcopenshell":{"version":"0.9.0"},"kernelAvailable":false,"kernels":[{"name":"opencascade","available":false,"reason":")") +
                             kernelReason +
                             QStringLiteral(R"("}]})");
    return QStringLiteral("    echo '") + json + QStringLiteral("'\n    exit 0\n");
}
// A STEP model file, as the application's cheap checks see it: the ISO header, one accepted
// schema, and a declared unit (the SI millimetre, the writer's own declaration).
QByteArray stepFile() {
    return QByteArray(
        "ISO-10303-21;\n"
        "HEADER;\n"
        "FILE_DESCRIPTION(('stand-in STEP model'),'2;1');\n"
        "FILE_NAME('step.model','2026-01-01',('Author'),(''),'stand-in','stand-in','');\n"
        "FILE_SCHEMA(('AUTOMOTIVE_DESIGN { 1 0 10303 214 0 1 1 1 }'));\n"
        "ENDSEC;\n"
        "DATA;\n"
        "#1=( LENGTH_UNIT ( ) NAMED_UNIT ( * ) SI_UNIT ( .MILLI., .METRE. ) );\n"
        "#2=( NAMED_UNIT ( * ) PLANE_ANGLE_UNIT ( ) SI_UNIT ( $, .RADIAN. ) );\n"
        "#3=PRODUCT('box','',#4);\n"
        "#4=PRODUCT_CONTEXT('c',#5);\n"
        "#5=ORGANIZATION('o','d','ctx');\n"
        "#6=SHAPE_DEFINITION_REPRESENTATION(#7,#8);\n"
        "#7=PRODUCT_DEFINITION_SHAPE('','',#9);\n"
        "#8=REPRESENTATION_CONTEXT('c','k');\n"
        "#9=PRODUCT_DEFINITION('design','',#10);\n"
        "ENDSEC;\n"
        "END-ISO-10303-21;\n");
}
// A STEP file with a schema the import does not accept (AP230's part 230).
QByteArray stepFileOtherSchema() {
    return QByteArray(
        "ISO-10303-21;\n"
        "HEADER;\n"
        "FILE_SCHEMA(('AP230'));\n"
        "DATA;\n"
        "1,STEP_TYPE('x');\n"
        "ENDSEC;\n"
        "END-ISO-10303-21;\n");
}
// A STEP file that declares no length unit at all (its unit context is bare).
QByteArray stepFileNoUnits() {
    return QByteArray(
        "ISO-10303-21;\n"
        "HEADER;\n"
        "FILE_SCHEMA(('AUTOMOTIVE_DESIGN { 1 0 10303 214 0 1 1 1 }'));\n"
        "ENDSEC;\n"
        "DATA;\n"
        "#1=GLOBAL_UNIT_ASSIGNED_CONTEXT((#2));\n"
        "ENDSEC;\n"
        "END-ISO-10303-21;\n");
}
QByteArray notStep() { return QByteArray("This is a note file, not a STEP file.\n"); }
QString writeFixture(const QString &dir, const QString &name, const QByteArray &bytes) {
    const QString path = dir + "/" + name;
    QFile file(path);
    check(file.open(QIODevice::WriteOnly), "write " + name.toStdString());
    check(file.write(bytes) == bytes.size(), "write " + name.toStdString());
    file.close();
    return path;
}
// The conversion package the stand-in copies: one assembly root and one 20 x 20 x 20 mm box
// (the tessellated solid the STEP import converts; the mesh is the box's twelve triangles,
// its vertices in millimetres the package's metresPerUnit of 0.001 keeps in millimetres).
QByteArray standInPackage(const QString &schema, const QString &unit, const QString &applied) {
    IfcPackage p;
    p.engine = QJsonObject{{"library", "stand-in"}, {"version", "stand-in"}, {"completed", true}};
    p.source = QJsonObject{{"schema", schema}, {"entities", 6}, {"bytes", 1000}};
    p.units = QJsonObject{{"declared", unit != QStringLiteral("none")},
                            {"length", unit == QStringLiteral("none") ? QString() : unit},
                            {"applied", applied}, {"mmPerUnit", 1.0}, {"metresPerUnit", 0.001}};
    p.report = QJsonObject{{"notices", QJsonArray()}, {"locations", QJsonArray()},
                            {"locationTotals", QJsonObject()}};
    // The vertices are metres in the element's own frame, as the package layout requires: a 20 mm box is 0.02 m.
    const double s = 0.02;
    IfcPackageMesh mesh;
    for (int i = 0; i < 8; ++i)
        mesh.vertices.push_back({(i & 1) ? s : 0.0, (i & 2) ? s : 0.0, (i & 4) ? s : 0.0});
    const std::array<std::array<std::uint32_t, 4>, 6> quads{{{0, 2, 3, 1}, {4, 5, 7, 6}, {0, 1, 5, 4},
                                                              {2, 6, 7, 3}, {0, 4, 6, 2}, {1, 3, 7, 5}}};
    for (const auto &q : quads) {
        mesh.triangles.push_back({q[0], q[1], q[2]});
        mesh.triangles.push_back({q[0], q[2], q[3]});
    }
    mesh.materials.assign(mesh.triangles.size(), IfcPackageMesh::noMaterial);
    p.meshes = {mesh};
    IfcPackageNode root;
    root.step = 0;
    root.className = "IfcStepAssembly";
    root.guid = "00000000000000000000000000000001";
    root.name = "step.model";
    root.role = IfcNodeRole::Assembly;
    root.parent = -1;
    IfcPackageNode box;
    box.step = 0;
    box.className = "IfcStepSolid";
    box.guid = "00000000000000000000000000000002";
    box.name = "Solid 1";
    box.role = IfcNodeRole::Element;
    box.parent = 0;
    box.mesh = 0;
    p.nodes = {root, box};
    const auto bytes = encodeIfcPackage(p);
    check(!bytes.isEmpty(), "the package encodes");
    return bytes;
}
// The driver: while a flow (a menu action) runs its nested loops, it closes the prompts it
// sees. It only records; the assertions run after the flow returns.
struct Driver {
    QString choose; // the file to choose in the picker; empty: reject the picker
    std::function<void(IfcImportDialog *)> onDialog;
    std::function<void(StepProgressSheet *)> onProgress;
    std::vector<QMessageBox::StandardButton> confirmPlan; // one button per "Unsaved changes"
    int pickers = 0;
    int confirmations = 0;
    bool dialogSeen = false;
    int dialogTicks = 0;
    bool dialogGoEnabled = false;
    bool dialogAngleVisible = false;
    bool dialogSpacesHidden = true; // the IFC-only choices are hidden for the STEP kind
    bool dialogDeviationIsStepDefault = false; // 1.0 (the STEP default), seen on any tick
    bool dialogBannerVisible = false;
    QString dialogBannerText;
    QString dialogHelperText;
    QString dialogFactsText;
    QString dialogTitleText;
    QStringList sheets;
    std::map<QString, QString> summaries;
    std::map<QString, QString> hows;
    std::map<QString, QString> banner;
    std::map<QString, QString> identity;
    std::map<QString, QString> notes;
    std::map<QString, QString> progressFile;
    int progressBar = -1;
    bool sawProgressSteps = false; // a later step was the working one (the helper ran)
    bool progressCancelFound = false;
    QString progressElapsed;
    QString actionFailure;
    QTimer timer;
    explicit Driver() {
        timer.setInterval(20);
        QObject::connect(&timer, &QTimer::timeout, [this] { stepBody(); });
    }
    void start() { timer.start(); }
    void stop() {
        timer.stop();
        if (!actionFailure.isEmpty())
            throw std::runtime_error(("the driver action failed: " + actionFailure).toStdString());
    }
    void stepBody() {
        for (auto *widget : QApplication::topLevelWidgets()) {
            if (!widget->isVisible())
                continue;
            if (auto *chooser = qobject_cast<QFileDialog *>(widget)) {
                if (!chooser->property("driven").toBool()) {
                    chooser->setProperty("driven", true);
                    ++pickers;
                }
                if (choose.isEmpty()) {
                    QMetaObject::invokeMethod(chooser, [chooser] { chooser->reject(); },
                                            Qt::QueuedConnection);
                    continue;
                }
                const QString path = choose;
                QMetaObject::invokeMethod(
                    chooser,
                    [chooser, path] {
                        chooser->selectFile(path);
                        if (auto *field = chooser->findChild<QLineEdit *>("fileNameEdit");
                            field && field->isVisible())
                            field->setText(path);
                        QMetaObject::invokeMethod(chooser, "accept", Qt::QueuedConnection);
                    },
                    Qt::QueuedConnection);
                continue;
            }
            if (auto *box = qobject_cast<QMessageBox *>(widget)) {
                if (box->property("driven").toBool() || confirmPlan.empty())
                    continue;
                box->setProperty("driven", true);
                ++confirmations;
                const auto button = confirmPlan.front();
                confirmPlan.erase(confirmPlan.begin());
                QMetaObject::invokeMethod(box, [box, button] { box->button(button)->click(); },
                                       Qt::QueuedConnection);
                continue;
            }
            if (auto *dialog = qobject_cast<IfcImportDialog *>(widget)) {
                dialogSeen = true;
                ++dialogTicks;
                if (auto *go = dialog->findChild<QPushButton *>("ifcImportGo"))
                    dialogGoEnabled = dialogGoEnabled || go->isEnabled();
                if (auto *helper = dialog->findChild<QLabel *>("ifcImportHelperLine"))
                    dialogHelperText = helper->text();
                if (auto *title = dialog->findChild<QLabel *>("ifcImportTitle"))
                    dialogTitleText = title->text();
                if (auto *facts = dialog->findChild<QLabel *>("ifcImportFileFacts"))
                    if (!facts->text().isEmpty())
                        dialogFactsText = facts->text();
                if (auto *angle = dialog->findChild<QDoubleSpinBox *>("ifcImportAngle"))
                    dialogAngleVisible = angle->isVisible();
                if (auto *spaces = dialog->findChild<QWidget *>("ifcImportSpacesSection"))
                    dialogSpacesHidden = !spaces->isVisible();
                if (auto *deviation = dialog->findChild<QDoubleSpinBox *>("ifcImportDeviation"))
                    dialogDeviationIsStepDefault = deviation->value() == 1.0;
                if (auto *b = dialog->findChild<QLabel *>("ifcImportBanner")) {
                    if (!b->text().isEmpty())
                        dialogBannerText = b->text();
                    dialogBannerVisible = dialogBannerVisible || b->isVisible();
                }
                if (auto handler = onDialog) {
                    if (dialogTicks >= 2) {
                        onDialog = nullptr;
                        try {
                            handler(dialog);
                        } catch (const std::exception &error) {
                            if (actionFailure.isEmpty())
                                actionFailure = error.what();
                        } catch (...) {
                            if (actionFailure.isEmpty())
                                actionFailure = "a driver action failed";
                        }
                    }
                }
                continue;
            }
            if (auto *sheet = qobject_cast<StepProgressSheet *>(widget)) {
                if (auto *state = sheet->findChild<QLabel *>("stepProgressStep4State"))
                    if (state->text() == "\u25CF")
                        sawProgressSteps = true;
                if (auto *file = sheet->findChild<QLabel *>("stepProgressFile"))
                    progressFile[sheet->objectName()] = file->text();
                if (auto *bar = sheet->findChild<QProgressBar *>("stepProgressBar"))
                    progressBar = std::max(progressBar, bar->value());

                if (auto *cancel = sheet->findChild<QPushButton *>("stepProgressCancel"))
                    progressCancelFound = true;
                if (auto *label = sheet->findChild<QLabel *>("stepProgressElapsed"))
                    progressElapsed = label->text();
                if (auto handler = onProgress) {
                    onProgress = nullptr;
                    try {
                        handler(sheet);
                    } catch (const std::exception &error) {
                        if (actionFailure.isEmpty())
                            actionFailure = error.what();
                    } catch (...) {
                        if (actionFailure.isEmpty())
                            actionFailure = "a driver action failed";
                    }
                }
                continue;
            }
            if (auto *report = dynamic_cast<ReportSheet *>(widget)) {
                const auto name = report->objectName();
                if (!sheets.contains(name))
                    sheets.append(name);
                summaries[name] = report->summary->text();
                if (auto *how = report->findChild<QPlainTextEdit *>("stepReportHow"))
                    hows[name] = how->toPlainText();
                if (auto *label = report->findChild<QLabel *>("stepReportBanner"))
                    banner[name] = label->text();
                if (auto *label = report->findChild<QLabel *>("stepReportIdentity"))
                    identity[name] = label->text();
                if (auto *label = report->findChild<QLabel *>("stepReportNote"))
                    notes[name] = label->text();
                if (!report->property("closed").toBool()) {
                    report->setProperty("closed", true);
                    for (auto *button : report->findChildren<QAbstractButton *>())
                        if (button->isVisible() && button->objectName().endsWith("OK")) {
                            QMetaObject::invokeMethod(
                                button, [button] { button->click(); }, Qt::QueuedConnection);
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
    QApplication app(argc, argv);
    if (!resolveIfcWorker("").isEmpty()) {
        std::cout << "skipping: a real sketchyup-ifc-worker is present; the stand-in suite "
                     "assumes none\n";
        return 0;
    }
    QTemporaryDir root;
    const QByteArray stepBytes = stepFile();
    const QString stepFile = writeFixture(root.path(), "step.model", stepBytes);
    const QString notes = writeFixture(root.path(), "notes.txt", notStep());
    const QString otherSchema = writeFixture(root.path(), "other.step", stepFileOtherSchema());
    const QString noUnits = writeFixture(root.path(), "nounits.step", stepFileNoUnits());
    const QString package = writeFixture(root.path(), "package.skifc",
                                          standInPackage("ap214", "millimetre", "declared"));
    const QString available = writeExecutable(
        root.path(), "worker-available", standInScript(probeJson(true, {}),
                                                      importStepBody(package, 1, false)));
    const QString limited = writeExecutable(
        root.path(), "worker-limited",
        standInScript(probeJson(false, "the OpenCASCADE kernel is not installed"),
                       importStepBody(package, 1, false)));
    const QString broken = writeExecutable(
        root.path(), "worker-broken",
        standInScript(QStringLiteral("echo 'error: cannot start' 1>&2; exit 1"), QString()));
    const QString refused =
        writeExecutable(root.path(), "worker-refused",
                         standInScript(probeJson(true, {}), importStepBody(package, 0, true)));

    Window window;
    window.resize(1280, 860);
    window.show();
    check(QTest::qWaitForWindowExposed(&window, 5000), "The window shows");
    auto *status = window.findChild<QLabel *>("statusMessage");
    check(status, "the status label is found");
    auto *chip = window.findChild<QPushButton *>("stepImportChip");
    check(chip, "the STEP report chip is found");
    check(!chip->isVisible(), "the chip is hidden before any import");

    try {
        // S0: the options dialog in its STEP kind, on its own.
        {
            IfcImportDialog dialog(&window);
            auto *go = dialog.findChild<QPushButton *>("ifcImportGo");
            check(go, "the Import button is found");
            auto *banner = dialog.findChild<QLabel *>("ifcImportBanner");
            auto *helper = dialog.findChild<QLabel *>("ifcImportHelperLine");
            auto *facts = dialog.findChild<QLabel *>("ifcImportFileFacts");
            auto *unit = dialog.findChild<QComboBox *>("ifcImportUnit");
            auto *deviation = dialog.findChild<QDoubleSpinBox *>("ifcImportDeviation");
            auto *angle = dialog.findChild<QDoubleSpinBox *>("ifcImportAngle");
            auto *spaces = dialog.findChild<QWidget *>("ifcImportSpacesSection");
            auto *geo = dialog.findChild<QWidget *>("ifcImportGeoSection");
            check(unit && deviation && angle && spaces && geo, "the controls are found");
            // The default is IFC: its wording, its choices, the angle hidden.
            check(dialog.findChild<QLabel *>("ifcImportTitle")->text() == "IFC import",
                  "the default kind is IFC");
            check(!dialog.isStep(), "the default kind is IFC");
            // The dialog is not shown: visible state is the explicit flag (isHidden), not on-screen visibility.
            check(angle->isHidden() && !spaces->isHidden() && !geo->isHidden(),
                  "IFC hides the angle and leaves the IFC-only choices unhidden");
            check(deviation->value() == 0.5 && unit->currentText() == "m",
                  "the IFC defaults are a 0.5 mm deviation and metres");
            // The STEP kind: its own wording and defaults; the IFC-only choices hidden.
            dialog.setKind(IfcImportDialog::Kind::Step);
            check(dialog.findChild<QLabel *>("ifcImportTitle")->text() == "STEP import",
                  "the STEP kind says STEP import");
            check(dialog.isStep(), "the kind is STEP");
            check(!angle->isHidden() && angle->value() == 30.0,
                  "the STEP kind leaves the crease angle unhidden (30 degrees)");
            check(spaces->isHidden() && geo->isHidden(),
                  "the STEP kind hides the IFC-only choices");
            check(deviation->value() == 1.0 && unit->currentText() == "mm",
                  "the STEP defaults are a 1.0 mm deviation and millimetres");
            // R098.d (ADR 0172 section 3): the dialog says the editability loss in the plain words.
            bool lossSaid = false;
            for (auto *label : dialog.findChildren<QLabel *>())
                lossSaid = lossSaid || label->text().contains("cannot be edited as curves");
            check(lossSaid, "the STEP import dialog says the editability loss");
            deviation->setValue(0.25);
            angle->setValue(45.0);
            unit->setCurrentText("in");
            const StepWorkerOptions worker = dialog.stepWorkerOptions();
            check(worker.deflectionMm == 0.25 && worker.angleDeg == 45.0 &&
                      worker.lengthUnitOverride == "in",
                  "the choices reach the worker options");
            // The helper line and the limited banner: the STEP kind shows the helper's own
            // reader, no kernel line, and a Limited state raises no banner.
            IfcProbe limitedProbe;
            limitedProbe.state = IfcState::Limited;
            limitedProbe.ifcOpenShell = "0.9.0";
            limitedProbe.workerVersion = "stand-in"; // the probe fills it; a hand-made probe must
            dialog.setProbe(limitedProbe);
            check(contains(helper->text(), "sketchyup-ifc-worker stand-in"),
                  "the helper is named: " + helper->text().toStdString());
            check(!contains(helper->text(), "kernel"),
                  "the STEP helper line carries no kernel line");
            check(banner->isHidden(), "a Limited helper raises no STEP banner");
            IfcImportDialog ifcDialog(&window);
            ifcDialog.setProbe(limitedProbe);
            auto *ifcBanner = ifcDialog.findChild<QLabel *>("ifcImportBanner");
            check(!ifcBanner->isHidden(), "the same Limited helper still raises the IFC banner");
            // A valid file: the facts (the schema and the declared unit) and an enabled Import.
            dialog.setFile(stepFile);
            check(dialog.fileName() == "step.model", "the file is named");
            check(facts->text() ==
                      QString("ap214 · millimetre · %1").arg(humanSize(stepBytes.size())),
                  "the facts name the schema and the declared unit: " +
                      facts->text().toStdString());
            check(go->isEnabled() && !banner->isVisible() && !dialog.refused(),
                  "a valid file enables Import");
            // A file with a schema the import does not accept: refused, named, Import off.
            dialog.setFile(otherSchema);
            check(dialog.refused() && !go->isEnabled(), "a non-accepted schema is refused");
            check(contains(banner->text(), "not AP203, AP214 or AP242"),
                  "the refusal names what is wrong: " + banner->text().toStdString());
            // A file without a unit: accepted by the cheap checks (the unit is the option's),
            // the unit is what a file that declares none would take.
            dialog.setFile(noUnits);
            check(!dialog.refused() && go->isEnabled(), "a unit-less file is not refused");
            check(contains(facts->text(), "no unit declared"),
                  "the facts say no unit is declared: " + facts->text().toStdString());
            // A non-STEP file: refused, named, Import off.
            dialog.setFile(notes);
            check(dialog.refused() && !go->isEnabled(), "a non-STEP file is refused");
            check(banner->text() ==
                      QString("✗ The file does not start with ISO-10303-21; it is not a "
                                      "STEP file."),
                  "the refusal names what is wrong: " + banner->text().toStdString());
        }
        // S1: a missing helper is "not installed"; the model is untouched.
        {
            window.setIfcWorkerPath("");
            check(window.ifcProbe().state == IfcState::Missing, "no helper: missing");
            Driver driver;
            driver.start();
            window.findChild<QAction *>("file.importStep")->trigger();
            driver.stop();
            check(driver.sheets == (QStringList{"stepUnavailable"}),
                  "a missing helper shows the unavailable sheet");
            check(contains(driver.summaries["stepUnavailable"], "not installed"),
                  "the sheet says the package is not installed");
            check(!contains(driver.summaries["stepUnavailable"], "cannot start"),
                  "missing and broken are different sheets");
            check(!chip->isVisible(), "the chip is hidden without a helper");
            auto &doc = window.document();
            check(doc.history().total == 0 && !doc.dirty(), "nothing was changed");
        }
        // S2: a broken helper is "cannot start", with the helper's last log line.
        {
            window.setIfcWorkerPath(broken);
            Driver driver;
            driver.start();
            window.findChild<QAction *>("file.importStep")->trigger();
            driver.stop();
            check(driver.sheets == (QStringList{"stepBroken"}),
                  "a broken helper shows the broken sheet");
            check(contains(driver.summaries["stepBroken"], "cannot start"),
                  "the sheet says the helper cannot start");
            check(contains(driver.summaries["stepBroken"], "error: cannot start"),
                  "the helper's last log line is shown");
            check(window.ifcProbe().state == IfcState::Broken && !chip->isVisible(),
                  "the broken state is recorded and the chip stays hidden");
        }
        // S3: a limited helper (no kernel) still imports: the STEP import needs none.
        {
            window.setIfcWorkerPath(limited);
            const auto history = window.document().history().total;
            Driver driver;
            driver.choose = stepFile;
            driver.onDialog = [] (IfcImportDialog *dialog) {
                if (auto *go = dialog->findChild<QPushButton *>("ifcImportGo"))
                    QTest::mouseClick(go, Qt::LeftButton);
            };
            driver.start();
            window.findChild<QAction *>("file.importStep")->trigger();
            driver.stop();

            check(driver.confirmations == 0, "an unsaved-model prompt appears for a dirty "
                                                   "model only");
            check(driver.dialogSeen, "the options dialog opened");
            check(!driver.dialogBannerVisible, "the limited helper raises no STEP banner");
            check(driver.dialogAngleVisible, "the STEP kind shows the crease angle");
            check(driver.dialogSpacesHidden, "the STEP kind hides the IFC-only choices");
            check(driver.dialogDeviationIsStepDefault, "the STEP default deviation is 1.0 mm");
            check(contains(driver.dialogHelperText, "the STEP import's own reader"),
                  "the helper line is the reader's own words: " +
                      driver.dialogHelperText.toStdString());
            check(driver.progressBar == 100, "the progress bar ran to completion");
            check(driver.sawProgressSteps, "a later step ran while the geometry converted");
            check(driver.progressFile["stepProgressSheet"] == "step.model",
                  "the progress sheet names the file");
            check(contains(driver.progressElapsed, "s"), "the elapsed time ticks");
            auto &doc = window.document();
            check(!doc.bodies().empty(), "the model has bodies");
            check(doc.history().total == history + 1, "the import is one edit");
            check(contains(status->text(), "Imported an unsaved STEP model") &&
                      contains(status->text(), "Source file unchanged"),
                  "the status names the outcome: " + status->text().toStdString());
            // The report chip: it appears on the status row; the helper omitted nothing, so
            // it offers the report without a count.
            check(chip->isVisible() && contains(chip->text(), "STEP import report"),
                  "the chip offers the report: " + chip->text().toStdString());
            // The report.
            check(driver.sheets.contains("stepImportReport"), "the report sheet was shown");
            check(contains(driver.banner["stepImportReport"], "✓ Import finished"),
                  "the banner names the outcome: " +
                      driver.banner["stepImportReport"].toStdString());
            check(driver.summaries["stepImportReport"].startsWith("step.model\n"),
                  "the summary names the file");
            check(contains(driver.summaries["stepImportReport"], "ap214"),
                  "the summary names the schema");
            check(contains(driver.summaries["stepImportReport"], "Converted: 1") &&
                      contains(driver.summaries["stepImportReport"], "Triangles: 12"),
                  "the summary names the helper's own counts: " +
                      driver.summaries["stepImportReport"].toStdString());
            check(driver.identity["stepImportReport"] ==
                          "nodes read 2 = converted to geometry 1 + structure and metadata 1 + omitted 0",
                  "the accounting identity holds: " +
                      driver.identity["stepImportReport"].toStdString());
            check(driver.hows["stepImportReport"] ==
                          "Schema: ap214\n"
                          "Units: millimetre (the file declares it)\n"
                          "Tessellation: 1.00 mm deviation · 30.0° crease angle · 100000 per element · 1000000 total\n"
                          "No georeference: the STEP import keeps the helper's axes",
                  "the how-it-was-read block is filled: " +
                      driver.hows["stepImportReport"].toStdString());
            // R098.d (ADR 0172 section 3): the report says the editability loss in the plain words.
            check(contains(driver.notes["stepImportReport"], "mesh of flat faces") &&
                      contains(driver.notes["stepImportReport"], "cannot be edited as curves"),
                  "the report's note says the editability loss: " + driver.notes["stepImportReport"].toStdString());
            // The report can be shown again from the menu.
            Driver again;
            again.start();
            window.findChild<QAction *>("file.stepImportReport")->trigger();
            again.stop();
            check(again.sheets == (QStringList{"stepImportReport"}),
                  "the menu re-shows the report");
            // The source file is never modified.
            QFile source(stepFile);
            check(source.open(QIODevice::ReadOnly), "the source opens");
            check(source.readAll() == stepBytes, "the source file is byte for byte unchanged");
            // Undo takes the import back.
            doc.undo();
            check(!doc.canUndo() && doc.bodies().empty(), "undo undoes the import");
        }
        // S4: a helper that refuses: the refusal sheet, the model untouched.
        {
            window.setIfcWorkerPath(refused);
            const auto encoded = encodeContainer(window.document());
            const auto history = window.document().history().total;
            Driver driver;
            driver.choose = stepFile;
            driver.onDialog = [] (IfcImportDialog *dialog) {
                if (auto *go = dialog->findChild<QPushButton *>("ifcImportGo"))
                    QTest::mouseClick(go, Qt::LeftButton);
            };
            driver.start();
            window.findChild<QAction *>("file.importStep")->trigger();
            driver.stop();
            check(driver.sheets == (QStringList{"stepRefusal"}), "the refusal sheet was shown");
            check(contains(driver.summaries["stepRefusal"], "units"),
                  "the refusal names the check that failed: " +
                      driver.summaries["stepRefusal"].toStdString());
            check(contains(driver.summaries["stepRefusal"], "The model was not changed"),
                  "the refusal says the model was not changed");
            auto &doc = window.document();
            check(encodeContainer(doc) == encoded && doc.history().total == history,
                  "the model is byte for byte unchanged");
        }
        // S5: a cancelled import leaves the model unchanged and clears nothing of the
        // previous import's chip.
        {
            window.setIfcWorkerPath(available);
            const auto encoded = encodeContainer(window.document());
            Driver driver;
            driver.choose = stepFile;
            driver.onDialog = [] (IfcImportDialog *dialog) {
                QTest::keyClick(dialog, Qt::Key_Return);
            };
            driver.onProgress = [] (StepProgressSheet *sheet) {
                if (auto *cancel = sheet->findChild<QPushButton *>("stepProgressCancel"))
                    QTest::mouseClick(cancel, Qt::LeftButton);
            };
            driver.start();
            window.findChild<QAction *>("file.importStep")->trigger();
            driver.stop();
            check(driver.progressCancelFound, "the progress sheet offers Cancel");
            check(contains(status->text(), "cancelled"),
                  "the status says the import was cancelled: " +
                      status->text().toStdString());
            auto &doc = window.document();
            check(encodeContainer(doc) == encoded, "the model is byte for byte unchanged");
            check(chip->isVisible() && contains(chip->text(), "STEP import report"),
                  "the previous import's chip stays");
        }
        // S6: the report chip's attention is the helper's own omitted count: a helper that
        // read solids it could not convert says so, and the chip counts them.
        {
            const QString attentionWorker =
                writeExecutable(root.path(), "worker-attention",
                                 standInScript(probeJson(true, {}),
                                               importStepBody(package, 0, false,
                                                              QStringLiteral(
                                                                   "{\"status\":\"ok\",\"bytes\":"
                                                                   "3000,\"nodes\":3,\"meshes\":"
                                                                   "1,\"converted\":1,\"omitted\":"
                                                                   "3,\"unsupported\":0,\"triangles\":"
                                                                   "12}"))));
            window.setIfcWorkerPath(attentionWorker);
            Driver driver;
            driver.choose = stepFile;
            driver.onDialog = [] (IfcImportDialog *dialog) {
                if (auto *go = dialog->findChild<QPushButton *>("ifcImportGo"))
                    QTest::mouseClick(go, Qt::LeftButton);
            };
            driver.start();
            window.findChild<QAction *>("file.importStep")->trigger();
            driver.stop();
check(chip->isVisible() && contains(chip->text(), "3 need attention"),
                  "the chip counts the helper's own omitted solids: " + chip->text().toStdString());
        }
    } catch (const std::exception &error) {
        std::cerr << "FAIL: " << error.what() << '\n';
        return 1;
    }
    std::cout << "passed: the STEP import screens\n";
    return 0;
}
