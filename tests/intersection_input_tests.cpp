#include "app/surface_format.hpp"
#include "app/window.hpp"
#include "core/components.hpp"
#include "core/groups.hpp"
#include "io/document_io.hpp"
#include <QAction>
#include <QApplication>
#include <QDir>
#include <QTemporaryDir>
#include <QTest>
#include <iostream>
using namespace sketchy;
namespace {
void check(bool ok, const char *message) {
    if (!ok)
        throw std::runtime_error(message);
}
struct Fixture {
    Id target, reference;
};
Fixture fixture(Document &doc, Viewport &view) {
    doc = Document{};
    const auto target = doc.addFace({{{0, 0, 0}, {4, 0, 0}, {4, 3, 0}, {0, 3, 0}}});
    const auto reference = doc.addFace({{{1, -1, -1}, {1, 4, -1}, {1, 4, 1}, {1, -1, 1}}});
    view.refresh();
    view.setTool(Viewport::Tool::Select);
    view.standardView(0);
    view.fit();
    return {target, reference};
}
bool seam(const Document &doc, Id body) {
    const auto &b = *doc.bodies().at(body);
    for (const auto &[id, e] : b.topology.edges) {
        const auto a = b.surface.vertices.at(e.a), p = b.surface.vertices.at(e.b);
        if ((length(a - Vec3{1, 0, 0}) < tolerance && length(p - Vec3{1, 3, 0}) < tolerance) ||
            (length(p - Vec3{1, 0, 0}) < tolerance && length(a - Vec3{1, 3, 0}) < tolerance))
            return true;
    }
    return false;
}
void start(Viewport &view) {
    view.setFocus();
    QTest::keyClick(&view, Qt::Key_I);
    QCoreApplication::processEvents();
}
void mode(Window &window, const char *name) {
    auto *action = window.findChild<QAction *>(QString("intersection.mode.") + name);
    check(action, "Intersection reference action exists");
    action->trigger();
    check(action->isChecked(), "Reference mode is visibly selected");
}
} // namespace
int main(int argc, char **argv) {
    QSurfaceFormat format;
    format.setVersion(3, 3);
    format.setProfile(QSurfaceFormat::CoreProfile);
    format.setDepthBufferSize(24);
    sketchy::setDefaultViewportFormat(format);
    QTemporaryDir preferences;
    qputenv("XDG_CONFIG_HOME", preferences.path().toUtf8());
    QApplication app(argc, argv);
    Window window;
    window.show();
    try {
        check(QTest::qWaitForWindowExposed(&window), "Intersection window exposed");
        window.activateWindow();
        check(QTest::qWaitForWindowActive(&window, 5000), "Intersection window active");
        auto &doc = window.document();
        auto &view = *window.viewport();
        QString message;
        QObject::connect(&view, &Viewport::message, &window,
                         [&](QString value) { message = value; });
        auto f = fixture(doc, view);
        QTest::qWait(200);
        QTest::mouseClick(&view, Qt::LeftButton, {}, view.project({3, .5, 0}).toPoint());
        QTest::mouseClick(&view, Qt::LeftButton, Qt::ControlModifier,
                          view.project({1, 3.5, .7}).toPoint());
        const SelectionSet selected{{f.target, SelectionKind::Face, 5},
                                    {f.reference, SelectionKind::Face, 5}};
        check(view.selectionState().entities() == selected,
              "Native clicks select both crossing faces");
        const auto before = encodeDocument(doc);
        const auto history = doc.history().total;
        start(view);
        check(view.tool() == Viewport::Tool::Intersect && view.previewValid() &&
                  encodeDocument(doc) == before,
              "I previews selected-face intersection without publication");
        QTest::qWait(100);
        const auto pixels = view.grabFramebuffer();
        size_t amber{};
        for (int y = 0; y < pixels.height(); ++y)
            for (int x = 0; x < pixels.width(); ++x) {
                const auto c = pixels.pixelColor(x, y);
                if (std::abs(c.red() - 185) < 15 && std::abs(c.green() - 118) < 15 &&
                    std::abs(c.blue() - 47) < 15)
                    ++amber;
            }
        check(amber > 100, "Intersection preview is visibly drawn in framebuffer");
        const auto capture = qEnvironmentVariable("SKETCHYUP_INTERSECTION_CAPTURE");
        if (!capture.isEmpty()) {
            QDir().mkpath(capture);
            check(pixels.save(capture + "/preview.png"), "Retain intersection preview capture");
        }
        QTest::keyClick(&view, Qt::Key_Escape);
        check(!view.previewValid() && encodeDocument(doc) == before &&
                  view.selectionState().entities() == selected,
              "Cancel preserves source, allocator, history and selection");
        start(view);
        const auto oldCamera = view.renderCamera().position;
        const QPoint center(view.width() / 2, view.height() / 2);
        QTest::mousePress(&view, Qt::LeftButton, Qt::AltModifier, center);
        QMouseEvent move(QEvent::MouseMove, center + QPoint(30, 15),
                         view.mapToGlobal(center + QPoint(30, 15)), Qt::NoButton, Qt::LeftButton,
                         Qt::AltModifier);
        QCoreApplication::sendEvent(&view, &move);
        QTest::mouseRelease(&view, Qt::LeftButton, Qt::AltModifier, center + QPoint(30, 15));
        check(view.previewValid() && length(view.renderCamera().position - oldCamera) > tolerance,
              "Orbit retains intersection preview");
        QTest::keyClick(&view, Qt::Key_Return);
        check(doc.history().total == history + 1 && seam(doc, f.target) && seam(doc, f.reference),
              "Enter applies matching seams in one history item");
        check(view.selectionState().entities().size() == 3,
              "Generated target descendants remain selected");
        QTest::qWait(100);
        if (!capture.isEmpty())
            check(window.grab().save(capture + "/applied.png"),
                  "Retain applied intersection capture");
        auto reopened = decodeContainer(encodeContainer(doc));
        check(encodeDocument(reopened) == encodeDocument(doc),
              "Native intersections persist exactly");
        QTest::keyClick(&view, Qt::Key_Z, Qt::ControlModifier);
        check(!seam(doc, f.target), "Undo restores original target topology");
        QTest::keyClick(&view, Qt::Key_Z, Qt::ControlModifier | Qt::ShiftModifier);
        check(seam(doc, f.target), "Redo restores intersection topology");
        f = fixture(doc, view);
        mode(window, "context");
        view.setSelection(f.target, 5);
        const auto reference = doc.bodies().at(f.reference);
        start(view);
        check(view.previewValid(), "Context mode previews one target against its context");
        QTest::mouseClick(&view, Qt::LeftButton, {}, center);
        check(seam(doc, f.target) && doc.bodies().at(f.reference) == reference,
              "Context mode retains unselected reference body");
        start(view);
        check(!view.previewValid() && message.contains("No new intersection"),
              "Existing seams have a usable no-change message");
        const auto unchanged = encodeDocument(doc);
        QTest::keyClick(&view, Qt::Key_Return);
        check(encodeDocument(doc) == unchanged, "No-change preview cannot publish");
        f = fixture(doc, view);
        mode(window, "selected");
        view.selectEntities(
            {{f.target, SelectionKind::Face, 5}, {f.reference, SelectionKind::Face, 5}});
        start(view);
        doc.move(f.reference, {.5, 0, 0});
        view.refresh();
        const auto stale = encodeDocument(doc);
        QTest::keyClick(&view, Qt::Key_Return);
        check(!view.previewValid() && encodeDocument(doc) == stale,
              "Manual edit invalidates the pending intersection");
        f = fixture(doc, view);
        const auto group = createGroup(doc, {f.target});
        view.refresh();
        view.enterContext(group);
        view.setSelection(f.target, 5);
        mode(window, "context");
        start(view);
        check(!view.previewValid() && message.contains("No new intersection"),
              "Context references stop at group boundary");
        const auto outside = doc.bodies().at(f.reference);
        mode(window, "model");
        check(view.previewValid(),
              "Changing to Model references previews across the group boundary");
        QTest::keyClick(&view, Qt::Key_Return);
        check(seam(doc, f.target) && doc.bodies().at(f.reference) == outside,
              "Model mode edits only target group geometry");
        f = fixture(doc, view);
        const auto component = createComponent(doc, f.target);
        const auto member = component.movedGeometry.at(f.target);
        view.refresh();
        view.enterContext(f.target);
        view.setSelection(member, 5);
        mode(window, "model");
        start(view);
        check(view.previewValid(), "Native component Model mode can read outer-scene reference");
        const auto outer = doc.bodies().at(f.reference);
        QTest::keyClick(&view, Qt::Key_Return);
        check(seam(doc, member) && doc.bodies().at(f.reference) == outer &&
                  view.selectionState().entities().size() == 2,
              "Component intersection preserves outer reference and selects scene descendants");
        std::cout << "Native intersection modes, selection, visible preview/cancel, orbit/apply, "
                     "stale/no-op, undo/persistence and component references passed\n";
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
