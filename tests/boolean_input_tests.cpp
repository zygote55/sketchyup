#include "app/window.hpp"
#include "core/components.hpp"
#include "core/groups.hpp"
#include "geometry/solid.hpp"
#include "io/document_io.hpp"
#include <QAction>
#include <QApplication>
#include <QDir>
#include <QSurfaceFormat>
#include <QTemporaryDir>
#include <QTest>
#include <iostream>
using namespace sketchy;
namespace {
void check(bool ok, const char *message) {
    if (!ok)
        throw std::runtime_error(message);
}
Id box(Document &doc, Vec3 origin, Vec3 size = {2, 2, 2}) {
    const auto id = doc.addFace({{origin, origin + Vec3{size.x, 0, 0},
                                  origin + Vec3{size.x, size.y, 0}, origin + Vec3{0, size.y, 0}}});
    doc.extrude(id, 5, size.z);
    return id;
}
void fixture(Document &doc, Viewport &view, Vec3 tool = {1, 0, 0}) {
    view.setTool(Viewport::Tool::Select);
    doc = Document{};
    box(doc, {});
    box(doc, tool);
    view.refresh();
    view.standardView(1);
    view.fit();
}
void select(Viewport &view) {
    view.selectEntities({{1, SelectionKind::Body, 0}, {2, SelectionKind::Body, 0}});
}
void start(Viewport &view) {
    view.setFocus();
    QTest::keyClick(&view, Qt::Key_B, Qt::ShiftModifier);
    QCoreApplication::processEvents();
}
void action(Window &window, const char *name) {
    auto *choice = window.findChild<QAction *>(name);
    check(choice, "Native Boolean action exists");
    choice->trigger();
}
void volume(const Document &doc, Id id, double expected) {
    auto surface = doc.bodies().at(id)->surface;
    const auto frame = doc.worldTransform(id);
    for (auto &[vertex, point] : surface.vertices)
        point = frame.point(point);
    const auto report = inspectSolid(surface, Topology::rebuild(surface, {}));
    check(report.status == "solid" && report.volume && std::abs(*report.volume - expected) < 1e-6,
          "Native Boolean result has independently expected world volume");
}
} // namespace
int main(int argc, char **argv) {
    QSurfaceFormat format;
    format.setVersion(3, 3);
    format.setProfile(QSurfaceFormat::CoreProfile);
    format.setDepthBufferSize(24);
    QSurfaceFormat::setDefaultFormat(format);
    QTemporaryDir preferences;
    qputenv("XDG_CONFIG_HOME", preferences.path().toUtf8());
    QApplication app(argc, argv);
    Window window;
    window.show();
    try {
        check(QTest::qWaitForWindowExposed(&window), "Boolean window exposed");
        window.activateWindow();
        check(QTest::qWaitForWindowActive(&window, 5000), "Boolean window active");
        auto &doc = window.document();
        auto &view = *window.viewport();
        QString message;
        QObject::connect(&view, &Viewport::message, &window,
                         [&](QString value) { message = value; });
        fixture(doc, view);
        QTest::qWait(200);
        QTest::mouseClick(&view, Qt::LeftButton, {}, view.project({.5, 1, 2}).toPoint());
        QTest::mouseClick(&view, Qt::LeftButton, Qt::ControlModifier,
                          view.project({2.5, 1, 2}).toPoint());
        const auto selected = view.selectionState().entities();
        check(selected.size() == 2 && selected.begin()->body == 1 && selected.rbegin()->body == 2,
              "Click and Ctrl-click select a face on each solid");
        const auto before = encodeDocument(doc);
        const auto source = doc.bodies().at(1), tool = doc.bodies().at(2);
        const auto history = doc.history().total;
        start(view);
        check(view.tool() == Viewport::Tool::Boolean && view.previewValid() &&
                  encodeDocument(doc) == before && message.contains("Target:") &&
                  message.contains("Tool:") && message.contains("Keep originals"),
              "Shift+B previews without mutation and identifies target/tool and retention");
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
        check(amber > 100, "Boolean preview is visible in framebuffer");
        const auto capture = qEnvironmentVariable("SKETCHYUP_BOOLEAN_CAPTURE");
        if (!capture.isEmpty()) {
            QDir().mkpath(capture);
            check(pixels.save(capture + "/preview.png"), "Save Boolean preview capture");
        }
        QTest::keyClick(&view, Qt::Key_Escape);
        check(!view.previewValid() && encodeDocument(doc) == before &&
                  view.selectionState().entities() == selected,
              "Escape retains sources, allocator, history and selection");
        start(view);
        const auto camera = view.renderCamera().position;
        const QPoint center(view.width() / 2, view.height() / 2);
        QTest::mousePress(&view, Qt::LeftButton, Qt::AltModifier, center);
        QMouseEvent move(QEvent::MouseMove, center + QPoint(30, 15),
                         view.mapToGlobal(center + QPoint(30, 15)), Qt::NoButton, Qt::LeftButton,
                         Qt::AltModifier);
        QCoreApplication::sendEvent(&view, &move);
        QTest::mouseRelease(&view, Qt::LeftButton, Qt::AltModifier, center + QPoint(30, 15));
        check(view.previewValid() && length(view.renderCamera().position - camera) > tolerance,
              "Alt-orbit retains Boolean preview");
        QTest::keyClick(&view, Qt::Key_Return);
        check(doc.history().total == history + 1 && doc.bodies().size() == 3 &&
                  doc.bodies().at(1) == source && doc.bodies().at(2) == tool,
              "Union publishes one history item and retains exact sources");
        volume(doc, 3, 12);
        check(view.selectionState().entities() == SelectionSet{{3, SelectionKind::Body, 0}},
              "Result solid is selected");
        QTest::qWait(100);
        if (!capture.isEmpty())
            check(window.grab().save(capture + "/applied.png"), "Save applied Boolean capture");
        auto reopened = decodeContainer(encodeContainer(doc));
        check(encodeDocument(reopened) == encodeDocument(doc), "Native Boolean persists exactly");
        QTest::keyClick(&view, Qt::Key_Z, Qt::ControlModifier);
        check(doc.bodies().size() == 2, "Native Undo removes generated result");
        QTest::keyClick(&view, Qt::Key_Z, Qt::ControlModifier | Qt::ShiftModifier);
        volume(doc, 3, 12);
        fixture(doc, view);
        select(view);
        action(window, "boolean.operation.intersection");
        start(view);
        check(view.previewValid(), "Overlapping intersection previews a positive solid");
        QTest::keyClick(&view, Qt::Key_Return);
        volume(doc, 3, 4);
        fixture(doc, view, {4, 0, 0});
        select(view);
        action(window, "boolean.operation.union");
        start(view);
        check(view.previewValid(), "Disjoint union previews both output parts");
        QTest::keyClick(&view, Qt::Key_Return);
        volume(doc, 3, 8);
        volume(doc, 4, 8);
        check(view.selectionState().entities() ==
                  SelectionSet{{3, SelectionKind::Body, 0}, {4, SelectionKind::Body, 0}},
              "All disconnected output solids are selected");
        fixture(doc, view);
        select(view);
        action(window, "boolean.operation.subtract");
        start(view);
        check(view.previewValid() && message.contains("subtract"), "Subtraction previews");
        action(window, "boolean.swap");
        check(view.previewValid() && view.booleanSummary().contains("Target: Face (#2)"),
              "Swap updates target label and preview");
        action(window, "boolean.keep");
        check(view.previewValid() && message.contains("Remove both originals"),
              "Operand consumption is explicit before Apply");
        QTest::mouseClick(&view, Qt::LeftButton, {}, center);
        volume(doc, 3, 4);
        check(doc.bodies().size() == 1, "Click applies and consumes both sources");
        double minX = 10;
        for (const auto &[id, p] : doc.bodies().at(3)->surface.vertices)
            minX = std::min(minX, p.x);
        check(std::abs(minX - 2) < tolerance, "Swapped subtraction cuts original target from tool");
        QTest::keyClick(&view, Qt::Key_Z, Qt::ControlModifier);
        check(doc.bodies().contains(1) && doc.bodies().contains(2),
              "Undo restores consumed sources");
        action(window, "boolean.swap");
        action(window, "boolean.keep");
        fixture(doc, view);
        select(view);
        start(view);
        doc.move(2, {.25, 0, 0});
        view.refresh();
        const auto stale = encodeDocument(doc);
        QTest::keyClick(&view, Qt::Key_Return);
        check(!view.previewValid() && encodeDocument(doc) == stale,
              "Manual edit invalidates pending Boolean");
        fixture(doc, view, {4, 0, 0});
        select(view);
        action(window, "boolean.operation.intersection");
        start(view);
        check(!view.previewValid() && message.contains("Empty Boolean"),
              "Retained empty intersection explains no change");
        const auto emptyBefore = encodeDocument(doc);
        QTest::keyClick(&view, Qt::Key_Return);
        check(encodeDocument(doc) == emptyBefore, "No-change preview cannot publish");
        action(window, "boolean.keep");
        check(view.previewValid() && message.contains("both originals will be removed"),
              "Empty consumption preview explicitly explains removal");
        QTest::keyClick(&view, Qt::Key_Return);
        check(doc.bodies().empty() && view.selectionState().entities().empty(),
              "Empty consumed intersection clears result selection");
        QTest::keyClick(&view, Qt::Key_Z, Qt::ControlModifier);
        check(doc.bodies().size() == 2, "Undo restores empty intersection operands");
        action(window, "boolean.keep");
        fixture(doc, view);
        doc.eraseFace(2, 5);
        view.refresh();
        select(view);
        start(view);
        check(!view.previewValid() && message.contains("Operand body 2") &&
                  message.contains("edges:"),
              "Open solid rejection identifies operand and defective edges");
        const auto invalid = encodeDocument(doc);
        QTest::keyClick(&view, Qt::Key_Return);
        check(encodeDocument(doc) == invalid, "Invalid solid cannot publish");
        fixture(doc, view);
        const auto group = createGroup(doc, {1, 2});
        const auto component = createComponent(doc, group);
        placeComponent(doc, component.definition, Transform::translation({10, 0, 0}));
        view.refresh();
        view.enterContext(group);
        select(view);
        action(window, "boolean.operation.union");
        start(view);
        check(view.previewValid(), "Native Boolean previews in shared component context");
        const auto sharedHistory = doc.history().total, oldBodies = doc.bodies().size();
        QTest::keyClick(&view, Qt::Key_Return);
        check(
            doc.history().total == sharedHistory + 1 && doc.bodies().size() == oldBodies + 2 &&
                view.selectionState().entities().size() == 1,
            "Shared component updates both instances in one history item and selects scene result");
        const auto result = view.selectionState().entities().begin()->body;
        check(doc.bodies().at(result)->parent == group,
              "Selected result belongs to active instance");
        volume(doc, result, 12);
        std::cout
            << "Native Boolean operations, target swap, retention, visible preview/cancel, "
               "orbit/apply, stale/invalid/empty, Undo/persistence and shared components passed\n";
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
