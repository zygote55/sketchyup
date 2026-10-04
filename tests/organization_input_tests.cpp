#include "app/window.hpp"
#include "core/components.hpp"
#include "core/groups.hpp"
#include "core/tags.hpp"
#include "io/document_io.hpp"
#include <QAction>
#include <QApplication>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QHeaderView>
#include <QMimeData>
#include <QPushButton>
#include <QSurfaceFormat>
#include <QTemporaryDir>
#include <QTest>
#include <QTimer>
#include <QTreeWidgetItemIterator>
#include <iostream>
using namespace sketchy;
void check(bool condition, const char *message) {
    if (!condition)
        throw std::runtime_error(message);
}
QTreeWidgetItem *row(QTreeWidget *tree, Id id) {
    for (QTreeWidgetItemIterator it(tree); *it; ++it)
        if ((*it)->data(0, Qt::UserRole).toULongLong() == id)
            return *it;
    throw std::runtime_error("Expected tree row missing");
}
void current(QTreeWidget *tree, Id id) {
    tree->window()->activateWindow();
    check(QTest::qWaitForWindowActive(tree->window()),
          "Hierarchy window active for keyboard input");
    tree->setCurrentItem(row(tree, id));
    tree->setFocus();
    // A modal dialog's deferred focus restoration can arrive after activation.
    // Set the test's input target after draining those events and verify it.
    QTest::qWait(10);
    tree->setFocus();
    check(QTest::qWaitFor([&] { return tree->hasFocus(); }),
          "Hierarchy tree focused before shortcut input");
}
void submit(Window &window, const std::function<void(QDialog *)> &fill) {
    QTimer::singleShot(20, &window, [&window, fill] {
        auto *dialog = window.findChild<QDialog *>("organizationDialog");
        if (!dialog)
            return;
        // Wait for the compositor to activate the modal before closing it.
        // Otherwise its delayed activation can steal focus from the next test.
        dialog->activateWindow();
        if (!QTest::qWaitForWindowActive(dialog)) {
            dialog->reject();
            return;
        }
        fill(dialog);
        dialog->findChild<QDialogButtonBox *>()->button(QDialogButtonBox::Ok)->click();
        if (dialog->isVisible())
            dialog->reject();
    });
}
void nameForm(Window &window, const QString &name) {
    submit(window, [name](QDialog *dialog) {
        dialog->findChild<QLineEdit *>("organizationName")->setText(name);
    });
}
void parentForm(Window &window, Id parent) {
    submit(window, [parent](QDialog *dialog) {
        auto *choice = dialog->findChild<QComboBox *>("organizationParent");
        choice->setCurrentIndex(choice->findData(QString::number(parent)));
    });
}
bool drop(QTreeWidget *tree, QMimeData *mime, QTreeWidgetItem *target) {
    const QPoint position =
        target ? tree->visualItemRect(target).center() : QPoint(8, tree->viewport()->height() - 8);
    QDragEnterEvent enter(position, Qt::MoveAction, mime, Qt::LeftButton, {});
    QCoreApplication::sendEvent(tree->viewport(), &enter);
    if (!enter.isAccepted())
        return false;
    QDragMoveEvent move(position, Qt::MoveAction, mime, Qt::LeftButton, {});
    QCoreApplication::sendEvent(tree->viewport(), &move);
    if (!move.isAccepted())
        return false;
    QDropEvent event(position, Qt::MoveAction, mime, Qt::LeftButton, {});
    QCoreApplication::sendEvent(tree->viewport(), &event);
    return event.isAccepted();
}
std::unique_ptr<QMimeData> dragData(QTreeWidget *tree) {
    return std::unique_ptr<QMimeData>(tree->model()->mimeData({tree->currentIndex()}));
}
int main(int argc, char **argv) {
    QSurfaceFormat format;
    format.setDepthBufferSize(24);
    QSurfaceFormat::setDefaultFormat(format);
    QTemporaryDir preferences;
    qputenv("XDG_CONFIG_HOME", preferences.path().toUtf8());
    QApplication app(argc, argv);
    Window window;
    window.resize(1280, 800);
    auto &doc = window.document();
    auto *view = window.viewport();
    try {
        const auto a = doc.addFace({{{0, 0, 0}, {1, 0, 0}, {1, 1, 0}, {0, 1, 0}}});
        const auto b = doc.addFace({{{3, 0, 0}, {4, 0, 0}, {4, 1, 0}, {3, 1, 0}}});
        const auto group = createGroup(doc, {a}, "Frame");
        doc.move(group, {1, 2, 0});
        const auto pose = doc.worldTransform(a);
        QMetaObject::invokeMethod(view, "changed");
        window.show();
        check(QTest::qWaitForWindowExposed(&window), "Organization window exposed");
        window.activateWindow();
        check(QTest::qWaitForWindowActive(&window), "Organization window active");
        view->standardView(1);
        view->fit();
        auto *tree = window.findChild<QTreeWidget *>("outlinerTree");
        auto *tags = window.findChild<QTreeWidget *>("tagTree");
        auto *tabs = window.findChild<QTabWidget *>("organizationTabs");
        auto *search = window.findChild<QLineEdit *>("outlinerSearch");
        check(row(tree, a)->parent() == row(tree, group), "Outliner represents actual ownership");
        view->setSelection(group);
        check(tree->selectedItems().size() == 1 && tree->selectedItems()[0] == row(tree, group),
              "Viewport selection appears in hierarchy");
        current(tree, b);
        check(view->selectedBody() == b, "Hierarchy selection appears in viewport");
        search->setText("Frame");
        check(!row(tree, group)->isHidden() && row(tree, b)->isHidden(),
              "Hierarchy search filters siblings");
        search->clear();
        current(tree, b);
        nameForm(window, "Loose panel");
        QTest::keyClick(tree, Qt::Key_F2);
        check(doc.bodies().at(b)->name == "Loose panel", "F2 renames through native form");
        QTest::keyClick(tree, Qt::Key_Space);
        check(doc.bodies().at(b)->hidden, "Space hides entity persistently");
        window.findChild<QPushButton *>("outliner.hideButton")->click();
        check(!doc.bodies().at(b)->hidden, "Button reveals the focused hidden entity");
        current(tree, b);
        QTest::keyClick(tree, Qt::Key_L, Qt::ControlModifier | Qt::ShiftModifier);
        check(doc.bodies().at(b)->locked, "Keyboard locks focused entity");
        window.findChild<QPushButton *>("outliner.lockButton")->click();
        check(!doc.bodies().at(b)->locked, "Button unlocks focused entity");
        current(tree, a);
        parentForm(window, 0);
        QTest::keyClick(tree, Qt::Key_M, Qt::ControlModifier | Qt::ShiftModifier);
        check(!doc.bodies().at(a)->parent && doc.worldTransform(a) == pose,
              "Keyboard reparent preserves exact world frame");
        window.findChild<QAction *>("edit.undo")->trigger();
        check(doc.bodies().at(a)->parent == group && doc.worldTransform(a) == pose,
              "Hierarchy reparent undoes in one step");
        current(tree, b);
        const auto bPose = doc.worldTransform(b);
        auto mime = dragData(tree);
        check(drop(tree, mime.get(), row(tree, group)) && doc.bodies().at(b)->parent == group &&
                  doc.worldTransform(b) == bPose,
              "Native drop event reparents through the same world-preserving command");
        window.findChild<QAction *>("edit.undo")->trigger();
        current(tree, b);
        mime = dragData(tree);
        renameEntity(doc, b, "Changed during drag");
        QMetaObject::invokeMethod(view, "changed");
        const auto staleSnapshot = encodeDocument(doc);
        check(!drop(tree, mime.get(), row(tree, group)) && encodeDocument(doc) == staleSnapshot,
              "A stale drag rejects without mutating document or visual ownership");
        current(tree, group);
        mime = dragData(tree);
        const auto cycleSnapshot = encodeDocument(doc);
        check(!drop(tree, mime.get(), row(tree, group)) && encodeDocument(doc) == cycleSnapshot,
              "Cyclic drop rejects atomically");
        current(tree, group);
        QTest::keyClick(tree, Qt::Key_Return);
        check(view->selectionState().context() == group,
              ("Enter opens focused context: actual=" +
               std::to_string(view->selectionState().context()) +
               " expected=" + std::to_string(group) + " current=" +
               std::to_string(tree->currentItem()
                                  ? tree->currentItem()->data(0, Qt::UserRole).toULongLong()
                                  : 0))
                  .c_str());
        current(tree, a);
        nameForm(window, "Nested panel");
        window.findChild<QPushButton *>("outliner.renameButton")->click();
        check(doc.bodies().at(a)->name == "Nested panel", "Pointer rename uses same command");
        view->enterContext(0);
        tabs->setCurrentIndex(1);
        nameForm(window, "Assembly");
        window.findChild<QPushButton *>("tags.folderButton")->click();
        const auto folder = doc.tags().rbegin()->first;
        current(tags, folder);
        nameForm(window, "Panels");
        window.findChild<QPushButton *>("tags.newButton")->click();
        const auto tag = doc.tags().rbegin()->first;
        check(doc.tags().at(tag)->parent == folder && row(tags, tag)->parent() == row(tags, folder),
              "Tag form creates folder-owned records");
        tabs->setCurrentIndex(0);
        current(tree, group);
        submit(window, [tag](QDialog *dialog) {
            auto *choice = dialog->findChild<QComboBox *>("organizationTag");
            choice->setCurrentIndex(choice->findData(QString::number(tag)));
        });
        window.findChild<QPushButton *>("outliner.tagButton")->click();
        check(doc.bodies().at(group)->tag == tag, "Assign tag form updates selected entity");
        const auto bodies = doc.bodies();
        tabs->setCurrentIndex(1);
        current(tags, folder);
        QTest::keyClick(tags, Qt::Key_Space);
        check(!doc.tags().at(folder)->visible && doc.bodies() == bodies,
              "Folder visibility checkbox changes no geometry records");
        check(!view->selectionAt(view->project(pose.point({.5, .5, 0}))),
              "Hidden tag prevents picking");
        current(tags, folder);
        const auto checkbox = QPoint(tags->header()->sectionViewportPosition(1) + 10,
                                     tags->visualItemRect(row(tags, folder)).center().y());
        QTest::mouseClick(tags->viewport(), Qt::LeftButton, {}, checkbox);
        check(doc.tags().at(folder)->visible, "Pointer restores folder visibility");
        current(tags, tag);

        parentForm(window, 0);
        QTest::keyClick(tags, Qt::Key_M, Qt::ControlModifier | Qt::ShiftModifier);
        check(!doc.tags().at(tag)->parent && doc.bodies() == bodies,
              ("Tag reparent leaves scene ownership intact: parent=" +
               std::to_string(doc.tags().at(tag)->parent) +
               " bodies=" + std::to_string(doc.bodies() == bodies) + " current=" +
               std::to_string(tags->currentItem()
                                  ? tags->currentItem()->data(0, Qt::UserRole).toULongLong()
                                  : 0) +
               " error=" + window.findChild<QLabel *>("organizationError")->text().toStdString())
                  .c_str());
        current(tags, tag);
        mime = dragData(tags);
        check(drop(tags, mime.get(), row(tags, folder)) && doc.tags().at(tag)->parent == folder &&
                  doc.bodies() == bodies,
              "Tag drag changes folder organization without touching scene parents");
        bool invalidRetained = false;
        const auto invalidSnapshot = encodeDocument(doc);
        QTimer::singleShot(20, &window, [&] {
            auto *dialog = window.findChild<QDialog *>("organizationDialog");
            if (!dialog)
                return;
            auto *field = dialog->findChild<QLineEdit *>("organizationName");
            field->clear();
            dialog->findChild<QDialogButtonBox *>()->button(QDialogButtonBox::Ok)->click();
            invalidRetained =
                dialog->isVisible() && field->text().isEmpty() &&
                !dialog->findChild<QLabel *>("organizationDialogError")->text().isEmpty() &&
                encodeDocument(doc) == invalidSnapshot;
            field->setText("Panel surfaces");
            dialog->findChild<QDialogButtonBox *>()->button(QDialogButtonBox::Ok)->click();
        });
        window.findChild<QPushButton *>("tags.renameButton")->click();
        check(invalidRetained && doc.tags().at(tag)->name == "Panel surfaces",
              "Invalid tag rename retains form and unchanged document before valid retry");
        const auto beforeDelete = encodeDocument(doc);
        window.findChild<QPushButton *>("tags.deleteButton")->click();
        check(encodeDocument(doc) == beforeDelete &&
                  !window.findChild<QLabel *>("organizationError")->text().isEmpty(),
              "Used-tag deletion rejects inline without changing document");
        tabs->setCurrentIndex(0);
        const auto component = createComponent(doc, b, "Reusable");
        const auto member = component.movedGeometry.at(b);
        const auto peer = placeComponent(doc, component.definition).instance;
        QMetaObject::invokeMethod(view, "changed");
        current(tree, b);
        QTest::keyClick(tree, Qt::Key_Return);
        current(tree, member);
        nameForm(window, "Shared panel");
        QTest::keyClick(tree, Qt::Key_F2);
        const auto peerMember = doc.instances().at(peer)->members.at(member);
        check(doc.bodies().at(member)->name == "Shared panel" &&
                  doc.bodies().at(peerMember)->name == "Shared panel",
              "Outliner rename inside component edits shared members");
        current(tree, b);
        nameForm(window, "Local placement");
        QTest::keyClick(tree, Qt::Key_F2);
        check(doc.bodies().at(b)->name == "Local placement" &&
                  doc.bodies().at(peer)->name != "Local placement",
              ("Open component root rename remains local: name=" + doc.bodies().at(b)->name +
               " peer=" + doc.bodies().at(peer)->name + " current=" +
               std::to_string(tree->currentItem()
                                  ? tree->currentItem()->data(0, Qt::UserRole).toULongLong()
                                  : 0) +
               " selected=" + std::to_string(view->selectedBody()) +
               " error=" + window.findChild<QLabel *>("organizationError")->text().toStdString())
                  .c_str());
        QTemporaryDir files;
        const auto path = files.filePath("organized.sketchyup");
        saveDocument(doc, path);
        check(encodeDocument(loadDocument(path)) == encodeDocument(doc),
              "Organization survives exact save/reopen");
        view->fit();
        QTest::qWait(50);
        if (argc == 2) {
            check(window.grab().save(QString::fromLocal8Bit(argv[1])),
                  "Organization screenshot saved");
            tabs->setCurrentIndex(1);
            QTest::qWait(30);
            check(window.grab().save(QString::fromLocal8Bit(argv[1]) + ".tags.png"),
                  "Tags screenshot saved");
        }
        doc.markSaved();
        std::cout << "Native hierarchy, search, selection, keyboard/pointer metadata, tags, "
                     "visibility, shared scope and persistence passed; DPR="
                  << view->devicePixelRatioF() << '\n';
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        doc.markSaved();
        return 1;
    }
}
