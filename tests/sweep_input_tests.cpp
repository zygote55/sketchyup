#include "app/surface_format.hpp"
#include "app/window.hpp"
#include "core/components.hpp"
#include "core/groups.hpp"
#include "geometry/solid.hpp"
#include "io/document_io.hpp"
#include <QAction>
#include <QApplication>
#include <QDir>
#include <QTemporaryDir>
#include <QTest>
#include <iostream>
#include <numbers>
using namespace sketchy;
namespace {
void check(bool ok, const char *message) {
    if (!ok)
        throw std::runtime_error(message);
}
struct Fixture {
    Id profile, face, path;
    SelectionSet selected;
};
Fixture fixture(Document &doc, Viewport &view, std::vector<Vec3> points, bool closed = false) {
    doc = Document{};
    const auto profile = doc.addFace({{{-.4, -.3, 0}, {.4, -.3, 0}, {.4, .3, 0}, {-.4, .3, 0}}});
    const auto face = doc.bodies().at(profile)->surface.faces.begin()->first;
    Id path{};
    for (size_t i = 1; i < points.size(); ++i)
        path = doc.addWire(path, points[i - 1], points[i]);
    if (closed)
        doc.addWire(path, points.back(), points.front());
    SelectionSet selected{{profile, SelectionKind::Face, face}};
    for (const auto &[edge, record] : doc.bodies().at(path)->topology.edges)
        selected.insert({path, SelectionKind::Edge, edge});
    view.refresh();
    view.setTool(Viewport::Tool::Select);
    view.standardView(0);
    view.fit();
    return {profile, face, path, selected};
}
void volume(const Document &doc, Id body, double expected) {
    const auto &b = *doc.bodies().at(body);
    const auto result = inspectSolid(b.surface, b.topology);
    check(result.volume && std::abs(*result.volume - expected) < 1e-6,
          "Native applied sweep has independently expected solid volume");
}
void start(Viewport &view) {
    view.setFocus();
    QTest::keyClick(&view, Qt::Key_F, Qt::ShiftModifier);
    QCoreApplication::processEvents();
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
        check(QTest::qWaitForWindowExposed(&window), "Follow Me window exposed");
        window.activateWindow();
        check(QTest::qWaitForWindowActive(&window, 5000), "Follow Me window active");
        auto &doc = window.document();
        auto &view = *window.viewport();
        QString message;
        QObject::connect(&view, &Viewport::message, &window, [&](QString text) { message = text; });
        auto f = fixture(doc, view, {{0, 0, 0}, {0, 0, 3}, {3, 0, 3}});
        QTest::qWait(200);
        auto *action = window.findChild<QAction *>("tool.22");
        check(action, "Follow Me action available");
        QTest::mouseClick(&view, Qt::LeftButton, {}, view.project({.15, -.08, 0}).toPoint());
        QTest::mouseClick(&view, Qt::LeftButton, Qt::ControlModifier,
                          view.project({0, 0, 1.5}).toPoint());
        QTest::mouseClick(&view, Qt::LeftButton, Qt::ControlModifier,
                          view.project({1.5, 0, 3}).toPoint());
        check(view.selectionState().entities() == f.selected,
              "Native clicks select profile and connected path");
        const auto source = doc.bodies().at(f.profile), path = doc.bodies().at(f.path);
        const auto before = encodeDocument(doc);
        start(view);
        check(view.tool() == Viewport::Tool::Sweep && action->isChecked() && view.previewValid(),
              "Shift+F previews the selected path");
        check(encodeDocument(doc) == before, "Follow Me preview does not publish");
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
        check(amber > 100, "Sweep preview is visibly drawn in framebuffer");
        const auto capture = qEnvironmentVariable("SKETCHYUP_SWEEP_CAPTURE");
        if (!capture.isEmpty()) {
            QDir().mkpath(capture);
            check(pixels.save(capture + "/preview.png"), "Retain sweep preview capture");
        }
        QTest::keyClick(&view, Qt::Key_Escape);
        check(!view.previewValid() && encodeDocument(doc) == before &&
                  view.selectionState().entities() == f.selected,
              "Escape preserves exact profile, path, selection and history");
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
              "Orbit retains validated sweep preview");
        const auto history = doc.history().total, generated = doc.nextId();
        QTest::keyClick(&view, Qt::Key_Return);
        check(doc.history().total == history + 1 && doc.bodies().size() == 3,
              "Enter commits exactly one result");
        volume(doc, generated, 2.88);
        check(doc.bodies().at(f.profile) == source && doc.bodies().at(f.path) == path &&
                  view.selectionState().entities() == f.selected,
              "Applying retains original profile and path records and selection");
        QTest::qWait(100);
        if (!capture.isEmpty())
            check(window.grab().save(capture + "/applied.png"), "Retain applied sweep capture");
        auto reopened = decodeContainer(encodeContainer(doc));
        check(encodeDocument(reopened) == encodeDocument(doc), "Native sweep persists exactly");
        QTest::keyClick(&view, Qt::Key_Z, Qt::ControlModifier);
        check(!doc.bodies().contains(generated) &&
                  doc.bodies().at(f.profile)->surface == source->surface,
              "Native Undo removes only result");
        QTest::keyClick(&view, Qt::Key_Z, Qt::ControlModifier | Qt::ShiftModifier);
        check(doc.bodies().contains(generated), "Native Redo restores result");
        start(view);
        doc.move(f.path, {1, 0, 0});
        view.refresh();
        const auto stale = encodeDocument(doc);
        QTest::keyClick(&view, Qt::Key_Return);
        check(!view.previewValid() && encodeDocument(doc) == stale,
              "Intervening edit invalidates sweep preview");
        f = fixture(doc, view, {{0, 0, 0}, {4, 0, 0}, {4, 3, 0}, {0, 3, 0}}, true);
        view.selectEntities(f.selected);
        start(view);
        check(view.previewValid(), "Closed path previews without a seam cap");
        const auto closed = doc.nextId();
        QTest::mouseClick(&view, Qt::LeftButton, {}, center);
        volume(doc, closed, 6.72);
        std::vector<Vec3> arc;
        double distance{};
        for (int i = 0; i <= 16; ++i) {
            const auto angle = std::numbers::pi * .5 * i / 16;
            arc.push_back({3 * (1 - std::cos(angle)), 0, 3 * std::sin(angle)});
            if (i)
                distance += length(arc[i] - arc[i - 1]);
        }
        f = fixture(doc, view, arc);
        view.selectEntities(f.selected);
        start(view);
        check(view.previewValid(), "Faceted curve previews");
        const auto curved = doc.nextId();
        QTest::keyClick(&view, Qt::Key_Return);
        volume(doc, curved, .48 * distance);
        f = fixture(doc, view, {{0, 0, 0}, {0, 0, .05}, {.05, 0, .05}});
        view.selectEntities(f.selected);
        const auto rejected = encodeDocument(doc);
        start(view);
        check(!view.previewValid() && message.contains("consumes a path segment"),
              "Consumed short path has an actionable native preview rejection");
        QTest::keyClick(&view, Qt::Key_Return);
        check(encodeDocument(doc) == rejected, "Invalid preview cannot apply");
        f = fixture(doc, view, {{0, 0, 0}, {0, 0, 3}, {3, 0, 3}});
        doc.addWire(f.path, {0, 0, 3}, {0, 2, 3});
        view.refresh();
        for (const auto &[edge, record] : doc.bodies().at(f.path)->topology.edges)
            f.selected.insert({f.path, SelectionKind::Edge, edge});
        view.selectEntities(f.selected);
        start(view);
        check(!view.previewValid() && message.contains("branches"),
              "Branched edge selection has an actionable rejection before mutation");
        f = fixture(doc, view, {{0, 0, 0}, {0, 0, 3}, {3, 0, 3}});
        const auto group = createGroup(doc, {f.profile, f.path});
        const auto component = createComponent(doc, group);
        placeComponent(doc, component.definition, Transform::translation({10, 0, 0}));
        view.refresh();
        view.enterContext(group);
        view.selectEntities(f.selected);
        start(view);
        check(view.previewValid(), "Follow Me previews in explicit shared component scope");
        const auto oldBodies = doc.bodies().size();
        const auto oldHistory = doc.history().total;
        QTest::keyClick(&view, Qt::Key_Return);
        check(doc.bodies().size() == oldBodies + 2 && doc.history().total == oldHistory + 1,
              "Shared component sweep updates both instances in one history item");
        size_t solids{};
        for (const auto &[id, body] : doc.bodies())
            if (body->name == "Sweep") {
                volume(doc, id, 2.88);
                ++solids;
            }
        check(solids == 2 && view.selectionState().entities() == f.selected,
              "Shared sweep retains profile/path selection and both generated solids");
        std::cout << "Native Follow Me selection, preview/cancel, orbit/apply, undo/persistence, "
                     "curved/closed paths and rejection passed\n";
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
