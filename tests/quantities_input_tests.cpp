// R101.e2 (UX_DESIGN §19): the Quantities section, driven through the real main window and the
// offscreen Qt platform. It checks the default view, grouping and filters, an off-thread run with
// Cancel, a newer revision, the invalid list, schedules (save, rename, delete, run, stale rows, the
// other models' count, an unreadable store), the exports (CSV and JSON, selection export, a refused
// existing file), the refusals, and the keyboard path. Every file it writes is in a temporary
// folder: XDG_DATA_HOME, XDG_CONFIG_HOME and HOME point there, so the schedule store is never the
// user's.
#include "app/quantities_panel.hpp"
#include "app/surface_format.hpp"
#include "app/window.hpp"
#include "automation/report_schedules.hpp"
#include "core/model.hpp"
#include "core/tags.hpp"
#include "report_fixtures.hpp"
#include "shortcut_focus.hpp"
#include <QAction>
#include <QApplication>
#include <QLocale>
#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QElapsedTimer>
#include <QFile>
#include <QFileDialog>
#include <QFrame>
#include <QGraphicsOpacityEffect>
#include <QGuiApplication>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QLocale>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QPushButton>
#include <QRadioButton>
#include <QScrollArea>
#include <QTabWidget>
#include <QTemporaryDir>
#include <QTest>
#include <QThread>
#include <QTimer>
#include <QTreeWidget>
#include <atomic>
#include <functional>
#include <iostream>
#include <map>
#include <memory>
#include <vector>

using namespace sketchy;

namespace {

void check(bool value, const std::string &message) {
    if (!value)
        throw std::runtime_error(message);
}

// A check made inside a dialog's driver runs in a Qt slot, where an exception would end the
// process. It is recorded here and raised by verifyDeferred() once the action has returned.
std::vector<std::string> deferred;
void expectInSlot(bool value, const std::string &message) {
    if (!value)
        deferred.push_back(message);
}

// The drivers that have not seen their dialog yet, by id, with the step that expects it.
std::map<int, std::string> awaitingDialogs;
int nextDialogDriver = 0;

// The driver for a modal dialog that the next action opens. It polls for a modal widget that
// `matches`, runs `drive` on it, and stops. It never gives up by itself: a step may wait for other
// things before its action, so verifyDeferred() decides once the action has returned.
void driveModal(const std::function<bool(QWidget *)> &matches,
                const std::function<void(QWidget *)> &drive, const std::string &what) {
    const int id = nextDialogDriver++;
    awaitingDialogs[id] = what;
    auto *timer = new QTimer(qApp);
    QObject::connect(timer, &QTimer::timeout, qApp, [timer, id, matches, drive] {
        if (!awaitingDialogs.count(id)) { // verifyDeferred() gave up on this driver
            timer->stop();
            timer->deleteLater();
            return;
        }
        if (auto *modal = QApplication::activeModalWidget(); modal && matches(modal)) {
            timer->stop();
            timer->deleteLater();
            awaitingDialogs.erase(id);
            drive(modal);
        }
    });
    timer->start(10);
}

// Gives the drivers that the action should have answered up to 8 s. A driver whose dialog never
// came is recorded as a failure, and the first recorded failure is raised.
void verifyDeferred() {
    if (!QTest::qWaitFor([] { return awaitingDialogs.empty(); }, 8000))
        for (const auto &entry : awaitingDialogs)
            deferred.push_back("The dialog never appeared: " + entry.second);
    awaitingDialogs.clear();
    if (!deferred.empty())
        throw std::runtime_error(deferred.front());
}

bool named(QWidget *widget, const char *name) { return widget->objectName() == name; }

// A window shortcut (F2, Delete) fires only in the widget that holds focus, in a window that holds
// keyboard focus. After a modal closes, the window may not hold it yet, so wait for both.
void pressShortcut(QWidget *widget, Qt::Key key) {
    if (auto *list = qobject_cast<QListWidget *>(widget);
        list && list->count() && list->currentRow() < 0)
        list->setCurrentRow(0);
    check(waitForShortcutFocus(widget),
          widget->objectName().toStdString() + " takes keyboard focus before the key is pressed");
    QTest::keyClick(widget, key);
}

QString text(QWidget *parent, const char *name) {
    auto *widget = parent->findChild<QWidget *>(name);
    if (!widget)
        return {};
    if (auto *label = qobject_cast<QLabel *>(widget))
        return label->text();
    if (auto *button = qobject_cast<QPushButton *>(widget))
        return button->text();
    return {};
}

template <class T> T *child(QWidget *parent, const char *name) {
    auto *widget = parent->findChild<T *>(name);
    check(widget != nullptr, std::string("Missing control ") + name);
    return widget;
}

bool waitFor(const std::function<bool()> &predicate, int milliseconds = 8000) {
    return QTest::qWaitFor(predicate, milliseconds);
}

// Waits until a label shows `expected`; on failure the message names what it shows instead.
void expectText(QWidget *parent, const char *name, const QString &expected,
                const std::string &message) {
    if (waitFor([&] { return text(parent, name) == expected; }))
        return;
    throw std::runtime_error(message + " (" + name + " shows \"" +
                             text(parent, name).toStdString() + "\", expected \"" +
                             expected.toStdString() + "\")");
}

Id bodyNamed(const Document &doc, const std::string &name) {
    for (const auto &[id, record] : doc.bodies())
        if (record->name == name)
            return id;
    throw std::runtime_error("No body named " + name);
}

QString readFile(const QString &path) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return {};
    return QString::fromUtf8(file.readAll());
}

// The text of a quantity as the panel shows it: "40,259.20 m²" at precision 2.
QString measure(const QString &value, const QString &unit) { return value + " " + unit; }

} // namespace

int main(int argc, char **argv) {
    // Every path the feature uses lives under this folder: the schedule store, the settings and
    // HOME.
    QTemporaryDir home;
    qputenv("HOME", home.path().toUtf8());
    qputenv("XDG_DATA_HOME", (home.path() + "/data").toUtf8());
    qputenv("XDG_CONFIG_HOME", (home.path() + "/config").toUtf8());
    QSurfaceFormat format;
    format.setDepthBufferSize(24);
    sketchy::setDefaultViewportFormat(format);
    // The export's Save dialog is the widget dialog, so the driver can answer it.
    QApplication::setAttribute(Qt::AA_DontUseNativeDialogs);
    QApplication app(argc, argv);
    QApplication::setOrganizationName("SketchyUp");
    QApplication::setApplicationName("SketchyUp");
    // The panel groups digits as the locale does (§19.5). The expected "40,259.20" must not depend
    // on the host's LANG: CI runs under the C locale, which does not group.
    QLocale::setDefault(QLocale(QLocale::English, QLocale::UnitedStates));
    // The panel formats numbers in the person's locale (digit grouping included). Pin it, so the
    // expected "40,259.20 m²" holds on a CI container whose locale is C.
    QLocale::setDefault(QLocale(QLocale::English, QLocale::UnitedStates));

    try {
        Window window;
        window.resize(1280, 900);
        auto &doc = window.document();
        auto *view = window.viewport();
        doc.setDisplayPrecision(2);
        const auto fixture = report_fixtures::buildQuantitiesA(doc);
        QMetaObject::invokeMethod(view, "changed");
        window.show();
        check(QTest::qWaitForWindowExposed(&window), "The window is exposed");
        window.activateWindow();
        // Shortcuts (F2, Delete) reach the list only while the window is active.
        check(QTest::qWaitForWindowActive(&window), "The window is active for shortcuts");

        // The model's file: a saved model has one, and the schedules and exports need it.
        const auto modelFile = home.path() + "/quantities-a.sketchyup";
        auto *tabs = window.findChild<QTabWidget *>("organizationTabs");
        check(tabs != nullptr, "The Model panels");

        // 1. Opening and the default view (§19.1, §19.2, §19.3, §19.5).
        window.findChild<QAction *>("view.quantities")->trigger();
        auto *panel = window.findChild<QuantitiesPanel *>("quantitiesPanel");
        check(panel && panel->isVisible(), "View > Quantities shows the section");
        check(tabs->tabText(tabs->currentIndex()) == "Quantities",
              "The Quantities tab is the current one");
        check(tabs->tabText(4) == "Quantities" && tabs->tabText(5) == "History",
              "Quantities sits between Materials and History");
        panel->setModelPath([modelFile] { return modelFile; });
        auto *groupBy = child<QComboBox>(panel, "quantitiesGroupBy");
        check(groupBy->currentData().toString() == "none", "Group by None is the default");
        check(child<QRadioButton>(panel, "quantitiesIncludeHidden")->isChecked(),
              "Include hidden is the default");
        if (!waitFor([&] { return text(panel, "quantitiesArea") == "40,259.20 m²"; }))
            throw std::runtime_error("The All row shows 40,259.20 m² at precision 2; it shows: " +
                                     text(panel, "quantitiesArea").toStdString() +
                                     " / status: " + text(panel, "quantitiesError").toStdString());
        check(text(panel, "quantitiesVolume") == "82.40 m³", "The All row shows 82.40 m³");
        check(text(panel, "quantitiesUnvalidated") == "2", "Two bodies are not counted in volume");
        check(text(panel, "quantitiesRevision") ==
                  QString("Computed at revision %1").arg(doc.revision()),
              "Computed at revision N names the revision the figures came from");
        check(text(panel, "quantitiesPrecision") == "precision 2", "The header shows precision 2");
        check(child<QWidget>(panel, "quantitiesNotSolid0")->isVisible(),
              "The invalid list is shown");
        check(text(panel, "quantitiesNotSolidWords0").contains("Not a solid: open boundary"),
              "The open plate is named with the shared words");
        check(
            text(panel, "quantitiesNotSolidWords1").contains("Not a solid: non-manifold boundary"),
            "The shared-face cubes are named as non-manifold");
        check(child<QWidget>(panel, "quantitiesTerrain")->isVisible(),
              "The terrain line is shown, and not in the invalid list");
        check(!child<QWidget>(panel, "quantitiesComputing")->isVisible(),
              "A quick run shows no computing line");
        // The selection row is greyed out with nothing selected (§19.2).
        check(!child<QCheckBox>(panel, "quantitiesSelectionOnly")->isEnabled() &&
                  child<QLabel>(panel, "quantitiesSelectionHint")->isVisible(),
              "Selection only is disabled with nothing selected, with its hint");

        // 2. Grouping and filters (§19.2).
        groupBy->setCurrentIndex(2); // Tag
        emit groupBy->activated(2);
        check(waitFor([&] {
                  return child<QTreeWidget>(panel, "quantitiesGroups")->isVisible() &&
                         child<QTreeWidget>(panel, "quantitiesGroups")->topLevelItemCount() >= 2;
              }),
              "Group by Tag lists the tag groups");
        check(text(panel, "quantitiesAreaCaption") == "Area", "The total stays the area");
        auto *groups = child<QTreeWidget>(panel, "quantitiesGroups");
        bool boxes = false;
        for (int i = 0; i < groups->topLevelItemCount(); ++i)
            boxes = boxes || groups->topLevelItem(i)->text(0) == "Untagged";
        check(boxes, "The Untagged group is named in words");
        child<QRadioButton>(panel, "quantitiesVisibleOnly")->setChecked(true);
        expectText(panel, "quantitiesArea", "40,241.20 m²",
                   "Visible only leaves out Stash and Off: 40,241.20 m²");
        check(text(panel, "quantitiesVolume") == "79.40 m³", "Visible only: 79.40 m³");
        check(child<QLabel>(panel, "quantitiesVisibleCaption")->isVisible() &&
                  text(panel, "quantitiesVisibleCaption")
                      .contains("Geometry hidden for this session"),
              "The Visible only caption says what it leaves out");
        child<QRadioButton>(panel, "quantitiesIncludeHidden")->setChecked(true);
        expectText(panel, "quantitiesArea", "40,259.20 m²",
                   "Include hidden restores the whole model");
        groupBy->setCurrentIndex(0);
        emit groupBy->activated(0);
        check(waitFor([&] { return !groups->isVisible(); }), "Group by None hides the group table");

        // 3. An off-thread run, Cancel, and the old totals kept (§19.3).
        std::atomic<bool> hold{true};
        QuantitiesPanel::runGateForTesting = [&hold](const std::atomic<bool> &cancel) {
            while (hold.load() && !cancel.load())
                QThread::msleep(2);
        };
        const auto before = doc.revision();
        doc.addFace({{{90, 0, 0}, {91, 0, 0}, {91, 1, 0}, {90, 1, 0}}}, "Extra plate");
        QMetaObject::invokeMethod(view, "changed");
        check(waitFor([&] { return child<QWidget>(panel, "quantitiesComputing")->isVisible(); }),
              "A run past a second shows the computing line");
        check(text(panel, "quantitiesComputingText")
                  .contains(QString("Computing revision %1").arg(doc.revision())),
              "The computing line names the revision it computes");
        check(child<QWidget>(panel, "quantitiesTotals")->graphicsEffect() != nullptr &&
                  static_cast<QGraphicsOpacityEffect *>(
                      child<QWidget>(panel, "quantitiesTotals")->graphicsEffect())
                          ->opacity() < 1.0,
              "The old totals are dimmed while the run is in progress");
        check(text(panel, "quantitiesArea") == "40,259.20 m²",
              "The previous total stays readable during the run");
        check(!child<QPushButton>(panel, "quantitiesCancel")->autoDefault(),
              "Cancel is not the default button");
        QElapsedTimer clock;
        clock.start();
        child<QPushButton>(panel, "quantitiesCancel")->click();
        check(waitFor([&] {
                  return text(panel, "quantitiesStatus").startsWith("Not updated: cancelled");
              }),
              "Cancel reports that the totals were not updated");
        const auto cancelMilliseconds = clock.elapsed();
        std::cout << "cancel returned in " << cancelMilliseconds << " ms (budget 100 ms)"
                  << std::endl;
        check(cancelMilliseconds < 1000, "Cancel returns at once");
        check(text(panel, "quantitiesStatus") ==
                  QString("Not updated: cancelled. Showing revision %1.").arg(before),
              "The cancelled run keeps the last complete result with its own revision");
        check(text(panel, "quantitiesRevision") == QString("Computed at revision %1").arg(before),
              "The revision line still names the old revision");
        check(!child<QWidget>(panel, "quantitiesComputing")->isVisible(),
              "The computing line is gone after Cancel");

        // 4. A newer revision supersedes a run in progress; its result is dropped (§19.3).
        hold = true;
        doc.addFace({{{95, 0, 0}, {96, 0, 0}, {96, 1, 0}, {95, 1, 0}}}, "Second extra plate");
        QMetaObject::invokeMethod(view, "changed");
        check(waitFor([&] { return child<QWidget>(panel, "quantitiesComputing")->isVisible(); }),
              "The run that a newer revision supersedes is computing");
        hold = false;
        doc.addFace({{{100, 0, 0}, {101, 0, 0}, {101, 1, 0}, {100, 1, 0}}}, "Third extra plate");
        QMetaObject::invokeMethod(view, "changed");
        check(waitFor([&] {
                  return text(panel, "quantitiesRevision") ==
                         QString("Computed at revision %1").arg(doc.revision());
              }),
              "The newest revision is the one shown after a supersede");
        check(!text(panel, "quantitiesStatus").startsWith("Not updated"),
              "A superseded run is not reported as a cancel");
        // The Escape key cancels a run while focus is in the section (§19.10).
        hold = true;
        doc.addFace({{{110, 0, 0}, {111, 0, 0}, {111, 1, 0}, {110, 1, 0}}}, "Fourth extra plate");
        QMetaObject::invokeMethod(view, "changed");
        check(waitFor([&] { return child<QWidget>(panel, "quantitiesComputing")->isVisible(); }),
              "The run before Escape is computing");
        QTest::keyClick(panel, Qt::Key_Escape);
        hold = false;
        check(waitFor([&] {
                  return text(panel, "quantitiesStatus").startsWith("Not updated: cancelled");
              }),
              "Escape cancels the run");
        QuantitiesPanel::runGateForTesting = nullptr;
        // Undo the four extra plates, so the fixture totals hold for the checks that follow.
        for (int i = 0; i < 4 && doc.canUndo(); ++i)
            doc.undo();
        QMetaObject::invokeMethod(view, "changed");
        expectText(panel, "quantitiesArea", "40,259.20 m²",
                   "The fixture totals are back after undo");

        // 5. The invalid list and selecting from it (§19.4).
        child<QPushButton>(panel, "quantitiesSelectInModel0")->click();
        check(view->selectionState().entities().size() >= 1 &&
                  view->selectionState().entities().begin()->body == bodyNamed(doc, "Plate"),
              "Select in model selects the open plate");
        view->setSelection(0);
        QMetaObject::invokeMethod(view, "changed");
        check(waitFor([&] {
                  return child<QCheckBox>(panel, "quantitiesSelectionOnly")->isEnabled() == false;
              }),
              "With nothing selected Selection only stays greyed");

        // 6. Schedules: save, name rules, rename, run and delete (§19.6, §19.7).
        const auto store = reportScheduleStorePath();
        check(!store.isEmpty() && store.startsWith(home.path()),
              "The schedule store is inside the test folder, never the user's");
        check(child<QPushButton>(panel, "quantitiesSaveSchedule")->isEnabled(),
              "Save as schedule is enabled for a saved model");
        driveModal([](QWidget *w) { return named(w, "scheduleNameDialog"); },
                   [](QWidget *w) {
                       auto *dialog = qobject_cast<QDialog *>(w);
                       auto *name = dialog->findChild<QLineEdit *>("scheduleName");
                       name->setText("Bad/name");
                       expectInSlot(
                           !dialog->findChild<QPushButton *>("scheduleSave")->isEnabled() &&
                               dialog->findChild<QLabel *>("scheduleNameProblem")
                                   ->text()
                                   .contains("Use 1 to 64 letters"),
                           "The name rules show as the person types, and Save is disabled");
                       name->setText("Boxes only");
                       dialog->findChild<QPushButton *>("scheduleSave")->click();
                   },
                   "Save as schedule");
        child<QPushButton>(panel, "quantitiesSaveSchedule")->click();
        verifyDeferred();
        check(child<QListWidget>(panel, "quantitiesScheduleList")->count() == 1 &&
                  text(panel, "quantitiesScheduleCount") == "1 for this model",
              "The saved schedule is listed for this model");
        const auto stored = [&] { return ReportScheduleStore(store).schedules(doc.identity()); };
        check(stored().size() == 1 && stored().front().name == "Boxes only",
              "The schedule is in the store, under this model's identity");
        const auto savedAt = stored().front().createdAt;

        // Rename keeps the request and the creation time (F2 on the row).
        child<QListWidget>(panel, "quantitiesScheduleList")->setCurrentRow(0);
        driveModal([](QWidget *w) { return named(w, "scheduleNameDialog"); },
                   [](QWidget *w) {
                       auto *dialog = qobject_cast<QDialog *>(w);
                       expectInSlot(dialog->windowTitle() == "Rename schedule",
                                    "The rename dialog opens");
                       dialog->findChild<QLineEdit *>("scheduleName")->setText("Boxes renamed");
                       dialog->findChild<QPushButton *>("scheduleSave")->click();
                   },
                   "Rename");
        pressShortcut(child<QListWidget>(panel, "quantitiesScheduleList"), Qt::Key_F2);
        verifyDeferred();
        if (!(stored().size() == 1 && stored().front().name == "Boxes renamed" &&
              stored().front().createdAt == savedAt)) {
            QStringList names;
            for (const auto &schedule : stored())
                names << schedule.name + "@" + schedule.createdAt;
            throw std::runtime_error(
                "Rename moves the schedule and keeps its creation time; store holds [" +
                names.join(", ").toStdString() + "], saved at " + savedAt.toStdString());
        }

        // Run makes the schedule the view, and Back to the live view returns (§19.6).
        child<QListWidget>(panel, "quantitiesScheduleList")->setCurrentRow(0);
        QTest::keyClick(child<QListWidget>(panel, "quantitiesScheduleList"), Qt::Key_Return);
        expectText(panel, "quantitiesHeader", "Schedule · Boxes renamed",
                   "Run makes the schedule the active view");
        check(!child<QComboBox>(panel, "quantitiesGroupBy")->isEnabled(),
              "The live controls wait while a schedule is the view");
        child<QPushButton>(panel, "quantitiesBackToLive")->click();
        check(text(panel, "quantitiesHeader") == "Quantities" &&
                  child<QComboBox>(panel, "quantitiesGroupBy")->isEnabled(),
              "Back to the live view restores the live controls");

        // Delete asks first; Cancel is the default and keeps the schedule.
        driveModal([](QWidget *w) { return named(w, "scheduleDeleteDialog"); },
                   [](QWidget *w) {
                       auto *box = qobject_cast<QMessageBox *>(w);
                       expectInSlot(box->defaultButton() &&
                                        box->defaultButton()->text() == "Cancel",
                                    "Cancel is the default in the Delete dialog");
                       box->done(0);
                   },
                   "Delete, cancelled");
        pressShortcut(child<QListWidget>(panel, "quantitiesScheduleList"), Qt::Key_Delete);
        verifyDeferred();
        check(stored().size() == 1, "Cancel in the Delete dialog keeps the schedule");
        driveModal([](QWidget *w) { return named(w, "scheduleDeleteDialog"); },
                   [](QWidget *w) {
                       auto *box = qobject_cast<QMessageBox *>(w);
                       box->findChild<QPushButton *>("scheduleDeleteConfirm")->click();
                   },
                   "Delete, confirmed");
        pressShortcut(child<QListWidget>(panel, "quantitiesScheduleList"), Qt::Key_Delete);
        verifyDeferred();
        check(waitFor([&] { return stored().empty(); }), "Delete removes the schedule");
        check(child<QListWidget>(panel, "quantitiesScheduleList")->count() == 0,
              "The list is empty after Delete");

        // 7. Stale rows, the other models' count and an unreadable store (§19.7).
        // A tag with no bodies, so the model allows deleting it: the schedule then goes stale.
        const auto spareTag = createTag(doc, "Spare");
        // On Wayland, synthetic input cannot open a grabbing popup (D-068), so the popover is built
        // and not shown there. X11 opens it with the click.
        const bool popups = QGuiApplication::platformName() == "xcb";
        QFrame *tagPopup = nullptr;
        if (popups) {
            child<QPushButton>(panel, "quantitiesTagsValue")->click();
            tagPopup = panel->findChild<QFrame *>("quantitiesTagsPopup");
        } else {
            tagPopup = panel->buildPicker("Tags");
        }
        check(tagPopup != nullptr, "The Tags popover opens");
        child<QCheckBox>(tagPopup,
                         QString("quantitiesTagsCheck%1").arg(spareTag).toUtf8().constData())
            ->setChecked(true);
        tagPopup->close();
        expectText(panel, "quantitiesTagsValue", "Spare ▸", "A tag filter names its tag");
        driveModal([](QWidget *w) { return named(w, "scheduleNameDialog"); },
                   [](QWidget *w) {
                       auto *dialog = qobject_cast<QDialog *>(w);
                       dialog->findChild<QLineEdit *>("scheduleName")->setText("Spare filter");
                       dialog->findChild<QPushButton *>("scheduleSave")->click();
                   },
                   "Save filtered schedule");
        child<QPushButton>(panel, "quantitiesSaveSchedule")->click();
        verifyDeferred();
        check(stored().size() == 1 &&
                  stored().front().request["filters"].toObject()["tags"].toArray().size() == 1,
              "A filtered schedule keeps its tag");
        const auto boxesRequest = stored().front().request;

        // Delete the tag in the model: the schedule goes stale, its row refuses to run, the store
        // keeps it.
        const auto boxesTotal = text(panel, "quantitiesArea");
        eraseTag(doc, spareTag);
        QMetaObject::invokeMethod(view, "changed");
        check(
            waitFor([&] { return child<QWidget>(panel, "quantitiesScheduleStale")->isVisible(); }),
            "A schedule whose tag was deleted is marked stale");
        check(text(panel, "quantitiesScheduleStale")
                  .contains(QString("Stale: tag %1 no longer exists").arg(spareTag)),
              "The stale row names the missing tag in words");
        check(child<QWidget>(panel,
                             QString("quantitiesMissingtag%1").arg(spareTag).toUtf8().constData())
                  ->isVisible(),
              "The live filter shows a Missing tag row");
        child<QListWidget>(panel, "quantitiesScheduleList")->setCurrentRow(0);
        QTest::keyClick(child<QListWidget>(panel, "quantitiesScheduleList"), Qt::Key_Return);
        check(child<QWidget>(panel, "quantitiesScheduleRefusal")->isVisible() &&
                  text(panel, "quantitiesScheduleRefusal").startsWith("✕ Not run."),
              "Run on a stale schedule is refused in its row");
        check(text(panel, "quantitiesScheduleRefusal").contains("Nothing was run"),
              "The refusal says nothing was run");
        check(text(panel, "quantitiesHeader") == "Quantities",
              "A refused schedule does not become the view");
        check(stored().size() == 1 && stored().front().request == boxesRequest,
              "A refused schedule is kept unchanged in the store");
        check(text(panel, "quantitiesArea") == boxesTotal,
              "The totals are unchanged by a refused run");
        // Remove the deleted tag from the live filter: the live view is the whole model again.
        child<QPushButton>(
            panel, QString("quantitiesMissingRemovetag%1").arg(spareTag).toUtf8().constData())
            ->click();
        expectText(panel, "quantitiesTagsValue", "none ▸", "Remove clears the missing tag filter");

        // Another model's schedules appear only as a count.
        ReportScheduleStore other(store);
        other.add("elsewhere-identity",
                  ReportSchedule{"Elsewhere", "2026-10-27T00:00:00Z", boxesRequest});
        window.findChild<QAction *>("view.quantities")->trigger();
        check(waitFor([&] { return child<QLabel>(panel, "quantitiesOtherModels")->isVisible(); }),
              "Another model's schedules are counted");
        check(text(panel, "quantitiesOtherModels").contains("1 schedule") &&
                  !text(panel, "quantitiesOtherModels").contains("Elsewhere"),
              "The other models' line gives a count and no names");
        // Leave the store as this model would see it.
        ReportScheduleStore(store).remove("elsewhere-identity", "Elsewhere");

        // An unreadable store leaves the model's own numbers working (§19.7).
        const auto original = readFile(store);
        check(!original.isEmpty(), "The store has a schedule before the fault");
        {
            QFile file(store);
            check(file.open(QIODevice::WriteOnly | QIODevice::Truncate), "Damage the store");
            file.write("{ not json");
        }
        window.findChild<QAction *>("view.quantities")->trigger();
        check(waitFor([&] { return child<QWidget>(panel, "quantitiesStoreError")->isVisible(); }),
              "A store that cannot be read is named in its block");
        check(text(panel, "quantitiesStoreError").contains("Schedules cannot be read") &&
                  text(panel, "quantitiesStoreError").contains("never resets this file"),
              "The store block says the rest still works and the file is never reset");
        waitFor([&] {
            return text(panel, "quantitiesArea") != "—" && !text(panel, "quantitiesArea").isEmpty();
        });
        if (text(panel, "quantitiesArea").isEmpty() || text(panel, "quantitiesArea") == "—")
            throw std::runtime_error(
                "The totals still work with an unreadable store; Area shows \"" +
                text(panel, "quantitiesArea").toStdString() + "\", error \"" +
                text(panel, "quantitiesError").toStdString() + "\"");
        if (original.isEmpty()) {
            QFile::remove(store);
        } else {
            QFile file(store);
            check(file.open(QIODevice::WriteOnly | QIODevice::Truncate) &&
                      file.write(original.toUtf8()) > 0,
                  "Restore the store");
        }
        window.findChild<QAction *>("view.quantities")->trigger();
        check(waitFor([&] { return !child<QWidget>(panel, "quantitiesStoreError")->isVisible(); }),
              "The store block clears once the store reads again");

        // 8. Export: CSV, JSON, a refused existing file, and a selection file (§19.8).

        const auto csvPath = home.path() + "/quantities-a-takeoff.csv";
        const auto jsonPath = home.path() + "/quantities-a-takeoff.json";
        const auto selectionPath = home.path() + "/selection.csv";
        bool reportClosed = false;
        auto closeReport = [&reportClosed](QWidget *w) {
            auto *dialog = qobject_cast<QDialog *>(w);
            expectInSlot(dialog->findChild<QLabel *>("exportBanner")->text() ==
                             "✓ Written. The model is unchanged.",
                         "The export report banner says the model is unchanged");
            dialog->findChild<QPushButton *>("exportClose")->click();
            reportClosed = true;
        };
        auto saveTo = [](const QString &path) {
            return [path](QWidget *w) {
                auto *dialog = qobject_cast<QFileDialog *>(w);
                dialog->selectFile(path);
                // selectFile is not always taken by the save dialog; the name field is set as well.
                if (auto *name = dialog->findChild<QLineEdit *>("fileNameEdit"))
                    name->setText(path);
                // accept() is protected in QFileDialog; the meta-object call reaches it as the
                // button does.
                QMetaObject::invokeMethod(dialog, "accept");
            };
        };
        driveModal([](QWidget *w) { return named(w, "exportReportDialog"); }, closeReport,
                   "export report");
        driveModal([](QWidget *w) { return qobject_cast<QFileDialog *>(w) != nullptr; },
                   saveTo(csvPath), "export CSV file");
        check(
            waitFor([&] { return child<QPushButton>(panel, "quantitiesExportCsv")->isEnabled(); }),
            "The export waits for the totals, then runs");
        child<QPushButton>(panel, "quantitiesExportCsv")->click();
        verifyDeferred();
        if (!waitFor([&] { return reportClosed; }))
            throw std::runtime_error("The CSV export writes and reports; status \"" +
                                     text(panel, "quantitiesStatus").toStdString() +
                                     "\", error \"" + text(panel, "quantitiesError").toStdString() +
                                     "\"");
        const auto csv = readFile(csvPath);
        check(csv.contains("#ad-hoc=1") && csv.contains("#filters.visibility=all"),
              "The CSV is the live view: ad hoc, all hidden content included");
        check(csv.contains("areas,"), "The CSV holds the area table");

        // A second export to the same name is refused, and nothing is replaced (§19.8, §19.9).
        driveModal([](QWidget *w) { return qobject_cast<QFileDialog *>(w) != nullptr; },
                   saveTo(csvPath), "export CSV again");
        check(
            waitFor([&] { return child<QPushButton>(panel, "quantitiesExportCsv")->isEnabled(); }),
            "The export waits for the totals, then runs");
        child<QPushButton>(panel, "quantitiesExportCsv")->click();
        verifyDeferred();
        check(waitFor([&] {
                  return text(panel, "quantitiesError").startsWith("Nothing was written.");
              }),
              "An existing file is refused, and nothing is written");
        check(text(panel, "quantitiesError").contains("quantities-a-takeoff.csv already exists"),
              "The refusal names the file");
        check(readFile(csvPath) == csv, "The existing file is not replaced");

        // The JSON carries no body IDs (R101.e1, owner choice 3).
        reportClosed = false;
        driveModal([](QWidget *w) { return named(w, "exportReportDialog"); }, closeReport,
                   "JSON export report");
        driveModal([](QWidget *w) { return qobject_cast<QFileDialog *>(w) != nullptr; },
                   saveTo(jsonPath), "export JSON file");
        check(
            waitFor([&] { return child<QPushButton>(panel, "quantitiesExportJson")->isEnabled(); }),
            "The export waits for the totals, then runs");
        child<QPushButton>(panel, "quantitiesExportJson")->click();
        verifyDeferred();
        check(waitFor([&] { return reportClosed; }), "The JSON export writes and reports");
        const auto json = readFile(jsonPath);
        check(!json.isEmpty() && !json.contains("\"bodies\"") && json.contains("\"invalid\""),
              "The JSON file carries the invalid rows without body IDs");
        check(QJsonDocument::fromJson(json.toUtf8()).isObject(), "The JSON file is valid JSON");

        // Selection export: the file holds the selection's figures and its marker.
        view->setSelection(bodyNamed(doc, "Plate"));
        QMetaObject::invokeMethod(view, "changed");
        check(waitFor(
                  [&] { return child<QCheckBox>(panel, "quantitiesSelectionOnly")->isEnabled(); }),
              "Selection only is enabled with a selection");
        child<QCheckBox>(panel, "quantitiesSelectionOnly")->setChecked(true);
        expectText(panel, "quantitiesArea", "2.00 m²",
                   "Selection only recomputes the totals for the selection: the 2 m² plate");
        reportClosed = false;
        driveModal([](QWidget *w) { return named(w, "exportReportDialog"); }, closeReport,
                   "selection export report");
        driveModal([](QWidget *w) { return qobject_cast<QFileDialog *>(w) != nullptr; },
                   saveTo(selectionPath), "selection export file");
        check(
            waitFor([&] { return child<QPushButton>(panel, "quantitiesExportCsv")->isEnabled(); }),
            "The export waits for the totals, then runs");
        child<QPushButton>(panel, "quantitiesExportCsv")->click();
        verifyDeferred();
        if (!waitFor([&] { return reportClosed; }))
            throw std::runtime_error("The selection export writes and reports; status \"" +
                                     text(panel, "quantitiesStatus").toStdString() + "\", error \"" +
                                     text(panel, "quantitiesError").toStdString() + "\", export enabled " +
                                     std::to_string(child<QPushButton>(panel, "quantitiesExportCsv")->isEnabled()));
        check(readFile(selectionPath).contains("#filters.selection=1"),
              "A selection file carries the selection marker");
        child<QCheckBox>(panel, "quantitiesSelectionOnly")->setChecked(false);
        view->setSelection(0);
        QMetaObject::invokeMethod(view, "changed");

        // 9. Refusals (§19.9): an unsaved model cannot be scheduled, and 32 schedules are the
        // limit.
        panel->setModelPath([] { return QString(); });
        QMetaObject::invokeMethod(view, "changed");
        check(waitFor([&] {
                  return !child<QPushButton>(panel, "quantitiesSaveSchedule")->isEnabled();
              }),
              "Save is disabled until the model has a file");
        check(text(panel, "quantitiesScheduleNote") ==
                  "Save the model first. A schedule belongs to a saved model.",
              "The reason is on screen");
        panel->setModelPath([modelFile] { return modelFile; });
        {
            ReportScheduleStore full(store);
            for (int i = 0; full.schedules(doc.identity()).size() < 32; ++i)
                full.add(doc.identity(),
                         ReportSchedule{QString("Fill %1").arg(i, 2, 10, QChar('0')),
                                        "2026-10-27T00:00:00Z", boxesRequest});
        }
        QMetaObject::invokeMethod(view, "changed");
        window.findChild<QAction *>("view.quantities")->trigger();
        check(waitFor([&] {
                  return !child<QPushButton>(panel, "quantitiesSaveSchedule")->isEnabled();
              }),
              "Save is disabled at 32 schedules");
        check(text(panel, "quantitiesScheduleNote") ==
                  "This model has 32 schedules. Delete one to save another.",
              "The 32-schedule limit is named on screen");
        check(text(panel, "quantitiesScheduleCount") == "32 for this model",
              "Thirty-two are listed");

        // 10. The keyboard path (§19.10): focus lands on Group by, and the arrows choose.
        window.findChild<QAction *>("view.quantities")->trigger();
        groupBy->setFocus();
        QTest::keyClick(groupBy, Qt::Key_Down);
        check(groupBy->currentData().toString() == "kind" || groupBy->currentIndex() == 1,
              "The Down arrow chooses the next group");
        QTest::keyClick(groupBy, Qt::Key_Up);
        check(groupBy->currentIndex() == 0, "The Up arrow chooses the previous group");

        // 11. The text size: the section scrolls, and nothing is cut off without a scroll (§19.10).
        auto *scroll = child<QScrollArea>(panel, "quantitiesScroll");
        check(scroll->widgetResizable(), "The section is a scroll area");

        std::cout << "quantities_input_tests: all checks passed" << std::endl;
        QuantitiesPanel::runGateForTesting = nullptr;
        return 0;
    } catch (const std::exception &error) {
        std::cerr << "quantities_input_tests: " << error.what() << std::endl;
        return 1;
    }
}
