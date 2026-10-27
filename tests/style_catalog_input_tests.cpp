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

#include <QAccessible>
#include <QAction>
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QItemSelectionModel>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMenu>
#include <QMenuBar>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSettings>
#include <QSplitter>
#include <QTemporaryDir>
#include <QTest>
#include <QTimer>
#include <QTreeWidget>
#include <QTreeWidgetItem>

#include <exception>
#include <functional>
#include <iostream>
#include <memory>

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

// Chooses `path` in a file chooser the way a person does: the chooser shows the file's folder, the
// file is selected in it, and Open or Save is pressed. A chooser that is not a QFileDialog fails.
void chooseFile(QDialog &chooser, const QString &path) {
    auto *picker = qobject_cast<QFileDialog *>(&chooser);
    check(picker, "A file chooser is open");
    const QFileInfo info(path);
    picker->setDirectory(info.absolutePath());
    // The chooser lists its folder asynchronously, so the file is selected after a short wait.
    QTest::qWait(300);
    picker->selectFile(info.fileName());
    check(picker->selectedFiles().contains(info.absoluteFilePath()),
          "The chooser selects the file");
    chooser.done(QDialog::Accepted);
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
        checkText(refused->child(0)->text(2), "Nothing is listed.",
                  "The refusal says nothing is listed");
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
        modal([&] { insert->click(); },
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
            modal([&] { insert->click(); },
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

} // namespace

int main(int argc, char **argv) {
    QSurfaceFormat format;
    format.setVersion(3, 3);
    format.setProfile(QSurfaceFormat::CoreProfile);
    format.setDepthBufferSize(24);
    sketchy::setDefaultViewportFormat(format);
    QTemporaryDir files;
    qputenv("XDG_CONFIG_HOME", files.path().toUtf8());
    qputenv("XDG_DATA_HOME", files.path().toUtf8());
    QApplication app(argc, argv);
    QSettings preferences("SketchyUp", "SketchyUp");
    preferences.setValue("recoverySeconds", 0);
    preferences.setValue("render/blenderPath", files.path() + "/absent");
    const auto fixtures = catalog_fixture::fixtures();
    try {
        Window window;
        window.resize(1280, 900);
        window.show();
        check(QTest::qWaitForWindowExposed(&window), "The window is exposed");
        window.demo();
        window.viewport()->refresh();
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
        exportAssetCatalog(window, files);
        largeText(files, fixtures);
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
    std::cout
        << "Asset catalogs checks pass: File menu, library folder, listing and states, search and "
           "filters, opened files, details, insert and undo, templates, keyboard, the report, "
           "export and text size\n";
    return 0;
}
