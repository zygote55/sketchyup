#include "app/surface_format.hpp"
#include "app/text_panel.hpp"
#include "app/window.hpp"
#include "automation/text_commands.hpp"
#include "core/components.hpp"
#include "io/document_io.hpp"
#include <QAction>
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QTemporaryDir>
#include <QTest>
#include <QTimer>
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
    check(button, "Native text button exists");
    button->click();
}
void type(QDialog *dialog, const char *name, QString text) {
    auto *field = dialog->findChild<QLineEdit *>(name);
    check(field, "Text input exists");
    field->setFocus();
    field->selectAll();
    QTest::keyClicks(field, text);
}
void save(QDialog *dialog) {
    dialog->findChild<QDialogButtonBox *>()->button(QDialogButtonBox::Save)->click();
}
void finished(QDialog *dialog) {
    check(QTest::qWaitFor(
              [&] {
                  return !dialog->isVisible() || dialog->findChild<QDialogButtonBox *>()
                                                     ->button(QDialogButtonBox::Save)
                                                     ->isEnabled();
              },
              15000),
          "Background text generation finishes");
}
void modal(Window &window, const char *button, const std::function<void(QDialog *)> &operation) {
    bool opened{};
    std::exception_ptr failure;
    QTimer timer;
    timer.setInterval(10);
    QObject::connect(&timer, &QTimer::timeout, &window, [&] {
        auto *dialog = window.findChild<QDialog *>("textDialog");
        if (!dialog || !dialog->isVisible())
            return;
        timer.stop();
        opened = true;
        dialog->activateWindow();
        try {
            check(QTest::qWaitFor(
                      [&] { return QGuiApplication::focusWindow() == dialog->windowHandle(); }),
                  "Native text dialog receives keyboard focus");
            operation(dialog);
        } catch (...) {
            failure = std::current_exception();
        }
        if (dialog->isVisible())
            dialog->reject();
    });
    timer.start();
    if (button) {
        click(window, button);
    } else {
        window.activateWindow();
        check(QTest::qWaitForWindowActive(&window), "Text shortcut parent activates");
        auto *panel = window.findChild<QWidget *>("textPanel");
        panel->setFocus();
        check(QTest::qWaitFor([&] {
                  return panel->hasFocus() || panel->isAncestorOf(QApplication::focusWidget());
              }),
              "Text panel owns shortcut focus");
        QTest::keyClick(QApplication::focusWidget(), Qt::Key_T,
                        Qt::ControlModifier | Qt::AltModifier | Qt::ShiftModifier);
    }
    check(opened, "Native text editor opens");
    if (failure)
        std::rethrow_exception(failure);
}
} // namespace
int main(int argc, char **argv) {
    QSurfaceFormat format;
    format.setVersion(3, 3);
    format.setProfile(QSurfaceFormat::CoreProfile);
    format.setDepthBufferSize(24);
    sketchy::setDefaultViewportFormat(format);
    QTemporaryDir isolated;
    qputenv("XDG_CONFIG_HOME", isolated.path().toUtf8());
    QApplication app(argc, argv);
    try {
        Window window;
        window.resize(1280, 950);
        auto &doc = window.document();
        auto &view = *window.viewport();
        doc.setDisplayUnits(DisplayUnit::Millimeters);
        doc.markSaved();
        window.show();
        check(QTest::qWaitForWindowExposed(&window), "Text window exposed");
        auto *action = window.findChild<QAction *>("view.text");
        check(action, "View menu exposes 3D text");
        action->trigger();
        check(window.findChild<QWidget *>("textPanel")->isVisible(), "Text panel opens");
        const auto history = doc.history().total;
        int heartbeat{};
        QTimer pulse;
        pulse.setInterval(5);
        QObject::connect(&pulse, &QTimer::timeout, [&] { ++heartbeat; });
        pulse.start();
        modal(window, nullptr, [&](QDialog *dialog) {
            type(dialog, "textName", "Native sign");
            type(dialog, "textHeight", "invalid");
            save(dialog);
            check(dialog->isVisible() && doc.bodies().empty(),
                  "Invalid dimensions leave document unchanged");
            type(dialog, "textHeight", "200mm");
            type(dialog, "textDepth", "30mm");
            dialog->findChild<QPlainTextEdit *>("textContent")
                ->setPlainText(QString::fromUtf8("O café\nلا"));
            const auto count = heartbeat;
            save(dialog);
            finished(dialog);
            check(!dialog->isVisible() && heartbeat > count,
                  "Generation keeps the GUI event loop responsive");
        });
        check(doc.bodies().size() == 1 && doc.history().total == history + 1,
              "Native creation is one history entry");
        const auto id = doc.bodies().begin()->first;
        const auto initial = doc.bodies().at(id);
        check(initial->textSource && initial->textSource->height == .2 &&
                  initial->textSource->depth == .03 && initial->textSource->text == "O café\nلا",
              "Native units and Unicode source persist");
        auto *list = window.findChild<QListWidget *>("textList");
        check(list && list->count() == 1, "Text appears in list");
        const auto exact = encodeContainer(doc);
        modal(window, "textEditButton", [&](QDialog *dialog) {
            save(dialog);
            finished(dialog);
        });
        check(encodeContainer(doc) == exact,
              "Untouched native editor adds no history and preserves exact settings");
        modal(window, "textEditButton", [&](QDialog *dialog) {
            dialog->findChild<QPlainTextEdit *>("textContent")->setPlainText("B");
            save(dialog);
            finished(dialog);
            check(!dialog->isVisible(), "Native regeneration succeeds");
        });
        check(doc.bodies().at(id)->textSource->text == "B", "Native editor updates source");
        doc.undo();
        sync(window);
        check(doc.bodies().at(id)->textSource == initial->textSource,
              "Native Undo restores source");
        const auto beforeCancel = encodeContainer(doc);
        modal(window, "textCreateButton", [&](QDialog *dialog) {
            dialog->findChild<QPlainTextEdit *>("textContent")->setPlainText(QString(500, 'O'));
            save(dialog);
            dialog->reject();
        });
        QTest::qWait(100);
        check(encodeContainer(doc) == beforeCancel, "Cancel cannot publish background geometry");
        modal(window, "textEditButton", [&](QDialog *dialog) {
            dialog->findChild<QPlainTextEdit *>("textContent")->setPlainText("Stale");
            save(dialog);
            doc.setDisplayUnits(DisplayUnit::FeetInches);
            const auto changed = encodeContainer(doc);
            finished(dialog);
            check(dialog->isVisible() && encodeContainer(doc) == changed &&
                      dialog->findChild<QLabel *>("textDialogError")->text().contains("changed"),
                  "Stale background result cannot overwrite model changes");
        });
        doc.undo();
        sync(window);
        auto old = doc.bodies().at(id);
        auto missing = std::make_shared<Body>(*old);
        missing->textSource->family = missing->textSource->actualFamily =
            "Missing native font 726c";
        missing->textSource->allowSubstitution = missing->textSource->substituted = false;
        for (auto &font : missing->textSource->fonts)
            font.family = "Missing native font 726c" + font.family;
        doc.apply({"Simulate portable font absence", {{id, old, missing}}}, doc.revision());
        sync(window);
        check(window.findChild<QLabel *>("textDetails")->text().contains("Missing local fonts"),
              "Missing font status is visible on reopened cached source");
        const auto cached = encodeContainer(doc);
        modal(window, "textEditButton", [&](QDialog *dialog) {
            type(dialog, "textHeight", "250mm");
            save(dialog);
            finished(dialog);
            check(dialog->isVisible() && encodeContainer(doc) == cached,
                  "Missing font failure keeps cached geometry");
        });
        modal(window, "textEditButton", [&](QDialog *dialog) {
            type(dialog, "textHeight", "250mm");
            dialog->findChild<QCheckBox *>("textSubstitution")->setChecked(true);
            save(dialog);
            finished(dialog);
            check(!dialog->isVisible(), "Explicit native font substitution regenerates text");
        });
        check(doc.bodies().at(id)->textSource->substituted &&
                  doc.bodies().at(id)->textSource->allowSubstitution,
              "Substitution provenance and consent are retained");
        modal(window, "textEditButton", [&](QDialog *dialog) {
            type(dialog, "textName", "Portable sign");
            save(dialog);
            finished(dialog);
            check(!dialog->isVisible(), "Renaming does not require the missing font");
        });
        click(window, "textFrameButton");
        const auto geometry = doc.bodies().at(id)->surface;
        click(window, "textBakeButton");
        check(!doc.bodies().at(id)->textSource && doc.bodies().at(id)->surface == geometry,
              "Native baking preserves exact geometry");
        doc.undo();
        sync(window);
        check(doc.bodies().at(id)->textSource.has_value(), "Native bake Undo restores source");
        check(encodeContainer(decodeContainer(encodeContainer(doc))) == encodeContainer(doc),
              "Native editable source save/reopen exact");
        const auto editorEvidence = qEnvironmentVariable("SKETCHYUP_TEXT_EDITOR_EVIDENCE");
        if (!editorEvidence.isEmpty())
            modal(window, "textEditButton", [&](QDialog *dialog) {
                check(dialog->grab().save(editorEvidence), "Editor evidence captured");
            });
        const auto component = createComponent(doc, id, "Text component");
        const auto second =
            placeComponent(doc, component.definition, Transform::translation({1, 0, 0}));
        const auto member = component.movedGeometry.at(id);
        const auto originalText = doc.bodies().at(member)->textSource->text;
        sync(window);
        for (int row = 0; row < list->count(); ++row)
            if (list->item(row)->data(Qt::UserRole).toULongLong() == member)
                list->setCurrentRow(row);
        modal(window, "textEditButton", [&](QDialog *dialog) {
            dialog->findChild<QPlainTextEdit *>("textContent")->setPlainText("B");
            save(dialog);
            finished(dialog);
            check(!dialog->isVisible(), "Native component text edit succeeds");
        });
        check(doc.bodies().at(member)->textSource->text == "B" &&
                  doc.instances().at(id)->definition !=
                      doc.instances().at(second.instance)->definition,
              "Native text edit makes only its component instance unique");
        bool other{};
        for (const auto &[_, body] : doc.bodies())
            if (body->parent == second.instance && body->textSource) {
                check(body->textSource->text == originalText,
                      "Other component placement retains source");
                other = true;
            }
        check(other, "Other component text still exists");
        doc.markSaved();
        const auto example = qEnvironmentVariable("SKETCHYUP_TEXT_MODEL");
        if (!example.isEmpty()) {
            window.openPath(example);
            action->trigger();
            view.standardView(1);
            view.fit();
            sync(window);
            QTest::qWait(150);
            const auto evidence = qEnvironmentVariable("SKETCHYUP_TEXT_VIEWPORT_EVIDENCE");
            if (!evidence.isEmpty())
                check(view.grabFramebuffer().save(evidence),
                      "Native text geometry framebuffer captured");
        }
        doc.markSaved();
        window.close();
        std::cout << "Native text editing, responsive generation, cancellation, stale results, "
                     "font portability and baking passed\n";
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
