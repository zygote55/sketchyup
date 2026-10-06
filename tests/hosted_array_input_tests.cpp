#include "app/window.hpp"
#include "core/components.hpp"
#include "geometry/solid.hpp"
#include "io/document_io.hpp"
#include <QAction>
#include <QApplication>
#include <QDir>
#include <QMouseEvent>
#include <QSurfaceFormat>
#include <QTest>
#include <QWindow>
#include <iostream>
using namespace sketchy;
namespace {
void check(bool value, const char *message) {
    if (!value)
        throw std::runtime_error(message);
}
void movePointer(Viewport &view, Vec3 point) {
    // Wayland does not support QTest's global cursor warp. Deliver the same
    // widget event used by the native drawing/transform acceptance fixtures.
    const auto position = view.project(point);
    QMouseEvent event(QEvent::MouseMove, position, view.mapToGlobal(position.toPoint()),
                     Qt::NoButton, Qt::NoButton, Qt::NoModifier);
    QCoreApplication::sendEvent(&view, &event);
}
void volume(const Document &doc, Id host, double expected) {
    const auto &body = *doc.bodies().at(host);
    const auto result = analyzeSolidShells(body.surface, body.topology);
    check(result.report.volume && std::abs(*result.report.volume - expected) < 1e-6,
          "Native hosted array has the independently expected material volume");
}
} // namespace
int main(int argc, char **argv) {
    QSurfaceFormat format;
    format.setVersion(3, 3);
    format.setProfile(QSurfaceFormat::CoreProfile);
    format.setDepthBufferSize(24);
    format.setSamples(0);
    QSurfaceFormat::setDefaultFormat(format);
    QApplication app(argc, argv);
    Window window;
    QString status;
    auto &doc = window.document();
    auto &view = *window.viewport();
    QObject::connect(&view, &Viewport::message, [&](QString message) { status = message; });
    try {
        const auto host = doc.addFace({{{0, 0, 0}, {20, 0, 0}, {20, 20, 0}, {0, 20, 0}}});
        doc.extrude(host, doc.bodies().at(host)->surface.faces.begin()->first, 1);
        Id face = 0;
        for (const auto &[id, value] : doc.bodies().at(host)->surface.faces)
            if (doc.bodies().at(host)->surface.normal(id).z > .99)
                face = id;
        const auto source =
            doc.addFace({{{0, 0, 0}, {1, 0, 0}, {1, 1, 0}, {0, 1, 0}},
                         {{.15, .15, 0}, {.15, .85, 0}, {.85, .85, 0}, {.85, .15, 0}}});
        const auto sourceFace = doc.bodies().at(source)->surface.faces.begin()->first;
        const auto made = createComponent(doc, source, "Array window");
        const auto root = made.instance;
        setComponentGlue(
            doc, made.definition,
            ComponentGlue{made.movedGeometry.at(source), sourceFace, {.5, .5, 0}, {1, 0, 0}, true});
        attachComponent(doc, root, host, face, {{3, 3, 1}}, .2);
        view.refresh();
        view.standardView(1);
        view.fit();
        window.show();
        check(QTest::qWaitForWindowExposed(&window), "Hosted array window exposed");
        window.activateWindow();
        check(QTest::qWaitFor(
                  [&] {
                      return QApplication::activeWindow() == &window &&
                             QGuiApplication::focusWindow() == window.windowHandle();
                  },
                  5000),
              "Hosted array window receives native focus");
        view.setFocus();
        view.setSelection(root);
        view.setTool(Viewport::Tool::Move);
        QTest::keyClick(&view, Qt::Key_Control);
        const auto original = encodeContainer(doc);
        const auto depth = doc.history().total;
        check(view.measurements("[3,3,1]"), "Native Move chooses its pivot on the attached face");
        movePointer(view, {7, 3, 1});
        QTest::qWait(40);
        check(view.previewValid() && encodeContainer(doc) == original,
              "Ctrl-copy previews the new component and host cut privately");
        auto *field = window.findChild<QLineEdit *>("measurements");
        check(field, "Native Measurements input exists");
        field->setFocus();
        field->setText("4m,0,0");
        QTest::keyClick(field, Qt::Key_Return);
        check(!field->property("invalid").toBool() &&
                  doc.hostedComponents().attachments.size() == 2,
              "Measurements Return commits a real attached copy");
        volume(doc, host, 398);
        check(view.measurements("x3") && doc.hostedComponents().attachments.size() == 4 &&
                  view.selectionState().entities().size() == 3,
              "xN creates attached copies and selects their independent roots");
        volume(doc, host, 396);
        check(view.measurements("/3") && view.measurements("12m,0,0"),
              "Division and spacing revision retain copied relationships");
        std::set<int> anchors;
        for (const auto &[id, attachment] : doc.hostedComponents().attachments)
            anchors.insert(int(std::lround(attachment->frame.point({}).x * 1000)));
        check(anchors == std::set<int>{3000, 7000, 11000, 15000} &&
                  doc.history().total == depth + 1,
              "Array revisions preserve exact host-local spacing and one Undo");
        const auto valid = encodeContainer(doc);
        check(!view.measurements("x20") && encodeContainer(doc) == valid,
              "Late out-of-bounds copy rejects the entire numeric revision");
        check(encodeContainer(decodeContainer(valid)) == valid,
              "Native copied relationships and openings persist exactly");
        const auto capture = qEnvironmentVariable("SKETCHYUP_CAPTURE_DIR");
        if (!capture.isEmpty()) {
            QDir().mkpath(capture);
            view.standardView(0);
            view.fit();
            check(view.grabFramebuffer().save(capture + "/hosted-array.png"),
                  "Capture actual native array geometry");
        }
        window.findChild<QAction *>("edit.undo")->trigger();
        check(doc.hostedComponents().attachments.size() == 1, "One native Undo removes every copy");
        volume(doc, host, 399);
        view.setTool(Viewport::Tool::Select);
        view.setSelection(host);
        view.lockSelection();
        view.setSelection(root);
        view.standardView(1);
        view.setTool(Viewport::Tool::Move);
        view.setTransformCopy(true);
        check(view.measurements("[3,3,1]"), "Choose copy pivot with host editor lock");
        const auto locked = encodeContainer(doc);
        movePointer(view, {7, 3, 1});
        QTest::qWait(40);
        check(!view.previewValid() && !view.measurements("4m,0,0") && status.contains("Unlock") &&
                  encodeContainer(doc) == locked,
              "Editor host lock rejects both copy preview and numeric commit");
        view.setTransformCopy(false);
        check(!view.measurements("1m,0,0") && encodeContainer(doc) == locked,
              "Ordinary attached movement also respects the host editor lock");
        view.unlockContexts();
        view.setSelection(root);
        view.lockSelection();
        view.setSelection(host);
        view.setTool(Viewport::Tool::Move);
        check(view.measurements("[0,0,0]") && !view.measurements("1m,0,0") &&
                  encodeContainer(doc) == locked,
              "Moving a host cannot carry an editor-locked attachment");
        view.unlockContexts();
        std::cout << "Native attached Ctrl-copy, xN/divisions, exact spacing, private preview, "
                     "one Undo, persistence and indirect editor locks passed; DPR "
                  << view.devicePixelRatioF() << std::endl;
        doc = Document{};
        view.refresh();
        window.close();
        check(QTest::qWaitFor([&] { return !window.isVisible(); }, 2000),
              "Native main window completes shutdown");
        QGuiApplication::sync();
    } catch (const std::exception &error) {
        std::cerr << error.what() << "\nStatus: " << status.toStdString() << '\n';
        return 1;
    }
}
