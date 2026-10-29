// R097.e1 (ADR 0171 section 16): the IFC import screens, offscreen, driven by a stand-in
// sketchyup-ifc-worker.
//
// The application's own side (the screens) is what this suite checks: the options dialog with
// its file facts, the unit / spaces / georeference / deviation choices, the helper line and
// the limited and refusal banners (16.2, 16.7); the dirty-model prompt before the helper
// starts and the second one at the commit phase (16.3); the progress sheet with its seven
// steps, the bar, the elapsed time and the Cancel (16.4); the import report with its
// accounting identity (16.5); the refusal sheet (16.6); and the "not installed" / "cannot
// start" / "limited" sheets (16.7). Enter imports and Escape cancels the options dialog
// (16.9); a cancelled import and a cancelled commit leave the model unchanged (16.3, 16.4).
//
// The heavy work is never done here: a stand-in helper (a shell script) answers --probe with
// a small JSON and --import by copying a fixture conversion package that the real helper
// wrote for the rv-tessellated-item fixture, playing the progress protocol along the way.
// The real worker and the IFC library belong to the IFC job; this suite runs in the fast
// tier with the helper absent (it skips when a real helper is present).
//
// The driver: while a flow (a menu action) runs its nested loops, it closes the prompts it
// sees (the file picker, the "Unsaved changes" boxes, the report OK) and records them. It
// only records; the assertions run after the flow returns.

#include "app/ifc_availability.hpp"
#include "app/ifc_export_dialog.hpp"
#include "app/ifc_import_dialog.hpp"
#include "app/ifc_progress_sheet.hpp"
#include "app/report_sheet.hpp"
#include "app/window.hpp"
#include "io/document_io.hpp"
#include "io/ifc_import.hpp"
#include "io/ifc_package.hpp"
#include "io/ifc_worker.hpp"
#include <QAction>
#include <QApplication>
#include <QAbstractButton>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QJsonDocument>
#include <QLabel>
#include <iostream>
#include <QLineEdit>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QProgressBar>
#include <QRadioButton>
#include <QPushButton>
#include <QTest>
#include <QTemporaryDir>
#include <QTimer>
#include <cstddef>
#include <functional>
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
// The document's unique identity (documentId) is assigned at creation; two independent
// conversions of the same file must agree on everything else.
bool sameContent(const QByteArray &a, const QByteArray &b) {
    auto norm = [] (const QByteArray &bytes) {
        QJsonObject json = QJsonDocument::fromJson(bytes).object();
        json["documentId"] = "same";
        return QJsonDocument(json).toJson();
    };
    return norm(a) == norm(b);
}
QRadioButton *radio(QWidget *root, const QString &text) {
    for (auto *button : root->findChildren<QRadioButton *>())
        if (button->text() == text)
            return button;
    return nullptr;
}
// The size line as the sheets write it.
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
// A stand-in helper: a shell script the application can exec. The helper runs in a scrubbed
// environment (only LC_ALL is set; there is no PATH), so every command is an absolute path.
// `probe` is the --probe body, `import` the --import body.
QString standInScript(const QString &probeBody, const QString &importBody) {
    return QString(R"PROG(#!/bin/sh
# A stand-in for sketchyup-ifc-worker (R097.e1 dialog tests).
mode="$1"
case "$mode" in
  --probe)
)PROG"
           + probeBody
           + R"PROG(
    ;;
  --import)
)PROG"
           + importBody
           + R"PROG(
    ;;
  *)
    echo 'usage: sketchyup-ifc-worker --probe | --import <file> --output <file>' >&2
    exit 2
    ;;
esac
)PROG");
}
// The shell lines that hold the helper until `gate` exists, for at most 20 s (2000 polls of 10 ms).
// /bin/sleep is absolute: the scrubbed environment has no PATH.
QString waitForGate(const QString &gate) {
    return QString("    i=0\n    while [ ! -e '") + gate +
           QString("' ] && [ \"$i\" -lt 2000 ]; do /bin/sleep 0.01; i=$((i + 1)); done\n");
}
// The --import body: take the --output argument, write the converted package there, play a
// little progress, then report. `sleep` seconds of work, via /bin/sleep (the scrubbed
// environment has no PATH); `exec` makes the process die at once when the application
// cancels it. `refuse` reports the helper's refusal line and exits 3 instead. A `gate` replaces
// the sleep: the helper holds at its geometry step until the driver has seen that step working
// (see heldImportBody).
QString importBody(const QString &package, const int sleepSeconds, const bool refuse,
                   const QString &gate = QString()) {
    const QString args =
        QString(R"(    out=""
    while [ "$#" -gt 0 ]; do
      case "$1" in
        --output) out="$2"; shift 2 ;;
        *) shift ;;
      esac
    done
    [ -z "$out" ] && { echo 'R {"status":"refused","code":"standIn","message":"the stand-in got no --output"}'; exit 3; }
)");
    if (refuse)
        return args + QString(R"(    echo 'R {"status":"refused","code":"tooManyEntities","message":"The file holds more entities than the bound allows."}'
    exit 3
)");
    const QString work = gate.isEmpty() ? QString("    /bin/sleep ") + QString::number(sleepSeconds) + QString("\n")
                                        : waitForGate(gate);
    return args + QString(R"(    /usr/bin/cp ')")
           + package
           + QString(R"(' "$out" || exit 1
    echo 'P phase geometry'
)")
           + work
           + QString(R"(    echo 'P tasks 3'
    echo 'P done 3'
    echo 'R {"status":"ok"}'
    exit 0
)");
}
// The --import body of the case that checks the model step: as importBody, with the geometry step
// held until the driver has seen it working (it creates `gate`; see Driver::releaseHeld). A driver
// that never sees the step gives up after 20 s and fails the check, rather than hanging the suite.
QString heldImportBody(const QString &package, const QString &gate) { return importBody(package, 0, false, gate); }
// Write a helper file and make it executable.
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
// The fixtures, as the cheap checks see them.
QByteArray modelIfc() {
    return QByteArray(
        "ISO-10303-21;\n"
        "HEADER;\n"
        "FILE_DESCRIPTION((''),'2.1;');\n"
        "FILE_NAME('model.ifc','2024-01-01',(''),(''),'stand-in','stand-in','');\n"
        "FILE_SCHEMA(('IFC4'));\n"
        "DATA;\n"
        "ENDSEC;\n"
        "END-ISO-10303-21;\n");
}
QByteArray truncatedIfc() {
    return QByteArray(
        "ISO-10303-21;\n"
        "HEADER;\n"
        "FILE_SCHEMA(('IFC4'));\n"
        "DATA;\n"
        "1,I#1,'a wall';\n");
}
QByteArray notIfc() { return QByteArray("This is a note file, not an IFC file.\n"); }
QString writeFixture(const QString &dir, const QString &name, const QByteArray &bytes) {
    const QString path = dir + "/" + name;
    QFile file(path);
    check(file.open(QIODevice::WriteOnly), "write " + name.toStdString());
    check(file.write(bytes) == bytes.size(), "write " + name.toStdString());
    file.close();
    return path;
}
// The "how it was read" and summary lines, as the report sheet writes them (the same rules
// as src/app/ifc_sheets.cpp).
QString unitsLine(const QJsonObject &report) {
    const QJsonObject units = report["units"].toObject();
    const QString name = units["lengthUnit"].toString();
    if (units["override"].toBool(false))
        return QStringLiteral("%1 (chosen; the file declared none)").arg(name);
    return QStringLiteral("%1 (the file declares it)").arg(name);
}
QString georeferenceLine(const QJsonObject &report) {
    const QJsonObject geo = report["georeference"].toObject();
    const QString result = geo["result"].toString();
    if (result == "siteOrigin") {
        const QString crsName = geo["crs"].toObject()["name"].toString();
        return crsName.isEmpty() ? QStringLiteral("from the IFC")
                                 : QStringLiteral("from the IFC (%1)").arg(crsName);
    }
    if (result == "gridAligned")
        return QStringLiteral("aligned to the grid");
    if (result == "notRepresentable")
        return QStringLiteral("not representable; the project axes were kept");
    return QStringLiteral("the project axes were kept");
}
// The driver: while a flow (a menu action) runs its nested loops, it closes the prompts it
// sees. It only records; the assertions run after the flow returns.
struct Driver {
    QString choose; // the file to choose in the picker; empty: reject the picker
    std::function<void(IfcImportDialog *)> onDialog; // one-shot action on the options dialog
    std::function<void(IfcProgressSheet *)> onProgress;
    std::vector<QMessageBox::StandardButton> confirmPlan; // one button per "Unsaved changes"
    int pickers = 0;
    int confirmations = 0;
    bool dialogSeen = false; // the options dialog was seen
    bool exportDialogSeen = false; // the IFC export plan dialog was seen (R097.e2)
    QString exportDialogBanner; // its last non-empty banner text
    int dialogTicks = 0; // ticks in which the options dialog was visible
    bool dialogGoEnabled = false; // seen on any tick
    bool dialogBannerVisible = false; // seen on any tick
    QString dialogBannerText; // the last non-empty banner text seen
    QString dialogHelperText; // the last helper line text seen
    QStringList sheets;
    std::map<QString, QString> summaries;
    std::map<QString, QString> hows;
    std::map<QString, QString> banner;
    std::map<QString, QString> identity;
    std::map<QString, QString> progressFile;
    int progressBar = -1;
    bool sawStep5Working = false; // the model step ran while the geometry converted
    QString gateFile; // a held helper's gate (heldImportBody): made once the model step is seen
    bool sawStep1Working = false; // the check step was the working one (a hung helper)
    bool progressCancelFound = false;
    QString progressElapsed;
    QString actionFailure; // a driver action threw; reported by stop()
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
    // Releases a held helper: the model step is working, so the helper may go on to its tasks.
    void releaseHeld() {
        if (gateFile.isEmpty() || QFileInfo::exists(gateFile))
            return;
        QFile gate(gateFile);
        if (!gate.open(QIODevice::WriteOnly) && actionFailure.isEmpty())
            actionFailure = "the held helper's gate could not be made";
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
                // The picker is driven on every tick: a selectFile+accept that lands before the
                // picker's view is ready is a no-op (accept() without a selection does nothing),
                // so the attempt is re-issued until it takes.
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
                // Record the dialog's state on every tick: the first tick may land mid first
                // layout, so visibility is "seen on any tick" and the text is the last seen.
                dialogSeen = true;
                ++dialogTicks;
                if (auto *go = dialog->findChild<QPushButton *>("ifcImportGo"))
                    dialogGoEnabled = dialogGoEnabled || go->isEnabled();
                if (auto *helper = dialog->findChild<QLabel *>("ifcImportHelperLine"))
                    dialogHelperText = helper->text();
                if (auto *b = dialog->findChild<QLabel *>("ifcImportBanner")) {
                    if (!b->text().isEmpty())
                        dialogBannerText = b->text();
                    dialogBannerVisible = dialogBannerVisible || b->isVisible();
                }
                // The action fires once the dialog has had a tick to settle, so it lands on a
                // shown dialog (the same way the picker is re-issued until it takes).
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
            if (auto *exportDialog = qobject_cast<IfcExportDialog *>(widget)) {
                // The IFC export plan dialog (R097.e2): record it and cancel it.
                exportDialogSeen = true;
                if (auto *banner = exportDialog->findChild<QLabel *>("ifcExportBanner"))
                    if (!banner->text().isEmpty())
                        exportDialogBanner = banner->text();
                if (!exportDialog->property("closed").toBool()) {
                    exportDialog->setProperty("closed", true);
                    if (auto *cancel = exportDialog->findChild<QPushButton *>("ifcExportCancel"))
                        QMetaObject::invokeMethod(cancel, [cancel] { cancel->click(); },
                                                 Qt::QueuedConnection);
                }
                continue;
            }
            if (auto *sheet = qobject_cast<IfcProgressSheet *>(widget)) {
                if (auto *file = sheet->findChild<QLabel *>("ifcProgressFile"))
                    progressFile[sheet->objectName()] = file->text();
                if (auto *bar = sheet->findChild<QProgressBar *>("ifcProgressBar"))
                    progressBar = std::max(progressBar, bar->value());
                if (auto *state = sheet->findChild<QLabel *>("ifcProgressStep5State"))
                    if (state->text() == "\u25CF") {
                        sawStep5Working = true;
                        releaseHeld();
                    }
                if (auto *state = sheet->findChild<QLabel *>("ifcProgressStep1State"))
                    if (state->text() == "\u25CF")
                        sawStep1Working = true;
                if (auto *cancel = sheet->findChild<QPushButton *>("ifcProgressCancel"))
                    progressCancelFound = true;
                if (auto *label = sheet->findChild<QLabel *>("ifcProgressElapsed"))
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
                if (auto *how = report->findChild<QPlainTextEdit *>("ifcReportHow"))
                    hows[name] = how->toPlainText();
                if (auto *label = report->findChild<QLabel *>("ifcReportBanner"))
                    banner[name] = label->text();
                if (auto *label = report->findChild<QLabel *>("ifcReportIdentity"))
                    identity[name] = label->text();
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
    const QByteArray modelBytes = modelIfc();
    const QString model = writeFixture(root.path(), "model.ifc", modelBytes);
    const QString notes = writeFixture(root.path(), "notes.txt", notIfc());
    const QString truncated = writeFixture(root.path(), "truncated.ifc", truncatedIfc());
    const QString seed =
        QString(SOURCE_DIR) + "/tests/fixtures/ifc/packages/rv-tessellated-item.skifc";
    check(QFile::exists(seed),
          (QString("the seed conversion package is present: ") + seed).toStdString());
    const QString available = writeExecutable(
        root.path(), "worker-available", standInScript(probeJson(true, {}),
                                                      importBody(seed, 1, false)));
    // The limited helper's geometry step is held until the driver has seen the model step working
    // (see heldImportBody); only the limited case's model check uses it.
    const QString gate = root.path() + "/geometry-step.gate";
    const QString limited = writeExecutable(
        root.path(), "worker-limited",
        standInScript(probeJson(false, "the OpenCASCADE kernel is not installed"),
                      heldImportBody(seed, gate)));
    const QString broken = writeExecutable(
        root.path(), "worker-broken",
        standInScript(QStringLiteral("echo 'error: cannot load the IfcOpenShell library' "
                                     "1>&2; exit 1"),
                      QString()));
    const QString refused =
        writeExecutable(root.path(), "worker-refused",
                        standInScript(probeJson(true, {}), importBody(seed, 0, true)));
    const QString hang = writeExecutable(
        root.path(), "worker-hang",
        standInScript(probeJson(true, {}), QStringLiteral("    exec /bin/sleep 300\n")));
    // The reference conversion (synchronous, stand-in): the expected document and report
    // the window flow must produce.
    IfcWorkerOptions referenceWorker;
    referenceWorker.workerPath = available;
    IfcImport reference;
    try {
        reference = importIfcFile(model, IfcImportOptions{}, referenceWorker);
    } catch (const std::exception &error) {
        std::cerr << "reference import failed: " << error.what() << '\n';
        return 2;
    }
    const QJsonObject &expected = reference.report;
    const qint64 entities = expected["accounting"].toObject()["entities"].toInt(0);
    check(entities > 0, "the seed package has entities");

    Window window;
    window.resize(1280, 860);
    window.show();
    check(QTest::qWaitForWindowExposed(&window, 5000), "The window shows");
    auto *status = window.findChild<QLabel *>("statusMessage");
    check(status, "the status label is found");
    auto *chip = window.findChild<QPushButton *>("ifcImportChip");
    check(chip, "the IFC report chip is found");
    check(!chip->isVisible(), "the chip is hidden before any import");

    try {
        // S0: the options dialog on its own.
        {
            IfcImportDialog dialog(&window);
            auto *go = dialog.findChild<QPushButton *>("ifcImportGo");
            check(go, "the Import button is found");
            auto *banner = dialog.findChild<QLabel *>("ifcImportBanner");
            auto *helper = dialog.findChild<QLabel *>("ifcImportHelperLine");
            auto *facts = dialog.findChild<QLabel *>("ifcImportFileFacts");
            check(contains(dialog.fileName(), "No file chosen"),
                  "a bare dialog names no file");
            check(facts->text().isEmpty(), "a bare dialog shows no file facts");
            check(!go->isEnabled(), "Import is disabled without a file");
            check(banner->isHidden(), "a bare dialog shows no banner");
            check(helper->text().isEmpty(), "a bare dialog shows no helper line");
            check(!dialog.refused(), "a bare dialog is not refused");
            check(dialog.options().spaces == IfcSpaces::Hidden &&
                      dialog.options().georeference == IfcGeoreference::KeepAxes,
                  "the defaults are Hidden spaces and the project axes");
            check(dialog.workerOptions().chordDeviationMm == 0.5 &&
                      dialog.workerOptions().lengthUnit == "m",
                  "the defaults are a 0.5 mm deviation and metres");
            auto *hidden = radio(&dialog, "Hidden");
            auto *shown = radio(&dialog, "Shown");
            auto *skip = radio(&dialog, "Skip");
            auto *keep = radio(&dialog, "Keep the project axes");
            auto *grid = radio(&dialog, "Align to the grid");
            check(hidden && shown && skip && keep && grid, "the choice controls are found");
            check(hidden->isChecked() && keep->isChecked(), "the defaults are checked");
            shown->setChecked(true);
            grid->setChecked(true);
            check(dialog.options().spaces == IfcSpaces::Shown &&
                      dialog.options().georeference == IfcGeoreference::Grid,
                  "the choices reach the options");
            auto *unit = dialog.findChild<QComboBox *>("ifcImportUnit");
            check(unit && unit->currentText() == "m", "the unit default is metres");
            auto *deviation = dialog.findChild<QDoubleSpinBox *>("ifcImportDeviation");
            check(deviation && deviation->value() == 0.5, "the deviation default is 0.5 mm");

            // The helper line and the limited banner.
            IfcProbe availableProbe;
            availableProbe.state = IfcState::Available;
            availableProbe.ifcOpenShell = "0.9.0";
            dialog.setProbe(availableProbe);
            check(contains(helper->text(), "IfcOpenShell 0.9.0") &&
                      contains(helper->text(), "OpenCASCADE kernel") && banner->isHidden(),
                  "the available helper is named");
            IfcProbe limitedProbe;
            limitedProbe.state = IfcState::Limited;
            limitedProbe.ifcOpenShell = "0.9.0";
            dialog.setProbe(limitedProbe);
            check(contains(helper->text(), "kernel: none"),
                  "the limited helper line names the missing kernel");
            check(contains(banner->text(), "Limited") &&
                      contains(banner->text(), "tessellated") && !banner->isHidden(),
                  "the limited banner explains what is converted");
            IfcProbe brokenProbe;
            brokenProbe.state = IfcState::Broken;
            dialog.setProbe(brokenProbe);
            check(contains(helper->text(), "kernel: none"),
                  "a broken helper does not reach the dialog (the sheet comes first)");

            // A valid file: the facts and an enabled Import.
            dialog.setFile(model);
            check(dialog.fileName() == "model.ifc", "the file is named");
            check(facts->text() == QString("IFC4 · %1").arg(humanSize(modelBytes.size())),
                  "the facts name the schema and the size: " + facts->text().toStdString());
            check(go->isEnabled() && !banner->isVisible() && !dialog.refused(),
                  "a valid file enables Import");

            // A non-IFC file: refused, named, Import off.
            dialog.setFile(notes);
            check(dialog.refused() && !go->isEnabled(), "a non-IFC file is refused");
            check(banner->text() ==
                      QString("✗ The file does not start with ISO-10303-21; "
                              "it is not an IFC STEP file."),
                  "the refusal names what is wrong: " + banner->text().toStdString());
            check(facts->text() == humanSize(QFile(notes).size()),
                  "a refusal shows the size only");

            // A truncated file: refused, named.
            dialog.setFile(truncated);
            check(dialog.refused() &&
                      banner->text() ==
                          QString("✗ The file does not end with END-ISO-10303-21; "
                                  "(it may be truncated)."),
                  "a truncated file is refused and named");

            // R105.a (ADR 0179 section 7.3): a downloaded file of a refused type is named in the
            // banner, with Import off. Each fixture is self-made
            // (tests/fixtures/import-refusal/SOURCE.txt).
            const QString refusalDir = QStringLiteral(SOURCE_DIR "/tests/fixtures/import-refusal/");
            dialog.setFile(refusalDir + "wrapped-box.zip");
            check(dialog.refused() && !go->isEnabled() && !banner->isHidden() &&
                      banner->text() ==
                          QString("✗ This is a ZIP archive. Extract it, then import the model file "
                                  "inside it."),
                  "a ZIP is refused in the dialog with its named reason: " +
                      banner->text().toStdString());
            dialog.setFile(refusalDir + "minimal.dae");
            check(dialog.refused() && !go->isEnabled() &&
                      contains(banner->text(), "COLLADA (.dae)") &&
                      contains(banner->text(), ".skp"),
                  "a COLLADA file is refused in the dialog, with the .skp advice: " +
                      banner->text().toStdString());
            dialog.setFile(refusalDir + "stub.usdz");
            check(dialog.refused() && !go->isEnabled() && contains(banner->text(), "USDZ (.usdz)"),
                  "a USDZ file is refused in the dialog: " + banner->text().toStdString());
            dialog.setFile(refusalDir + "stub.fbx");
            check(dialog.refused() && !go->isEnabled() && contains(banner->text(), "FBX (.fbx)") &&
                      facts->text() == humanSize(QFile(refusalDir + "stub.fbx").size()),
                  "an FBX file is refused in the dialog, and its facts show the size only");
            // Enter imports, Escape cancels.
            dialog.setFile(model);
            dialog.show();
            check(QTest::qWaitForWindowExposed(&dialog, 3000), "the dialog shows");
            QTest::keyClick(&dialog, Qt::Key_Return);
            check(QTest::qWaitFor([&] { return dialog.result() == QDialog::Accepted; }, 3000),
                  "Enter accepts the dialog");
            IfcImportDialog second(&window);
            second.setFile(model);
            second.show();
            check(QTest::qWaitForWindowExposed(&second, 3000), "the dialog shows");
            QTest::keyClick(&second, Qt::Key_Escape);
            check(QTest::qWaitFor([&] { return second.result() == QDialog::Rejected; }, 3000),
                  "Escape cancels the dialog");
        }
        // The Choose… button opens the picker and fills the facts.
        {
            IfcImportDialog dialog(&window);
            dialog.show();
            Driver driver;
            driver.choose = model;
            driver.onDialog = [] (IfcImportDialog *) {};
            driver.start();
            QTest::mouseClick(dialog.findChild<QPushButton *>("ifcImportChoose"),
                             Qt::LeftButton);
            driver.stop();
            check(driver.pickers == 1, "the picker opened once");
            check(dialog.fileName() == "model.ifc" &&
                      dialog.findChild<QLabel *>("ifcImportFileFacts")->text() ==
                          QString("IFC4 · %1").arg(humanSize(modelBytes.size())),
                  "the picker fills the file and its facts");
            check(!dialog.refused(), "the chosen file is accepted");
        }

        // S1: a missing helper is "not installed"; export opens the plan dialog (R097.e2).
        {
            window.setIfcWorkerPath("");
            check(window.ifcProbe().state == IfcState::Missing, "no helper: missing");
            Driver driver;
            driver.start();
            window.findChild<QAction *>("file.importIfc")->trigger();
            driver.stop();
            check(driver.sheets == (QStringList{"ifcUnavailable"}),
                  "a missing helper shows the unavailable sheet");
            check(contains(driver.summaries["ifcUnavailable"], "not installed"),
                  "the sheet says the package is not installed");
            check(!contains(driver.summaries["ifcUnavailable"], "cannot start"),
                  "missing and broken are different sheets");
            check(!chip->isVisible(), "the chip is hidden without a helper");
            auto &doc = window.document();
            check(doc.history().total == 0 && !doc.dirty(), "nothing was changed");
            // Export without a helper: the unavailable sheet, in its export variant.
            Driver exportDriver;
            exportDriver.start();
            window.findChild<QAction *>("file.exportIfc")->trigger();
            exportDriver.stop();
            check(exportDriver.sheets == (QStringList{"ifcUnavailable"}),
                  "export without a helper shows the unavailable sheet");
            check(contains(exportDriver.summaries["ifcUnavailable"], "IFC export") &&
                      contains(exportDriver.summaries["ifcUnavailable"], "not installed"),
                  "the sheet names the export and what is missing");
            // Export with a helper present: the plan dialog (R097.e2); an empty model is a
            // cheap refusal in the dialog, with Export disabled before a file is chosen.
            window.setIfcWorkerPath(available);
            Driver pendingDriver;
            pendingDriver.start();
            window.findChild<QAction *>("file.exportIfc")->trigger();
            pendingDriver.stop();
            check(pendingDriver.exportDialogSeen, "export with a helper opens the plan dialog");
            check(contains(pendingDriver.exportDialogBanner, "Nothing to export"),
                  "an empty model refuses in the dialog, before a file is chosen");
        }

        // S2: a broken helper is "cannot start", with the helper's last log line.
        {
            window.setIfcWorkerPath(broken);
            Driver driver;
            driver.start();
            window.findChild<QAction *>("file.importIfc")->trigger();
            driver.stop();
            check(driver.sheets == (QStringList{"ifcBroken"}),
                  "a broken helper shows the broken sheet, not the unavailable one");
            check(contains(driver.summaries["ifcBroken"], "cannot start"),
                  "the sheet says the helper cannot start");
            check(contains(driver.summaries["ifcBroken"],
                           "error: cannot load the IfcOpenShell library"),
                  "the helper's last log line is shown");
            check(window.ifcProbe().state == IfcState::Broken &&
                      !chip->isVisible(),
                  "the broken state is recorded and the chip stays hidden");
            auto &doc = window.document();
            check(doc.history().total == 0, "nothing was changed");
        }

        // S3: a limited helper, the full mouse flow. The helper holds its geometry step until the
        // driver has seen the model step working, so that step is always sampled.
        {
            window.setIfcWorkerPath(limited);
            const auto history = window.document().history().total;
            Driver driver;
            driver.choose = model;
            driver.gateFile = gate;
            driver.onDialog = [] (IfcImportDialog *dialog) {
                // The mouse flow: click Import. The dialog's state is asserted below, from
                // the driver's records of the dialog while it was open.
                if (auto *go = dialog->findChild<QPushButton *>("ifcImportGo"))
                    QTest::mouseClick(go, Qt::LeftButton);
            };
            driver.start();
            window.findChild<QAction *>("file.importIfc")->trigger();
            driver.stop();
            check(driver.confirmations == 0, "an unsaved model prompt appears for a dirty "
                                               "model only");
            check(driver.dialogSeen, "the options dialog opened");
            check(contains(driver.dialogBannerText, "Limited") && driver.dialogBannerVisible,
                  "the limited banner is shown in the dialog");
            check(driver.progressBar == 100, "the progress bar ran to completion");
            check(driver.sawStep5Working, "the model step ran while the geometry converted");
            check(driver.progressFile["ifcProgressSheet"] == "model.ifc",
                  "the progress sheet names the file");
            check(contains(driver.progressElapsed, "s"), "the elapsed time ticks");
            auto &doc = window.document();
            check(doc.history().total == 1 && doc.history().entries.back().label == "Import IFC",
                  "the import is one edit, labelled");
            check(!doc.bodies().empty(), "the model has bodies");
            check(contains(status->text(), "Imported an unsaved IFC model") &&
                      contains(status->text(), "Source file unchanged"),
                  "the status names the outcome: " + status->text().toStdString());
            // The report chip (the design's mockup `after`): it appears on the status row for
            // the imported model; its attention count is what the helper did not convert.
            const int attention = expected["accounting"].toObject()["omitted"].toInt(0);
            if (attention > 0)
                check(chip->isVisible() && contains(chip->text(), "IFC import") &&
                          contains(chip->text(), QString::number(attention)) &&
                          contains(chip->text(), "Show report"),
                      "the chip names the import and its attention count: " +
                          chip->text().toStdString());
            else
                check(chip->isVisible() && contains(chip->text(), "IFC import report"),
                      "the chip offers the report: " + chip->text().toStdString());
            // The report.
            check(driver.sheets.contains("ifcImportReport"), "the report sheet was shown");
            check(contains(driver.banner["ifcImportReport"], "✓ Import finished") &&
                      contains(driver.banner["ifcImportReport"], "0 restarts"),
                  "the banner names the outcome: " +
                          driver.banner["ifcImportReport"].toStdString());
            check(driver.summaries["ifcImportReport"] ==
                          QString("model.ifc\n%1 · %2\nEntities read: %3")
                              .arg(expected["source"].toObject()["schema"].toString())
                              .arg(humanSize(modelBytes.size()))
                              .arg(entities),
                  "the summary names the file, the schema and the entities");
            const auto accounting = expected["accounting"].toObject();
            check(driver.identity["ifcImportReport"] ==
                          QString("entities read %1 = converted to geometry %2 + structure "
                                  "and metadata %3 + omitted %4")
                              .arg(accounting["entities"].toInt(0))
                              .arg(accounting["converted"].toInt(0))
                              .arg(accounting["supporting"].toInt(0))
                              .arg(accounting["omitted"].toInt(0)),
                  "the accounting identity holds");
            check(driver.hows["ifcImportReport"] ==
                          QString("Schema: %1\nUnits: %2\nGeoreference: %3\nMaximum "
                                  "deviation: %4 mm")
                              .arg(expected["source"].toObject()["schema"].toString())
                              .arg(unitsLine(expected))
                              .arg(georeferenceLine(expected))
                              .arg(QString::number(
                                  expected["precision"].toObject()["chordDeviationMm"]
                                       .toDouble(0.5),
                                  'f', 2)),
                  "the how-it-was-read block is filled: " +
                          driver.hows["ifcImportReport"].toStdString());
            // The report can be shown again from the menu.
            Driver again;
            again.start();
            window.findChild<QAction *>("file.ifcImportReport")->trigger();
            again.stop();
            check(again.sheets == (QStringList{"ifcImportReport"}),
                  "the menu re-shows the report");
            // Undo takes the import back: the import replaced the document, so its counters stay;
            // the content is the original (the terrain suite asserts the same shape).
            doc.undo();
            check(!doc.canUndo() && doc.bodies().empty(), "undo undoes the import");
            check(doc.history().position == history, "undo takes the import back");
        }

        // S4: an available helper, the keyboard flow.
        // The probe is on-demand: it runs when the import is opened (synchronously, with its
        // own deadline), not at window start.
        {
            window.setIfcWorkerPath(available);
            const auto encoded = encodeContainer(window.document());
            Driver driver;
            driver.choose = model;
            driver.onDialog = [] (IfcImportDialog *dialog) {
                // The keyboard flow: Enter imports.
                QTest::keyClick(dialog, Qt::Key_Return);
            };
            driver.start();
            window.findChild<QAction *>("file.importIfc")->trigger();
            driver.stop();
            check(window.ifcProbe().state == IfcState::Available,
                  "the on-demand probe ran when the import opened");
            check(driver.confirmations == 0,
                  "the document is not dirty (S3's import was undone), so no prompt");
            check(contains(driver.dialogHelperText, "OpenCASCADE kernel"),
                  "the available helper is named");
            auto &doc = window.document();
            check(sameContent(encodeDocument(doc), encodeDocument(reference.document)),
                  "the window's import matches the reference conversion");
            check(doc.history().total == 1 && doc.history().entries.back().label == "Import IFC",
                  "the import is one labelled edit");
            // The report chip for the imported model (the design's mockup `after`): its
            // attention count is what the helper did not convert.
            const int attention = expected["accounting"].toObject()["omitted"].toInt(0);
            if (attention > 0)
                check(chip->isVisible() && contains(chip->text(), QString::number(attention)),
                      "the chip names the import and its attention count: " +
                          chip->text().toStdString());
            else
                check(chip->isVisible() && contains(chip->text(), "IFC import report"),
                      "the chip offers the report: " + chip->text().toStdString());
            // The source file is never modified.
            QFile source(model);
            check(source.open(QIODevice::ReadOnly), "the source opens");
            check(source.readAll() == modelBytes, "the source file is byte for byte unchanged");
        }

        // S5: a non-IFC file is refused in the dialog; nothing runs, nothing is shown.
        {
            window.setIfcWorkerPath(available);
            const auto encoded = encodeContainer(window.document());
            const auto history = window.document().history().total;
            Driver driver;
            driver.choose = notes;
            driver.onDialog = [] (IfcImportDialog *dialog) {
                QTest::keyClick(dialog, Qt::Key_Return); // a disabled default button: nothing
                QMetaObject::invokeMethod(
                    dialog, [dialog] {
                        if (dialog->isVisible())
                            QTest::keyClick(dialog, Qt::Key_Escape);
                    },
                    Qt::QueuedConnection);
            };
            driver.start();
            window.findChild<QAction *>("file.importIfc")->trigger();
            driver.stop();
            check(driver.confirmations == 0, "a refused dialog never reaches the prompt");
            check(contains(driver.dialogBannerText, "not an IFC STEP file"),
                  "the refusal banner is shown");
            check(!driver.dialogGoEnabled, "Import stays disabled");
            check(driver.sheets.isEmpty(), "no sheet is shown for a refused dialog");
            check(encodeContainer(window.document()) == encoded &&
                      window.document().history().total == history,
                  "the model was not changed");
        }

        // S6: a refused import (the helper's words) shows the refusal sheet.
        {
            window.setIfcWorkerPath(refused);
            const auto encoded = encodeContainer(window.document());
            const auto history = window.document().history().total;
            Driver driver;
            driver.choose = model;
            driver.confirmPlan = {QMessageBox::Discard};
            driver.onDialog = [] (IfcImportDialog *dialog) {
                if (auto *go = dialog->findChild<QPushButton *>("ifcImportGo"))
                    QTest::mouseClick(go, Qt::LeftButton);
            };
            driver.start();
            window.findChild<QAction *>("file.importIfc")->trigger();
            driver.stop();
            check(driver.confirmations == 1, "the confirm phase asked once");
            check(!driver.dialogBannerVisible && driver.dialogGoEnabled,
                  "a valid file shows no banner");
            check(driver.sheets == (QStringList{"ifcRefusal"}),
                  "a refused import shows the refusal sheet");
            check(contains(driver.summaries["ifcRefusal"], "The model was not changed") &&
                      contains(driver.summaries["ifcRefusal"], "tooManyEntities") &&
                      contains(driver.summaries["ifcRefusal"],
                                 "The file holds more entities than the bound allows."),
                  "the refusal sheet names the code and the helper's words");
            check(encodeContainer(window.document()) == encoded &&
                      window.document().history().total == history,
                  "the refusal leaves the model unchanged");
        }

        // S7: a hung helper: Cancel leaves the model unchanged.
        {
            window.setIfcWorkerPath(hang);
            const auto encoded = encodeContainer(window.document());
            const auto history = window.document().history().total;
            Driver driver;
            driver.choose = model;
            driver.confirmPlan = {QMessageBox::Discard};
            driver.onDialog = [] (IfcImportDialog *dialog) {
                // Advance the options dialog so the import starts; the sheet is cancelled
                // from onProgress.
                if (auto *go = dialog->findChild<QPushButton *>("ifcImportGo"))
                    QTest::mouseClick(go, Qt::LeftButton);
            };
            driver.onProgress = [] (IfcProgressSheet *sheet) {
                if (auto *cancel = sheet->findChild<QPushButton *>("ifcProgressCancel"))
                    QMetaObject::invokeMethod(cancel, [cancel] { cancel->click(); },
                                             Qt::QueuedConnection);
            };
            driver.start();
            window.findChild<QAction *>("file.importIfc")->trigger();
            driver.stop();
            check(driver.progressCancelFound, "the progress sheet has a Cancel");
            check(driver.sawStep1Working, "while hung, the check step is the working one "
                                             "and no step advanced");
            check(contains(status->text(), "cancelled"),
                  "the status says the import was cancelled: " +
                          status->text().toStdString());
            check(encodeContainer(window.document()) == encoded &&
                      window.document().history().total == history,
                  "cancelling leaves the model unchanged");
            check(chip->isVisible(),
                  "cancelling keeps the previous import's chip (the model is unchanged): " +
                          chip->text().toStdString());
        }

        // S7b: Escape on the progress sheet is Cancel: the sheet stays until the helper has ended,
        // so the window never reads a running import (no refusal sheet, no blocked UI).
        {
            window.setIfcWorkerPath(hang);
            const auto encoded = encodeContainer(window.document());
            const auto history = window.document().history().total;
            Driver driver;
            driver.choose = model;
            driver.confirmPlan = {QMessageBox::Discard};
            driver.onDialog = [] (IfcImportDialog *dialog) {
                if (auto *go = dialog->findChild<QPushButton *>("ifcImportGo"))
                    QTest::mouseClick(go, Qt::LeftButton);
            };
            driver.onProgress = [] (IfcProgressSheet *sheet) {
                QMetaObject::invokeMethod(sheet, [sheet] { QTest::keyClick(sheet, Qt::Key_Escape); },
                                         Qt::QueuedConnection);
            };
            driver.start();
            window.findChild<QAction *>("file.importIfc")->trigger();
            driver.stop();
            check(!driver.sheets.contains("ifcRefusal"),
                  "Escape during the import shows no refusal sheet");
            check(contains(status->text(), "cancelled"),
                  "Escape cancels the import: " + status->text().toStdString());
            check(encodeContainer(window.document()) == encoded &&
                      window.document().history().total == history,
                  "Escape leaves the model unchanged");
        }

        // S8: a dirty model: Discard before the helper, Cancel at the commit phase.
        {
            window.setIfcWorkerPath(available);
            window.demo();
            check(window.document().dirty(), "the demo model is dirty");
            const auto encoded = encodeContainer(window.document());
            const auto history = window.document().history().total;
            Driver driver;
            driver.choose = model;
            driver.confirmPlan = {QMessageBox::Discard, QMessageBox::Cancel};
            driver.onDialog = [] (IfcImportDialog *dialog) {
                QTest::mouseClick(dialog->findChild<QPushButton *>("ifcImportGo"),
                                 Qt::LeftButton);
            };
            driver.start();
            window.findChild<QAction *>("file.importIfc")->trigger();
            driver.stop();
            check(driver.confirmations == 2,
                  "the prompt appeared before the helper and again at the commit");
            check(contains(status->text(), "the change was cancelled"),
                  "the status says the finished import was cancelled: " +
                          status->text().toStdString());
            check(encodeContainer(window.document()) == encoded &&
                      window.document().history().total == history,
                  "the model was not replaced");
            check(!chip->isVisible(),
                  "the chip is with the replaced model; the demo model has no import");
        }

        // S9: a dirty model: Discard both prompts; the import replaces the model.
        {
            window.demo();
            Driver driver;
            driver.choose = model;
            driver.confirmPlan = {QMessageBox::Discard, QMessageBox::Discard};
            driver.onDialog = [] (IfcImportDialog *dialog) {
                QTest::keyClick(dialog, Qt::Key_Return);
            };
            driver.start();
            window.findChild<QAction *>("file.importIfc")->trigger();
            driver.stop();
            check(driver.confirmations == 2, "both prompts were answered");
            auto &doc = window.document();
            check(sameContent(encodeDocument(doc), encodeDocument(reference.document)),
                  "the import replaced the model with the converted one");
            check(doc.history().total == 1 && doc.history().entries.back().label == "Import IFC",
                  "the replacement is one labelled edit");
            check(contains(status->text(), "Imported an unsaved IFC model"),
                  "the status names the outcome");
            // The report chip for the replaced model (the design's mockup `after`).
            const int attention = expected["accounting"].toObject()["omitted"].toInt(0);
            if (attention > 0)
                check(chip->isVisible() && contains(chip->text(), QString::number(attention)),
                      "the chip names the import and its attention count: " +
                          chip->text().toStdString());
            else
                check(chip->isVisible() && contains(chip->text(), "IFC import report"),
                      "the chip offers the report: " + chip->text().toStdString());
            check(window.findChild<QAction *>("file.ifcImportReport")->isEnabled(),
                  "the import report action is enabled");
        }
    } catch (const std::exception &error) {
        std::cerr << "FAIL: " << error.what() << '\n';
        return 1;
    }
    std::cout << "ifc import screens: dialog states, picker, Enter and Escape, missing, "
                 "broken, limited, the full flow, a refused and a hung import, and the "
                 "dirty-model prompts\n";
    return 0;
}
