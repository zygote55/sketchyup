// R098.c2 (ADR 0172 section 8, ADR 0171 section 16.8): the STEP export screens, offscreen, driven by
// a stand-in sketchyup-ifc-worker.
//
// The application's own side (the screens) is what this suite checks: the plan dialog with its
// schema choice, written and not-written lines and the size warning; its keyboard (Enter exports,
// Escape cancels, the Tab order); the system Save dialog; the existing-destination and empty-model
// refusals; the progress sheet with its four steps and a Cancel and an Escape that write nothing and
// leave no private directory behind; the refusal for a helper that stops; and the missing helper's
// availability sheet. The heavy work is never done here: a stand-in helper (a shell script) answers
// --probe and --export-step, and records the arguments it was given.
//
// The driver: while a flow (the menu action) runs its nested loops, it walks the plan dialog (picks
// AP203 when asked, then accepts or cancels), answers the Save dialog, optionally cancels the progress
// sheet (with its button or with Escape) and closes the report or refusal it sees. It records; the
// assertions run after the flow returns.

#include "app/report_sheet.hpp"
#include "app/step_export_dialog.hpp"
#include "app/step_export_progress_sheet.hpp"
#include "app/window.hpp"
#include "core/annotation_anchors.hpp"
#include "core/annotations.hpp"
#include "core/assets.hpp"
#include "core/body.hpp"
#include "core/drawing_sheets.hpp"
#include "core/reference_images.hpp"
#include "core/scenes.hpp"
#include "core/sections.hpp"
#include "geometry/section.hpp"
#include "io/ifc_worker.hpp"
#include "io/step_export.hpp"
#include "io/texture_image.hpp"
#include <QAction>
#include <QAbstractButton>
#include <QApplication>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QList>
#include <QMap>
#include <QPushButton>
#include <QProgressBar>
#include <QRadioButton>
#include <QScrollArea>
#include <QScrollBar>
#include <QTest>
#include <QTemporaryDir>
#include <QTimer>
#include <algorithm>
#include <cstdint>
#include <functional>
#include <iostream>
#include <map>
#include <memory>
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

// Write a helper file and make it executable. The application execs it directly (a shebang); the
// helper runs in a scrubbed environment (only LC_ALL, no PATH), so the body uses only shell builtins
// and absolute paths.
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
QStringList readLines(const QString &path) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return {};
    QStringList lines;
    for (const auto &line : QString::fromUtf8(file.readAll()).split('\n', Qt::SkipEmptyParts))
        lines << line;
    return lines;
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
// A model with a group of two boxes, an empty group, a body with wires only (no faces, not written),
// a reference image (a kind the export counts as not written), and one annotation, scene, section and
// sheet (kinds the export does not write).
Document makeModel() {
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
    // A reference image, a kind the export counts as not written (the plan's notWritten).
    {
        const auto png = encodeTexturePng(TextureImage(2, 1, {255, 0, 0, 255, 0, 255, 0, 128}));
        const auto asset = createAsset(doc, "Pixels", "image/png",
                                       std::make_shared<AssetPayload>(std::vector<std::uint8_t>(png.begin(), png.end())));
        createReferenceImage(doc, ReferenceImage{asset, 4, 2, .6}, Transform::translation({30, 30, 0}));
    }
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
    return doc;
}

// ---- The stand-in helper -----------------------------------------------------------------------------------------
// The result line the stand-in prints: the counts the application checks against the plan (the
// assemblies are the plan's groups, the instances its placed occurrences), in the helper's own shape.
QString resultLine(const QJsonObject &plan) {
    const QJsonObject result{
        {"status", "ok"},
        {"occt", "stand-in"},
        {"assemblies", plan["groups"].toInt(0)},
        {"instances", plan["instances"].toInt(0)},
        {"parts", plan["meshes"].toInt(0)},
        {"faces", plan["faces"].toInt(0)},
        {"solids", plan["elements"].toInt(0)},
        {"shells", 0},
        {"fallbacks", 0},
        {"hidden", plan["hidden"].toInt(0)},
        {"layers", 0},
        {"colouredParts", 0},
    };
    return QStringLiteral("R ") + QString::fromUtf8(QJsonDocument(result).toJson(QJsonDocument::Compact));
}
// The stand-in helper: --probe answers as the application's probe expects; --export-step records its
// arguments (one per line) in `argvPath`, writes a small STEP file to --output (unless `fail`, which
// stops it with exit 1 and no file) after `sleepSeconds`, and prints the result line.
QString stepWorkerScript(const QString &argvPath, const QString &rLine, int sleepSeconds, bool fail) {
    const QString sleep = sleepSeconds > 0 ? QString("    /bin/sleep %1\n").arg(sleepSeconds) : QString{};
    const QString exportBody =
        fail ? QStringLiteral("    echo 'R {\"status\":\"refused\",\"code\":\"standIn\",\"message\":\"the stand-in stopped\"}'\n"
                              "    exit 1\n")
             : sleep +
                   QStringLiteral("    printf '%s\\n' 'ISO-10303-21;' 'HEADER;' 'FILE_DESCRIPTION(,);' "
                                  "'FILE_NAME(stand.step,,(),,stand-in,stand-in,);' 'FILE_SCHEMA((AP214));' "
                                  "'DATA;' 'ENDSEC;' 'END-ISO-10303-21;' > \"$out\"\n"
                                  "    echo 'P phase build'\n") +
                   "    echo '" + rLine + "'\n"
                   "    exit 0\n";
    const QString probe = QStringLiteral(
        R"({"worker":{"version":"stand-in"},"ifcopenshell":{"version":"0.9.0"},"kernelAvailable":true,"kernels":[]})");
    return QString(R"PROG(#!/bin/sh
# A stand-in for sketchyup-ifc-worker (R098.c2 STEP export dialog tests).
mode="$1"
case "$mode" in
  --probe)
    echo ')PROG") + probe +
           QString(R"PROG('
    exit 0
    ;;
  --export-step)
    printf '%s\n' "$@" > ')PROG") +
           argvPath +
           R"PROG('
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
  *)
    echo 'usage: sketchyup-ifc-worker --probe | --export-step' >&2
    exit 2
    ;;
esac
)PROG";
}

// ---- The driver --------------------------------------------------------------------------------------------------
struct Driver {
    int exportDialogMode = 0; // 0: leave it, 1: Export to file..., 2: Cancel
    QString schema; // "ap203": pick AP203 in the plan dialog before accepting it
    QString savePath; // the Save dialog's target; empty: reject it
    int progressAction = 0; // 0: none, 1: the progress sheet's Cancel, 2: Escape on the progress sheet
    int saveCount = 0;
    bool progressSeen = false;
    int progressMaxBar = -1;
    QString progressFile;
    bool progressAllDone = false;
    bool dialogSeen = false;
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
            if (auto *dialog = qobject_cast<StepExportDialog *>(widget)) {
                dialogSeen = true;
                if (!dialog->property("driven").toBool() && schema == "ap203") {
                    if (auto *radio = dialog->findChild<QRadioButton *>("stepExportSchemaAP203"))
                        radio->click();
                }
                if (exportDialogMode != 0 && !dialog->property("closed").toBool()) {
                    dialog->setProperty("closed", true);
                    auto *button = exportDialogMode == 1 ? dialog->findChild<QPushButton *>("stepExportGo")
                                                        : dialog->findChild<QPushButton *>("stepExportCancel");
                    QMetaObject::invokeMethod(button, [button] { button->click(); }, Qt::QueuedConnection);
                }
                dialog->setProperty("driven", true);
                continue;
            }
            if (auto *save = qobject_cast<QFileDialog *>(widget)) {
                if (save->objectName() != "stepExportFileDialog")
                    continue;
                if (!save->property("driven").toBool()) {
                    save->setProperty("driven", true);
                    ++saveCount;
                }
                // A save to a file that does not exist yet: set the name field and accept (the pattern
                // from tests/ifc_export_dialog_tests.cpp). Re-issued every tick until it takes.
                if (savePath.isEmpty()) {
                    QMetaObject::invokeMethod(save, [save] { save->reject(); }, Qt::QueuedConnection);
                    continue;
                }
                if (auto *name = save->findChild<QLineEdit *>("fileNameEdit"))
                    name->setText(savePath);
                QMetaObject::invokeMethod(save, "accept");
                continue;
            }
            if (auto *sheet = qobject_cast<StepExportProgressSheet *>(widget)) {
                progressSeen = true;
                if (auto *file = sheet->findChild<QLabel *>("stepExportProgressFile"))
                    progressFile = file->text();
                if (auto *bar = sheet->findChild<QProgressBar *>("stepExportProgressBar"))
                    progressMaxBar = std::max(progressMaxBar, bar->value());
                auto *cancel = sheet->findChild<QPushButton *>("stepExportProgressCancel");
                if (progressAction == 1 && cancel && !sheet->property("cancelled").toBool()) {
                    sheet->setProperty("cancelled", true);
                    QMetaObject::invokeMethod(cancel, [cancel] { cancel->click(); }, Qt::QueuedConnection);
                }
                if (progressAction == 2 && !sheet->property("cancelled").toBool()) {
                    sheet->setProperty("cancelled", true);
                    QMetaObject::invokeMethod(sheet, [sheet] { QTest::keyClick(sheet, Qt::Key_Escape); },
                                              Qt::QueuedConnection);
                }
                // Finished state: every step is a check mark.
                int done = 0;
                for (int i = 1; i <= 4; ++i)
                    if (auto *state = sheet->findChild<QLabel *>(QString("stepExportProgressStep%1State").arg(i));
                        state && state->text() == "✓")
                        ++done;
                progressAllDone = progressAllDone || done == 4;
                continue;
            }
            if (auto *report = dynamic_cast<ReportSheet *>(widget)) {
                const auto name = report->objectName();
                if (!sheets.contains(name))
                    sheets.append(name);
                summaries[name] = report->summary->text();
                if (auto *label = report->findChild<QLabel *>("stepExportReportBanner"))
                    banners[name] = label->text();
                if (auto *label = report->findChild<QLabel *>("stepExportRefusalFirst"))
                    banners[name] = label->text();
                if (auto *label = report->findChild<QLabel *>("stepExportReportIdentity"))
                    identities[name] = label->text();
                if (!report->property("closed").toBool()) {
                    report->setProperty("closed", true);
                    // Close the sheet with its own close button (OK, or Close); Copy details does not close it.
                    for (auto *button : report->findChildren<QAbstractButton *>())
                        if (button->isVisible() && (button->objectName().endsWith("OK") ||
                                                   button->objectName().endsWith("Close"))) {
                            QMetaObject::invokeMethod(button, [button] { button->click(); }, Qt::QueuedConnection);
                            break;
                        }
                }
                continue;
            }
        }
    }
};

// The private working directories a cancelled export must leave behind: none.
QStringList privateDirectories(const QString &tempPath) {
    return QDir(tempPath).entryList({QStringLiteral("sketchyup-step-*")}, QDir::Dirs | QDir::NoDotAndDotDot);
}
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
    // The export's private working directory is made under the system temporary directory; point it at
    // a folder of this test, so a directory a cancelled export leaves behind is seen.
    const QString tempPath = root.path() + "/tmp";
    check(QDir(root.path()).mkpath("tmp"), "make the temporary folder");
    qputenv("TMPDIR", tempPath.toUtf8());
    check(QDir(QDir::tempPath()).absolutePath() == QDir(tempPath).absolutePath(),
          "the system temporary directory is the test's folder");

    // The plan the stand-in answers with, computed from the same model the flows use.
    const Document model = makeModel();
    const QJsonObject plan = captureStepExport(model, QStringLiteral("model.step")).plan;
    check(plan["groups"].toInt(0) == 1 && plan["elements"].toInt(0) == 2, "the model has one group and two bodies");
    const QString argvPath = root.path() + "/argv.txt";
    const QString okWorker =
        writeExecutable(root.path(), "worker-ok", stepWorkerScript(argvPath, resultLine(plan), 0, false));
    const QString slowWorker =
        writeExecutable(root.path(), "worker-slow", stepWorkerScript(argvPath, resultLine(plan), 5, false));
    const QString failWorker =
        writeExecutable(root.path(), "worker-fail", stepWorkerScript(argvPath, resultLine(plan), 0, true));

    Window window;
    window.resize(1280, 860);
    window.show();
    check(QTest::qWaitForWindowExposed(&window, 5000), "The window shows");
    auto *status = window.findChild<QLabel *>("statusMessage");
    check(status, "the status label is found");
    window.document() = model;

    try {
        // S1: the plan dialog on its own: the numbers, the not-written lines, the schema, the helper line
        // and the default button.
        {
            StepExportDialog dialog(&window);
            auto *go = dialog.findChild<QPushButton *>("stepExportGo");
            auto *banner = dialog.findChild<QLabel *>("stepExportBanner");
            auto *helper = dialog.findChild<QLabel *>("stepExportHelperLine");
            auto *what = dialog.findChild<QLabel *>("stepExportWhatThisIs");
            auto *ap214 = dialog.findChild<QRadioButton *>("stepExportSchemaAP214");
            auto *ap203 = dialog.findChild<QRadioButton *>("stepExportSchemaAP203");
            check(go && banner && helper && what && ap214 && ap203, "the dialog controls are found");
            check(banner->isHidden(), "a healthy model shows no banner");
            check(helper->text().isEmpty(), "no helper line before a probe");
            check(ap214->isChecked() && !ap203->isChecked(), "AP214 is the default schema");
            check(dialog.options().schema == "ap214", "the default options are AP214");
            ap203->click();
            check(dialog.options().schema == "ap203", "the AP203 choice is the options' schema");
            ap214->click();
            dialog.setDocument(model);
            check(!dialog.refused(), "a model with bodies is not refused");
            check(dialog.options().schema == "ap214", "the schema choice survives the plan");
            // The written lines, as the plan counts them.
            const auto written = [&](const char *key) {
                auto *label = dialog.findChild<QLabel *>(QString("stepExport%1Text").arg(key));
                check(label, QString("the written line for %1 is found").arg(key).toStdString());
                return label;
            };
            check(written("Structure")->text().startsWith(QString("%1 groups, as assemblies").arg(plan["groups"].toInt())),
                  "the structure line names the groups: " + written("Structure")->text().toStdString());
            check(written("Bodies")->text().startsWith(QString("%1 bodies.").arg(plan["elements"].toInt())),
                  "the bodies line names the bodies: " + written("Bodies")->text().toStdString());
            check(written("Components")->text().startsWith(QString("%1 parts for %2 bodies.")
                                                              .arg(plan["meshes"].toInt())
                                                              .arg(plan["elements"].toInt())),
                  "the components line names the parts: " + written("Components")->text().toStdString());
            check(contains(written("Geometry")->text(), QString("%1 triangles").arg(plan["triangles"].toInt())),
                  "the geometry line names the triangles: " + written("Geometry")->text().toStdString());
            check(written("Hidden")->text() == "None hidden", "a model with no hidden body says so");
            // The not-written lines: the counted kinds show their count, the zero ones are hidden, and the
            // kinds the plan does not count are always listed.
            const auto nameOf = [&](const char *key) {
                auto *label = dialog.findChild<QLabel *>(QString("stepExportNotWritten_") + key);
                check(label, QString("the not-written line for %1 is found").arg(key).toStdString());
                return label;
            };
            const auto countOf = [&](const char *key) {
                auto *label = dialog.findChild<QLabel *>(QString("stepExportCount_") + key);
                check(label, QString("the not-written count for %1 is found").arg(key).toStdString());
                return label;
            };
            check(plan["notWritten"].toInt(0) == 1, "the reference image is one not-written item");
            check(!nameOf("terrain")->isHidden() && countOf("terrain")->text() == "1",
                  "the terrain and reference image line is shown with its count");
            check(countOf("untessellated")->isHidden() && nameOf("untessellated")->isHidden(),
                  "a zero counted kind hides its line");
            check(!nameOf("materials")->isHidden() && !nameOf("textures")->isHidden() &&
                      !nameOf("annotations")->isHidden() && !nameOf("sections")->isHidden() &&
                      !nameOf("sheets")->isHidden() && !nameOf("curves")->isHidden() && !nameOf("site")->isHidden(),
                  "the kinds the plan does not count are always listed");
            check(!nameOf("layers")->isHidden(), "a hidden layer's own switch is listed as not written");
            check(contains(what->text(), "STEP from SketchyUp is a mesh") &&
                      contains(what->text(), "parametric and NURBS surfaces are not created"),
                  "the what-this-is line says it is a mesh");
            check(dialog.findChild<QLabel *>("stepExportSizeWarning")->isHidden(),
                  "a small model shows no size warning");
            // The helper line: available, and a limited probe still exports.
            IfcProbe availableProbe;
            availableProbe.state = IfcState::Available;
            availableProbe.workerPath = okWorker;
            dialog.setProbe(availableProbe);
            check(contains(helper->text(), "sketchyup-ifc-worker"),
                  "the available helper is named: " + helper->text().toStdString());
            IfcProbe limitedProbe;
            limitedProbe.state = IfcState::Limited;
            dialog.setProbe(limitedProbe);
            check(contains(helper->text(), "does not use the kernel"),
                  "a limited helper still exports, and the line says the kernel is not used");
            // The keyboard: Export is the default button; Enter exports; Escape cancels.
            dialog.setProbe(availableProbe);
            dialog.show();
            check(QTest::qWaitForWindowExposed(&dialog, 3000), "the dialog shows");
            check(go->isDefault(), "Export to file... is the default button");
            QTest::keyClick(&dialog, Qt::Key_Return);
            check(QTest::qWaitFor([&] { return dialog.result() == QDialog::Accepted; }, 3000),
                  "Enter accepts the dialog");
            StepExportDialog second(&window);
            second.setProbe(availableProbe);
            second.setDocument(model);
            second.show();
            check(QTest::qWaitForWindowExposed(&second, 3000), "the second dialog shows");
            QTest::keyClick(&second, Qt::Key_Escape);
            check(QTest::qWaitFor([&] { return second.result() == QDialog::Rejected; }, 3000),
                  "Escape cancels the dialog");
        }
        // S2: the size warning, over a threshold (a test threshold: the model's 24 triangles are over 10),
        // and none under the default limit.
        {
            StepExportDialog dialog(&window);
            auto *warning = dialog.findChild<QLabel *>("stepExportSizeWarning");
            check(warning, "the size warning is found");
            dialog.setTriangleWarningLimit(10);
            dialog.setDocument(model);
            check(!warning->isHidden(), "a model over the threshold shows the size warning");
            check(contains(warning->text(), "256 MiB") && contains(warning->text(), "refused"),
                  "the size warning says the file may exceed 256 MiB and be refused: " + warning->text().toStdString());
            check(!dialog.refused() && dialog.findChild<QPushButton *>("stepExportGo")->isEnabled(),
                  "the size warning does not block the export");
            dialog.setTriangleWarningLimit(StepExportDialog::kTriangleWarningLimit);
            check(warning->isHidden(), "the warning goes when the model is under the limit");
        }
        // S3: an empty model is refused in the dialog, with Export disabled (no file is chosen).
        {
            StepExportDialog dialog(&window);
            auto *go = dialog.findChild<QPushButton *>("stepExportGo");
            auto *banner = dialog.findChild<QLabel *>("stepExportBanner");
            dialog.setDocument(Document());
            check(dialog.refused(), "an empty model is refused");
            check(contains(banner->text(), "Nothing to export"), "the refusal names what is missing: " + banner->text().toStdString());
            check(!go->isEnabled(), "Export is disabled for an empty model");
        }
        // S4: the full flow, a successful export to a new file, with the default schema.
        {
            window.setIfcWorkerPath(okWorker);
            const QString out = root.path() + "/result.step";
            Driver driver;
            driver.exportDialogMode = 1; // Export to file...
            driver.savePath = out;
            driver.start();
            window.findChild<QAction *>("file.exportStep")->trigger();
            driver.stop();
            check(driver.dialogSeen, "the plan dialog was shown");
            check(driver.sheets.contains("stepExportReport"), "the report sheet was shown");
            check(driver.progressSeen, "the progress sheet was shown");
            check(driver.saveCount == 1, "the Save dialog was answered once");
            check(QFile::exists(out), "the STEP file was written");
            {
                QFile f(out);
                check(f.open(QIODevice::ReadOnly), "the written file opens");
                const auto bytes = f.readAll();
                check(bytes.startsWith("ISO-10303-21;") && bytes.right(64).trimmed().endsWith("END-ISO-10303-21;"),
                      "the written file is a complete STEP file");
            }
            check(contains(driver.banners["stepExportReport"], "A new STEP file was written"),
                  "the report banner says a new file was written: " + driver.banners.value("stepExportReport").toStdString());
            check(contains(driver.summaries["stepExportReport"], "result.step"),
                  "the report summary names the file: " + driver.summaries.value("stepExportReport").toStdString());
            check(contains(driver.identities["stepExportReport"], "match"),
                  "the report says the counts match the plan: " + driver.identities.value("stepExportReport").toStdString());
            check(contains(status->text(), "Exported a new STEP file"), "the status line names the export");
            const auto argv = readLines(argvPath);
            check(argv.indexOf("--step-schema") >= 0 && argv.value(argv.indexOf("--step-schema") + 1) == "ap214",
                  "the default AP214 reaches the helper");
            check(argv.indexOf("--export-step") == 0, "the helper is run with --export-step");
        }
        // S5: the AP203 choice reaches the helper's argv.
        {
            window.setIfcWorkerPath(okWorker);
            const QString out = root.path() + "/ap203.step";
            Driver driver;
            driver.exportDialogMode = 1;
            driver.schema = "ap203";
            driver.savePath = out;
            driver.start();
            window.findChild<QAction *>("file.exportStep")->trigger();
            driver.stop();
            check(QFile::exists(out), "the AP203 file was written");
            const auto argv = readLines(argvPath);
            const int at = argv.indexOf("--step-schema");
            check(at >= 0 && argv.value(at + 1) == "ap203", "the AP203 choice reaches the helper's argv");
        }
        // S6: a destination that exists is a refusal; the file is not replaced.
        {
            window.setIfcWorkerPath(okWorker);
            const QString existing = writeFixture(root.path(), "exists.step", QByteArray("ISO-10303-21;\nEND-ISO-10303-21;\n"));
            const qint64 before = QFileInfo(existing).lastModified().toMSecsSinceEpoch();
            const qint64 beforeSize = QFileInfo(existing).size();
            Driver driver;
            driver.exportDialogMode = 1;
            driver.savePath = existing;
            driver.start();
            window.findChild<QAction *>("file.exportStep")->trigger();
            driver.stop();
            check(driver.sheets.contains("stepExportRefusal"), "the refusal sheet was shown");
            check(contains(driver.banners["stepExportRefusal"], "Nothing was written"), "the refusal says nothing was written");
            check(contains(driver.summaries["stepExportRefusal"], "already exists"),
                  "the refusal names the existing file: " + driver.summaries.value("stepExportRefusal").toStdString());
            check(QFileInfo(existing).lastModified().toMSecsSinceEpoch() == before && QFileInfo(existing).size() == beforeSize,
                  "the existing file was not touched");
        }
        // S7: a helper that stops is a refusal; nothing is written.
        {
            window.setIfcWorkerPath(failWorker);
            const QString out = root.path() + "/failed.step";
            Driver driver;
            driver.exportDialogMode = 1;
            driver.savePath = out;
            driver.start();
            window.findChild<QAction *>("file.exportStep")->trigger();
            driver.stop();
            check(driver.sheets.contains("stepExportRefusal"), "the refusal sheet was shown");
            check(contains(driver.banners["stepExportRefusal"], "Nothing was written"), "the refusal says nothing was written");
            check(contains(driver.summaries["stepExportRefusal"], "workerFailed") || contains(driver.summaries["stepExportRefusal"], "stopped"),
                  "the refusal names the failed helper: " + driver.summaries.value("stepExportRefusal").toStdString());
            check(!QFile::exists(out), "no file was written");
        }
        // S8: a cancelled export (the Cancel button) writes nothing, leaves no private directory, shows no
        // report and no refusal.
        {
            window.setIfcWorkerPath(slowWorker);
            const QString out = root.path() + "/cancelled.step";
            Driver driver;
            driver.exportDialogMode = 1;
            driver.progressAction = 1;
            driver.savePath = out;
            driver.start();
            window.findChild<QAction *>("file.exportStep")->trigger();
            driver.stop();
            check(driver.progressSeen, "the progress sheet was shown");
            check(!driver.sheets.contains("stepExportReport"), "a cancel shows no report");
            check(!driver.sheets.contains("stepExportRefusal"), "a cancel is not a refusal");
            check(!QFile::exists(out), "a cancelled export writes nothing");
            check(privateDirectories(tempPath).isEmpty(), "a cancelled export leaves no private directory");
            check(contains(status->text(), "cancelled"), "the status line names the cancel: " + status->text().toStdString());
        }
        // S9: the same with Escape on the progress sheet.
        {
            window.setIfcWorkerPath(slowWorker);
            const QString out = root.path() + "/escaped.step";
            Driver driver;
            driver.exportDialogMode = 1;
            driver.progressAction = 2;
            driver.savePath = out;
            driver.start();
            window.findChild<QAction *>("file.exportStep")->trigger();
            driver.stop();
            check(driver.progressSeen, "the progress sheet was shown");
            check(!driver.sheets.contains("stepExportReport") && !driver.sheets.contains("stepExportRefusal"),
                  "an Escape shows no report and no refusal");
            check(!QFile::exists(out), "an Escape writes nothing");
            check(privateDirectories(tempPath).isEmpty(), "an Escape leaves no private directory");
        }
        // S10: the Tab order runs AP214, AP203, the form, Cancel, Export to file...
        {
            StepExportDialog dialog(&window);
            dialog.setProbe(IfcProbe{IfcState::Available, okWorker});
            dialog.setDocument(model);
            auto *ap214 = dialog.findChild<QRadioButton *>("stepExportSchemaAP214");
            auto *ap203 = dialog.findChild<QRadioButton *>("stepExportSchemaAP203");
            auto *scroll = dialog.findChild<QScrollArea *>("stepExportFormScroll");
            auto *cancel = dialog.findChild<QPushButton *>("stepExportCancel");
            auto *go = dialog.findChild<QPushButton *>("stepExportGo");
            check(ap214 && ap203 && scroll && cancel && go, "the Tab stops are found");
            QList<QWidget *> chain;
            for (QWidget *at = ap214; at && chain.size() < 500; at = at->nextInFocusChain()) {
                chain << at;
                if (at == go)
                    break;
            }
            const auto at = [&](QWidget *widget) { return chain.indexOf(widget); };
            check(at(go) >= 0, "the Tab chain reaches Export to file...");
            check(at(ap214) < at(ap203) && at(ap203) < at(scroll) && at(scroll) < at(cancel) && at(cancel) < at(go),
                  "the Tab order is AP214, AP203, the form, Cancel, Export to file...");
        }
        // S11: the helper is missing: the availability sheet, with the export's own words.
        {
            window.setIfcWorkerPath("");
            Driver driver;
            driver.exportDialogMode = 2; // cancel the dialog if one opens
            driver.start();
            window.findChild<QAction *>("file.exportStep")->trigger();
            driver.stop();
            check(driver.sheets.contains("stepExportUnavailable"), "a missing helper shows the not-installed sheet");
            check(contains(driver.summaries["stepExportUnavailable"], "STEP export"),
                  "the not-installed sheet is for export: " + driver.summaries.value("stepExportUnavailable").toStdString());
            check(contains(driver.summaries["stepExportUnavailable"], "sketchyup-ifc"), "the not-installed sheet names the package");
            check(!driver.dialogSeen, "no plan dialog opens without a helper");
        }
    } catch (const std::exception &e) {
        std::cerr << e.what() << std::endl;
        return 1;
    }
    std::cout << "step export screens: the plan, the schema, the size warning, a success, AP203 in the helper's argv, "
                 "a destination, a failed worker, a cancel and an Escape, the Tab order and a missing helper"
              << std::endl;
    return 0;
}
