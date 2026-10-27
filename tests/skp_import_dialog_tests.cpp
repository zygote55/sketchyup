// R102.c (ADR 0176 section 9): the SketchUp (.skp) import screens, offscreen, driven by a stand-in
// sketchyup-skp-worker.
//
// The application's own side (the screens) is what this suite checks: the File menu's order (the
// import after the STEP pair, its report beneath it, disabled until an import); the progress sheet with
// its five steps, the Cancel and Escape (both cancel, and the sheet closes only after the run ends);
// the dirty-model prompts before the helper starts and again at the commit; the import report with
// its identity per kind, the version and container, the counts, the notices in plain words, what the
// package does not carry, and the trademark note; the report chip; the refusals (a file that is not a
// SketchUp file, a file over the size budget, a file from before 2013, a parse failure, a helper that
// stops, and a helper that runs past its deadline); and the "not installed" sheet. A refused or
// cancelled import leaves the model unchanged.
//
// The heavy work is never done here: a stand-in helper (a shell script) answers --import by copying
// a conversion package this suite writes with the package's own writer (the contract the real helper
// writes), and plays the helper's progress protocol along the way. The real helper and its parser
// belong to the SKP job (skp_import, skp_mapping); this suite runs in the fast tier, with a helper
// path it chooses for each case, never the one beside the test binary.
//
// The driver: while a flow (a menu action) runs its nested loops, it closes the prompts it sees (the
// file picker, the "Unsaved changes" boxes, the report and refusal OK). A prompt the flow did not plan
// is rejected and counted, so a missed answer fails the check instead of hanging the suite. It only
// records; the assertions run after the flow returns.

#include "app/import_report.hpp"
#include "app/report_sheet.hpp"
#include "app/skp_progress_sheet.hpp"
#include "app/window.hpp"
#include "io/document_io.hpp"
#include "skp/package.hpp"

#include <QAction>
#include <QApplication>
#include <QAbstractButton>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QTest>
#include <QTemporaryDir>
#include <QTimer>
#include <QTreeWidget>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <iostream>
#include <map>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

using namespace sketchy;
namespace wp = sketchyup::skp; // the helper's package writer, which the stand-in's package comes from

void check(bool ok, const std::string &message) {
    if (!ok)
        throw std::runtime_error(message);
}

bool contains(const QString &text, const QString &needle) { return text.contains(needle); }

// A stand-in helper: a shell script the application can exec. The helper runs in a scrubbed
// environment (only LC_ALL is set; there is no PATH), so every command is an absolute path.
// `importBody` is the --import body; the argument loop reads --import and --output the helper's way.
QString standInScript(const QString &importBody) {
    return QString(R"PROG(#!/bin/sh
# A stand-in for sketchyup-skp-worker (R102.c dialog tests).
input=""
out=""
while [ "$#" -gt 0 ]; do
  case "$1" in
    --import) input="$2"; shift 2 ;;
    --output) out="$2"; shift 2 ;;
    *) shift ;;
  esac
done
[ -z "$out" ] && { echo 'R {"status":"refused","code":"standIn","message":"the stand-in got no --output"}'; exit 3; }
)PROG") + importBody;
}

// The --import body that succeeds: the helper's phases are played (read, parse, package), the package
// is written to --output after `sleepSeconds` of read work, and the helper reports its own line.
QString importBody(const QString &package, const QString &sleepSeconds) {
    return QString("echo 'P phase read'\n/bin/sleep ") + sleepSeconds +
           QString("\necho 'P phase parse'\necho 'P phase package'\n/usr/bin/cp '") + package +
           QString("' \"$out\" || exit 1\necho 'R {\"status\":\"ok\",\"bytes\":900}'\nexit 0\n");
}

// The --import body that refuses: the helper's own refusal line, and exit 3.
QString refusalBody(const QString &code, const QString &message) {
    return QString("echo 'P phase parse'\necho 'R {\"status\":\"refused\",\"code\":\"") + code +
           QString("\",\"message\":\"") + message + QString("\"}'\nexit 3\n");
}

// The --import body that stops by itself, as a helper that crashes does (SIGSEGV on its own shell).
QString crashBody() { return QString("echo 'P phase parse'\nkill -SEGV $$\n"); }

// The --import body that runs past any deadline the application sets: it sleeps for `sleepSeconds`.
QString slowBody(const QString &sleepSeconds) {
    return QString("echo 'P phase read'\n/bin/sleep ") + sleepSeconds + QString("\nexit 0\n");
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

// A SketchUp file, as the application's cheap checks see it: the UTF-16 byte order mark and the 0x0E of
// the header's marker, then whatever the stand-in does not read.
QByteArray skpFile() {
    QByteArray bytes;
    bytes.append(char(0xFF));
    bytes.append(char(0xFE));
    bytes.append(char(0xFF));
    bytes.append(char(0x0E));
    bytes.append("stand-in SketchUp model, not read by the stand-in helper");
    return bytes;
}

QByteArray notSkp() { return QByteArray("This is a note file, not a SketchUp file.\n"); }

QString writeFixture(const QString &dir, const QString &name, const QByteArray &bytes) {
    const QString path = dir + "/" + name;
    QFile file(path);
    check(file.open(QIODevice::WriteOnly), "write " + name.toStdString());
    check(file.write(bytes) == bytes.size(), "write " + name.toStdString());
    file.close();
    return path;
}

// The conversion package the stand-in copies, written by the helper's own writer: the root (definition
// 0) with one square face of an inch, and a definition "Chair" with one triangle, placed once 0.1 m
// along x. With `omittedFace`, the source had one more face than the helper kept (the report's
// attention and the invalidFaces notice).
QByteArray standInPackage(const bool omittedFace) {
    wp::SkpPackage package;
    package.formatVersion = "{25.0.575}";
    package.container = "vff";
    package.units = "Inches";
    package.unitScale = 0.0254;
    package.sourceName = "model.skp";
    package.sourceSha256 = std::string(64, 'a');
    package.sourceSize = 1000;
    const double side = 0.0254;
    package.definitions.resize(2); // the references below must not move: no growth after this
    auto &root = package.definitions[0];
    root.name = "ROOT";
    root.vertices = {{0, 0, 0}, {side, 0, 0}, {side, side, 0}, {0, side, 0}};
    for (std::uint32_t v = 0; v < 4; ++v) {
        wp::SkpPackageEdge edge;
        edge.v1 = v;
        edge.v2 = (v + 1) % 4;
        root.edges.push_back(edge);
    }
    wp::SkpPackageFace square;
    square.loops = {{0, 1, 2, 3}};
    square.materialFront = 0;
    root.faces.push_back(square);
    auto &chair = package.definitions[1];
    chair.name = "Chair";
    chair.vertices = {{0, 0, 0}, {side, 0, 0}, {0, side, 0}};
    for (std::uint32_t v = 0; v < 3; ++v) {
        wp::SkpPackageEdge edge;
        edge.v1 = v;
        edge.v2 = (v + 1) % 3;
        chair.edges.push_back(edge);
    }
    wp::SkpPackageFace triangle;
    triangle.loops = {{0, 1, 2}};
    triangle.materialFront = 0;
    chair.faces.push_back(triangle);
    wp::SkpPackageInstance placed;
    placed.definition = 1;
    placed.transform[12] = 0.1;
    root.instances.push_back(placed);
    wp::SkpPackageMaterial paint;
    paint.name = "Paint";
    paint.color = {200, 100, 50, 255};
    package.materials = {paint};
    package.counts.definitions = 2;
    package.counts.vertices = 7;
    package.counts.edges = 7;
    package.counts.faces = 2;
    package.counts.instances = 1;
    package.counts.materials = 1;
    package.sourceTotals = {{"edges", 7}, {"faces", omittedFace ? 3 : 2}, {"instances", 1}, {"materials", 1}};
    if (omittedFace) {
        // The helper's own accounting: the face it left out, which the decoder's identity checks.
        package.omitted["face"] = 1;
        package.notices["invalidFaces"] = 1;
    }
    std::string problem;
    const std::string encoded = wp::serializeSkpPackage(package, problem);
    check(!encoded.empty(), "the package encodes: " + problem);
    return QByteArray(encoded.data(), static_cast<int>(encoded.size()));
}

// A file over the import's size budget: the header, then a sparse extension past 256 MiB, which the
// application refuses by its size before it reads any of it.
QString writeOversize(const QString &dir) {
    const QString path = dir + "/oversize.skp";
    QFile file(path);
    check(file.open(QIODevice::WriteOnly), "write the oversize file");
    file.write(skpFile());
    check(file.resize(qint64(256) * 1024 * 1024 + 1), "extend the oversize file");
    file.close();
    return path;
}

// The driver: while a flow (a menu action) runs its nested loops, it closes the prompts it sees. It
// only records; the assertions run after the flow returns.
struct Driver {
    QString choose; // the file to choose in the picker; empty: reject the picker
    std::function<void(SkpProgressSheet *)> onProgress;
    std::vector<QMessageBox::StandardButton> confirmPlan; // one button per "Unsaved changes"
    int pickers = 0;
    int confirmations = 0;
    int unexpectedPrompts = 0; // a prompt the plan did not answer: rejected, so a flow cannot hang on it
    bool progressSeen = false;
    bool sawReadStep = false; // the read step was the working one (the helper ran)
    bool progressCancelFound = false;
    QString progressFile;
    QStringList sheets;
    std::map<QString, QString> summaries;
    std::map<QString, QString> hows;
    std::map<QString, QString> banners;
    std::map<QString, QString> identities;
    std::map<QString, QString> notes;
    std::map<QString, QStringList> listRows; // every row of a report's list, "label: count"
    std::map<QString, QString> refused;      // a refusal's "Refused: code" line
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
                    QMetaObject::invokeMethod(chooser, [chooser] { chooser->reject(); }, Qt::QueuedConnection);
                    continue;
                }
                const QString path = choose;
                QMetaObject::invokeMethod(
                    chooser,
                    [chooser, path] {
                        chooser->selectFile(path);
                        if (auto *field = chooser->findChild<QLineEdit *>("fileNameEdit"); field && field->isVisible())
                            field->setText(path);
                        QMetaObject::invokeMethod(chooser, "accept", Qt::QueuedConnection);
                    },
                    Qt::QueuedConnection);
                continue;
            }
            if (auto *box = qobject_cast<QMessageBox *>(widget)) {
                if (box->property("driven").toBool())
                    continue;
                box->setProperty("driven", true);
                ++confirmations;
                if (confirmPlan.empty()) {
                    ++unexpectedPrompts;
                    QMetaObject::invokeMethod(box, [box] { box->reject(); }, Qt::QueuedConnection);
                    continue;
                }
                const auto button = confirmPlan.front();
                confirmPlan.erase(confirmPlan.begin());
                QMetaObject::invokeMethod(box, [box, button] { box->button(button)->click(); }, Qt::QueuedConnection);
                continue;
            }
            if (auto *sheet = qobject_cast<SkpProgressSheet *>(widget)) {
                progressSeen = true;
                if (auto *state = sheet->findChild<QLabel *>("skpProgressStep2State"))
                    if (state->text() == "●")
                        sawReadStep = true;
                if (auto *file = sheet->findChild<QLabel *>("skpProgressFile"))
                    progressFile = file->text();
                if (sheet->findChild<QPushButton *>("skpProgressCancel"))
                    progressCancelFound = true;
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
                if (auto *how = report->findChild<QPlainTextEdit *>("skpReportHow"))
                    hows[name] = how->toPlainText();
                if (auto *label = report->findChild<QLabel *>("skpRefusalRefused"))
                    refused[name] = label->text();
                if (auto *label = report->findChild<QLabel *>("skpReportBanner"))
                    banners[name] = label->text();
                if (auto *label = report->findChild<QLabel *>("skpReportIdentity"))
                    identities[name] = label->text();
                if (auto *label = report->findChild<QLabel *>("skpReportNote"))
                    notes[name] = label->text();
                if (auto *list = report->findChild<QTreeWidget *>("skpReportList")) {
                    QStringList rows;
                    for (QTreeWidgetItemIterator it(list); *it; ++it)
                        rows << (*it)->text(0) + ": " + (*it)->text(1);
                    listRows[name] = rows;
                }
                if (!report->property("closed").toBool()) {
                    report->setProperty("closed", true);
                    for (auto *button : report->findChildren<QAbstractButton *>())
                        if (button->isVisible() && button->objectName().endsWith("OK")) {
                            QMetaObject::invokeMethod(button, [button] { button->click(); }, Qt::QueuedConnection);
                            break;
                        }
                }
                continue;
            }
        }
    }
};

// The File menu's actions, by object name, in the order the menu shows them.
QStringList fileMenuOrder(Window &window) {
    QStringList names;
    for (auto *action : window.menuBar()->actions())
        if (action->menu() && action->menu()->title().remove('&') == "File")
            for (auto *item : action->menu()->actions())
                names << item->objectName();
    return names;
}

} // namespace

int main(int argc, char **argv) {
    QTemporaryDir preferences;
    qputenv("XDG_CONFIG_HOME", preferences.path().toUtf8());
    // The dialogs are the application's own (the file picker, the message boxes), not the platform's.
    QApplication::setAttribute(Qt::AA_DontUseNativeDialogs);
    QApplication app(argc, argv);
    QTemporaryDir root;
    const QString model = writeFixture(root.path(), "model.skp", skpFile());
    const QString notes = writeFixture(root.path(), "notes.txt", notSkp());
    const QString oversize = writeOversize(root.path());
    const QString package = writeFixture(root.path(), "package.skskpp", standInPackage(false));
    const QString omitted = writeFixture(root.path(), "omitted.skskpp", standInPackage(true));
    const QString available = writeExecutable(root.path(), "worker-available", standInScript(importBody(package, "0.3")));
    const QString slowWorker = writeExecutable(root.path(), "worker-slow", standInScript(importBody(package, "3")));
    const QString sleeper = writeExecutable(root.path(), "worker-sleeper", standInScript(slowBody("4")));
    const QString withOmitted = writeExecutable(root.path(), "worker-omitted", standInScript(importBody(omitted, "0")));
    const QString pre2013 = writeExecutable(
        root.path(), "worker-pre2013",
        standInScript(refusalBody("pre2013", "This SketchUp file is from before 2013. Save it from SketchUp 2021 or later and try again.")));
    const QString parseFailure = writeExecutable(
        root.path(), "worker-parse",
        standInScript(refusalBody("parse", "record 12 of the model is not a face")));
    const QString crashing = writeExecutable(root.path(), "worker-crash", standInScript(crashBody()));

    Window window;
    window.resize(1280, 860);
    window.show();
    check(QTest::qWaitForWindowExposed(&window, 5000), "The window shows");
    auto *status = window.findChild<QLabel *>("statusMessage");
    check(status, "the status label is found");
    auto *chip = window.findChild<QPushButton *>("skpImportChip");
    check(chip, "the SketchUp report chip is found");
    check(!chip->isVisible(), "the chip is hidden before any import");
    auto *importAction = window.findChild<QAction *>("file.importSkp");
    auto *reportAction = window.findChild<QAction *>("file.skpImportReport");
    check(importAction && reportAction, "the File menu has the import and its report");
    check(!reportAction->isEnabled(), "the report is disabled before an import");

    try {
        // S0: the File menu: the import comes after the STEP pair (Import STEP… and its report, which sits
        // directly below it), and its own report directly below it.
        {
            const QStringList order = fileMenuOrder(window);
            const int stepReport = order.indexOf("file.stepImportReport");
            check(stepReport >= 0 && order.value(stepReport + 1) == "file.importSkp" &&
                      order.value(stepReport + 2) == "file.skpImportReport",
                  "the SketchUp import follows the STEP pair, with its report below: " + order.join(" ").toStdString());
        }
        // S1: a missing helper is "not installed"; the model is untouched and no prompt appears.
        {
            window.setSkpWorkerPath("/nonexistent/sketchyup-skp-worker");
            const auto history = window.document().history().total;
            Driver driver;
            driver.start();
            importAction->trigger();
            driver.stop();
            check(driver.sheets == (QStringList{"skpUnavailable"}), "a missing helper shows the unavailable sheet");
            check(contains(driver.summaries["skpUnavailable"], "not installed"),
                  "the sheet says the helper is not installed");
            check(!driver.progressSeen, "no progress sheet opens without a helper");
            check(!chip->isVisible(), "the chip is hidden without a helper");
            auto &doc = window.document();
            check(doc.history().total == history && !doc.dirty(), "nothing was changed");
        }
        // S2: a file that is not a SketchUp file is refused before the helper starts.
        {
            window.setSkpWorkerPath(available);
            const auto encoded = encodeContainer(window.document());
            Driver driver;
            driver.choose = notes;
            driver.start();
            importAction->trigger();
            driver.stop();
            check(driver.sheets == (QStringList{"skpRefusal"}), "the refusal sheet was shown");
            check(contains(driver.summaries["skpRefusal"], "does not start with a SketchUp header"),
                  "the refusal names the check: " + driver.summaries["skpRefusal"].toStdString());
            check(contains(driver.summaries["skpRefusal"], "The model was not changed"),
                  "the refusal says the model was not changed");
            check(contains(driver.summaries["skpRefusal"], "Choose a SketchUp (.skp) file"),
                  "the refusal says what to do next");
            check(encodeContainer(window.document()) == encoded, "the model is byte for byte unchanged");
            check(!chip->isVisible(), "a refused import shows no chip");
        }
        // S3: a file over the size budget is refused by its size, before the helper reads it.
        {
            Driver driver;
            driver.choose = oversize;
            driver.start();
            importAction->trigger();
            driver.stop();
            check(driver.sheets == (QStringList{"skpRefusal"}), "the oversize file is refused");
            check(contains(driver.summaries["skpRefusal"], "larger than the 256 MiB"),
                  "the refusal names the size budget: " + driver.summaries["skpRefusal"].toStdString());
            // The check is the first step ("Check the file") and runs before the helper's read step, so the
            // progress sheet may flash open at that step; it never reaches the helper's read.
            check(!driver.sawReadStep, "the helper's read step never ran for an oversize file");
        }
        // S4: a file from before 2013: the refusal gives the advice to save it again.
        {
            window.setSkpWorkerPath(pre2013);
            const auto encoded = encodeContainer(window.document());
            Driver driver;
            driver.choose = model;
            driver.start();
            importAction->trigger();
            driver.stop();
            check(driver.sheets == (QStringList{"skpRefusal"}), "the pre-2013 refusal sheet was shown");
            check(contains(driver.summaries["skpRefusal"], "before 2013") &&
                      contains(driver.summaries["skpRefusal"], "Open the file in SketchUp 2021 or later and save it"),
                  "the pre-2013 refusal gives the 2021 advice: " + driver.summaries["skpRefusal"].toStdString());
            check(driver.refused["skpRefusal"] == "Refused: pre2013", "the refusal names its code");
            check(encodeContainer(window.document()) == encoded, "the model is byte for byte unchanged");
        }
        // S5: a parse failure: the advice, and the helper's own words.
        {
            window.setSkpWorkerPath(parseFailure);
            Driver driver;
            driver.choose = model;
            driver.start();
            importAction->trigger();
            driver.stop();
            check(driver.sheets == (QStringList{"skpRefusal"}), "the parse refusal sheet was shown");
            check(contains(driver.summaries["skpRefusal"], "could not read this file") &&
                      contains(driver.summaries["skpRefusal"], "The words the helper used: record 12 of the model is not a face"),
                  "the parse refusal names the reader and the helper's words: " + driver.summaries["skpRefusal"].toStdString());
            check(contains(driver.summaries["skpRefusal"], "2021 or later"), "the parse refusal gives the 2021 advice");
        }
        // S6: a helper that stops (a crash): the refusal names the stop, and the model is untouched.
        {
            window.setSkpWorkerPath(crashing);
            Driver driver;
            driver.choose = model;
            driver.start();
            importAction->trigger();
            driver.stop();
            check(driver.sheets == (QStringList{"skpRefusal"}), "the helper-stopped refusal sheet was shown");
            check(contains(driver.summaries["skpRefusal"], "stopped before it finished (crash)"),
                  "the refusal names the stop: " + driver.summaries["skpRefusal"].toStdString());
            check(driver.refused["skpRefusal"] == "Refused: workerFailed", "the refusal names its code");
        }
        // S7: a helper that runs past its deadline: the refusal names the time limit.
        {
            window.setSkpWorkerPath(sleeper);
            window.setSkpDeadlineSeconds(1);
            Driver driver;
            driver.choose = model;
            driver.start();
            importAction->trigger();
            driver.stop();
            window.setSkpDeadlineSeconds(120);
            check(driver.sheets == (QStringList{"skpRefusal"}), "the timeout refusal sheet was shown");
            check(contains(driver.summaries["skpRefusal"], "past its time limit"),
                  "the refusal names the time limit: " + driver.summaries["skpRefusal"].toStdString());
        }
        // S8: a full import. The progress sheet runs its steps, the model is one labelled edit, the
        // report shows the version, the identity per kind, the notices, what is not carried and the
        // trademark note, the chip offers the report, and the source file is untouched.
        {
            window.setSkpWorkerPath(available);
            const auto history = window.document().history().total;
            Driver driver;
            driver.choose = model;
            driver.start();
            importAction->trigger();
            driver.stop();
            check(driver.confirmations == 0, "no unsaved-model prompt for a clean model");
            check(driver.progressSeen, "the progress sheet opened");
            check(driver.progressFile == "model.skp", "the progress sheet names the file");
            check(driver.sawReadStep, "the read step was the working one while the helper ran");
            auto &doc = window.document();
            check(!doc.bodies().empty(), "the model has bodies");
            check(doc.history().total == history + 1, "the import is one edit");
            check(doc.history().entries.back().label == "Import .skp file", "the edit is labelled Import .skp file");
            check(contains(status->text(), "Imported an unsaved SketchUp model") &&
                      contains(status->text(), "Source file unchanged"),
                  "the status names the outcome: " + status->text().toStdString());
            check(chip->isVisible() && contains(chip->text(), ".skp import report"),
                  "the chip offers the report: " + chip->text().toStdString());
            check(reportAction->isEnabled(), "the report action is enabled after an import");
            check(driver.sheets.contains("skpImportReport"), "the report sheet was shown");
            const QString name = "skpImportReport";
            check(contains(driver.banners[name], "✓ Import finished") && !contains(driver.banners[name], "restart"),
                  "the banner names the outcome without restarts: " + driver.banners[name].toStdString());
            check(driver.summaries[name].startsWith("model.skp\n"), "the summary names the file");
            check(contains(driver.summaries[name], "\nSketchUp 25.0.575 · "),
                  "the summary names the version: " + driver.summaries[name].toStdString());
            check(contains(driver.summaries[name], "Faces: 2 · Instances: 1 · Materials: 1"),
                  "the summary names the counts: " + driver.summaries[name].toStdString());
            check(contains(driver.identities[name], "faces read 2 = imported 2 + omitted 0") &&
                      contains(driver.identities[name], "instances read 1 = imported 1 + omitted 0") &&
                      contains(driver.identities[name], "materials read 1 = imported 1 + omitted 0") &&
                      contains(driver.identities[name], "edges read "),
                  "the accounting identity holds per kind: " + driver.identities[name].toStdString());
            check(contains(driver.identities[name], "attributes read "),
                  "the identity names the attributes: " + driver.identities[name].toStdString());
            check(driver.hows[name] ==
                      "Format: SketchUp 25.0.575, SketchUp 2021 and later (VFF container)\n"
                      "Units: Inches; the geometry is read in inches and converted to metres",
                  "the how-it-was-read block is filled: " + driver.hows[name].toStdString());
            check(contains(driver.notes[name], "SketchUp is a trademark of Trimble Inc.") &&
                      contains(driver.notes[name], "SketchyUp is not affiliated with or endorsed by Trimble") &&
                      contains(driver.notes[name], "some files may not import exactly"),
                  "the trademark note is in the report: " + driver.notes[name].toStdString());
            const QStringList rows = driver.listRows[name];
            check(rows.contains("Structure: "), "the report has its Structure group");
            check(rows.contains("Not carried by this import: "), "the report has the not-carried group");
            check(rows.contains("Scenes: not imported") && rows.contains("Section planes: not imported"),
                  "the report names what the package does not carry");
            check(rows.contains("Notices: "), "the report has its notices group");
            check(rows.contains("None: "), "the report says there are no notices");
            // The report can be shown again from the menu.
            Driver again;
            again.start();
            reportAction->trigger();
            again.stop();
            check(again.sheets == (QStringList{"skpImportReport"}), "the menu re-shows the report");
            // The source file is never modified.
            QFile source(model);
            check(source.open(QIODevice::ReadOnly), "the source opens");
            check(source.readAll() == skpFile(), "the source file is byte for byte unchanged");
            // Undo takes the import back.
            doc.undo();
            check(!doc.canUndo() && doc.bodies().empty(), "undo undoes the import");
        }
        // S9: the report's attention: the helper left out a face the source had, so the chip counts it,
        // the identity says so, and the notice names the face the native model refused.
        {
            window.setSkpWorkerPath(withOmitted);
            Driver driver;
            driver.choose = model;
            driver.start();
            importAction->trigger();
            driver.stop();
            check(contains(chip->text(), "1 need attention"),
                  "the chip counts the omitted face: " + chip->text().toStdString());
            const QString name = "skpImportReport";
            check(driver.identities[name].contains("faces read 3 = imported 2 + omitted 1"),
                  "the identity counts the omitted face: " + driver.identities[name].toStdString());
            check(driver.listRows[name].join("|").contains("Faces the native model refuses (not planar, self-intersecting or collapsed): dropped: 1"),
                  "the notice names the refused face in plain words");
        }
        // S10: Cancel during the import leaves the model unchanged and keeps the previous chip.
        {
            window.setSkpWorkerPath(slowWorker);
            const auto encoded = encodeContainer(window.document());
            const auto history = window.document().history().total;
            Driver driver;
            driver.choose = model;
            driver.confirmPlan = {QMessageBox::Discard}; // the model is dirty after S9's import
            driver.onProgress = [](SkpProgressSheet *sheet) {
                if (auto *cancel = sheet->findChild<QPushButton *>("skpProgressCancel"))
                    QTest::mouseClick(cancel, Qt::LeftButton);
            };
            driver.start();
            importAction->trigger();
            driver.stop();
            check(driver.unexpectedPrompts == 0 && driver.confirmations == 1, "one prompt, answered");
            check(driver.progressCancelFound, "the progress sheet offers Cancel");
            check(contains(status->text(), "cancelled"), "the status says the import was cancelled: " + status->text().toStdString());
            check(encodeContainer(window.document()) == encoded && window.document().history().total == history,
                  "the model is byte for byte unchanged");
            check(chip->isVisible() && contains(chip->text(), ".skp import"), "the previous import's chip stays: " + chip->text().toStdString());
        }
        // S11: Escape is Cancel during the import, as the Cancel button is.
        {
            window.setSkpWorkerPath(slowWorker);
            const auto encoded = encodeContainer(window.document());
            Driver driver;
            driver.choose = model;
            driver.confirmPlan = {QMessageBox::Discard}; // the model is dirty after the last import
            driver.onProgress = [](SkpProgressSheet *sheet) { QTest::keyClick(sheet, Qt::Key_Escape); };
            driver.start();
            importAction->trigger();
            driver.stop();
            check(driver.unexpectedPrompts == 0, "no prompt beyond the plan");
            check(contains(status->text(), "cancelled"), "Escape cancels: " + status->text().toStdString());
            check(encodeContainer(window.document()) == encoded, "the model is unchanged after Escape");
        }
        // S12: a dirty model: Cancel at the first prompt, before the helper starts.
        {
            window.setSkpWorkerPath(available);
            window.demo();
            check(window.document().dirty(), "the demo model is dirty");
            const auto encoded = encodeContainer(window.document());
            Driver driver;
            driver.choose = model;
            driver.confirmPlan = {QMessageBox::Cancel};
            driver.start();
            importAction->trigger();
            driver.stop();
            check(driver.confirmations == 1, "the prompt appeared before the helper");
            check(!driver.progressSeen, "Cancel at the first prompt starts no helper");
            check(encodeContainer(window.document()) == encoded, "the model is unchanged");
        }
        // S13: a dirty model: Discard before the helper, Cancel at the commit phase.
        {
            window.setSkpWorkerPath(available);
            window.demo();
            const auto encoded = encodeContainer(window.document());
            const auto history = window.document().history().total;
            Driver driver;
            driver.choose = model;
            driver.confirmPlan = {QMessageBox::Discard, QMessageBox::Cancel};
            driver.start();
            importAction->trigger();
            driver.stop();
            check(driver.confirmations == 2, "the prompt appeared before the helper and again at the commit");
            check(contains(status->text(), "the change was cancelled"),
                  "the status says the finished import was cancelled: " + status->text().toStdString());
            check(encodeContainer(window.document()) == encoded && window.document().history().total == history,
                  "the model was not replaced");
            check(!chip->isVisible(), "the demo model has no SketchUp import, so no chip");
        }
        // S14: a dirty model: Discard both prompts; the import replaces the model with one labelled edit.
        {
            window.setSkpWorkerPath(available);
            window.demo();
            Driver driver;
            driver.choose = model;
            driver.confirmPlan = {QMessageBox::Discard, QMessageBox::Discard};
            driver.start();
            importAction->trigger();
            driver.stop();
            check(driver.confirmations == 2, "both prompts were answered");
            auto &doc = window.document();
            check(doc.history().total == 1 && doc.history().entries.back().label == "Import .skp file",
                  "the replacement is one labelled edit");
            check(contains(status->text(), "Imported an unsaved SketchUp model"), "the status names the outcome");
            check(chip->isVisible() && contains(chip->text(), ".skp import report"),
                  "the chip offers the report of the replaced model");
        }
        // S15: a new model ends the SketchUp chip's lifetime, as it ends the STEP chip's.
        {
            window.demo();
            check(!chip->isVisible() && !reportAction->isEnabled(),
                  "a new model clears the SketchUp chip and its report");
        }
        // S16 (R105.a, ADR 0179 section 7.3): a downloaded ZIP chosen in the picker is named before
        // the helper runs, and the model is unchanged. The picker's "All files" filter lets it be
        // chosen.
        {
            window.setSkpWorkerPath(available);
            const auto encoded = encodeContainer(window.document());
            Driver driver;
            driver.choose =
                QStringLiteral(SOURCE_DIR "/tests/fixtures/import-refusal/wrapped-box.zip");
            driver.start();
            importAction->trigger();
            driver.stop();
            check(driver.sheets == (QStringList{"importRefusal"}),
                  "the ZIP refusal sheet was shown");
            check(contains(
                      driver.summaries["importRefusal"],
                      "This is a ZIP archive. Extract it, then import the model file inside it."),
                  "the sheet names the archive and the step: " +
                      driver.summaries["importRefusal"].toStdString());
            check(contains(driver.summaries["importRefusal"], "The model was not changed"),
                  "the sheet says the model was not changed");
            check(encodeContainer(window.document()) == encoded,
                  "the model is byte for byte unchanged");
            check(!chip->isVisible(), "a refused file shows no chip");
        }
    } catch (const std::exception &error) {
        std::cerr << "FAIL: " << error.what() << '\n';
        return 1;
    }
    std::cout << "passed: the SketchUp import screens\n";
    return 0;
}
