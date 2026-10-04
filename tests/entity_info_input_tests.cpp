#include "app/window.hpp"
#include "core/components.hpp"
#include "core/entity_measure.hpp"
#include "core/groups.hpp"
#include "core/tags.hpp"
#include "io/document_io.hpp"
#include <QAction>
#include <QApplication>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QPushButton>
#include <QSurfaceFormat>
#include <QTemporaryDir>
#include <QTest>
#include <QTimer>
#include <iostream>
#include <numbers>
using namespace sketchy;
void check(bool value, const char *message) {
    if (!value)
        throw std::runtime_error(message);
}
void type(QDialog *dialog, const QString &field, const QString &text) {
    auto *input = dialog->findChild<QLineEdit *>(field);
    input->setFocus();
    input->selectAll();
    QTest::keyClicks(input, text);
}
void accept(QDialog *dialog) {
    dialog->findChild<QDialogButtonBox *>()->button(QDialogButtonBox::Ok)->click();
}
void edit(Window &window, const std::function<void(QDialog *)> &operation, bool keyboard = false) {
    window.activateWindow();
    check(QTest::qWaitForWindowActive(&window), "Entity info window active");
    auto *frame = window.findChild<QComboBox *>("entityInfoFrame");
    frame->setFocus();
    bool opened = false;
    QTimer::singleShot(20, &window, [&] {
        auto *dialog = window.findChild<QDialog *>("entityInfoDialog");
        if (!dialog)
            return;
        dialog->activateWindow();
        if (!QTest::qWaitForWindowActive(dialog)) {
            dialog->reject();
            return;
        }
        opened = true;
        operation(dialog);
        if (dialog->isVisible())
            dialog->reject();
    });
    if (keyboard)
        QTest::keyClick(frame, Qt::Key_F2);
    else
        window.findChild<QPushButton *>("entityInfoEdit")->click();
    check(opened, "Native Entity info editor opened");
}
int main(int argc, char **argv) {
    QSurfaceFormat format;
    format.setDepthBufferSize(24);
    QSurfaceFormat::setDefaultFormat(format);
    QTemporaryDir preferences;
    qputenv("XDG_CONFIG_HOME", preferences.path().toUtf8());
    QApplication app(argc, argv);
    Window window;
    window.resize(1280, 850);
    auto &doc = window.document();
    auto *view = window.viewport();
    try {
        const auto body = doc.addFace({{{0, 0, 0}, {2, 0, 0}, {2, 3, 0}, {0, 3, 0}}});
        doc.extrude(body, 5, 4);
        const auto group = createGroup(doc, {body}, "Scaled parent");
        doc.transform(group, Transform::translation({10, 20, 30}) *
                                 Transform::rotation({0, 0, 1}, std::numbers::pi / 2) *
                                 Transform::scaling({-2, 3, .5}));
        const auto tag = createTag(doc, "Measured");
        QMetaObject::invokeMethod(view, "changed");
        window.show();
        check(QTest::qWaitForWindowExposed(&window), "Entity info window exposed");
        window.activateWindow();
        check(QTest::qWaitForWindowActive(&window), "Entity info active");
        view->enterContext(group);
        view->setSelection(body);
        view->fit();
        auto *tabs = window.findChild<QTabWidget *>("organizationTabs");
        tabs->setCurrentIndex(2);
        QTest::qWait(30);
        auto *frame = window.findChild<QComboBox *>("entityInfoFrame");
        auto *volume = window.findChild<QLabel *>("entityInfoVolume");
        check(volume->text().contains("72"), "Info reports mirrored world solid volume");
        frame->setCurrentIndex(2);
        check(volume->text().contains("24"), "Intrinsic readout excludes placement scale");
        const auto oldBody = *doc.bodies().at(body);
        const auto revision = doc.revision();
        edit(
            window,
            [&](QDialog *dialog) {
                dialog->findChild<QComboBox *>("entityEditFrame")->setCurrentIndex(1);
                type(dialog, "entityEditPosition0", "100cm");
                type(dialog, "entityEditDimensions0", "4m");
                type(dialog, "entityEditName", "Measured block");
                auto *tags = dialog->findChild<QComboBox *>("entityEditTag");
                tags->setCurrentIndex(tags->findData(QString::number(tag)));
                accept(dialog);
            },
            true);
        auto measured = measureEntity(doc, {body, SelectionKind::Body, 0});
        check(doc.revision() == revision + 1 && std::abs(measured.parentOrigin.x - 1) < tolerance &&
                  std::abs(measured.parent.bounds->dimensions().x - 4) < tolerance &&
                  doc.bodies().at(body)->name == "Measured block" &&
                  doc.bodies().at(body)->tag == tag,
              "Units, dimensions, name and tag publish as one atomic native edit");
        window.findChild<QAction *>("edit.undo")->trigger();
        check(*doc.bodies().at(body) == oldBody,
              "One undo restores every edited field and placement");
        view->setSelection(body);
        edit(window, [&](QDialog *dialog) {
            dialog->findChild<QComboBox *>("entityEditFrame")->setCurrentIndex(0);
            type(dialog, "entityEditDimensions0", "18m");
            type(dialog, "entityEditPosition0", "10m");
            accept(dialog);
        });
        measured = measureEntity(doc, {body, SelectionKind::Body, 0});
        check(std::abs(measured.worldOrigin.x - 10) < tolerance &&
                  std::abs(measured.world.bounds->dimensions().x - 18) < tolerance,
              "Explicit origin wins after resizing mirrored off-origin bounds");
        window.findChild<QAction *>("edit.undo")->trigger();
        view->setSelection(body);
        const auto beforeInvalid = encodeDocument(doc);
        bool retained = false;
        edit(window, [&](QDialog *dialog) {
            dialog->findChild<QComboBox *>("entityEditFrame")->setCurrentIndex(1);
            type(dialog, "entityEditPosition0", "55m");
            type(dialog, "entityEditName", "Corrected block");
            type(dialog, "entityEditDimensions0", "0m");
            accept(dialog);
            retained = dialog->isVisible() &&
                       !dialog->findChild<QLabel *>("entityEditError")->text().isEmpty() &&
                       dialog->findChild<QLineEdit *>("entityEditDimensions0")->text() == "0m" &&
                       encodeDocument(doc) == beforeInvalid;
            type(dialog, "entityEditDimensions0", "4m");
            accept(dialog);
        });
        check(retained && doc.bodies().at(body)->name == "Corrected block",
              "Invalid geometry retains input and atomically rolls back before correction");
        window.findChild<QAction *>("edit.undo")->trigger();
        view->setSelection(body);
        const auto beforeCancel = encodeDocument(doc);
        edit(window, [&](QDialog *dialog) {
            type(dialog, "entityEditDimensions0", "20m");
            dialog->reject();
        });
        check(encodeDocument(doc) == beforeCancel, "Cancel leaves exact document unchanged");
        const auto oldLocale = QLocale();
        QLocale::setDefault(QLocale(QLocale::German, QLocale::Germany));
        edit(window, [&](QDialog *dialog) {
            dialog->findChild<QComboBox *>("entityEditFrame")->setCurrentIndex(1);
            type(dialog, "entityEditDimensions0", "3,5m");
            accept(dialog);
        });
        check(std::abs(
                  measureEntity(doc, {body, SelectionKind::Body, 0}).parent.bounds->dimensions().x -
                  3.5) < tolerance,
              "Native dimension fields accept decimal-comma locale units");
        QLocale::setDefault(oldLocale);
        window.findChild<QAction *>("edit.undo")->trigger();
        view->setSelection(body);
        edit(window, [&](QDialog *dialog) {
            dialog->findChild<QComboBox *>("entityEditFrame")->setCurrentIndex(0);
            type(dialog, "entityEditPosition1", "18in");
            accept(dialog);
        });
        check(std::abs(doc.worldTransform(body).point({}).y - .4572) < tolerance,
              "Imperial position entry updates actual world coordinates");
        window.findChild<QAction *>("edit.undo")->trigger();
        view->setSelection(body);
        bool stale = false;
        edit(window, [&](QDialog *dialog) {
            type(dialog, "entityEditName", "Stale name");
            createTag(doc, "Concurrent metadata");
            const auto external = encodeDocument(doc);
            accept(dialog);
            stale = dialog->isVisible() &&
                    dialog->findChild<QLabel *>("entityEditError")->text().contains("changed") &&
                    encodeDocument(doc) == external;
        });
        check(stale && doc.bodies().at(body)->name == oldBody.name,
              "Stale form rejects without overwriting a newer document");
        const auto component = createComponent(doc, body, "Block");
        const auto member = component.movedGeometry.at(body);
        const auto peer =
            placeComponent(doc, component.definition, Transform::translation({5, 0, 0}), group)
                .instance;
        QMetaObject::invokeMethod(view, "changed");
        view->setSelection(body);
        const auto peerPose = doc.worldTransform(peer);
        edit(window, [&](QDialog *dialog) {
            dialog->findChild<QComboBox *>("entityEditFrame")->setCurrentIndex(1);
            type(dialog, "entityEditDimensions0", "4m");
            accept(dialog);
        });
        check(doc.worldTransform(peer) == peerPose &&
                  std::abs(measureEntity(doc, {body, SelectionKind::Body, 0})
                               .parent.bounds->dimensions()
                               .x -
                           4) < tolerance &&
                  std::abs(measureEntity(doc, {peer, SelectionKind::Body, 0})
                               .parent.bounds->dimensions()
                               .x -
                           2) < tolerance,
              "Closed component dimension edit changes only that placement");
        window.findChild<QAction *>("edit.undo")->trigger();
        view->enterContext(body);
        view->setSelection(member);
        edit(window, [&](QDialog *dialog) {
            dialog->findChild<QComboBox *>("entityEditFrame")->setCurrentIndex(1);
            type(dialog, "entityEditDimensions0", "4m");
            type(dialog, "entityEditName", "Shared block");
            accept(dialog);
        });
        const auto peerMember = doc.instances().at(peer)->members.at(member);
        check(doc.bodies().at(peerMember)->name == "Shared block" &&
                  std::abs(measureEntity(doc, {peerMember, SelectionKind::Body, 0})
                               .parent.bounds->dimensions()
                               .x -
                           4) < tolerance,
              "Open component member fields publish through explicit shared scope");
        view->enterContext(0);
        const auto sheet = doc.addFace({{{0, 0, 0}, {1, 0, 0}, {1, 1, 0}, {0, 1, 0}}});
        QMetaObject::invokeMethod(view, "changed");
        view->setSelection(sheet);
        check(volume->text().contains("Not a solid"),
              "Open surface reports an explicit unavailable volume");
        const auto unchanged = doc.saveStamp();
        window.findChild<QPushButton *>("entityInfoProblems")->click();
        check(view->selectionState().entities().size() == 1 &&
                  view->selectionState().entities().begin()->kind == SelectionKind::Edge &&
                  doc.isCurrentSnapshot(unchanged),
              "Inspect problems selects a boundary edge without mutating geometry");
        const auto guide = doc.bodies().at(sheet)->surface.nextId;
        doc.addGuide(sheet, guideLine({0, 0, 0}, {1, 0, 0}));
        QMetaObject::invokeMethod(view, "changed");
        view->selectEntities({{sheet, SelectionKind::Guide, guide}});
        check(window.findChild<QLabel *>("entityInfoLength")->text() == "Infinite guide" &&
                  !window.findChild<QPushButton *>("entityInfoEdit")->isEnabled(),
              "Infinite guide readout avoids fabricated lengths and whole-entity editing");
        view->enterContext(group);
        view->setSelection(body);
        frame->setCurrentIndex(0);
        view->fit();
        QTest::qWait(40);
        QTemporaryDir files;
        const auto path = files.filePath("info.sketchyup");
        saveDocument(doc, path);
        check(encodeDocument(loadDocument(path)) == encodeDocument(doc),
              "Native entity edits survive exact save/reopen");
        if (argc == 2)
            check(window.grab().save(QString::fromLocal8Bit(argv[1])),
                  "Entity info screenshot saved");
        doc.markSaved();
        std::cout << "Native entity frames, metric/imperial/locale edits, atomic undo, retained "
                     "errors, shared scope and diagnostics passed; DPR="
                  << view->devicePixelRatioF() << '\n';
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        doc.markSaved();
        return 1;
    }
}
