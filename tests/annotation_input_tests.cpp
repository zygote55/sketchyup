#include "app/annotation_display.hpp"
#include "app/annotations_panel.hpp"
#include "app/window.hpp"
#include "core/annotations.hpp"
#include "io/document_io.hpp"
#include <QAction>
#include <QApplication>
#include <QCheckBox>
#include <QColorDialog>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QLineEdit>
#include <QPlainTextEdit>
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
    check(button, "Annotation action exists");
    button->click();
}
void type(QDialog *dialog, const char *name, const QString &text) {
    auto *field = dialog->findChild<QLineEdit *>(name);
    check(field, "Annotation input exists");
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
        auto *dialog = window.findChild<QDialog *>("annotationDialog");
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
    check(opened, "Native annotation dialog opened");
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
        auto &view = *window.viewport();
        const auto body = doc.addWire(0, {0, 0, 0}, {4, 0, 0});
        const auto edge = doc.bodies().at(body)->topology.edges.begin()->first;
        doc.setDisplayUnits(DisplayUnit::Millimeters);
        doc.markSaved();
        const auto saved = doc.saveStamp();
        const auto geometry = doc.bodies();
        window.show();
        check(QTest::qWaitForWindowExposed(&window), "Annotation window exposed");
        auto *action = window.findChild<QAction *>("view.annotations");
        check(action, "View menu exposes annotations");
        action->trigger();
        view.selectEntities({{body, SelectionKind::Edge, edge}});
        sync(window);
        check(window.findChild<QWidget *>("annotationsPanel")->isVisible(), "Annotation tab opens");
        const auto history = doc.history().total;
        modal(window, "annotationDistanceButton", [&](QDialog *dialog) {
            type(dialog, "annotationName", "Edge length");
            type(dialog, "annotationOffset1", "500mm");
            type(dialog, "annotationOffset0", "invalid");
            accept(dialog);
            check(dialog->isVisible() &&
                      !dialog->findChild<QLabel *>("annotationDialogError")->text().isEmpty() &&
                      doc.annotations().empty(),
                  "Invalid units stay editable without mutation");
            type(dialog, "annotationOffset0", "0");
            accept(dialog);
        });
        check(doc.annotations().size() == 1 && doc.history().total == history + 1 &&
                  doc.bodies() == geometry,
              "Native dimension is one metadata edit");
        const auto span = doc.annotations().begin()->first;
        check(doc.annotations().at(span)->anchors[0].kind == AnchorKind::Vertex &&
                  doc.annotations().at(span)->offset == Vec3{0, .5, 0},
              "Selected edge attaches endpoints and parses units");
        check(window.findChild<QLabel *>("annotationDetails")->text().contains("4000 mm"),
              "Dimension displays document units");
        doc.undo();
        sync(window);
        check(doc.annotations().empty() && doc.isCurrentSnapshot(saved),
              "One Undo restores saved state");
        doc.redo();
        sync(window);
        view.selectEntities({{body, SelectionKind::Edge, edge}});
        modal(window, "annotationLabelButton", [&](QDialog *dialog) {
            type(dialog, "annotationName", "Joint");
            dialog->findChild<QPlainTextEdit *>("annotationText")
                ->setPlainText(QString::fromUtf8("Café\n入口"));
            accept(dialog);
        });
        const auto label = doc.annotations().rbegin()->first;
        check(doc.annotations().at(label)->anchors[0].kind == AnchorKind::Edge,
              "Native label attaches edge midpoint");
        doc.splitEdge(body, edge, .5);
        sync(window);
        auto *list = window.findChild<QListWidget *>("annotationsList");
        list->setCurrentRow(1);
        check(list->currentItem()->text().contains("Ambiguous"),
              "Broken association visible in list");
        modal(window, "annotationEditButton", [&](QDialog *dialog) {
            dialog->findChild<QPlainTextEdit *>("annotationText")->setPlainText("Review joint");
            accept(dialog);
        });
        check(measureAnnotation(doc, *doc.annotations().at(label)).state == AnchorState::Ambiguous,
              "Editing text preserves broken attachment");
        modal(window, "annotationEditButton", [&](QDialog *dialog) {
            auto *binding = dialog->findChild<QComboBox *>("annotationBinding");
            binding->setCurrentIndex(binding->findData("fixed"));
            type(dialog, "annotationPoint00", "1m");
            type(dialog, "annotationPoint01", "250mm");
            accept(dialog);
        });
        check(doc.annotations().at(label)->anchors[0].kind == AnchorKind::Point &&
                  doc.annotations().at(label)->anchors[0].fallback == Vec3{1, .25, 0},
              "Explicit fixed-point rebinding");
        const auto rebound = encodeContainer(doc);
        modal(window, "annotationEditButton", [&](QDialog *dialog) {
            auto *binding = dialog->findChild<QComboBox *>("annotationBinding");
            binding->setCurrentIndex(binding->findData("fixed"));
            accept(dialog);
            check(!dialog->isVisible(), "Equal explicit rebind closes normally");
        });
        check(encodeContainer(doc) == rebound, "Equal explicit rebind preserves history");
        doc.undo();
        sync(window);
        list->setCurrentRow(1);
        check(measureAnnotation(doc, *doc.annotations().at(label)).state == AnchorState::Ambiguous,
              "Rebind Undo restores exact broken state");
        doc.transform(body, Transform::scaling({-2, 3, 1}));
        doc.setDisplayUnits(DisplayUnit::FeetInches);
        sync(window);
        list->setCurrentRow(0);
        check(measureAnnotation(doc, *doc.annotations().at(span)).distance == 8 &&
                  window.findChild<QLabel *>("annotationDetails")->text().contains("26'"),
              "Reflected scale changes distance and imperial display");
        auto precise = *doc.annotations().at(span);
        precise.offset = {.123456789123, .345678912345, 0};
        precise.textSize = 12.345678;
        precise.color = {.1234567f, .2345678f, .3456789f};
        updateAnnotation(doc, span, precise);
        sync(window);
        list->setCurrentRow(0);
        const auto exact = encodeContainer(doc);
        modal(window, "annotationEditButton", [&](QDialog *dialog) { accept(dialog); });
        check(encodeContainer(doc) == exact, "Untouched editor preserves exact record and history");
        modal(window, "annotationEditButton", [&](QDialog *dialog) {
            type(dialog, "annotationName", "Stale");
            doc.move(body, {1, 0, 0});
            const auto moved = encodeContainer(doc);
            accept(dialog);
            check(dialog->isVisible() && encodeContainer(doc) == moved,
                  "Stale editor cannot overwrite changes");
        });
        doc.erase(body);
        sync(window);
        list->setCurrentRow(0);
        check(list->currentItem()->text().contains("Missing") &&
                  window.findChild<QLabel *>("annotationDetails")
                      ->text()
                      .contains("Missing reference"),
              "Missing geometry visibly invalidates dimension");
        click(window, "annotationFrameButton");
        click(window, "annotationDeleteButton");
        check(!doc.annotations().contains(span), "Native delete");
        doc.undo();
        sync(window);
        check(doc.annotations().contains(span), "Native delete Undo");
        check(encodeContainer(decodeContainer(encodeContainer(doc))) == encodeContainer(doc),
              "Native annotation save/reopen exact");
        const auto capture = qEnvironmentVariable("SKETCHYUP_ANNOTATION_EDITOR_EVIDENCE");
        if (!capture.isEmpty())
            modal(window, "annotationEditButton", [&](QDialog *dialog) {
                check(dialog->grab().save(capture), "Editor evidence saved");
            });
        doc.markSaved();
        const auto model = qEnvironmentVariable("SKETCHYUP_ANNOTATION_MODEL");
        if (!model.isEmpty()) {
            window.openPath(model);
            action->trigger();
            view.standardView(1);
            view.frameBounds({-1, -1, -1}, {5, 4, 1});
            sync(window);
            check(doc.annotations().size() == 3 &&
                      measureAnnotation(doc, *doc.annotations().at(1)).distance == 4 &&
                      measureAnnotation(doc, *doc.annotations().at(2)).distance == 3,
                  "Native example opens with exact associated dimensions");
            const auto image = view.grabFramebuffer();
            check(!image.isNull() && view.rendererReady() && view.renderStats().glError == 0, "Native example framebuffer exists");
            const auto evidence = qEnvironmentVariable("SKETCHYUP_ANNOTATION_EXAMPLE_EVIDENCE");
            if (!evidence.isEmpty()) {
                check(image.save(evidence + ".viewport.png"), "Native example viewport saved");
                check(window.grab().save(evidence), "Native example evidence saved");
            }
        }
        window.close();
        std::cout
            << "Native annotation authoring, units, rebind, history and broken references passed\n";
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
