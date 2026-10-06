#include "app/window.hpp"
#include "core/components.hpp"
#include "geometry/solid.hpp"
#include "io/document_io.hpp"
#include <QAction>
#include <QApplication>
#include <QCheckBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QPushButton>
#include <QSurfaceFormat>
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
void focus(Window &window) {
    window.activateWindow();
    check(QTest::qWaitFor(
              [&] {
                  return QApplication::activeWindow() == &window &&
                         QGuiApplication::focusWindow() == window.windowHandle();
              },
              5000),
          "Model receives native focus");
    window.viewport()->setFocus();
}
void trigger(Window &window, const char *name) {
    auto *action = window.findChild<QAction *>(name);
    check(action && action->isEnabled(), "Hosted action exists and is enabled");
    action->trigger();
}
template <class F> void dialog(Window &window, const char *action, const char *name, F edit) {
    std::exception_ptr failure;
    QTimer::singleShot(80, &window, [&] {
        auto *panel = window.findChild<QDialog *>(name);
        try {
            check(panel && panel->isVisible(), "Hosted dialog is visible");
            check(
                QTest::qWaitFor(
                    [&] { return QGuiApplication::focusWindow() == panel->windowHandle(); }, 5000),
                "Hosted dialog receives native focus");
            edit(*panel);
        } catch (...) {
            failure = std::current_exception();
            if (panel)
                panel->reject();
        }
    });
    trigger(window, action);
    if (failure)
        std::rethrow_exception(failure);
    focus(window);
}
void accept(QDialog &dialog) {
    QTest::mouseClick(dialog.findChild<QDialogButtonBox *>()->button(QDialogButtonBox::Ok),
                      Qt::LeftButton);
}
void field(QDialog &dialog, const char *name, const char *value) {
    auto *input = dialog.findChild<QLineEdit *>(name);
    check(input, "Hosted input exists");
    input->setText(value);
}
void volume(const Document &doc, Id host, double expected) {
    const auto &body = *doc.bodies().at(host);
    const auto solid = analyzeSolidShells(body.surface, body.topology);
    check(solid.report.volume && std::abs(*solid.report.volume - expected) < 1e-6,
          "Independent host volume matches the opening");
}
void restoredBodies(const Document &doc, const std::map<Id, BodyPtr> &before) {
    const auto expected = encodeBodies(before), actual = encodeBodies(doc.bodies());
    check(actual.size() == expected.size(), "Undo restores the original body set");
    for (int i = 0; i < actual.size(); ++i) {
        auto record = actual[i].toObject();
        const auto original = expected[i].toObject();
        // Undo preserves monotonic allocation floors so retired IDs stay retired.
        // Every geometric, topology, transform and appearance field must be exact.
        for (const auto *counter : {"nextId", "nextEdgeId"}) {
            check(record[counter].toString().toULongLong() >=
                      original[counter].toString().toULongLong(),
                  "Undo retains monotonic body allocator floors");
            record[counter] = original[counter];
        }
        check(record == original, "Undo restores exact body geometry and presentation");
    }
}
struct Fixture {
    Window &window;
    Document &doc;
    Viewport &view;
    Id host{}, face{}, root{}, definition{}, member{}, sourceFace{};
    explicit Fixture(Window &window)
        : window(window), doc(window.document()), view(*window.viewport()) {
        host = doc.addFace({{{0, 0, 0}, {10, 0, 0}, {10, 10, 0}, {0, 10, 0}}});
        doc.extrude(host, doc.bodies().at(host)->surface.faces.begin()->first, 1);
        for (const auto &[id, value] : doc.bodies().at(host)->surface.faces)
            if (doc.bodies().at(host)->surface.normal(id).z > .99)
                face = id;
        const auto source = doc.addFace({{{0, 0, 0}, {2, 0, 0}, {2, 2, 0}, {0, 2, 0}},
                                         {{.2, .2, 0}, {.2, 1.8, 0}, {1.8, 1.8, 0}, {1.8, .2, 0}}});
        sourceFace = doc.bodies().at(source)->surface.faces.begin()->first;
        const auto made = createComponent(doc, source, "Hosted window");
        root = made.instance;
        definition = made.definition;
        member = doc.instances().at(root)->members.at(made.movedGeometry.at(source));
        view.refresh();
        view.standardView(1);
        view.fit();
    }
    void pair() {
        view.setTool(Viewport::Tool::Select);
        view.enterContext(0);
        view.setSelection(root);
        QTest::qWait(30);
        QTest::mouseClick(&view, Qt::LeftButton, Qt::ControlModifier,
                          view.project(doc.worldTransform(host).point({1, 8, 1})).toPoint());
        check(view.selectionState().entities() ==
                  SelectionSet{{root, SelectionKind::Body, 0}, {host, SelectionKind::Face, face}},
              "Ctrl-click adds one host face to the selected whole component");
    }
    void undo() {
        trigger(window, "edit.undo");
        view.setTool(Viewport::Tool::Select);
    }
};
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
    QObject::connect(window.viewport(), &Viewport::message, [&](QString text) { status = text; });
    try {
        Fixture f(window);
        auto &doc = f.doc;
        auto &view = f.view;
        window.show();
        check(QTest::qWaitForWindowExposed(&window), "Hosted model is exposed");
        focus(window);
        view.enterContext(f.root);
        view.setSelection(f.member, f.sourceFace);
        dialog(window, "component.glue", "hostedGlueDialog", [&](QDialog &panel) {
            const auto before = encodeContainer(doc);
            field(panel, "hostedGlueAnchor", "1m,1m,1m");
            accept(panel);
            check(panel.isVisible() && encodeContainer(doc) == before &&
                      !panel.findChild<QLabel *>("hostedGlueError")->text().isEmpty(),
                  "Invalid glue stays in the dialog without a model edit");
            field(panel, "hostedGlueAnchor", "1m,1m,0m");
            field(panel, "hostedGlueTangent", "1,0,0");
            panel.findChild<QCheckBox *>("hostedGlueCutsOpening")->setChecked(true);
            accept(panel);
            check(!panel.isVisible(), "Valid native glue setup closes");
        });
        check(doc.definitions().at(f.definition)->glue->cutsOpening,
              "Native glue setup changes the canonical definition");
        f.pair();
        const auto original = encodeContainer(doc);
        const auto originalBodies = doc.bodies();
        const auto originalDefinitions = doc.definitions();
        const auto originalInstances = doc.instances();
        const auto originalHosted = doc.hostedComponents();
        const auto depth = doc.history().total;
        QTest::mouseMove(&view, view.project({3, 3, 1}).toPoint());
        QTest::qWait(30);
        QTest::keyClick(&view, Qt::Key_H, Qt::ShiftModifier);
        check(view.tool() == Viewport::Tool::HostedPlacement && view.previewValid() &&
                  encodeContainer(doc) == original,
              "Shift+H starts a private cut preview");
        QTest::keyClick(&view, Qt::Key_Escape);
        check(!view.previewValid() && encodeContainer(doc) == original &&
                  doc.history().total == depth,
              "Escape cancels attachment without history");
        f.pair();
        trigger(window, "component.attach");
        QTest::mouseClick(&view, Qt::LeftButton, {}, view.project({3, 3, 1}).toPoint());
        check(doc.hostedComponents().attachments.contains(f.root) &&
                  doc.history().total == depth + 1,
              "Click commits one native attachment");
        volume(doc, f.host, 96);
        auto *measurements = window.findChild<QLineEdit *>("measurements");
        check(measurements, "Native Measurements input exists");
        measurements->setFocus();
        measurements->setText("[4m,3m,1m]");
        QTest::keyClick(measurements, Qt::Key_Return);
        check(!measurements->property("invalid").toBool() && view.measurements("0.2m"),
              "Measurements Return and explicit inset revise the last attachment");
        check(doc.history().total == depth + 1 &&
                  length(doc.worldTransform(f.root).point({1, 1, 0}) - Vec3{4, 3, 1.2}) < 1e-6,
              "Numeric amendment preserves one Undo and exact host-local placement");
        const auto attached = encodeContainer(doc);
        check(!view.measurements("[4m,3m,2m]") && encodeContainer(doc) == attached,
              "Off-plane numeric input rejects without changing the model");
        QTest::keyClick(&view, Qt::Key_Return);
        check(encodeContainer(doc) == attached, "Invalid preview cannot commit with Enter");
        check(encodeContainer(decodeContainer(attached)) == attached,
              "Native attachment survives exact save/reopen");
        f.undo();
        restoredBodies(doc, originalBodies);
        check(doc.definitions() == originalDefinitions && doc.instances() == originalInstances &&
                  doc.hostedComponents() == originalHosted,
              "One Undo restores exact pre-attachment component and host records");
        trigger(window, "edit.redo");
        view.setSelection(f.root);
        trigger(window, "component.detach");
        volume(doc, f.host, 100);
        check(doc.hostedComponents().attachments.empty(), "Native detach restores the host");
        const auto pose = doc.worldTransform(f.root);
        f.pair();
        trigger(window, "component.bind");
        check(view.previewValid(), "Current pose previews with the explicit remembered inset");
        QTest::keyClick(&view, Qt::Key_Return);
        check(doc.worldTransform(f.root) == pose &&
                  doc.hostedComponents().attachments.contains(f.root),
              "Enter binds without moving the component");
        volume(doc, f.host, 96);
        view.setTool(Viewport::Tool::Select);
        view.setSelection(f.host);
        trigger(window, "component.bake_host");
        check(doc.hostedComponents().hosts.empty(), "Native bake releases attachment records");
        volume(doc, f.host, 96);
        f.undo();
        view.setSelection(f.root);
        trigger(window, "component.clear_glue");
        check(!doc.definitions().at(f.definition)->glue &&
                  doc.hostedComponents().attachments.empty(),
              "Clearing shared glue releases its opening");
        volume(doc, f.host, 100);
        f.undo();
        check(doc.hostedComponents().attachments.contains(f.root),
              "Undo restores glue and its attachment together");
        view.setSelection(f.root);
        trigger(window, "component.detach");
        dialog(
            window, "component.placement_options", "hostedPlacementOptions", [&](QDialog &panel) {
                field(panel, "hostedPlacementScale", "0,1,1");
                accept(panel);
                check(
                    panel.isVisible() &&
                        !panel.findChild<QLabel *>("hostedPlacementOptionsError")->text().isEmpty(),
                    "Singular scale stays in options with an actionable error");
                field(panel, "hostedPlacementAngle", "90");
                field(panel, "hostedPlacementScale", "-1,0.5,1");
                field(panel, "hostedPlacementInset", "0m");
                accept(panel);
                check(!panel.isVisible(), "Valid native placement options close");
            });
        f.pair();
        trigger(window, "component.attach");
        check(view.measurements("[6m,6m,1m]"), "Mirrored rotated placement commits");
        volume(doc, f.host, 98);
        const auto reflected = doc.worldTransform(f.root);
        check(dot(cross(reflected.vector({1, 0, 0}), reflected.vector({0, 1, 0})),
                  reflected.vector({0, 0, 1})) < 0,
              "Native signed scale retains reflection");
        const auto capture = qEnvironmentVariable("SKETCHYUP_CAPTURE_DIR");
        if (!capture.isEmpty()) {
            QDir().mkpath(capture);
            view.standardView(0);
            view.fit();
            check(view.grabFramebuffer().save(capture + "/hosted-placement.png"),
                  "Capture native opening and component geometry");
        }
        // A mirrored, nonuniform host exercises pointer-to-host coordinates and
        // the atomic old/new opening update through the actual native action.
        const auto nextHost = doc.addFace({{{0, 0, 0}, {10, 0, 0}, {10, 10, 0}, {0, 10, 0}}});
        doc.extrude(nextHost, doc.bodies().at(nextHost)->surface.faces.begin()->first, 1);
        Id nextFace = 0;
        for (const auto &[id, face] : doc.bodies().at(nextHost)->surface.faces)
            if (doc.bodies().at(nextHost)->surface.normal(id).z > .99)
                nextFace = id;
        doc.transform(nextHost,
                      Transform::translation({20, 0, 2}) * Transform::scaling({-1.2, .8, 2}));
        view.setTool(Viewport::Tool::Select);
        view.refresh();
        view.standardView(1);
        view.fit();
        view.selectEntities(
            {{f.root, SelectionKind::Body, 0}, {nextHost, SelectionKind::Face, nextFace}});
        trigger(window, "component.attach");
        const auto worldAnchor = doc.worldTransform(nextHost).point({5, 5, 1});
        QTest::mouseClick(&view, Qt::LeftButton, {}, view.project(worldAnchor).toPoint());
        check(doc.hostedComponents().attachments.at(f.root)->host == nextHost &&
                  view.measurements("[5m,5m,1m]"),
              "Native ray placement and exact amendment rehost onto a mirrored affine face");
        check(length(doc.worldTransform(f.root).point({1, 1, 0}) - worldAnchor) < 1e-6,
              "Rehost retains independent component size and exact host-local anchor");
        volume(doc, f.host, 100);
        volume(doc, nextHost, 100 - 2 / .96);
        f.undo();
        volume(doc, f.host, 98);
        volume(doc, nextHost, 100);
        check(doc.hostedComponents().attachments.at(f.root)->host == f.host,
              "One Undo restores both hosts and the prior attachment");
        f.pair();
        trigger(window, "component.attach");
        doc.move(f.root, {.25, 0, 0});
        const auto stale = encodeContainer(doc);
        check(!view.measurements("[5m,5m,1m]") && encodeContainer(doc) == stale,
              "An intervening model edit rejects the stale native placement");
        view.setTool(Viewport::Tool::Select);
        view.setSelection(f.host);
        view.lockSelection();
        view.setSelection(f.root);
        const auto locked = encodeContainer(doc);
        QString refusal;
        try {
            view.detachSelectedComponent();
        } catch (const std::exception &error) {
            refusal = error.what();
        }
        check(encodeContainer(doc) == locked && refusal.contains("Unlock"),
              "Native detach respects the former host's editor lock");
        view.unlockContexts();
        std::cout
            << "Native glue, private attachment preview, exact amendment, mirrored placement, "
               "bind/detach/bake, Undo and locks passed; DPR "
            << view.devicePixelRatioF() << std::endl;
        doc = Document{};
        view.refresh();
        window.close();
        QTest::qWait(100);
        QGuiApplication::sync();
    } catch (const std::exception &error) {
        std::cerr << error.what() << "\nStatus: " << status.toStdString() << '\n';
        return 1;
    }
}
