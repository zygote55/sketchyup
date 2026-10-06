#include "app/sections_panel.hpp"
#include "app/window.hpp"
#include "core/sections.hpp"
#include "io/document_io.hpp"
#include <QAction>
#include <QApplication>
#include <QCheckBox>
#include <QColorDialog>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QLineEdit>
#include <QPushButton>
#include <QSurfaceFormat>
#include <QTemporaryDir>
#include <QTest>
#include <QTimer>
#include <QWindow>
#include <iostream>
using namespace sketchy;
namespace {
void check(bool value, const char *message) {
    if (!value)
        throw std::runtime_error(message);
}
void sync(Window &window) {
    window.viewport()->refresh();
    QMetaObject::invokeMethod(window.viewport(), "changed");
    QCoreApplication::processEvents();
}
void click(Window &window, const char *name) {
    auto *button = window.findChild<QPushButton *>(name);
    check(button, "Section action exists");
    button->click();
}
void type(QDialog *dialog, const char *name, const QString &text) {
    auto *field = dialog->findChild<QLineEdit *>(name);
    check(field, "Section input exists");
    field->setFocus();
    field->selectAll();
    QTest::keyClicks(field, text);
}
void accept(QDialog *dialog) {
    dialog->findChild<QDialogButtonBox *>()->button(QDialogButtonBox::Save)->click();
}
void modal(Window &window, const char *button, const std::function<void(QDialog *)> &operation) {
    bool opened = false;
    std::exception_ptr failure;
    QTimer timer;
    timer.setInterval(10);
    QObject::connect(&timer, &QTimer::timeout, &window, [&] {
        auto *dialog = window.findChild<QDialog *>("sectionPlaneDialog");
        if (!dialog || !dialog->isVisible())
            return;
        timer.stop();
        dialog->activateWindow();
        if (!QTest::qWaitFor(
                [&] { return QGuiApplication::focusWindow() == dialog->windowHandle(); })) {
            dialog->reject();
            return;
        }
        opened = true;
        try {
            operation(dialog);
        } catch (...) {
            failure = std::current_exception();
        }
        if (dialog->isVisible())
            dialog->reject();
    });
    timer.start();
    click(window, button);
    check(opened, "Native section dialog opened");
    if (failure)
        std::rethrow_exception(failure);
}
} // namespace
int main(int argc, char **argv) {
    QSurfaceFormat format;
    format.setVersion(3, 3);
    format.setProfile(QSurfaceFormat::CoreProfile);
    format.setDepthBufferSize(24);
    QSurfaceFormat::setDefaultFormat(format);
    QTemporaryDir isolated;
    qputenv("XDG_CONFIG_HOME", isolated.path().toUtf8());
    QApplication app(argc, argv);
    try {
        Window window;
        window.resize(1280, 950);
        auto &doc = window.document();
        const auto body = doc.addFace({{{0, 0, 0}, {2, 0, 0}, {2, 2, 0}, {0, 2, 0}}});
        doc.extrude(body, doc.bodies().at(body)->surface.faces.begin()->first, 2);
        doc.setDisplayUnits(DisplayUnit::Millimeters);
        doc.markSaved();
        const auto saved = doc.saveStamp();
        const auto geometry = doc.bodies();
        window.show();
        check(QTest::qWaitForWindowExposed(&window), "Section window exposed");
        auto *action = window.findChild<QAction *>("view.sections");
        check(action, "View menu exposes section planes");
        action->trigger();
        sync(window);
        auto *panel = window.findChild<QWidget *>("sectionsPanel");
        check(panel && panel->isVisible(), "Sections tab opens");
        const auto history = doc.history().total;
        modal(window, "sectionNewButton", [&](QDialog *dialog) {
            type(dialog, "sectionName", "Horizontal cut");
            type(dialog, "sectionDistance", "500mm");
            type(dialog, "sectionNormal2", "0");
            accept(dialog);
            check(dialog->isVisible() &&
                      !dialog->findChild<QLabel *>("sectionDialogError")->text().isEmpty() &&
                      doc.sections().empty(),
                  "Zero normal remains editable without model changes");
            type(dialog, "sectionNormal2", "2");
            accept(dialog);
        });
        check(doc.sections().size() == 1 && doc.history().total == history + 1 &&
                  doc.bodies() == geometry,
              "Create and activate is one edit without geometry changes");
        const auto id = doc.sections().begin()->first;
        check(doc.activeSections() == ActiveSections{{0, id}} &&
                  doc.sections().at(id)->plane == SectionPlane{{0, 0, 1}, -.5},
              "Native normal and units normalize exactly");
        doc.undo();
        sync(window);
        check(doc.sections().empty() && doc.activeSections().empty() &&
                  doc.isCurrentSnapshot(saved),
              "One Undo restores saved state");
        doc.redo();
        sync(window);
        auto *list = window.findChild<QListWidget *>("sectionPlanesList");
        check(list && list->count() == 1, "Redo restores stable list identity");
        list->setCurrentRow(0);
        click(window, "sectionOffButton");
        check(doc.activeSections().empty(), "Context off deactivates plane");
        const auto revision = doc.revision();
        click(window, "sectionOffButton");
        check(doc.revision() == revision, "Repeated off is a no-op");
        click(window, "sectionActivateButton");
        const auto original = *doc.sections().at(id);
        click(window, "sectionFlipButton");
        check(doc.sections().at(id)->plane.normal == original.plane.normal * -1 &&
                  doc.sections().at(id)->plane.offset == -original.plane.offset,
              "Reverse negates normal and offset together");
        doc.undo();
        sync(window);
        list->setCurrentRow(0);
        modal(window, "sectionEditButton", [&](QDialog *dialog) {
            auto *context = dialog->findChild<QComboBox *>("sectionContext");
            context->setCurrentIndex(context->findData(QVariant::fromValue<qulonglong>(body)));
            dialog->findChild<QCheckBox *>("sectionFill")->setChecked(false);
            dialog->findChild<QCheckBox *>("sectionEdges")->setChecked(false);
            accept(dialog);
        });
        check(doc.sections().at(id)->context == body && !doc.sections().at(id)->fill &&
                  !doc.sections().at(id)->edges && doc.activeSections().empty(),
              "Relocation preserves local values and deactivates old context");
        click(window, "sectionActivateButton");
        check(doc.activeSections() == ActiveSections{{body, id}},
              "Native activation uses selected context");
        auto precise = *doc.sections().at(id);
        precise.plane.offset = -.123456789123456;
        precise.color = {.1234567f, .2345678f, .3456789f};
        updateSection(doc, id, precise);
        sync(window);
        list->setCurrentRow(0);
        const auto exact = encodeContainer(doc);
        modal(window, "sectionEditButton", [&](QDialog *dialog) { accept(dialog); });
        check(encodeContainer(doc) == exact, "Untouched edit preserves precision and history");
        modal(window, "sectionEditButton", [&](QDialog *dialog) {
            type(dialog, "sectionDistance", "1m");
            doc.move(body, {1, 0, 0});
            const auto moved = encodeContainer(doc);
            accept(dialog);
            check(dialog->isVisible() && encodeContainer(doc) == moved,
                  "Stale section draft cannot overwrite current document");
        });
        sync(window);
        list->setCurrentRow(0);
        click(window, "sectionDeleteButton");
        check(doc.sections().empty() && doc.activeSections().empty(),
              "Delete removes plane and activation");
        doc.undo();
        sync(window);
        check(doc.sections().contains(id) && doc.activeSections() == ActiveSections{{body, id}},
              "Delete Undo restores both record and activation");
        const auto capture = qEnvironmentVariable("SKETCHYUP_SECTION_EDITOR_EVIDENCE");
        if (!capture.isEmpty()) {
            list->setCurrentRow(0);
            modal(window, "sectionEditButton", [&](QDialog *dialog) {
                check(dialog->grab().save(capture), "Raw section editor saved");
            });
        }
        check(decodeContainer(encodeContainer(doc)).activeSections() == doc.activeSections(),
              "Native section state persists");
        doc.markSaved();
        window.close();
        std::cout << "Native section authoring, context, precise units, history, stale drafts and "
                     "persistence passed\n";
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
