// R100.f3 (ADR 0174 sections 8 and 10, UX_DESIGN 18.1, 18.5 to 18.7): the Asset catalogs dialog,
// its missing-dependency report and the Export asset catalog dialog, driven the way a person drives
// them. The File menu opens each one. The library folder, search, the Kind filter and Open catalog
// file list entries; Insert is off with its reason when a selected entry cannot go in; one insert
// is one undo step; a refused insert opens the report, whose row actions change the selection. The
// catalog cases are separate test functions, so R100.f2 can add its style cases to this suite
// beside them.
#include "app/catalog_dialog.hpp"
#include "app/catalog_export_dialog.hpp"
#include "app/catalog_report.hpp"
#include "app/surface_format.hpp"
#include "app/viewport.hpp"
#include "app/window.hpp"
#include "catalog_fixtures.hpp"
#include "core/components.hpp"
#include "io/catalog_io.hpp"
#include "io/library_bundle.hpp"
#include "app/style_dialogs.hpp"
#include "app/styles_panel.hpp"
#include "core/assets.hpp"
#include "io/assets.hpp"
#include "io/model_style_io.hpp"
#include "io/style_bundle.hpp"
#include "io/texture_image.hpp"

#include <QAccessible>
#include <QAction>
#include <QApplication>
#include <QCheckBox>
#include <QClipboard>
#include <QComboBox>
#include <QDialog>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QItemSelectionModel>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSettings>
#include <QSplitter>
#include <QTemporaryDir>
#include <QTest>
#include <QThread>
#include <QTimer>
#include <QProgressBar>
#include <QGuiApplication>
#include <QWindow>
#include <QTreeWidget>
#include <QTreeWidgetItem>
#include <QtGui/qguiapplication_platform.h>

#include <atomic>
#include <exception>
#include <functional>
#include <iostream>
#include <memory>
#include <wayland-client.h>

#include <QAbstractButton>
#include <QCheckBox>
#include <QComboBox>
#include <QCryptographicHash>
#include <QElapsedTimer>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRadioButton>
#include <QSurfaceFormat>
#include <QTemporaryDir>
#include <QtEndian>
#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

using namespace sketchy;

namespace {

void check(bool ok, const char *message) {
    if (!ok)
        throw std::runtime_error(message);
}

void checkText(const QString &actual, const QString &expected, const char *message) {
    if (!actual.contains(expected))
        throw std::runtime_error(std::string(message) + ": expected \"" + expected.toStdString() +
                                 "\" in \"" + actual.toStdString() + "\"");
}

QWidget *focused() { return QApplication::focusWidget(); }

// Presses Tab until `target` has focus, as a keyboard user reaches a control.
bool tabTo(QWidget *target, int limit = 120) {
    for (int step = 0; step < limit; ++step) {
        if (focused() == target)
            return true;
        QTest::keyClick(focused() ? focused() : target, Qt::Key_Tab);
    }
    return focused() == target;
}

// Makes the window of `widget` active and gives `widget` focus. Offscreen, nothing gives focus back
// after a modal dialog closes, so the test does it.
void refocus(QWidget *widget) {
    QT_WARNING_PUSH
    QT_WARNING_DISABLE_DEPRECATED
    QApplication::setActiveWindow(widget->window());
    QT_WARNING_POP
    widget->window()->activateWindow();
    widget->setFocus(Qt::OtherFocusReason);
    check(QTest::qWaitFor([&] { return focused() == widget; }, 2000), "Focus reaches the control");
}

// Every control a Tab stop reaches has an accessible name (the audit's rule).
void checkNamed(QWidget &dialog) {
    for (auto *widget : dialog.findChildren<QWidget *>()) {
        if (!widget->isVisibleTo(&dialog) || !widget->isEnabled() ||
            !(widget->focusPolicy() & Qt::TabFocus))
            continue;
        auto *accessible = QAccessible::queryAccessibleInterface(widget);
        const auto name = accessible ? accessible->text(QAccessible::Name) : QString();
        check(!name.trimmed().isEmpty(),
              ("Unnamed control: " + widget->objectName()).toUtf8().constData());
    }
}

// Chooses `path` in a file chooser the way a person does: the file's full path is typed into the
// chooser's file name field, and Open or Save is pressed. A selection made from the chooser's list
// is not used, because the list is filled asynchronously and differs between platforms.
void chooseFile(QDialog &chooser, const QString &path) {
    auto *picker = qobject_cast<QFileDialog *>(&chooser);
    check(picker, "A file chooser is open");
    auto *name = picker->findChild<QLineEdit *>("fileNameEdit");
    check(name, "The chooser has a file name field");
    name->setText(QFileInfo(path).absoluteFilePath());
    QMetaObject::invokeMethod(picker, "accept");
    check(picker->selectedFiles().contains(QFileInfo(path).absoluteFilePath()),
          "The chooser returns the typed file");
}

// Chooses the folder `path` in a folder chooser: the chooser shows it, and nothing is selected.
void chooseFolder(QDialog &chooser, const QString &path) {
    auto *picker = qobject_cast<QFileDialog *>(&chooser);
    check(picker, "A folder chooser is open");
    picker->setDirectory(path);
    chooser.done(QDialog::Accepted);
}

QAction *actionNamed(QWidget &window, const char *name) {
    auto *found = window.findChild<QAction *>(name);
    check(found, name);
    return found;
}

template <class T> T *child(QWidget &parent, const char *name) {
    auto *found = parent.findChild<T *>(name);
    check(found, name);
    return found;
}

// Runs `act` on the modal dialog that `open` shows. `open` returns when that dialog closes. `act`
// may close the dialog itself; if it does not, the helper rejects it. A failure in `act` is kept
// and thrown again after the dialog has closed, so it fails this test.
void modal(const std::function<void()> &open, const std::function<void(QDialog &)> &act,
           bool closeAfter = true) {
    std::exception_ptr failure;
    QTimer::singleShot(0, [&] {
        auto *dialog = qobject_cast<QDialog *>(QApplication::activeModalWidget());
        try {
            check(dialog, "A modal dialog is open");
            act(*dialog);
        } catch (...) {
            failure = std::current_exception();
        }
        // A failed check closes the dialog too, so the failure is reported rather than left open.
        if (dialog && dialog->isVisible() && (closeAfter || failure))
            dialog->reject();
    });
    open();
    if (failure)
        std::rethrow_exception(failure);
}

// Like modal(), for a dialog that opens only when a background insert has ended. `open` starts the
// insert and returns when the insert has settled, as with modal(). `act` runs on the first modal
// dialog that is not `parent`, which a poll finds however late it opens, so a report that the insert
// opens is driven too. A failure in `act` is thrown again after `open` has returned.
void modalAfter(QDialog &parent, const std::function<void()> &open,
                const std::function<void(QDialog &)> &act) {
    std::exception_ptr failure;
    QTimer poll;
    poll.setInterval(5);
    QObject::connect(&poll, &QTimer::timeout, [&] {
        auto *dialog = qobject_cast<QDialog *>(QApplication::activeModalWidget());
        if (!dialog || dialog == &parent)
            return;
        poll.stop();
        try {
            act(*dialog);
        } catch (...) {
            failure = std::current_exception();
        }
        if (dialog->isVisible())
            dialog->reject();
    });
    poll.start();
    open();
    poll.stop();
    if (failure)
        std::rethrow_exception(failure);
}

// Schedules `act` on the modal dialog that opens after the current one returns. A failure is kept
// in `failure` for the caller to rethrow once the dialog has closed.
void nextModal(const std::shared_ptr<std::exception_ptr> &failure,
               const std::function<void(QDialog &)> &act) {
    QTimer::singleShot(0, [failure, act] {
        auto *dialog = qobject_cast<QDialog *>(QApplication::activeModalWidget());
        try {
            check(dialog, "A modal dialog is open");
            act(*dialog);
        } catch (...) {
            *failure = std::current_exception();
        }
        if (dialog && dialog->isVisible())
            dialog->reject();
    });
}

// The Asset catalogs dialog, opened from the File menu, with `act` run inside it.
void catalogDialog(Window &window, const std::function<void(QDialog &)> &act,
                   bool closeAfter = true) {
    modal([&] { actionNamed(window, "file.assetCatalogs")->trigger(); }, act, closeAfter);
}

// The catalog list's top-level groups are catalogs; their rows are entries. Two catalogs may share
// a name, so a test that lists several finds a group by its position.
QTreeWidgetItem *group(QTreeWidget &tree, const QString &prefix) {
    for (int row = 0; row < tree.topLevelItemCount(); ++row)
        if (tree.topLevelItem(row)->text(0).startsWith(prefix))
            return tree.topLevelItem(row);
    return nullptr;
}

// The row of entry `name` in a group, found by the group's title prefix.
QTreeWidgetItem *entryRow(QTreeWidget &tree, const QString &catalog, const QString &name) {
    auto *parent = group(tree, catalog);
    check(parent, ("A catalog group: " + catalog.toStdString()).c_str());
    for (int row = 0; row < parent->childCount(); ++row)
        if (parent->child(row)->text(0).startsWith(name + QStringLiteral(" · ")))
            return parent->child(row);
    return nullptr;
}

// The row of entry `name` in the group at `index`, for lists with two catalogs of one name.
QTreeWidgetItem *entryRowAt(QTreeWidget &tree, int index, const QString &name) {
    check(index < tree.topLevelItemCount(), "A catalog group at this position");
    auto *parent = tree.topLevelItem(index);
    for (int row = 0; row < parent->childCount(); ++row)
        if (parent->child(row)->text(0).startsWith(name + QStringLiteral(" · ")))
            return parent->child(row);
    return nullptr;
}

int countEntryRows(QTreeWidget &tree) {
    int count = 0;
    for (int row = 0; row < tree.topLevelItemCount(); ++row)
        count += tree.topLevelItem(row)->childCount();
    return count;
}

// Selects exactly the named rows of the catalog group, and no other row.
void selectOnly(QTreeWidget &tree, const QStringList &entries, const QString &catalog) {
    tree.clearSelection();
    for (const auto &name : entries) {
        auto *row = entryRow(tree, catalog, name);
        check(row, name.toUtf8().constData());
        row->setSelected(true);
    }
}

void writeFile(const QString &path, const QByteArray &bytes) {
    QFile file(path);
    check(file.open(QIODevice::WriteOnly | QIODevice::Truncate), "Write a fixture file");
    file.write(bytes);
}

QString libraryFolder() { return catalogLibraryFolder(); }

// Empties the library, forgets a chosen folder and fills the library with the named fixtures, each
// written as `<fixture>.sketchycat`.
void resetLibrary(const QTemporaryDir &files, const QMap<QString, QByteArray> &fixtures,
                  const QStringList &catalogs) {
    QSettings("SketchyUp", "SketchyUp").remove("catalogFolder");
    QDir(libraryFolder()).removeRecursively();
    QDir().mkpath(libraryFolder());
    for (const auto &name : catalogs)
        writeFile(libraryFolder() + "/" + name + ".sketchycat", fixtures.value(name));
    QDir(files.path() + "/outside").removeRecursively();
    QDir().mkpath(files.path() + "/outside");
}

// The title of the basic fixture's group: five entries, version 1.0.0.
const QString kBasic = QStringLiteral("Fixture catalog · 1.0.0 · 5 entries");
const QString kEdge = QStringLiteral("Fixture catalog · 1.0.0 · 3 entries");

qsizetype history(Window &window) { return window.document().history().position; }

QAction *undo(Window &window) { return actionNamed(window, "edit.undo"); }

QString lastLabel(Window &window) {
    const auto page = window.document().history();
    return page.entries.empty() ? QString() : QString::fromStdString(page.entries.back().label);
}

QString statusText(Window &window) { return child<QLabel>(window, "statusMessage")->text(); }

// ---------------------------------------------------------------------------------------------

// §18.1: Asset catalogs… after Import terrain…, and Export asset catalog… after Export STEP….
// Nothing else moves, and neither has a default shortcut.
void fileMenuEntries(Window &window) {
    QMenu *file = nullptr;
    for (auto *menu : window.menuBar()->findChildren<QMenu *>())
        if (menu->title() == "&File")
            file = menu;
    check(file, "The File menu");
    QStringList texts;
    for (auto *action : file->actions())
        texts << action->text();
    const auto at = [&](const QString &text) {
        const auto index = texts.indexOf(text);
        check(index >= 0, ("A File menu entry: " + text.toStdString()).c_str());
        return index;
    };
    check(at("Asset catalogs…") == at("Import terrain…") + 1,
          "Asset catalogs… follows Import terrain…");
    check(at("Export asset catalog…") == at("Export STEP…") + 1,
          "Export asset catalog… follows Export STEP…");
    check(actionNamed(window, "file.assetCatalogs")->shortcut().isEmpty(),
          "Asset catalogs… has no default shortcut");
    check(actionNamed(window, "file.exportAssetCatalog")->shortcut().isEmpty(),
          "Export asset catalog… has no default shortcut");
}

// §18.1 and §18.8 gap 4: the folder starts at the default catalogs folder, and Choose folder… is
// remembered in preferences. A catalog in the chosen folder is listed, and an insert from it names
// the catalog by its bytes, since the batch reads only the default folder.
void libraryFolderRemembered(Window &window, const QTemporaryDir &files,
                             const QMap<QString, QByteArray> &fixtures) {
    resetLibrary(files, fixtures, {});
    const auto other = files.path() + "/shared";
    QDir().mkpath(other);
    writeFile(other + "/shared-basic.sketchycat", fixtures.value("basic"));
    catalogDialog(
        window,
        [&](QDialog &dialog) {
            check(child<QLabel>(dialog, "catalogFolder")->text() == libraryFolder(),
                  "The folder starts at the default catalogs folder");
            check(!QSettings("SketchyUp", "SketchyUp").contains("catalogFolder"),
                  "No folder is remembered before a choice");
            modal([&] { child<QPushButton>(dialog, "catalogChooseFolder")->click(); },
                  [&](QDialog &chooser) { chooseFolder(chooser, other); });
            check(QSettings("SketchyUp", "SketchyUp").value("catalogFolder").toString() ==
                      QDir(other).absolutePath(),
                  "Choose folder… is remembered in preferences");
            check(child<QLabel>(dialog, "catalogFolder")->text() == QDir(other).absolutePath(),
                  "The dialog shows the chosen folder");
            auto *list = child<QTreeWidget>(dialog, "catalogEntries");
            check(group(*list, kBasic), "The catalog in the chosen folder is listed");
            const auto before = window.document().definitions().size();
            selectOnly(*list, {"Block"}, kBasic);
            auto *insert = child<QPushButton>(dialog, "catalogInsert");
            check(insert->isEnabled(), "Insert is on for a component in the chosen folder");
            insert->click();
            check(window.document().definitions().size() == before + 1,
                  "The component from the chosen folder is inserted");
        },
        false);
    QSettings("SketchyUp", "SketchyUp").remove("catalogFolder");
}

// §18.5: the grouped entry list with its states and reasons, a refused catalog, and Refresh
// library.
void listsAndStates(Window &window, const QTemporaryDir &files,
                    const QMap<QString, QByteArray> &fixtures) {
    resetLibrary(files, fixtures, {});
    // Three catalogs, sorted by file name: the basic one, one whose material names a missing
    // texture, and one whose shared texture is tampered. The first two catalogs share a name.
    writeFile(libraryFolder() + "/a-basic.sketchycat", fixtures.value("basic"));
    writeFile(libraryFolder() + "/b-missing.sketchycat", fixtures.value("missing-dependency"));
    writeFile(libraryFolder() + "/c-tampered.sketchycat", fixtures.value("tampered-asset"));
    catalogDialog(window, [&](QDialog &dialog) {
        auto *list = child<QTreeWidget>(dialog, "catalogEntries");
        check(list->topLevelItemCount() == 3, "Three catalogs are listed, one group each");
        check(list->topLevelItem(0)->text(0) == kBasic, "The basic catalog's group is titled");
        check(list->topLevelItem(0)->childCount() == 5, "The basic catalog lists its five entries");
        check(entryRowAt(*list, 0, "Block")->text(1) == "✓ Insertable",
              "A component that can be inserted says so");
        check(entryRowAt(*list, 0, "Oak")->text(1) == "✓ Insertable",
              "A material that can be inserted says so");
        auto *template_ = entryRowAt(*list, 0, "Starting room");
        check(template_->text(1) == "✕ Not insertable", "A template is marked not insertable");
        checkText(template_->text(2),
                  "Templates open as a new model, and no command does that yet.",
                  "A template's reason");
        // A missing texture is named by its id, with the reason in words, never by colour alone.
        auto *missing = entryRowAt(*list, 1, "Oak");
        check(missing && missing->text(1) == "✕ Not insertable",
              "A missing texture is not insertable");
        checkText(missing->text(2), "Asset 9 is not in this catalog.",
                  "The missing texture's reason");
        // A tampered texture refuses both materials that use it.
        auto *tampered = entryRowAt(*list, 2, "Oak tint");
        check(tampered && tampered->text(1) == "✕ Not insertable", "A tampered texture is refused");
        checkText(entryRowAt(*list, 2, "Oak")->text(2),
                  "Asset 1 does not match its recorded SHA-256.", "The tampered texture's reason");
        // Refresh library lists a catalog added while the dialog is open, and keeps the others.
        writeFile(libraryFolder() + "/d-edge.sketchycat", fixtures.value("edge-texture"));
        child<QPushButton>(dialog, "catalogRefresh")->click();
        check(list->topLevelItemCount() == 4,
              "Refresh library lists the new catalog and keeps the rest");
        check(group(*list, kEdge) && entryRow(*list, kEdge, "Edge style"),
              "The new catalog's entries are listed");
    });
}

// §18.5: the search matches names, descriptions and labels, and the Kind filter narrows the list.
void searchAndKindFilter(Window &window, const QTemporaryDir &files,
                         const QMap<QString, QByteArray> &fixtures) {
    resetLibrary(files, fixtures, {"basic"});
    catalogDialog(window, [&](QDialog &dialog) {
        auto *list = child<QTreeWidget>(dialog, "catalogEntries");
        auto *search = child<QLineEdit>(dialog, "catalogSearch");
        auto *kind = child<QComboBox>(dialog, "catalogKind");
        check(countEntryRows(*list) == 5, "All five entries are listed with no filter");
        search->setText("tint");
        check(countEntryRows(*list) == 1 && entryRow(*list, kBasic, "Oak tint"),
              "A search matches a name");
        search->setText("textured oak");
        check(countEntryRows(*list) == 1 && entryRow(*list, kBasic, "Oak"),
              "A search matches a description");
        search->setText("nothing matches this");
        check(countEntryRows(*list) == 0 && group(*list, kBasic) == nullptr,
              "A search with no match hides the group");
        search->setText("fixture");
        check(countEntryRows(*list) == 5, "A search matches a catalog label");
        search->clear();
        kind->setCurrentIndex(3); // Style
        check(countEntryRows(*list) == 1 && entryRow(*list, kBasic, "Plain edges"),
              "The Kind filter shows only styles");
        kind->setCurrentIndex(4); // Template
        check(countEntryRows(*list) == 1 && entryRow(*list, kBasic, "Starting room"),
              "The Kind filter shows only templates");
        kind->setCurrentIndex(0);
        check(countEntryRows(*list) == 5, "All kinds shows every entry again");
    });
}

// §18.5: Open catalog file… reads one catalog outside the folder, through the bounded loader. A
// file that is not a catalog, or is over 130 MiB, is listed as refused, and nothing from it is
// listed.
void openCatalogFile(Window &window, const QTemporaryDir &files,
                     const QMap<QString, QByteArray> &fixtures) {
    resetLibrary(files, fixtures, {});
    const auto outside = files.path() + "/outside";
    writeFile(outside + "/edge.sketchycat", fixtures.value("edge-texture"));
    writeFile(outside + "/notes.txt", "Not a catalog.\n");
    QFile huge(outside + "/huge.sketchycat");
    check(huge.open(QIODevice::WriteOnly), "Create a large file");
    check(huge.resize(qint64(libraryBundleLimit) + 1), "Size a large file");
    huge.close();
    catalogDialog(window, [&](QDialog &dialog) {
        auto *list = child<QTreeWidget>(dialog, "catalogEntries");
        const auto openFile = [&](const QString &name) {
            modal([&] { child<QPushButton>(dialog, "catalogOpen")->click(); },
                  [&](QDialog &chooser) { chooseFile(chooser, outside + "/" + name); });
        };
        openFile("edge.sketchycat");
        check(group(*list, kEdge) && entryRow(*list, kEdge, "Edge style"),
              "An opened catalog is listed with its entries");
        openFile("notes.txt");
        auto *refused = group(*list, "notes.txt · could not be read");
        check(refused, "A file that is not a catalog is listed as refused");
        checkText(refused->child(0)->text(1), "✕ Refused", "The refused state is a word");
        checkText(refused->child(0)->text(2), "This file is not a catalog SketchyUp can read",
                  "The refusal says why");
        check(refused->child(0)->text(2).startsWith("Nothing is listed. This file is not a catalog"),
              "The refusal says first that nothing is listed");
        openFile("huge.sketchycat");
        auto *big = group(*list, "huge.sketchycat · could not be read");
        check(big, "A file over 130 MiB is listed as refused");
        checkText(big->child(0)->text(2), "130 MiB", "The refusal names the limit");
    });
}

// §18.5: the details pane shows the current row: an entry's kind, description, dependencies with
// their states and its placement; a catalog's metadata.
void detailsPane(Window &window, const QTemporaryDir &files,
                 const QMap<QString, QByteArray> &fixtures) {
    resetLibrary(files, fixtures, {"basic"});
    catalogDialog(window, [&](QDialog &dialog) {
        auto *list = child<QTreeWidget>(dialog, "catalogEntries");
        auto *details = child<QPlainTextEdit>(dialog, "catalogDetails");
        list->setCurrentItem(entryRow(*list, kBasic, "Block"));
        checkText(details->toPlainText(), "Block · Component · Insertable",
                  "The details name the entry");
        checkText(details->toPlainText(), "Placement: inserted at the model’s origin.",
                  "A component's details say where it goes");
        list->setCurrentItem(entryRow(*list, kBasic, "Oak"));
        checkText(details->toPlainText(), "✓ Asset 1 “Oak texture” · present",
                  "A material's dependency is shown with its state");
        checkText(details->toPlainText(), "licence LicenseRef-SketchyUp-Tests",
                  "The licence is shown");
        list->setCurrentItem(entryRow(*list, kBasic, "Starting room"));
        checkText(details->toPlainText(), "Templates open as a new model", "A template says why");
        list->setCurrentItem(group(*list, kBasic));
        checkText(details->toPlainText(), "Author: SketchyUp tests",
                  "A catalog's details name its author");
    });
}

// §18.5: Insert is off, not hidden, when a selected entry cannot be inserted, and its reason names
// the entry and the reason. Deselecting it turns Insert back on.
void insertOffWithReason(Window &window, const QTemporaryDir &files,
                         const QMap<QString, QByteArray> &fixtures) {
    resetLibrary(files, fixtures, {"missing-dependency"});
    catalogDialog(window, [&](QDialog &dialog) {
        auto *list = child<QTreeWidget>(dialog, "catalogEntries");
        auto *insert = child<QPushButton>(dialog, "catalogInsert");
        auto *reason = child<QLabel>(dialog, "catalogInsertReason");
        check(insert->text() == "Insert" && !insert->isEnabled(),
              "Insert is off with nothing selected");
        selectOnly(*list, {"Oak"}, kBasic);
        check(!insert->isEnabled(), "Insert is off for a selected entry that cannot be inserted");
        check(insert->isVisibleTo(&dialog), "Insert stays shown when it is off");
        checkText(reason->text(),
                  "Insert is off: “Oak” cannot be inserted. Asset 9 is not in this catalog.",
                  "The reason names the entry and why");
        checkText(reason->text(), "Select another entry to insert.", "The reason says what to do");
        checkText(insert->accessibleDescription(), "Insert is off: “Oak”",
                  "Insert's description is its reason");
        selectOnly(*list, {"Oak", "Block"}, kBasic);
        checkText(reason->text(), "Deselect it to insert the other 1.",
                  "The reason counts the others");
        selectOnly(*list, {"Block"}, kBasic);
        check(insert->isEnabled() && insert->text() == "Insert component", "Insert is on again");
        checkText(reason->text(), "Inserts “Block” at the model’s origin.",
                  "The placement is said");
    });
}

// §18.5: templates are listed and marked not insertable, with the reason.
void templatesRefused(Window &window, const QTemporaryDir &files,
                      const QMap<QString, QByteArray> &fixtures) {
    resetLibrary(files, fixtures, {"basic"});
    catalogDialog(window, [&](QDialog &dialog) {
        auto *list = child<QTreeWidget>(dialog, "catalogEntries");
        auto *insert = child<QPushButton>(dialog, "catalogInsert");
        selectOnly(*list, {"Starting room"}, kBasic);
        check(!insert->isEnabled(), "A template cannot be inserted");
        checkText(child<QLabel>(dialog, "catalogInsertReason")->text(),
                  "Templates open as a new model, and no command does that yet.",
                  "The template's refusal is the reason line");
    });
}

// §18.5, R100.d: several entries are one undo step named for the catalog, and the status bar names
// the count. One undo takes every record of the insert back.
void severalEntriesOneUndoStep(Window &window, const QTemporaryDir &files,
                               const QMap<QString, QByteArray> &fixtures) {
    resetLibrary(files, fixtures, {"basic"});
    const auto steps = history(window);
    const auto definitions = window.document().definitions().size();
    const auto materials = window.document().materials().size();
    const auto style = window.document().style();
    catalogDialog(
        window,
        [&](QDialog &dialog) {
            auto *list = child<QTreeWidget>(dialog, "catalogEntries");
            auto *insert = child<QPushButton>(dialog, "catalogInsert");
            selectOnly(*list, {"Block", "Oak", "Oak tint", "Plain edges"}, kBasic);
            check(insert->isEnabled() && insert->text() == "Insert 4 entries",
                  "Four entries insert together");
            insert->click();
        },
        false);
    check(history(window) == steps + 1, "Several entries are one undo step");
    check(lastLabel(window) == "Insert 4 entries from “Fixture catalog”",
          "The undo step is named for the catalog");
    check(window.document().definitions().size() == definitions + 1, "The component is inserted");
    check(window.document().materials().size() == materials + 2, "Both materials are inserted");
    checkText(statusText(window), "Inserted 4 entries at the model’s origin.",
              "The status bar names the count");
    undo(window)->trigger();
    check(history(window) == steps, "Undo removes the insert");
    check(window.document().definitions().size() == definitions, "Undo removes the component");
    check(window.document().materials().size() == materials, "Undo removes both materials");
    check(window.document().style() == style, "Undo restores the style");
}

// §18.5: one component is one undo step, named for the component.
void oneComponentUndoStep(Window &window, const QTemporaryDir &files,
                          const QMap<QString, QByteArray> &fixtures) {
    resetLibrary(files, fixtures, {"basic"});
    const auto steps = history(window);
    const auto definitions = window.document().definitions().size();
    catalogDialog(
        window,
        [&](QDialog &dialog) {
            auto *list = child<QTreeWidget>(dialog, "catalogEntries");
            selectOnly(*list, {"Block"}, kBasic);
            child<QPushButton>(dialog, "catalogInsert")->click();
        },
        false);
    check(history(window) == steps + 1, "One component is one undo step");
    check(lastLabel(window) == "Insert component “Block”", "The undo step names the component");
    check(window.document().definitions().size() == definitions + 1, "The component is inserted");
    undo(window)->trigger();
    check(window.document().definitions().size() == definitions, "Undo removes the component");
}

// §18.5, §18.7: the list is one Tab stop. Space toggles the current row, Ctrl+A selects every
// insertable entry and not the refused one, Enter in the search field does nothing, and Escape
// closes the dialog.
void keyboardReachability(Window &window, const QTemporaryDir &files,
                          const QMap<QString, QByteArray> &fixtures) {
    resetLibrary(files, fixtures, {"basic"});
    const auto steps = history(window);
    catalogDialog(
        window,
        [&](QDialog &dialog) {
            auto *list = child<QTreeWidget>(dialog, "catalogEntries");
            auto *search = child<QLineEdit>(dialog, "catalogSearch");
            auto *insert = child<QPushButton>(dialog, "catalogInsert");
            checkNamed(dialog);
            refocus(search);
            check(tabTo(list), "Tab reaches the entry list");
            // Make the row current without selecting it: a keyboard user moves with the arrows.
            list->clearSelection();
            list->setCurrentItem(entryRow(*list, kBasic, "Block"), 0,
                                 QItemSelectionModel::NoUpdate);
            check(!entryRow(*list, kBasic, "Block")->isSelected(),
                  "Moving to a row does not select it");
            QTest::keyClick(list, Qt::Key_Space);
            check(entryRow(*list, kBasic, "Block")->isSelected(),
                  "Space selects the current entry");
            QTest::keyClick(list, Qt::Key_Space);
            check(!entryRow(*list, kBasic, "Block")->isSelected(),
                  "Space clears the current entry");
            QTest::keyClick(list, Qt::Key_A, Qt::ControlModifier);
            check(entryRow(*list, kBasic, "Block")->isSelected() &&
                      entryRow(*list, kBasic, "Oak")->isSelected() &&
                      entryRow(*list, kBasic, "Plain edges")->isSelected(),
                  "Ctrl+A selects the insertable entries");
            check(!entryRow(*list, kBasic, "Starting room")->isSelected(),
                  "Ctrl+A leaves the template out");
            check(insert->isEnabled() && insert->text() == "Insert 4 entries",
                  "The selection is insertable");
            refocus(search);
            QTest::keyClick(search, Qt::Key_Return);
            check(dialog.isVisible(), "Enter in the search field neither inserts nor closes");
            check(history(window) == steps, "Enter in the search field changes nothing");
            check(tabTo(insert), "Tab reaches Insert");
            QTest::keyClick(&dialog, Qt::Key_Escape);
            check(!dialog.isVisible(), "Escape closes the dialog");
        },
        false);
    check(history(window) == steps, "Escape changes nothing");
}

// §18.6 and §18.5: a refused insert opens the report. The file changed after it was listed, so the
// insert reads it again, and the report names the entry, its dependency and the reason. Select the
// other entries leaves the entries that can go in selected, and Remove leaves the rest selected.
void refusedInsertReport(Window &window, const QTemporaryDir &files,
                         const QMap<QString, QByteArray> &fixtures) {
    const auto steps = history(window);
    const auto definitions = window.document().definitions().size();

    // Select the other entries: Insert is then on for the entries that can go in.
    resetLibrary(files, fixtures, {"basic"});
    catalogDialog(window, [&](QDialog &dialog) {
        auto *list = child<QTreeWidget>(dialog, "catalogEntries");
        auto *insert = child<QPushButton>(dialog, "catalogInsert");
        selectOnly(*list, {"Block", "Oak tint"}, kBasic);
        check(insert->isEnabled(), "The selection is insertable as it was listed");
        // The catalog changes on disk after the listing: its shared texture is now tampered.
        writeFile(libraryFolder() + "/basic.sketchycat", fixtures.value("tampered-asset"));
        modalAfter(dialog, [&] { insert->click(); },
              [&](QDialog &report) {
                  check(report.objectName() == "catalogInsertReport",
                        "A refused insert opens the report");
                  checkNamed(report);
                  checkText(child<QLabel>(report, "catalogReportBanner")->text(),
                            "✕ Nothing was inserted", "The report says nothing was inserted");
                  checkText(child<QLabel>(report, "catalogReportSummary")->text(),
                            "Your model and the catalog file are unchanged.",
                            "The report says what is unchanged");
                  checkText(child<QLabel>(report, "catalogReportIdentity")->text(),
                            "0 + 1 + 1 = 2 ✓ balanced", "The accounting identity balances");
                  auto *tree = child<QTreeWidget>(report, "catalogReportTree");
                  auto *attention = group(*tree, "Needs your attention · 1");
                  check(attention, "One entry needs attention");
                  check(attention->childCount() == 1 &&
                            attention->child(0)->text(0) ==
                                "Oak tint · material · entry 3 — tampered texture",
                        "The refused row names the entry and the reason in words");
                  auto *row = attention->child(0);
                  check(row->childCount() == 2 &&
                            row->child(0)->text(0) == "Asset 1 “Oak texture” · tampered",
                        "The row names its dependency and the reason word");
                  checkText(row->child(1)->text(0), "Asset 1 does not match its recorded SHA-256.",
                            "The row explains the tampered texture");
                  auto *notInserted = group(*tree, "Not inserted · 1");
                  check(notInserted && notInserted->child(0)->text(0) ==
                                           "Block · component · entry 5 — refused with the request",
                        "An entry that can go in is not inserted with the refused request");
                  check(group(*tree, "Notices · 1"), "The notices are listed");
                  auto *others = child<QPushButton>(report, "catalogReportSelectOthers");
                  check(others->isEnabled(), "Select the other entries is on");
                  others->click();
              });
        check(insert->isEnabled() && insert->text() == "Insert component",
              "Select the other entries leaves the component selected and Insert on");
    });
    check(history(window) == steps, "A refused insert changes nothing");
    check(window.document().definitions().size() == definitions, "No component is inserted");

    // Remove from selection: the entry named is taken out, and the rest inserts.
    resetLibrary(files, fixtures, {"basic"});
    catalogDialog(
        window,
        [&](QDialog &dialog) {
            auto *list = child<QTreeWidget>(dialog, "catalogEntries");
            auto *insert = child<QPushButton>(dialog, "catalogInsert");
            selectOnly(*list, {"Block", "Oak tint"}, kBasic);
            writeFile(libraryFolder() + "/basic.sketchycat", fixtures.value("tampered-asset"));
            modalAfter(dialog, [&] { insert->click(); },
                  [&](QDialog &report) {
                      auto *remove = child<QPushButton>(report, "catalogReportRemove");
                      check(remove->isEnabled() &&
                                remove->text() == "Remove “Oak tint” from selection",
                            "The refused row offers to remove its entry");
                      remove->click();
                  });
            check(insert->isEnabled() && insert->text() == "Insert component",
                  "Removing the refused entry leaves the rest insertable");
            insert->click();
        },
        false);
    check(history(window) == steps + 1, "The rest inserts as one undo step");
    check(lastLabel(window) == "Insert component “Block”", "The undo step names the component");
    check(window.document().definitions().size() == definitions + 1, "The rest is inserted");
    undo(window)->trigger();
}

// ADR 0174 section 4 (R100.g F-1): a catalog opened with Open catalog file… gives the same Insert
// state and reason as the same file in the library, for each selection; and an insert from it is
// one undo step. The engine's dry run decides both (the dialog passes the catalog it listed).
void outsideAndLibraryInserts(Window &window, const QTemporaryDir &files,
                              const QMap<QString, QByteArray> &fixtures) {
    const std::vector<QStringList> selections{{"Oak tint"}, {"Oak"}, {"Oak", "Oak tint"}};
    // The Insert button's state and reason for each selection in turn.
    const auto readInserts = [&](QDialog &dialog) {
        auto *list = child<QTreeWidget>(dialog, "catalogEntries");
        auto *insert = child<QPushButton>(dialog, "catalogInsert");
        auto *reason = child<QLabel>(dialog, "catalogInsertReason");
        std::vector<QString> states;
        for (const auto &selection : selections) {
            selectOnly(*list, selection, kBasic);
            states.push_back(
                QStringLiteral("%1 | %2 | %3")
                    .arg(insert->isEnabled() ? QStringLiteral("on") : QStringLiteral("off"),
                         insert->text(), reason->text()));
        }
        return states;
    };

    resetLibrary(files, fixtures, {"missing-dependency"});
    std::vector<QString> library;
    catalogDialog(window, [&](QDialog &dialog) { library = readInserts(dialog); });

    resetLibrary(files, fixtures, {});
    const auto opened = files.path() + "/outside/missing-dependency.sketchycat";
    writeFile(opened, fixtures.value("missing-dependency"));
    const auto steps = history(window);
    const auto definitions = window.document().definitions().size();
    std::vector<QString> outside;
    catalogDialog(
        window,
        [&](QDialog &dialog) {
            modal([&] { child<QPushButton>(dialog, "catalogOpen")->click(); },
                  [&](QDialog &chooser) { chooseFile(chooser, opened); });
            auto *list = child<QTreeWidget>(dialog, "catalogEntries");
            check(group(*list, kBasic), "The opened catalog is listed");
            outside = readInserts(dialog);
            auto *insert = child<QPushButton>(dialog, "catalogInsert");
            selectOnly(*list, {"Block"}, kBasic);
            check(insert->isEnabled() && insert->text() == "Insert component",
                  "An entry of the opened catalog is insertable");
            insert->click();
        },
        false);

    check(outside.size() == library.size() && outside == library,
          "The opened and library forms give the same Insert state and reason for each selection");
    check(library[0].startsWith("on | "), "The insertable selection is on in the library form");
    check(library[1].startsWith("off | ") && library[1].contains("Asset 9 is not in this catalog"),
          "The refused selection is off, with its reason, in the library form");
    check(library[2].startsWith("off | "), "A selection with a refused entry is off");
    check(history(window) == steps + 1, "The insert from the opened catalog is one undo step");
    check(window.document().definitions().size() == definitions + 1,
          "The insert from the opened catalog adds its component");
    undo(window)->trigger();
    check(history(window) == steps, "Undo removes the insert from the opened catalog");
}

// §18.5, §18.7: the Export asset catalog dialog writes the chosen records as a new file. The model
// is not changed, nothing can be exported with nothing chosen, and an existing file is refused.
void exportAssetCatalog(Window &window, const QTemporaryDir &files) {
    const auto body = window.document().addFace({{{0, 0, 0}, {2, 0, 0}, {2, 1, 0}, {0, 1, 0}}});
    createComponent(window.document(), body, "Export block");
    window.viewport()->refresh();
    const auto target = files.path() + "/exported.sketchycat";
    QFile::remove(target);
    const auto steps = history(window);
    const auto setChecked = [](QListWidget &list, const QString &name, bool on) {
        for (int row = 0; row < list.count(); ++row)
            if (list.item(row)->text() == name)
                list.item(row)->setCheckState(on ? Qt::Checked : Qt::Unchecked);
    };
    modal([&] { actionNamed(window, "file.exportAssetCatalog")->trigger(); },
          [&](QDialog &dialog) {
              auto *components = child<QListWidget>(dialog, "catalogExportComponents");
              auto *materials = child<QListWidget>(dialog, "catalogExportMaterials");
              auto *style = child<QCheckBox>(dialog, "catalogExportStyle");
              auto *button = child<QPushButton>(dialog, "catalogExportButton");
              check(components->count() >= 1, "The model's components are offered");
              check(button->isEnabled(), "Export is on with records chosen");
              // Nothing chosen: Export is off, and its reason says what to choose.
              for (int row = 0; row < components->count(); ++row)
                  components->item(row)->setCheckState(Qt::Unchecked);
              for (int row = 0; row < materials->count(); ++row)
                  materials->item(row)->setCheckState(Qt::Unchecked);
              style->setChecked(false);
              check(!button->isEnabled(), "Export is off with nothing chosen");
              checkText(child<QLabel>(dialog, "catalogExportReason")->text(),
                        "Choose at least one component", "The reason says what to choose");
              // The new component and the style: a catalog of two entries.
              setChecked(*components, "Export block", true);
              style->setChecked(true);
              check(button->isEnabled(), "Export is on again");
              modal([&] { button->click(); },
                    [&](QDialog &chooser) { chooseFile(chooser, target); });
          },
          false);
    check(QFileInfo(target).exists(), "The catalog is written as a new file");
    const auto written = loadCatalog(target);
    check(written.entries.size() == 2, "The file holds the component and the style");
    for (const auto &entry : written.entries)
        check(entry.importable, "Every exported entry is importable");
    checkText(statusText(window),
              "Exported asset catalog “Asset catalog”: 2 entries. Your model is unchanged.",
              "The status bar says what was written");
    check(history(window) == steps, "Export changes no undo step");

    // Export again to the same name: the existing file is refused and nothing is written.
    QFile written_(target);
    check(written_.open(QIODevice::ReadOnly), "Read the written file");
    const auto existing = written_.readAll();
    written_.close();
    auto refusal = std::make_shared<std::exception_ptr>();
    modal([&] { actionNamed(window, "file.exportAssetCatalog")->trigger(); },
          [&](QDialog &dialog) {
              auto *components = child<QListWidget>(dialog, "catalogExportComponents");
              auto *button = child<QPushButton>(dialog, "catalogExportButton");
              setChecked(*components, "Export block", true);
              modal([&] { button->click(); },
                    [&](QDialog &chooser) {
                        chooseFile(chooser, target);
                        // The refusal opens once the chooser has closed; the check runs in it.
                        nextModal(refusal, [](QDialog &sheet) {
                            check(sheet.objectName() == "catalogExportRefused",
                                  "An existing file is refused");
                            checkText(child<QLabel>(sheet, "catalogExportFirst")->text(),
                                      "✕ Nothing was written.",
                                      "The refusal says first that nothing was written");
                            checkText(child<QLabel>(sheet, "catalogExportSummary")->text(),
                                      "That file already exists. An existing file is never "
                                      "replaced; choose a new name.",
                                      "The refusal uses the IFC export's words");
                        });
                    });
          });
    if (*refusal)
        std::rethrow_exception(*refusal);
    QFile after(target);
    check(after.open(QIODevice::ReadOnly) && after.readAll() == existing,
          "An existing file is never replaced");
}

// §18.7: at 200 % text, Insert stays inside the dialog; a narrow dialog stacks the list above its
// details, and a wide one puts them side by side.
void largeText(const QTemporaryDir &files, const QMap<QString, QByteArray> &fixtures) {
    QSettings("SketchyUp", "SketchyUp").setValue("interfaceTextPercent", 200);
    {
        Window large;
        large.resize(1280, 900);
        large.show();
        check(QTest::qWaitForWindowExposed(&large), "The 200% window is exposed");
        large.demo();
        large.viewport()->refresh();
        resetLibrary(files, fixtures, {"basic"});
        catalogDialog(large, [&](QDialog &dialog) {
            auto *insert = child<QPushButton>(dialog, "catalogInsert");
            auto *list = child<QTreeWidget>(dialog, "catalogEntries");
            auto *split = child<QSplitter>(dialog, "catalogSplit");
            selectOnly(*list, {"Block"}, kBasic);
            check(insert->isVisibleTo(&dialog), "At 200% Insert is shown");
            check(insert->geometry().bottom() < dialog.height() &&
                      insert->geometry().right() < dialog.width(),
                  "At 200% Insert stays inside the dialog");
            check(QTest::qWaitFor([&] { return split->orientation() == Qt::Vertical; }, 3000),
                  "At 200% the list stacks above its details");
            // At 100 % the list sits beside its details when the dialog is wide, and stacks when
            // narrow.
            QSettings("SketchyUp", "SketchyUp").setValue("interfaceTextPercent", 100);
            dialog.resize(1020, 700); // a size change, so the layout is decided again
            dialog.resize(1040, 700);
            check(QTest::qWaitFor([&] { return split->orientation() == Qt::Horizontal; }, 3000),
                  "At 100% a wide dialog puts the list beside its details");
        });
    }
    QSettings("SketchyUp", "SketchyUp").setValue("interfaceTextPercent", 100);
}

// The library folder's files, by name, with their bytes: a cancelled insert must leave them as they were.
QMap<QString, QByteArray> libraryContents() {
    QMap<QString, QByteArray> contents;
    for (const auto &name : QDir(libraryFolder()).entryList(QDir::Files)) {
        QFile file(libraryFolder() + "/" + name);
        check(file.open(QIODevice::ReadOnly), "Read the library file back");
        contents[name] = file.readAll();
    }
    return contents;
}

// True once the platform has given the sheet the keyboard focus. A person clicks Cancel after that, and
// a click before it waits for the platform's focus (the settled close of a prompt, UX_DESIGN 16.9).
bool hasFocus(const QDialog *sheet) {
    return sheet->windowHandle() && QGuiApplication::focusWindow() == sheet->windowHandle();
}

// Restores the insert's test hooks when a test leaves, whatever happens in it.
struct InsertHooksRestored {
    ~InsertHooksRestored() {
        CatalogDialog::insertGateForTesting = nullptr;
        CatalogDialog::insertProgressDelayMs = 1000;
    }
};

// R100.i (ADR 0174 section 9 row 4; UX_DESIGN 18.5): an insert that runs long shows a progress sheet
// with its three steps and the catalog's name, and Cancel ends it. A cancelled insert makes no undo
// step, adds no record, and leaves the library folder as it was. A quick insert shows no sheet, and a
// completed insert is one labelled undo step. The slow insert is held by the test's gate until the
// sheet has been seen, and Cancel is clicked then, so no check depends on a timing window.
void insertProgressAndCancel(Window &window, const QTemporaryDir &files,
                             const QMap<QString, QByteArray> &fixtures) {
    resetLibrary(files, fixtures, {"basic"});
    InsertHooksRestored restored;
    const auto steps = history(window);
    const auto definitions = window.document().definitions().size();
    const auto materials = window.document().materials().size();
    const auto style = window.document().style();
    const auto library = libraryContents();

    // A slow insert: the worker waits at the gate until the test has seen the sheet and clicked Cancel.
    CatalogDialog::insertProgressDelayMs = 0;
    CatalogDialog::insertGateForTesting = [](const std::function<bool()> &cancelled) {
        // Bounded, so that a sheet that never opens fails the check below instead of hanging the run.
        QElapsedTimer held;
        held.start();
        while (!cancelled() && held.elapsed() < 20000)
            QThread::msleep(2);
    };
    struct Seen {
        bool sheet = false, cancelled = false, busy = false;
        QStringList steps, states;
        QString file, reason;
        double closeMs = -1;
    } seen;
    QElapsedTimer clock;
    catalogDialog(window, [&](QDialog &dialog) {
        auto *list = child<QTreeWidget>(dialog, "catalogEntries");
        auto *insert = child<QPushButton>(dialog, "catalogInsert");
        selectOnly(*list, {"Block"}, kBasic);
        QTimer poll;
        poll.setInterval(5);
        int stage = 0; // 0: waiting for the sheet; 1: waiting for its focus; 2: Cancel clicked
        QElapsedTimer focusWait;
        QObject::connect(&poll, &QTimer::timeout, &dialog, [&] {
            auto *sheet = dialog.findChild<QDialog *>("catalogInsertProgressSheet");
            if (!sheet || !sheet->isVisible() || stage == 2)
                return;
            if (stage == 0) {
                seen.sheet = true;
                // The sheet's own close, as the time Cancel takes to close it.
                QObject::connect(sheet, &QDialog::finished, sheet, [&] {
                    if (seen.closeMs < 0)
                        seen.closeMs = clock.nsecsElapsed() / 1e6;
                });
                for (int i = 1; i <= 3; ++i) {
                    const auto prefix = QStringLiteral("catalogInsertProgressStep%1").arg(i);
                    const auto *name = sheet->findChild<QLabel *>(prefix);
                    const auto *state = sheet->findChild<QLabel *>(prefix + "State");
                    seen.steps << (name ? name->text() : QString());
                    seen.states << (state ? state->text() : QString());
                }
                if (const auto *file = sheet->findChild<QLabel *>("catalogInsertProgressFile"))
                    seen.file = file->text();
                if (const auto *bar = sheet->findChild<QProgressBar *>("catalogInsertProgressBar"))
                    seen.busy = bar->maximum() == 0;
                stage = 1;
                focusWait.start();
            }
            if (!hasFocus(sheet) && focusWait.elapsed() < 1000)
                return;
            stage = 2;
            seen.cancelled = true;
            clock.start();
            if (auto *cancel = sheet->findChild<QPushButton *>("catalogInsertProgressCancel"))
                cancel->click();
        });
        poll.start();
        insert->click();
        poll.stop();
        seen.reason = child<QLabel>(dialog, "catalogInsertReason")->text();
        check(seen.sheet, "The progress sheet opens for a slow insert");
        check(seen.steps == QStringList{"Read the catalog", "Verify its hashes", "Insert the entries"},
              "The sheet names the three phases");
        check(seen.states == QStringList{"●", "○", "○"},
              "The first phase is working when the sheet opens");
        check(seen.file.contains("Fixture catalog"), "The sheet names the catalog");
        check(seen.busy, "The sheet's bar is busy: no step has a count");
        check(seen.closeMs >= 0, "Cancel closes the sheet");
        std::cout << "insert cancel: the sheet closed " << seen.closeMs << " ms after Cancel"
                  << std::endl;
        check(history(window) == steps, "A cancelled insert adds no undo step");
        check(window.document().definitions().size() == definitions,
              "A cancelled insert adds no component definition");
        check(window.document().materials().size() == materials,
              "A cancelled insert adds no material");
        check(window.document().style() == style, "A cancelled insert changes no style");
        check(libraryContents() == library, "A cancelled insert leaves the library folder as it was");
        check(seen.reason.startsWith("Insert cancelled. Nothing was inserted"),
              "The dialog says the insert was cancelled and nothing was inserted");
        check(insert->isEnabled(), "Insert is on again after a cancel");
        check(dialog.isVisible(), "The dialog stays open after a cancel");
    });

    // A quick insert: the delay is longer than the insert, so no sheet is ever made, and the insert is
    // one labelled undo step.
    CatalogDialog::insertGateForTesting = nullptr;
    CatalogDialog::insertProgressDelayMs = 60000;
    bool sheetMade = false;
    catalogDialog(
        window,
        [&](QDialog &dialog) {
            auto *list = child<QTreeWidget>(dialog, "catalogEntries");
            auto *insert = child<QPushButton>(dialog, "catalogInsert");
            selectOnly(*list, {"Block"}, kBasic);
            QTimer poll;
            poll.setInterval(5);
            QObject::connect(&poll, &QTimer::timeout, &dialog, [&] {
                if (dialog.findChild<QDialog *>("catalogInsertProgressSheet"))
                    sheetMade = true;
            });
            poll.start();
            insert->click();
            poll.stop();
            check(!sheetMade, "A quick insert shows no progress sheet");
        },
        false);
    check(history(window) == steps + 1, "A completed insert is one undo step");
    check(lastLabel(window) == "Insert component “Block”", "The undo step is labelled");
    check(window.document().definitions().size() == definitions + 1, "The component is inserted");
    undo(window)->trigger();
    check(history(window) == steps, "Undo removes the completed insert");
    check(window.document().definitions().size() == definitions, "Undo removes the component");
}

// R100.i (ADR 0174 section 9 row 4): the insert's cost on a large catalog. It runs only when
// SKETCHYUP_R100I_CATALOG names a catalog (`style_catalog_benchmark write` makes one), so no CI run
// does it. It prints three measurements: one insert of one entry with the default delay, the longest
// stop of the UI thread during that insert (the gap between ticks of a 10 ms timer, as
// quantities_large_input_tests measures it), and the time from Cancel to the sheet's close, with Cancel
// clicked as soon as the sheet opens.
void insertBudget(Window &window, const QTemporaryDir &files) {
    const auto source = qEnvironmentVariable("SKETCHYUP_R100I_CATALOG");
    if (source.isEmpty())
        return;
    InsertHooksRestored restored;
    resetLibrary(files, {}, {});
    check(QFile::copy(source, libraryFolder() + "/budget.sketchycat"),
          "Copy the budget catalog into the library");
    CatalogDialog::insertGateForTesting = nullptr;
    // One insert of one entry, timed, with the longest stop of the UI thread during it.
    const auto timedInsert = [&](int delayMs, const char *label) {
        CatalogDialog::insertProgressDelayMs = delayMs;
        const auto steps = history(window);
        catalogDialog(window, [&](QDialog &dialog) {
            auto *list = child<QTreeWidget>(dialog, "catalogEntries");
            auto *insert = child<QPushButton>(dialog, "catalogInsert");
            check(list->topLevelItemCount() == 1, "The library's budget catalog is listed");
            selectOnly(*list, {"Big 1"}, list->topLevelItem(0)->text(0));
            std::vector<double> ticks{0};
            QElapsedTimer clock;
            QTimer ticker;
            ticker.setInterval(10);
            QObject::connect(&ticker, &QTimer::timeout, &dialog,
                             [&] { ticks.push_back(clock.nsecsElapsed() / 1e6); });
            clock.start();
            ticker.start();
            insert->click();
            ticker.stop();
            const double total = clock.nsecsElapsed() / 1e6;
            ticks.push_back(total);
            double stall = 0;
            for (std::size_t i = 0; i + 1 < ticks.size(); ++i)
                stall = std::max(stall, ticks[i + 1] - ticks[i]);
            std::cout << label << ": " << total << " ms; the UI thread's longest stop " << stall
                      << " ms; one undo step: " << (history(window) == steps + 1 ? "yes" : "no")
                      << std::endl;
            check(history(window) == steps + 1, "The budget insert is one undo step");
        }, false);
        undo(window)->trigger();
    };
    timedInsert(1000, "insert one entry, default delay (no sheet)");
    timedInsert(0, "insert one entry, sheet from the start");

    // Cancel at several points of one insert: the time from Cancel to the sheet's close. The dialog stays
    // open after each cancel, so the same insert is run again.
    CatalogDialog::insertProgressDelayMs = 0;
    catalogDialog(window, [&](QDialog &dialog) {
        auto *list = child<QTreeWidget>(dialog, "catalogEntries");
        auto *insert = child<QPushButton>(dialog, "catalogInsert");
        selectOnly(*list, {"Big 1"}, list->topLevelItem(0)->text(0));
        const auto steps = history(window);
        for (const int afterMs : {0, 50, 150, 250}) {
            double cancelMs = -1;
            bool clicked = false;
            QElapsedTimer clock, shownFor;
            bool shown = false;
            QTimer poll;
            poll.setInterval(1);
            QObject::connect(&poll, &QTimer::timeout, &dialog, [&] {
                auto *sheet = dialog.findChild<QDialog *>("catalogInsertProgressSheet");
                if (clicked || !sheet || !sheet->isVisible())
                    return;
                if (!shown) {
                    shown = true;
                    shownFor.start();
                }
                // `afterMs` counts from the sheet's opening, so Cancel lands that far into the insert.
                if (shownFor.elapsed() < afterMs)
                    return;
                auto *cancel = sheet->findChild<QPushButton *>("catalogInsertProgressCancel");
                if (!cancel)
                    return;
                clicked = true;
                QObject::connect(sheet, &QDialog::finished, sheet, [&] {
                    if (cancelMs < 0)
                        cancelMs = clock.nsecsElapsed() / 1e6;
                });
                clock.start();
                cancel->click();
            });
            poll.start();
            insert->click();
            poll.stop();
            // An insert whose composed edit was complete before Cancel took effect is committed, as a
            // finished insert is; it is reported, and undone so the next cancel starts from the same model.
            const bool committed = history(window) != steps;
            std::cout << "insert cancel, " << afterMs << " ms into the insert: "
                      << (!clicked ? std::string("the insert ended first")
                          : committed
                              ? std::string("the insert finished before Cancel took effect")
                              : "the sheet closed " + std::to_string(cancelMs) + " ms after Cancel")
                      << std::endl;
            if (committed)
                undo(window)->trigger();
        }
    });
}

// R100.g (ADR 0174 section 9; UX_DESIGN 18.5): the time each selection change takes on a large
// catalog. R100.f3 left the dialog's dry run on each selection unmeasured. It runs only when
// SKETCHYUP_R100G_CATALOG names a catalog (`style_catalog_benchmark write` makes one), so no CI run
// does it. It times Big 1's selection five times with the catalog opened by Open catalog file…, and
// again with the catalog in the library folder, where the dialog names it in its batch.
void selectionBudget(Window &window, const QTemporaryDir &files) {
    const auto source = qEnvironmentVariable("SKETCHYUP_R100G_CATALOG");
    if (source.isEmpty())
        return;
    const auto timed = [](QTreeWidget &list, const QString &title) {
        std::vector<double> samples;
        for (int i = 0; i < 5; ++i) {
            list.clearSelection();
            QElapsedTimer timer;
            timer.start();
            entryRow(list, title, QStringLiteral("Big 1"))->setSelected(true);
            samples.push_back(timer.nsecsElapsed() / 1e6);
        }
        std::sort(samples.begin(), samples.end());
        return samples;
    };
    const auto report = [](const char *label, const std::vector<double> &samples) {
        std::cout << label << ": n=" << samples.size() << ", median " << samples[samples.size() / 2]
                  << " ms, max " << samples.back() << " ms\n";
    };
    resetLibrary(files, {}, {});
    const auto opened = files.path() + "/outside/budget.sketchycat";
    check(QFile::copy(source, opened), "Copy the budget catalog for Open catalog file");
    catalogDialog(window, [&](QDialog &dialog) {
        auto *list = child<QTreeWidget>(dialog, "catalogEntries");
        modal([&] { child<QPushButton>(dialog, "catalogOpen")->click(); },
              [&](QDialog &chooser) { chooseFile(chooser, opened); });
        check(list->topLevelItemCount() == 1, "The opened budget catalog is listed");
        report("dialog selection, opened file", timed(*list, list->topLevelItem(0)->text(0)));
    });
    resetLibrary(files, {}, {});
    check(QFile::copy(source, libraryFolder() + "/budget.sketchycat"),
          "Copy the budget catalog into the library");
    catalogDialog(window, [&](QDialog &dialog) {
        auto *list = child<QTreeWidget>(dialog, "catalogEntries");
        check(list->topLevelItemCount() == 1, "The library's budget catalog is listed");
        report("dialog selection, library folder", timed(*list, list->topLevelItem(0)->text(0)));
    });
}

// Waits until the compositor answers a sync on the display's default queue. The answer comes after
// the events the compositor sent earlier, and those are dispatched first. Qt's
// QGuiApplication::sync is not a round trip on Wayland (see window_diagnostics.cpp). Without this,
// the focus change after the last window closes can stay queued at exit: its clipboard offer is a
// new-id proxy, and libwayland leaks it when it releases the queue (D-072). Off Wayland there is
// nothing to drain.
bool drainWaylandEvents() {
    auto *native = qGuiApp->nativeInterface<QNativeInterface::QWaylandApplication>();
    if (!native)
        return true;
    bool answered = false;
    wl_callback *callback = wl_display_sync(native->display());
    if (!callback)
        return false;
    static const wl_callback_listener listener{[](void *data, wl_callback *done, std::uint32_t) {
        *static_cast<bool *>(data) = true;
        wl_callback_destroy(done);
    }};
    wl_callback_add_listener(callback, &listener, &answered);
    wl_display_flush(native->display());
    return QTest::qWaitFor([&] { return answered; }, 5000);
}
} // namespace

// R100.f2 (UX_DESIGN §18.1 to §18.3, §18.6, §18.7): the Styles tab's edge stroke and style library,
// Import style… and Export style…, with their refusals. These cases run before the window below is
// created, so the long-lived window does not take the keyboard focus from them.
namespace style_suite {
namespace {

void check(bool ok, const char *message) {
    if (!ok)
        throw std::runtime_error(message);
}

QByteArray texturePng() {
    return encodeTexturePng(TextureImage(2, 1, {255, 0, 0, 255, 0, 255, 0, 255}));
}

void write(const QString &path, const QByteArray &bytes) {
    QFile file(path);
    check(file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size(), "Write a fixture");
    file.close();
}

QByteArray read(const QString &path) {
    QFile file(path);
    check(file.open(QIODevice::ReadOnly), "Read a file");
    return file.readAll();
}

// A style with an edge stroke, whose texture is `texture` (zero for none).
ModelStyle strokeStyle(double width, double opacity, EdgeDash dash, Id texture = 0) {
    ModelStyle style;
    style.edgeStroke = EdgeStroke{width, opacity, dash, texture};
    return style;
}

// A library file: a textured style (Present), or a plain one (None).
QByteArray texturedFile(const QString &name, const ModelStyle &style, Id id) {
    AssetRecord texture{id, "Weave", "image/png", assetPayload(texturePng())};
    return encodeStyleBundle({{name, "", {}}, style, texture, StyleTextureStatus::Present});
}

QByteArray plainFile(const QString &name, const ModelStyle &style) {
    return encodeStyleBundle({{name, "", {}}, style, {}, StyleTextureStatus::None});
}

// A `.sketchylib`-envelope file built by hand, so a damaged or foreign file can be made: the
// header (magic, manifest length, payload length, zero thumbnail length), the manifest, the payload.
QByteArray envelope(const QJsonObject &manifest, const QByteArray &payload) {
    const auto text = QJsonDocument(manifest).toJson(QJsonDocument::Compact);
    QByteArray bytes(24, '\0');
    std::copy_n("SKYLIB\0\1", 8, bytes.begin());
    qToLittleEndian<quint32>(quint32(text.size()), bytes.data() + 8);
    qToLittleEndian<quint64>(quint64(payload.size()), bytes.data() + 12);
    return bytes + text + payload;
}

// A style manifest with the given style record and texture descriptor (QJsonValue::Null for none).
QJsonObject manifest(const QString &kind, const QString &name, const QJsonObject &style,
                     const QJsonValue &texture) {
    return {{"bundleVersion", 1},
            {"kind", kind},
            {"name", name},
            {"description", ""},
            {"labels", QJsonArray{}},
            {"style", style},
            {"texture", texture}};
}

QJsonObject textureDescriptor(Id id, bool missing, const QByteArray &payload,
                              const QString &sha256) {
    return {{"id", QString::number(id)},
            {"name", "Weave"},
            {"mediaType", "image/png"},
            {"missing", missing},
            {"bytes", missing ? 0 : double(payload.size())},
            {"sha256", missing ? QJsonValue::Null : QJsonValue(sha256)}};
}

QString sha256Of(const QByteArray &bytes) {
    return QString::fromLatin1(QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex());
}

// The label of the newest applied edit (history lists the oldest first).
std::string lastLabel(const Document &doc) {
    const auto position = doc.history().position;
    return doc.history(position - 1, 1).entries.front().label;
}

// Runs a blocking action (a dialog's exec) while `step` is called every 10 ms. `step` returns true
// when the dialogs it drives are done. A check that fails inside `step` is kept and rethrown once the
// action returns, since Qt does not carry an exception out of a timer. A watchdog closes whatever is
// open after 15 s, so a dialog that never appears fails the test instead of hanging the run.
void drive(const std::function<void()> &action, const std::function<bool()> &step) {
    QTimer timer;
    timer.setInterval(10);
    QElapsedTimer clock;
    clock.start();
    std::string failure;
    bool timedOut = false;
    auto closeOpen = [] {
        while (auto *modal = QApplication::activeModalWidget())
            QTest::keyClick(modal, Qt::Key_Escape);
    };
    QObject::connect(&timer, &QTimer::timeout, [&] {
        try {
            if (step()) {
                timer.stop();
                return;
            }
        } catch (const std::exception &error) {
            failure = error.what();
            timer.stop();
            closeOpen();
            return;
        }
        if (clock.elapsed() > 15000) {
            timer.stop();
            timedOut = true;
            closeOpen();
        }
    });
    timer.start();
    action();
    timer.stop();
    if (!failure.empty())
        throw std::runtime_error(failure);
    if (timedOut) {
        // Name the windows that were open, so a stalled flow shows where it stopped.
        QStringList open;
        for (auto *widget : QApplication::topLevelWidgets())
            if (widget->isVisible())
                open << widget->objectName() + "/" + widget->windowTitle();
        throw std::runtime_error(("A dialog the test drives did not open; open: " +
                                  open.join(", ")).toStdString());
    }
}

// A click that runs after the current event, so a file dialog the button opens is driven by the
// timer and is not nested inside the timer's own callback.
void queueClick(QAbstractButton *button) {
    QMetaObject::invokeMethod(button, "click", Qt::QueuedConnection);
}

// Accepts a Qt file chooser on `path`, as a person would type the path. Once per chooser: it is
// marked handled, so a later tick does not accept it again.
void choose(QFileDialog &chooser, const QString &path) {
    if (chooser.property("handled").toBool())
        return;
    chooser.setProperty("handled", true);
    auto *name = chooser.findChild<QLineEdit *>("fileNameEdit");
    check(name, "The file chooser has a file name field");
    name->setText(path);
    QMetaObject::invokeMethod(&chooser, "accept", Qt::DirectConnection);
}

// Sets the model's style as the Styles tab does, so the tab refreshes (its Viewport::changed).
void setModelStyle(Window &window, const ModelStyle &style) {
    window.viewport()->applyModelStyle(style);
}

// Shows the Styles tab and returns its panel.
QWidget &stylesTab(Window &window) {
    window.findChild<QAction *>("view.styles")->trigger();
    auto *panel = window.findChild<QWidget *>("stylesPanel");
    check(panel, "The Styles tab exists");
    return *panel;
}

template <class T> T *field(QWidget &root, const char *name) {
    auto *widget = root.findChild<T *>(name);
    check(widget, name);
    return widget;
}

QString statusText(Window &window) { return window.findChild<QLabel *>("statusMessage")->text(); }

// The Styles tab's library rows, by name, found by their first line.
QListWidgetItem *row(QListWidget &list, const QString &name) {
    for (int i = 0; i < list.count(); ++i)
        if (list.item(i)->text().startsWith(name + "\n"))
            return list.item(i);
    throw std::runtime_error("A library row is listed");
}

// Each Edge stroke control is one labelled undo step, and undo returns the record before it.
void edgeStrokeControls() {
    Window window;
    window.resize(1180, 850);
    window.show();
    check(QTest::qWaitForWindowExposed(&window), "Window exposed");
    auto &doc = window.document();
    auto &panel = stylesTab(window);
    auto *stroke = field<QCheckBox>(panel, "styleEdgeStroke");
    auto *width = field<QLineEdit>(panel, "styleEdgeWidth");
    auto *opacity = field<QLineEdit>(panel, "styleEdgeOpacity");
    auto *dash = field<QComboBox>(panel, "styleEdgeDash");
    auto *texture = field<QComboBox>(panel, "styleEdgeTexture");
    check(!stroke->isChecked() && !width->isEnabled() && !opacity->isEnabled() &&
              !dash->isEnabled() && !texture->isEnabled(),
          "With the stroke off, its fields are off");
    const auto asset = createAsset(doc, "Weave", "image/png", assetPayload(texturePng()));

    // Turning the stroke on copies the profile width (2 px by default) and is one labelled step.
    auto total = doc.history().total;
    stroke->click();
    check(doc.style().edgeStroke && doc.style().edgeStroke->width == doc.style().profileWidth,
          "Turning the stroke on copies the profile width");
    check(doc.history().total == total + 1 && lastLabel(doc) == "Turn on edge stroke",
          "Turn on edge stroke is one labelled edit");
    check(width->isEnabled() && width->text() == "2", "The width shows the stroke's width");

    // Width: Enter commits one labelled edit.
    total = doc.history().total;
    width->selectAll();
    QTest::keyClicks(width, "2.5");
    QTest::keyClick(width, Qt::Key_Return);
    check(doc.style().edgeStroke->width == 2.5 && doc.history().total == total + 1 &&
              lastLabel(doc) == "Set edge width 2.5 px",
          "Width is one labelled edit");

    // Opacity is typed as a percentage and stored as a fraction.
    total = doc.history().total;
    opacity->selectAll();
    QTest::keyClicks(opacity, "60");
    QTest::keyClick(opacity, Qt::Key_Return);
    check(std::abs(doc.style().edgeStroke->opacity - 0.6) < 1e-12 &&
              lastLabel(doc) == "Set edge opacity 60 %" && doc.history().total == total + 1,
          "Opacity is one labelled edit in percent");

    // Dash: the activated choice is one labelled edit.
    total = doc.history().total;
    dash->setCurrentIndex(1);
    emit dash->activated(1);
    check(doc.style().edgeStroke->dash == EdgeDash::Dashed &&
              lastLabel(doc) == "Set edge dash Dashed" && doc.history().total == total + 1,
          "Dash is one labelled edit");

    // Texture: choosing the model's PNG is one labelled edit; None clears it.
    total = doc.history().total;
    const auto index = texture->findData(QVariant::fromValue<qulonglong>(asset));
    check(index >= 0, "The model's PNG is listed as an edge texture");
    texture->setCurrentIndex(index);
    emit texture->activated(index);
    check(doc.style().edgeStroke->texture == asset &&
              lastLabel(doc) == "Set edge texture “Weave”" && doc.history().total == total + 1,
          "Texture is one labelled edit");
    const auto none = texture->findData(QVariant::fromValue<qulonglong>(0));
    texture->setCurrentIndex(none);
    emit texture->activated(none);
    check(!doc.style().edgeStroke->texture && lastLabel(doc) == "Clear edge texture",
          "None clears the texture in one labelled edit");

    // An invalid width is refused under the field, names the value, and changes nothing.
    total = doc.history().total;
    width->selectAll();
    QTest::keyClicks(width, "9");
    QTest::keyClick(width, Qt::Key_Return);
    auto *error = field<QLabel>(panel, "styleEdgeError");
    check(error->isVisible() && error->text().startsWith("Invalid: Edge width 9 px is outside 1 to 8"),
          "An invalid width is refused under its field");
    check(doc.history().total == total && doc.style().edgeStroke->width == 2.5,
          "A refused width changes nothing");

    // Esc restores the field to the document's value.
    width->selectAll();
    QTest::keyClicks(width, "7");
    QTest::keyClick(width, Qt::Key_Escape);
    check(width->text() == "2.5" && !error->isVisible(), "Esc restores the width");

    // Turning the stroke off removes it in one labelled edit, and undo restores it.
    total = doc.history().total;
    stroke->click();
    check(!doc.style().edgeStroke && lastLabel(doc) == "Turn off edge stroke" &&
              doc.history().total == total + 1,
          "Turn off edge stroke removes the stroke in one edit");
    doc.undo();
    check(doc.style().edgeStroke && doc.style().edgeStroke->width == 2.5,
          "Undo restores the stroke that was turned off");

    // Reset model style removes the stroke in one labelled edit. The position counts the applied
    // steps, which a new edit after an undo advances by one (the total keeps the redo entries).
    const auto position = doc.history().position;
    field<QPushButton>(panel, "styleReset")->click();
    check(!doc.style().edgeStroke && lastLabel(doc) == "Reset model style" &&
              doc.history().position == position + 1 && !stroke->isChecked(),
          "Reset model style removes the stroke in one labelled edit");
}

// Colors and details dims Profile width while the stroke is on (UX_DESIGN §18.3, gap 2).
void profileWidthDimmed() {
    Window window;
    window.resize(1180, 850);
    window.show();
    check(QTest::qWaitForWindowExposed(&window), "Window exposed");
    auto &doc = window.document();
    setModelStyle(window, strokeStyle(3, 1, EdgeDash::Solid));
    auto &panel = stylesTab(window);
    bool checked = false;
    drive([&] { QTest::mouseClick(field<QPushButton>(panel, "styleCustomize"), Qt::LeftButton); },
          [&] {
              auto *dialog = window.findChild<QDialog *>("styleDialog");
              if (!dialog || !dialog->isVisible())
                  return false;
              check(!field<QLineEdit>(*dialog, "styleProfileWidth")->isEnabled(),
                    "Profile width is dimmed while the stroke is on");
              check(field<QLabel>(*dialog, "styleProfileWidthNote")->text() ==
                        "Set by the edge stroke",
                    "Profile width says it is set by the edge stroke");
              checked = true;
              dialog->reject();
              return true;
          });
    check(checked, "The Colors and details dialog was checked");
    setModelStyle(window, ModelStyle{});
    drive([&] { QTest::mouseClick(field<QPushButton>(panel, "styleCustomize"), Qt::LeftButton); },
          [&] {
              auto *dialog = window.findChild<QDialog *>("styleDialog");
              if (!dialog || !dialog->isVisible())
                  return false;
              check(field<QLineEdit>(*dialog, "styleProfileWidth")->isEnabled(),
                    "Profile width is editable while the stroke is off");
              dialog->reject();
              return true;
          });
}

// A texture the model does not hold, or holds without bytes, draws as a missing state: the field,
// the caption and the tooltip (which is the accessible description) all say so.
void missingTextureState() {
    Window window;
    window.resize(1180, 850);
    window.show();
    check(QTest::qWaitForWindowExposed(&window), "Window exposed");
    auto &doc = window.document();
    const auto lost = createAsset(doc, "Edge texture 2", "image/png");
    setModelStyle(window, strokeStyle(2, 1, EdgeDash::Dashed, lost));
    auto &panel = stylesTab(window);
    auto *texture = field<QComboBox>(panel, "styleEdgeTexture");
    const auto index = texture->findData(QVariant::fromValue<qulonglong>(lost));
    check(index >= 0 && texture->itemText(index).startsWith("⚠ Edge texture 2 · Missing"),
          "The missing texture is listed with its warning word");
    check(!texture->model()->index(index, 0).flags().testFlag(Qt::ItemIsEnabled),
          "A missing texture cannot be chosen");
    auto *note = field<QLabel>(panel, "styleEdgeTextureNote");
    check(note->isVisible() && note->text().startsWith("Missing:"),
          "The missing caption says how the edges draw");
    const auto tip = QString("Edge texture 2 is missing from this model (asset %1). Edges that use "
                             "it draw hatched until it is restored. No other texture is used.")
                         .arg(lost);
    check(texture->toolTip() == tip && texture->accessibleDescription() == tip,
          "The missing tooltip is the accessible description");
    check(window.findChild<QPushButton *>("edgeTextureChip")->isVisible(),
          "The status chip shows the missing texture");
    setModelStyle(window, strokeStyle(2, 1, EdgeDash::Dashed));
    check(!note->isVisible() && texture->toolTip().isEmpty(), "No stroke, no missing state");
}

// The Style library: rows show the states of §18.2, search matches names and descriptions, Apply
// applies one labelled step and says so, and a file that cannot be read cannot be selected.
void libraryStates() {
    QTemporaryDir folder;
    QSettings("SketchyUp", "SketchyUp").remove("styleFolder");
    check(folder.isValid(), "A temporary folder");
    setStyleLibraryPreference(folder.path());
    write(folder.filePath("Style A.sketchystyle"),
          texturedFile("Style A", strokeStyle(2, .6, EdgeDash::Dashed, 4), 4));
    write(folder.filePath("Style B.sketchystyle"), plainFile("Style B", ModelStyle{}));
    write(folder.filePath("Style C.sketchystyle"),
          envelope(manifest("style", "Style C",
                            encodeModelStyle(strokeStyle(3, 1, EdgeDash::DashDot, 4)),
                            textureDescriptor(4, true, {}, {})),
                   {}));
    write(folder.filePath("Broken.sketchystyle"), QByteArray("garbage"));
    write(folder.filePath("Notes.txt"), QByteArray("not a style"));

    Window window;
    window.resize(1180, 850);
    window.show();
    check(QTest::qWaitForWindowExposed(&window), "Window exposed");
    auto &doc = window.document();
    auto &panel = stylesTab(window);
    auto *list = field<QListWidget>(panel, "styleLibrary");
    auto *apply = field<QPushButton>(panel, "styleApplyLibrary");
    auto *count = window.findChild<QLabel *>("styleLibraryCount");
    check(list->count() == 4, "The folder lists its style files and the one it cannot read");
    check(row(*list, "Style A")->text().contains("2.0 px · 60 % · Dashed · texture stored · ✓ Stored"),
          "A stored style shows its settings in words and the ✓ Stored state");
    check(row(*list, "Style C")->text().contains("⚠ Texture missing from file · cannot apply"),
          "A missing texture is listed with its warning and cannot be applied");
    check(row(*list, "Broken")->text().contains("✕ Cannot read:") &&
              !row(*list, "Broken")->flags().testFlag(Qt::ItemIsSelectable),
          "A file that cannot be read is listed with its reason and cannot be selected");
    check(window.findChild<QLabel *>("styleLibraryNotice")->text().contains("Skipped 1 file that is not a style file"),
          "The scan reports the file it skipped and why");
    check(count->text() == "4 styles", "The count names the listed styles");

    // Search matches names and descriptions.
    auto *search = field<QLineEdit>(panel, "styleSearch");
    search->setText("style b");
    check(list->count() == 1 && list->item(0)->text().startsWith("Style B"),
          "Search narrows the rows to the match");
    search->clear();
    check(list->count() == 4, "Clearing the search lists every style");

    // A missing texture cannot be applied, and nothing changes.
    list->setCurrentItem(row(*list, "Style C"));
    check(!apply->isEnabled(), "Apply is off for a style whose texture is missing");
    const auto history = doc.history().total;
    list->setCurrentItem(row(*list, "Style A"));
    check(apply->isEnabled(), "Apply is on for a stored style");
    apply->click();
    const auto applied = doc.style();
    check(applied.edgeStroke && applied.edgeStroke->dash == EdgeDash::Dashed &&
              std::abs(applied.edgeStroke->opacity - .6) < 1e-12,
          "Apply sets the stored style on the model");
    check(doc.history().total == history + 1 && lastLabel(doc) == "Apply style “Style A”",
          "Apply is one labelled step");
    check(statusText(window).startsWith("Applied “Style A”: edge stroke on, "),
          "Apply says what changed in the status line");
    check(doc.assets().size() == 1, "Apply adds the style's texture to the model");

    // Applying the same style again is the neutral result, with no edit.
    const auto again = doc.history().total;
    apply->click();
    check(doc.history().total == again && statusText(window) ==
              "This model already has this style. Nothing changed.",
          "A style equal to the model's style is a neutral result");

    // Enter applies the selected row (keyboard reachability).
    list->setCurrentItem(row(*list, "Style B"));
    QTest::keyClick(list, Qt::Key_Return);
    check(doc.style() == ModelStyle{} && lastLabel(doc) == "Apply style “Style B”",
          "Enter on a row applies it");

    // Refresh re-reads the folder; a file added since then appears.
    write(folder.filePath("Style D.sketchystyle"), plainFile("Style D", ModelStyle{}));
    field<QPushButton>(panel, "styleRefresh")->click();
    check(list->count() == 5 && count->text() == "5 styles", "Refresh lists the new style");
}

// Refresh stays inside the ADR 0132 bounds: a style past the 128 listed is skipped and counted.
void libraryBounds() {
    QTemporaryDir folder;
    setStyleLibraryPreference(folder.path());
    for (int i = 0; i < int(styleScanCandidateLimit) + 2; ++i)
        write(folder.filePath(QString("Style %1.sketchystyle").arg(i, 3, 10, QChar('0'))),
              plainFile(QString("Style %1").arg(i, 3, 10, QChar('0')), ModelStyle{}));
    const auto library = scanStyleDirectory(folder.path());
    check(library.entries.size() == styleScanCandidateLimit && library.skippedPastCandidates == 2,
          "A folder lists at most 128 styles and counts the rest");
    check(library.notices.join("\n").contains("Skipped 2 styles past the 128 listed"),
          "The notice names how many styles were skipped and why");
}

// Save to library refuses a name that is taken, suggests a free one, and never replaces a file.
void saveToLibrary() {
    QTemporaryDir folder;
    setStyleLibraryPreference(folder.path());
    write(folder.filePath("Style B.sketchystyle"), plainFile("Style B", ModelStyle{}));
    Window window;
    window.resize(1180, 850);
    window.show();
    check(QTest::qWaitForWindowExposed(&window), "Window exposed");
    setModelStyle(window, strokeStyle(2, .5, EdgeDash::Dotted));
    auto &panel = stylesTab(window);
    const auto original = read(folder.filePath("Style B.sketchystyle"));
    bool sawTaken = false, saved = false;
    drive([&] { field<QPushButton>(panel, "styleSaveLibrary")->click(); },
          [&] {
              auto *dialog = window.findChild<QDialog *>("saveStyleDialog");
              if (!dialog || !dialog->isVisible())
                  return saved;
              auto *name = field<QLineEdit>(*dialog, "libraryStyleName");
              auto *save = field<QPushButton>(*dialog, "libraryStyleSave");
              if (!sawTaken) {
                  name->setText("Style B");
                  check(!save->isEnabled() &&
                            field<QLabel>(*dialog, "libraryStyleError")->text().startsWith(
                                "✕ Nothing was saved.\nA style named “Style B” is already"),
                        "A taken name is refused in the dialog, and nothing was saved");
                  check(field<QLabel>(*dialog, "libraryStyleFile")->text().startsWith("Style B 2.sketchystyle"),
                        "The dialog suggests a free name");
                  sawTaken = true;
                  name->setText("Style D");
                  check(save->isEnabled(), "A free name can be saved");
                  save->click();
                  saved = true;
                  return false;
              }
              return false;
          });
    check(sawTaken && saved, "The save dialog was driven");
    check(QFileInfo::exists(folder.filePath("Style D.sketchystyle")),
          "The new style file is written to the library");
    check(read(folder.filePath("Style B.sketchystyle")) == original,
          "The taken file is never replaced");
    check(decodeStyleBundle(read(folder.filePath("Style D.sketchystyle"))).style.edgeStroke->dash ==
              EdgeDash::Dotted,
          "The saved file carries the model's stroke");
}

// Save to library refuses a missing edge texture before it writes anything. The alert says what did
// not happen on its first line (UX_DESIGN §18.7), and the reason follows on the next line.
void saveToLibraryRefusal() {
    QTemporaryDir folder;
    setStyleLibraryPreference(folder.path());
    Window window;
    window.resize(1180, 850);
    window.show();
    check(QTest::qWaitForWindowExposed(&window), "Window exposed");
    auto &doc = window.document();
    const auto lost = createAsset(doc, "Lost", "image/png");
    setModelStyle(window, strokeStyle(2, 1, EdgeDash::Solid, lost));
    auto &panel = stylesTab(window);
    const auto history = doc.history().total;
    bool named = false;
    drive([&] { field<QPushButton>(panel, "styleSaveLibrary")->click(); }, [&] {
        auto *dialog = window.findChild<QDialog *>("saveStyleDialog");
        if (!dialog || !dialog->isVisible())
            return false;
        field<QPushButton>(*dialog, "libraryStyleSave")->click();
        named = field<QLabel>(*dialog, "libraryStyleError")->text() ==
                "✕ Nothing was saved.\nSave refused: “Lost” (asset " + QString::number(lost) +
                    ") is missing from this model.";
        field<QPushButton>(*dialog, "libraryStyleCancel")->click();
        return true;
    });
    check(named, "The library-save refusal opens with \"Nothing was saved.\" and names the texture");
    check(QDir(folder.path()).entryList({"*.sketchystyle"}).isEmpty(),
          "A refused library save writes no file");
    check(doc.history().total == history, "A refused library save changes nothing");
}

// File > Import style…: the file's facts, Apply (one labelled step, then the same-style result),
// and Save to the library (a new file only).
void importApplyAndSave() {
    QTemporaryDir files;
    QTemporaryDir folder;
    setStyleLibraryPreference(folder.path());
    const auto source = files.filePath("Import A.sketchystyle");
    write(source, texturedFile("Imported A", strokeStyle(3, .4, EdgeDash::Solid, 9), 9));
    Window window;
    window.resize(1180, 850);
    window.show();
    check(QTest::qWaitForWindowExposed(&window), "Window exposed");
    auto &doc = window.document();
    auto *importAction = window.findChild<QAction *>("file.importStyle");
    check(importAction && importAction->shortcut().isEmpty(), "Import style has no shortcut");

    // Apply to this model is the default outcome.
    bool checkedFacts = false;
    drive([&] { importAction->trigger(); }, [&] {
        if (auto *chooser = window.findChild<QFileDialog *>("styleImportFileDialog");
            chooser && chooser->isVisible()) {
            choose(*chooser, source);
            return false;
        }
        auto *dialog = window.findChild<QDialog *>("importStyleDialog");
        if (!dialog || !dialog->isVisible())
            return false;
        check(field<QRadioButton>(*dialog, "importApply")->isChecked(), "Apply is the default outcome");
        check(field<QLabel>(*dialog, "importStroke")->text() == "3.0 px · 40 % · Solid",
              "The file's stroke is shown in words");
        check(field<QLabel>(*dialog, "importTexture")->text().contains("stored in the file"),
              "The texture is stored in the file");
        checkedFacts = true;
        field<QPushButton>(*dialog, "importPrimary")->click();
        return true;
    });
    check(checkedFacts, "The Import style dialog was checked");
    check(doc.style().edgeStroke && doc.style().edgeStroke->dash == EdgeDash::Solid &&
              std::abs(doc.style().edgeStroke->opacity - .4) < 1e-12,
          "Apply sets the imported style on the model");
    check(lastLabel(doc) == "Apply style “Imported A”", "Import apply is labelled");
    check(statusText(window).startsWith("Applied “Imported A”: edge stroke on, "),
          "Import apply says what changed");

    // The same style again is the neutral result and changes nothing.
    const auto total = doc.history().total;
    drive([&] { importAction->trigger(); }, [&] {
        if (auto *chooser = window.findChild<QFileDialog *>("styleImportFileDialog");
            chooser && chooser->isVisible()) {
            choose(*chooser, source);
            return false;
        }
        auto *dialog = window.findChild<QDialog *>("importStyleDialog");
        if (!dialog || !dialog->isVisible())
            return false;
        field<QPushButton>(*dialog, "importPrimary")->click();
        return true;
    });
    check(doc.history().total == total && statusText(window) ==
              "This model already has this style. Nothing changed.",
          "An import of the same style is the neutral result");

    // Save to the style library: a new file, and the name rule applies.
    bool saved = false;
    drive([&] { importAction->trigger(); }, [&] {
        if (auto *chooser = window.findChild<QFileDialog *>("styleImportFileDialog");
            chooser && chooser->isVisible()) {
            choose(*chooser, source);
            return false;
        }
        auto *dialog = window.findChild<QDialog *>("importStyleDialog");
        if (!dialog || !dialog->isVisible())
            return false;
        field<QRadioButton>(*dialog, "importSave")->click();
        auto *name = field<QLineEdit>(*dialog, "importName");
        check(name->isEnabled() && field<QPushButton>(*dialog, "importPrimary")->text() == "Save to library",
              "Save names the outcome on the primary button");
        name->setText("Kept A");
        field<QPushButton>(*dialog, "importPrimary")->click();
        saved = true;
        return true;
    });
    check(saved && QFileInfo::exists(folder.filePath("Kept A.sketchystyle")),
          "Save to the library writes a new file");
    check(doc.history().total == total, "Saving to the library does not change the model");
    check(read(folder.filePath("Kept A.sketchystyle")) == read(source),
          "The library file is the file as read, byte for byte");
}

// The §18.6 refusals of Import style: each names what did not happen and changes nothing.
void importRefusals() {
    QTemporaryDir files;
    QTemporaryDir folder;
    setStyleLibraryPreference(folder.path());
    const auto good = files.filePath("Good.sketchystyle");
    write(good, plainFile("Good", strokeStyle(4, 1, EdgeDash::Dotted)));
    const auto bad = files.filePath("Bad.sketchystyle");
    write(bad, QByteArray("not a style file"));
    const auto missing = files.filePath("Missing.sketchystyle");
    write(missing, envelope(manifest("style", "Missing", encodeModelStyle(strokeStyle(2, 1, EdgeDash::Solid, 4)),
                                     textureDescriptor(4, true, {}, {})),
                            {}));
    const auto hash = files.filePath("Hash.sketchystyle");
    write(hash, envelope(manifest("style", "Hash", encodeModelStyle(strokeStyle(2, 1, EdgeDash::Solid, 4)),
                                  textureDescriptor(4, false, texturePng(), QString(64, '0'))),
                         texturePng()));
    const auto gif = files.filePath("Gif.sketchystyle");
    write(gif, envelope(manifest("style", "Gif", encodeModelStyle(strokeStyle(2, 1, EdgeDash::Solid, 4)),
                                 QJsonObject{{"id", "4"}, {"name", "Weave"}, {"mediaType", "image/gif"},
                                             {"missing", false}, {"bytes", 3},
                                             {"sha256", sha256Of(QByteArray("GIF"))}}),
                        QByteArray("GIF")));
    const auto catalog = files.filePath("Catalog.sketchystyle");
    write(catalog, envelope(manifest("catalog", "Catalog", encodeModelStyle(ModelStyle{}), QJsonValue::Null),
                            {}));
    const auto wide = files.filePath("Wide.sketchystyle");
    {
        // Built by hand: encodeModelStyle refuses a width of 9, which is what the file must hold.
        auto style = encodeModelStyle(strokeStyle(2, 1, EdgeDash::Solid));
        auto stroke = style["edgeStroke"].toObject();
        stroke["width"] = 9;
        style["edgeStroke"] = stroke;
        write(wide, envelope(manifest("style", "Wide", style, QJsonValue::Null), {}));
    }
    const auto huge = files.filePath("Huge.sketchystyle");
    {
        QByteArray header(24, '\0');
        std::copy_n("SKYLIB\0\1", 8, header.begin());
        qToLittleEndian<quint32>(2, header.data() + 8);
        qToLittleEndian<quint64>(quint64(17) * 1024 * 1024, header.data() + 12);
        write(huge, header + QByteArray("{}"));
    }

    Window window;
    window.resize(1180, 850);
    window.show();
    check(QTest::qWaitForWindowExposed(&window), "Window exposed");
    auto &doc = window.document();
    const auto history = doc.history().total;
    auto *importAction = window.findChild<QAction *>("file.importStyle");

    // Each refusal opens "Nothing was applied." first (UX_DESIGN §18.7), then its sentence and reason.
    auto refuse = [&](const QString &path, const char *sentence, const char *reason) {
        bool seen = false;
        drive([&] { importAction->trigger(); }, [&] {
            if (auto *chooser = window.findChild<QFileDialog *>("styleImportFileDialog");
                chooser && chooser->isVisible()) {
                choose(*chooser, path);
                return false;
            }
            auto *dialog = window.findChild<QDialog *>("importStyleRefused");
            if (!dialog || !dialog->isVisible())
                return false;
            check(dialog->findChild<QLabel *>("styleRefusalTitle")->text() == "Nothing was applied.",
                  "The refusal says first that nothing was applied");
            const auto text = field<QLabel>(*dialog, "styleRefusalReason")->text();
            check(text.startsWith(sentence) && text.contains(reason),
                  "The refusal names its reason");
            seen = true;
            field<QPushButton>(*dialog, "importClose")->click();
            return true;
        });
        check(seen, sentence);
        check(doc.history().total == history, "A refusal changes nothing");
    };
    refuse(missing, "The edge texture is not in the file.", "has no bytes");
    refuse(hash, "The edge texture does not match its checksum.", "may be damaged");
    refuse(gif, "Import style refused.", "not an image SketchyUp can read");
    refuse(catalog, "This file is a catalog, not a style.", "not imported as a style");
    refuse(wide, "Import style refused.", "outside 1 to 8 px");
    refuse(huge, "Import refused: the texture is too large.", "17.0 MiB");
    refuse(bad, "Import style refused.", "not a style SketchyUp can read");
    check(folder.isValid() && QDir(folder.path()).entryList({"*.sketchystyle"}).isEmpty(),
          "Refusals write nothing to the library");

    // Choose another file reopens the chooser, and the next file is applied.
    bool reopened = false, applied = false;
    drive([&] { importAction->trigger(); }, [&] {
        if (auto *chooser = window.findChild<QFileDialog *>("styleImportFileDialog");
            chooser && chooser->isVisible()) {
            choose(*chooser, reopened ? good : bad);
            reopened = true;
            return false;
        }
        auto *dialog = window.findChild<QDialog *>("importStyleRefused");
        if (dialog && dialog->isVisible()) {
            field<QPushButton>(*dialog, "importChooseAnother")->click();
            return false;
        }
        auto *import = window.findChild<QDialog *>("importStyleDialog");
        if (import && import->isVisible()) {
            applied = true;
            field<QPushButton>(*import, "importPrimary")->click();
            return true;
        }
        return false;
    });
    check(reopened && applied, "Choose another file reopens the chooser and applies the next file");
    check(lastLabel(doc) == "Apply style “Good”", "The chosen file is applied in one labelled step");
}

// File > Export style…: an existing name is refused and the file is left alone, a new name writes
// the style, and a missing edge texture is named before any file dialog opens.
void exportStyle() {
    QTemporaryDir folder;
    Window window;
    window.resize(1180, 850);
    window.show();
    check(QTest::qWaitForWindowExposed(&window), "Window exposed");
    auto &doc = window.document();
    setModelStyle(window, strokeStyle(2, 1, EdgeDash::Dashed));
    auto *exportAction = window.findChild<QAction *>("file.exportStyle");
    check(exportAction, "Export style is a File entry");
    const auto existing = folder.filePath("Exists.sketchystyle");
    write(existing, QByteArray("keep me"));

    // Each button press is queued, so the file dialog it opens is driven by the timer on the next
    // tick (see queueClick). The stages run in order, one per tick.
    int stage = 0;
    bool refused = false;
    drive([&] { exportAction->trigger(); }, [&] {
        auto *dialog = window.findChild<QDialog *>("exportStyleDialog");
        auto *chooser = window.findChild<QFileDialog *>("exportStyleFileDialog");
        if (stage == 0 && dialog && dialog->isVisible()) {
            field<QLineEdit>(*dialog, "exportName")->setText("Exported");
            queueClick(field<QPushButton>(*dialog, "exportPrimary"));
            stage = 1;
        } else if (stage == 1 && chooser && chooser->isVisible()) {
            choose(*chooser, existing);
            stage = 2;
        } else if (stage == 2 && dialog && dialog->isVisible()) {
            refused = field<QLabel>(*dialog, "exportError")
                          ->text()
                          .startsWith("✕ Nothing was written.\nThat file already exists. An existing "
                                      "file is never replaced");
            queueClick(field<QPushButton>(*dialog, "exportCancel"));
            stage = 3;
        }
        return false;
    });
    check(refused && stage == 3, "An existing name is refused in the dialog");
    check(read(existing) == QByteArray("keep me"), "An existing file is never replaced");

    // A new name writes the style, and the file reads back with the model's stroke.
    const auto target = folder.filePath("New style.sketchystyle");
    stage = 0;
    drive([&] { exportAction->trigger(); }, [&] {
        auto *dialog = window.findChild<QDialog *>("exportStyleDialog");
        auto *chooser = window.findChild<QFileDialog *>("exportStyleFileDialog");
        if (stage == 0 && dialog && dialog->isVisible()) {
            queueClick(field<QPushButton>(*dialog, "exportPrimary"));
            stage = 1;
        } else if (stage == 1 && chooser && chooser->isVisible()) {
            choose(*chooser, target);
            stage = 2;
        }
        return false;
    });
    check(QFileInfo::exists(target), "A new name writes the style file");
    check(decodeStyleBundle(read(target)).style.edgeStroke->dash == EdgeDash::Dashed,
          "The exported file carries the model's stroke");

    // A missing edge texture is named when the dialog opens, and no file dialog or file follows.
    const auto lost = createAsset(doc, "Lost", "image/png");
    setModelStyle(window, strokeStyle(2, 1, EdgeDash::Solid, lost));
    const auto refusedTarget = folder.filePath("Refused.sketchystyle");
    bool namedAtOpen = false, chooserOpened = false;
    stage = 0;
    drive([&] { exportAction->trigger(); }, [&] {
        auto *dialog = window.findChild<QDialog *>("exportStyleDialog");
        if (auto *chooser = window.findChild<QFileDialog *>("exportStyleFileDialog");
            chooser && chooser->isVisible())
            chooserOpened = true;
        if (stage == 0 && dialog && dialog->isVisible()) {
            namedAtOpen = field<QLabel>(*dialog, "exportError")->text() ==
                          "✕ Nothing was written.\nExport refused: “Lost” (asset " +
                              QString::number(lost) + ") is missing from this model.";
            queueClick(field<QPushButton>(*dialog, "exportPrimary"));
            stage = 1;
        } else if (stage == 1 && dialog && dialog->isVisible()) {
            queueClick(field<QPushButton>(*dialog, "exportCancel"));
            stage = 2;
        }
        return false;
    });
    check(namedAtOpen, "The missing texture is named in the dialog");
    check(!chooserOpened && !QFileInfo::exists(refusedTarget),
          "A missing texture stops the export before any file dialog, and writes nothing");
}

// The File menu holds the two entries after their siblings, and Ctrl+K lists them by name.
void fileMenu() {
    Window window;
    window.show();
    check(QTest::qWaitForWindowExposed(&window), "Window exposed");
    QStringList names;
    QMenu *file = nullptr;
    for (auto *action : window.menuBar()->actions())
        if (action->menu() && action->text().remove('&') == "File")
            file = action->menu();
    check(file, "The File menu exists");
    for (auto *action : file->actions())
        if (!action->isSeparator())
            names << action->text();
    // Asset catalogs… (R100.f3) follows Import terrain…, and Import style… follows it; likewise
    // Export asset catalog… follows Export STEP…, and Export style… follows that.
    const auto importTerrain = names.indexOf("Import terrain…");
    const auto assetCatalogs = names.indexOf("Asset catalogs…");
    const auto exportStep = names.indexOf("Export STEP…");
    const auto exportCatalog = names.indexOf("Export asset catalog…");
    check(importTerrain >= 0 && assetCatalogs == importTerrain + 1 &&
              names.value(assetCatalogs + 1) == "Import style…",
          "Import style… follows Asset catalogs…, which follows Import terrain…");
    check(exportStep >= 0 && exportCatalog == exportStep + 1 &&
              names.value(exportCatalog + 1) == "Export style…",
          "Export style… follows Export asset catalog…, which follows Export STEP…");
    for (const auto *action : file->actions())
        if (action->text() == "Import style…" || action->text() == "Export style…")
            check(action->shortcut().isEmpty(), "The style entries have no default shortcut");
}

// The Styles tab's new controls have accessible names, and they stay reachable and legible at 200 %
// text: the application font is doubled, and every row and label still has its size.
void accessibleAndLargeText() {
    Window window;
    window.resize(1180, 850);
    window.show();
    check(QTest::qWaitForWindowExposed(&window), "Window exposed");
    auto &doc = window.document();
    setModelStyle(window, strokeStyle(2, 1, EdgeDash::Dashed));
    auto &panel = stylesTab(window);
    for (const char *name : {"styleEdgeStroke", "styleEdgeWidth", "styleEdgeOpacity", "styleEdgeDash",
                             "styleEdgeTexture", "styleLibraryFolder", "styleSearch", "styleLibrary",
                             "styleChooseFolder", "styleRefresh", "styleApplyLibrary",
                             "styleSaveLibrary"}) {
        auto *widget = field<QWidget>(panel, name);
        check(!widget->accessibleName().isEmpty(), name);
    }
    QFont large = QApplication::font();
    large.setPointSizeF(large.pointSizeF() * 2);
    const auto normal = QApplication::font();
    QApplication::setFont(large);
    auto *library = field<QListWidget>(panel, "styleLibrary");
    check(library->isVisible() && library->height() > 0, "The library list is shown at 200 % text");
    check(field<QPushButton>(panel, "styleApplyLibrary")->sizeHint().width() > 0 &&
              field<QPushButton>(panel, "styleSaveLibrary")->sizeHint().width() > 0,
          "The library buttons keep their size at 200 % text");
    QApplication::setFont(normal);
}

// The §18.7 keyboard order of the Styles tab: from the stroke's check box, Tab reaches each control
// in turn, the library list, then Choose folder, Refresh, Apply and Save. Each is reached, in order,
// by moving the focus chain forward.
void tabOrder() {
    Window window;
    window.resize(1180, 850);
    window.show();
    check(QTest::qWaitForWindowExposed(&window), "Window exposed");
    setModelStyle(window, strokeStyle(2, 1, EdgeDash::Dashed));
    auto &panel = stylesTab(window);
    const QStringList expected = {"styleEdgeStroke", "styleEdgeWidth", "styleEdgeOpacity",
                                  "styleEdgeDash", "styleEdgeTexture", "styleSearch", "styleLibrary",
                                  "styleChooseFolder", "styleRefresh", "styleApplyLibrary",
                                  "styleSaveLibrary"};
    QWidget *widget = field<QWidget>(panel, "styleEdgeStroke");
    QStringList reached;
    for (int step = 0; step < 400 && widget && reached.size() < expected.size(); ++step) {
        if (!widget->objectName().isEmpty() && expected.contains(widget->objectName()) &&
            (reached.isEmpty() || reached.last() != widget->objectName()))
            reached << widget->objectName();
        widget = widget->nextInFocusChain();
    }
    check(reached == expected, "The Styles tab's controls are reached in the §18.7 order");
}

} // namespace

// Runs every style case in order. A failing case is named; the rest still run, and the run fails
// at the end, so the report lists each failure.
// UX_DESIGN 18.5 and 18.7: the accounting bar's three parts differ without colour. The bar's brush
// styles and its painted pixels show solid, hatched and cross-hatched fills, one per part.
void reportBarFills() {
    CatalogAccountingBar bar(1, 1, 1);
    bar.resize(300, 14);
    check(bar.fills() ==
              std::vector<Qt::BrushStyle>{Qt::SolidPattern, Qt::BDiagPattern, Qt::DiagCrossPattern},
          "The three parts are solid, hatched and cross-hatched");
    // Each part is a third of the painted bar, in device pixels (the image is 2x at a 2x scale). Its
    // minority share is the share of pixels that differ from the part's most common colour: none for
    // a solid fill, the lines for a pattern.
    const auto image = bar.grab().toImage();
    const int third = image.width() / 3;
    double minority[3]{};
    for (int part = 0; part < 3; ++part) {
        QHash<QRgb, int> colours;
        for (int x = part * third; x < (part + 1) * third; ++x)
            for (int y = 0; y < image.height(); ++y)
                ++colours[image.pixel(x, y)];
        int most = 0, total = 0;
        for (auto it = colours.cbegin(); it != colours.cend(); ++it) {
            most = std::max(most, it.value());
            total += it.value();
        }
        minority[part] = 1.0 - double(most) / total;
    }
    check(minority[0] == 0.0, "The solid part paints one colour");
    check(minority[1] > 0.0 && minority[2] > minority[1],
          "The hatched part has lines, and the cross-hatched part has more of them");
}

// Types `path` in a file chooser and presses Save once, after the current timer tick. Save opens the
// overwrite prompt when the file exists, and a prompt opened inside the tick is never answered, since
// the tick does not run again while it is open. The accept is queued, so the drive's tick answers it.
void chooseLater(QFileDialog &chooser, const QString &path) {
    if (chooser.property("queued").toBool())
        return;
    chooser.setProperty("queued", true);
    auto *name = chooser.findChild<QLineEdit *>("fileNameEdit");
    check(name, "The file chooser has a file name field");
    name->setText(path);
    QMetaObject::invokeMethod(&chooser, "accept", Qt::QueuedConnection);
}

// Save report… writes the report's text, the text Copy report puts on the clipboard, to a .txt file
// the person chooses. The system dialog confirms an overwrite: No keeps the file, and a new name is
// written. The report is not stored in the model.
void reportSaveToFile(Window &window, const QTemporaryDir &files,
                      const QMap<QString, QByteArray> &fixtures) {
    const auto steps = history(window);
    resetLibrary(files, fixtures, {"basic"});
    const auto contents = [](const QString &path) {
        QFile file(path);
        return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
    };
    catalogDialog(window, [&](QDialog &dialog) {
        auto *list = child<QTreeWidget>(dialog, "catalogEntries");
        auto *insert = child<QPushButton>(dialog, "catalogInsert");
        selectOnly(*list, {"Block", "Oak tint"}, kBasic);
        writeFile(libraryFolder() + "/basic.sketchycat", fixtures.value("tampered-asset"));
        modalAfter(dialog, [&] { insert->click(); }, [&](QDialog &report) {
            checkText(child<QLabel>(report, "catalogReportLegend")->text(), "Solid · Inserted",
                      "The legend names the solid fill");
            checkText(child<QLabel>(report, "catalogReportLegend")->text(), "Hatched · Refused",
                      "The legend names the hatched fill");
            checkText(child<QLabel>(report, "catalogReportLegend")->text(),
                      "Cross-hatched · Not inserted", "The legend names the cross-hatched fill");
            check(child<CatalogAccountingBar>(report, "catalogReportBar")->fills() ==
                      std::vector<Qt::BrushStyle>{Qt::BDiagPattern, Qt::DiagCrossPattern},
                  "The refused part is hatched and the not-inserted part cross-hatched");

            child<QPushButton>(report, "catalogReportCopy")->click();
            const auto text = QApplication::clipboard()->text().toUtf8();
            check(text.startsWith("Asset catalog: nothing was inserted"),
                  "Copy report puts the report text on the clipboard");

            auto *save = child<QPushButton>(report, "catalogReportSave");
            check(save->isEnabled() && save->accessibleName() == "Save report",
                  "Save report is on, and it is named");

            // An existing file asks first. No keeps it, and the chooser is then cancelled.
            const auto existing = files.filePath("Catalog report.txt");
            writeFile(existing, QByteArray("keep me"));
            bool asked = false;
            drive([&] { save->click(); }, [&] {
                if (auto *question = qobject_cast<QMessageBox *>(QApplication::activeModalWidget())) {
                    asked = true;
                    question->button(QMessageBox::No)->click();
                    return false;
                }
                auto *chooser = qobject_cast<QFileDialog *>(QApplication::activeModalWidget());
                if (!chooser)
                    return false;
                if (asked)
                    chooser->reject();
                else
                    chooseLater(*chooser, existing);
                return false;
            });
            check(asked, "An existing file is confirmed before it is replaced");
            check(contents(existing) == QByteArray("keep me"), "No keeps the existing file");
            check(child<QLabel>(report, "catalogReportStatus")->text().isEmpty(),
                  "A cancelled save reports nothing");

            // A new name writes the report text, which is the text Copy report copied.
            const auto fresh = files.filePath("Catalog report 2.txt");
            drive([&] { save->click(); }, [&] {
                if (auto *chooser = qobject_cast<QFileDialog *>(QApplication::activeModalWidget()))
                    chooseLater(*chooser, fresh);
                return false;
            });
            check(contents(fresh) == text, "The file holds the text that Copy report copies");
            checkText(child<QLabel>(report, "catalogReportStatus")->text(),
                      "Saved the report as “Catalog report 2.txt”", "The status names the file");
        });
    });
    check(history(window) == steps, "Save report changes nothing in the model");
}

void runAll() {
    struct Test {
        const char *name;
        void (*run)();
    };
    const Test tests[] = {{"edge stroke controls", edgeStrokeControls},
                          {"profile width dimmed", profileWidthDimmed},
                          {"missing texture state", missingTextureState},
                          {"library states", libraryStates},
                          {"library bounds", libraryBounds},
                          {"save to library", saveToLibrary},
                          {"save to library refusal", saveToLibraryRefusal},
                          {"catalog report bar", reportBarFills},
                          {"import apply and save", importApplyAndSave},
                          {"import refusals", importRefusals},
                          {"export style", exportStyle},
                          {"file menu", fileMenu},
                          {"accessible names and large text", accessibleAndLargeText},
                          {"tab order", tabOrder}};
    std::string failures;
    for (const auto &test : tests) {
        try {
            test.run();
            std::cout << "ok: style " << test.name << '\n';
        } catch (const std::exception &error) {
            failures += std::string(test.name) + ": " + error.what() + "\n";
        }
    }
    if (!failures.empty())
        throw std::runtime_error("Style checks failed:\n" + failures);
}

} // namespace style_suite

int main(int argc, char **argv) {
    QSurfaceFormat format;
    format.setVersion(3, 3);
    format.setProfile(QSurfaceFormat::CoreProfile);
    format.setDepthBufferSize(24);
    sketchy::setDefaultViewportFormat(format);
    QTemporaryDir files;
    qputenv("XDG_CONFIG_HOME", files.path().toUtf8());
    qputenv("XDG_DATA_HOME", files.path().toUtf8());
    // The file and folder choosers are Qt's own widgets here, so the test can choose in them. A
    // platform chooser has no widgets to drive, and its selection is not the test's to set.
    qputenv("QT_QPA_PLATFORMTHEME", "generic");
    QApplication app(argc, argv);
    QSettings preferences("SketchyUp", "SketchyUp");
    preferences.setValue("recoverySeconds", 0);
    preferences.setValue("render/blenderPath", files.path() + "/absent");
    const auto fixtures = catalog_fixture::fixtures();
    // SKETCHYUP_CATALOG_CASE names one case to run alone (the R100.i case is run that way under load).
    const auto only = qEnvironmentVariable("SKETCHYUP_CATALOG_CASE");
    try {
        if (only.isEmpty())
            style_suite::runAll();
        Window window;
        window.resize(1280, 900);
        window.show();
        check(QTest::qWaitForWindowExposed(&window), "The window is exposed");
        window.demo();
        window.viewport()->refresh();
        if (only.isEmpty()) {
            fileMenuEntries(window);
            libraryFolderRemembered(window, files, fixtures);
            listsAndStates(window, files, fixtures);
            searchAndKindFilter(window, files, fixtures);
            openCatalogFile(window, files, fixtures);
            detailsPane(window, files, fixtures);
            insertOffWithReason(window, files, fixtures);
            templatesRefused(window, files, fixtures);
            severalEntriesOneUndoStep(window, files, fixtures);
            oneComponentUndoStep(window, files, fixtures);
            keyboardReachability(window, files, fixtures);
            refusedInsertReport(window, files, fixtures);
            style_suite::reportSaveToFile(window, files, fixtures);
            outsideAndLibraryInserts(window, files, fixtures);
            insertProgressAndCancel(window, files, fixtures);
            exportAssetCatalog(window, files);
            largeText(files, fixtures);
            selectionBudget(window, files);
            insertBudget(window, files);
        } else if (only == "insertProgress") {
            insertProgressAndCancel(window, files, fixtures);
        } else {
            throw std::runtime_error("SKETCHYUP_CATALOG_CASE names no case");
        }
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
    // The window above is destroyed with the try block. Dispatch the focus events its destruction
    // sent before the display is released (D-072).
    if (!drainWaylandEvents()) {
        std::cerr << "The compositor did not answer the sync before exit\n";
        return 1;
    }
    if (!only.isEmpty()) {
        std::cout << "Asset catalogs case " << only.toStdString() << " passes\n";
        return 0;
    }
    std::cout
        << "Asset catalogs checks pass: File menu, library folder, listing and states, search and "
           "filters, opened files, details, insert and undo, templates, keyboard, the report, "
           "export and text size; style checks pass: the Styles tab's edge stroke and library, "
           "Import and Export style with their refusals, the File menu, accessibility and tab order\n";
    return 0;
}
