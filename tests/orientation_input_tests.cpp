#include "app/surface_format.hpp"
#include "app/window.hpp"
#include "core/components.hpp"
#include "core/face_orientation.hpp"
#include "core/groups.hpp"
#include "core/materials.hpp"
#include "geometry/solid.hpp"
#include "io/document_io.hpp"
#include <QAction>
#include <QApplication>
#include <QDir>
#include <QLineEdit>
#include <QTemporaryDir>
#include <QTest>
#include <iostream>
using namespace sketchy;
namespace {
void check(bool value, const char *message) {
    if (!value)
        throw std::runtime_error(message);
}
void action(Window &window, const char *name) {
    auto *choice = window.findChild<QAction *>(name);
    check(choice, "Native orientation action exists");
    choice->trigger();
}
void start(Viewport &view) {
    view.setFocus();
    QTest::keyClick(&view, Qt::Key_O, Qt::ShiftModifier);
    QCoreApplication::processEvents();
}
Id box(Document &doc) {
    const auto id = doc.addFace({{{0, 0, 0}, {2, 0, 0}, {2, 2, 0}, {0, 2, 0}}});
    doc.extrude(id, 5, 2);
    return id;
}
QColor sample(Viewport &view, Vec3 point) {
    QCoreApplication::processEvents();
    const auto image = view.grabFramebuffer();
    const auto p = view.project(point);
    const QPoint pixel(qRound(p.x() * image.width() / view.width()),
                       qRound(p.y() * image.height() / view.height()));
    check(image.rect().contains(pixel), "Material sample lies within viewport");
    return image.pixelColor(pixel);
}
void near(QColor a, QColor b) {
    if (std::abs(a.red() - b.red()) >= 6 || std::abs(a.green() - b.green()) >= 6 ||
        std::abs(a.blue() - b.blue()) >= 6)
        std::cerr << "Actual " << a.name().toStdString() << ", expected " << b.name().toStdString()
                  << '\n';
    check(std::abs(a.red() - b.red()) < 6 && std::abs(a.green() - b.green()) < 6 &&
              std::abs(a.blue() - b.blue()) < 6,
          "Physical-side framebuffer appearance stays unchanged");
}
} // namespace
int main(int argc, char **argv) {
    QSurfaceFormat format;
    format.setVersion(3, 3);
    format.setProfile(QSurfaceFormat::CoreProfile);
    format.setDepthBufferSize(24);
    format.setSamples(0);
    sketchy::setDefaultViewportFormat(format);
    QTemporaryDir preferences;
    qputenv("XDG_CONFIG_HOME", preferences.path().toUtf8());
    QApplication app(argc, argv);
    Window window;
    window.show();
    try {
        check(QTest::qWaitForWindowExposed(&window), "Orientation window exposed");
        window.activateWindow();
        check(QTest::qWaitForWindowActive(&window, 5000), "Orientation window active");
        auto &doc = window.document();
        auto &view = *window.viewport();
        QString message;
        QObject::connect(&view, &Viewport::message, &window,
                         [&](QString value) { message = value; });
        const auto capture = qEnvironmentVariable("SKETCHYUP_ORIENTATION_CAPTURE");
        if (!capture.isEmpty())
            QDir().mkpath(capture);
        for (bool mirrored : {false, true}) {
            view.setTool(Viewport::Tool::Select);
            doc = Document{};
            const auto body = doc.addFace({{{-2, -2, 1}, {2, -2, 1}, {2, 2, 1}, {-2, 2, 1}}});
            const auto red = createMaterial(doc, "Front red", {.9f, .1f, .1f});
            const auto blue = createMaterial(doc, "Back blue", {.1f, .1f, .9f});
            assignMaterial(doc, body, {}, red, true, false);
            assignMaterial(doc, body, {}, blue, false, true);
            if (mirrored)
                doc.transform(body, Transform::scaling({-1.5, .75, 1}));
            view.refresh();
            view.standardView(1);
            view.fit();
            const Vec3 probe{-.63, .43, 1};
            const auto front = sample(view, probe);
            check(front.red() > front.blue() * 3, "Red physical front is visible");
            view.standardView(6);
            const auto back = sample(view, probe);
            check(back.blue() > back.red() * 3, "Blue physical back is visible");
            view.standardView(1);
            QTest::mouseClick(&view, Qt::LeftButton, {}, view.project(probe).toPoint());
            check(view.selectionState().entities() == SelectionSet{{body, SelectionKind::Face, 5}},
                  "Real click selects the face to reverse");
            const auto original = doc.bodies().at(body);
            const auto before = encodeDocument(doc);
            const auto history = doc.history().total;
            action(window, "orientation.reverse");
            start(view);
            check(view.tool() == Viewport::Tool::Orientation && view.previewValid() &&
                      encodeDocument(doc) == before && message.contains("1 face(s) change") &&
                      message.contains("Arrows show new fronts"),
                  "Shift+O previews winding with explicit direction feedback");
            const auto camera = view.renderCamera().position;
            const QPoint center(view.width() / 2, view.height() / 2);
            QTest::mousePress(&view, Qt::LeftButton, Qt::AltModifier, center);
            QMouseEvent move(QEvent::MouseMove, center + QPoint(30, 15),
                             view.mapToGlobal(center + QPoint(30, 15)), Qt::NoButton,
                             Qt::LeftButton, Qt::AltModifier);
            QCoreApplication::sendEvent(&view, &move);
            QTest::mouseRelease(&view, Qt::LeftButton, Qt::AltModifier, center + QPoint(30, 15));
            check(view.previewValid() && length(view.renderCamera().position - camera) > tolerance,
                  "Alt-orbit preserves orientation preview");
            QTest::keyClick(&view, Qt::Key_Escape);
            check(!view.previewValid() && encodeDocument(doc) == before &&
                      view.selectionState().entities().size() == 1,
                  "Cancel preserves bytes, selection and history");
            start(view);
            if (!mirrored && !capture.isEmpty()) {
                view.standardView(0);
                QTest::qWait(100);
                check(view.grabFramebuffer().save(capture + "/reverse-preview.png"),
                      "Capture reversal arrows");
            }
            const auto previewPixels = view.grabFramebuffer();
            size_t amber{};
            for (int y = 0; y < previewPixels.height(); ++y)
                for (int x = 0; x < previewPixels.width(); ++x) {
                    const auto pixel = previewPixels.pixelColor(x, y);
                    if (std::abs(pixel.red() - 185) < 15 && std::abs(pixel.green() - 118) < 15 &&
                        std::abs(pixel.blue() - 47) < 15)
                        ++amber;
                }
            check(amber > 100, "Orientation preview is visibly drawn in the native framebuffer");
            QTest::keyClick(&view, Qt::Key_1);
            check(view.hasFocus() && view.previewValid(),
                  "Orientation never captures numeric measurements");
            QTest::keyClick(&view, Qt::Key_Return);
            check(doc.history().total == history + 1 &&
                      doc.bodies().at(body)->surface.normal(5).z < -.99 &&
                      view.selectionState().entities() ==
                          SelectionSet{{body, SelectionKind::Face, 5}},
                  "Apply reverses face in one Undo item and retains selection");
            // The selection stipple deliberately replaces some surface pixels;
            // verify selection above, then compare unobscured physical materials.
            view.selectEntities({});
            view.standardView(1);
            near(sample(view, probe), front);
            view.standardView(6);
            near(sample(view, probe), back);
            view.selectEntities({{body, SelectionKind::Face, 5}});
            auto reopened = decodeContainer(encodeContainer(doc));
            check(encodeDocument(reopened) == encodeDocument(doc),
                  "Native reversed face/material sides persist");
            QTest::keyClick(&view, Qt::Key_Z, Qt::ControlModifier);
            check(doc.bodies().at(body) == original, "Native Undo restores exact original record");
            QTest::keyClick(&view, Qt::Key_Z, Qt::ControlModifier | Qt::ShiftModifier);
            check(doc.bodies().at(body)->surface.normal(5).z < -.99,
                  "Native Redo restores reversal");
        }
        view.setTool(Viewport::Tool::Select);
        doc = Document{};
        const auto body = box(doc);
        const auto original = doc.bodies().at(body);
        reverseSelectedFaces(doc, {{body, SelectionKind::Face, 10}}, 0);
        view.refresh();
        view.selectEntities({{body, SelectionKind::Face, 5}});
        action(window, "orientation.connected");
        const auto damaged = encodeDocument(doc);
        start(view);
        check(view.previewValid() && encodeDocument(doc) == damaged &&
                  message.contains("1 face(s) change"),
              "Orient previews only the inconsistent face from explicit reference");
        if (!capture.isEmpty()) {
            view.standardView(0);
            view.fit();
            QTest::qWait(100);
            check(view.grabFramebuffer().save(capture + "/orient-preview.png"),
                  "Capture connected orientation preview");
        }
        QTest::mouseClick(&view, Qt::LeftButton, {}, QPoint(view.width() / 2, view.height() / 2));
        check(*doc.bodies().at(body) == *original && view.selectionState().entities().size() == 1,
              "Click Apply repairs the shell and retains reference selection");
        const auto repaired = encodeDocument(doc);
        start(view);
        check(!view.previewValid() && message.contains("already match"),
              "Consistent connected faces explain no change");
        QTest::keyClick(&view, Qt::Key_Return);
        check(encodeDocument(doc) == repaired, "No-change orientation cannot publish");
        action(window, "orientation.reverse");
        start(view);
        doc.move(body, {.1, 0, 0});
        view.refresh();
        const auto stale = encodeDocument(doc);
        QTest::keyClick(&view, Qt::Key_Return);
        check(!view.previewValid() && encodeDocument(doc) == stale,
              "External edit invalidates orientation preview");
        view.setTool(Viewport::Tool::Select);
        doc = Document{};
        const auto raw = box(doc), group = createGroup(doc, {raw});
        const auto component = createComponent(doc, group);
        const auto other =
            placeComponent(doc, component.definition, Transform::translation({10, 0, 0})).instance;
        const auto sibling = doc.instances().at(other)->members.at(raw);
        view.refresh();
        view.enterContext(group);
        view.selectEntities({{raw, SelectionKind::Face, 5}});
        start(view);
        check(view.previewValid() && message.contains("1 face(s) change"),
              "Shared component arrows show only the active instance");
        const auto sharedHistory = doc.history().total;
        QTest::keyClick(&view, Qt::Key_Return);
        check(doc.history().total == sharedHistory + 1 &&
                  doc.bodies().at(raw)->surface.normal(5).z > .99 &&
                  doc.bodies().at(sibling)->surface.normal(5).z > .99 &&
                  view.selectionState().entities() == SelectionSet{{raw, SelectionKind::Face, 5}},
              "Shared reversal updates both instances and preserves active scene selection");
        auto reopened = decodeContainer(encodeContainer(doc));
        check(encodeDocument(reopened) == encodeDocument(doc),
              "Shared native orientation persists");
        QTest::keyClick(&view, Qt::Key_Z, Qt::ControlModifier);
        check(doc.bodies().at(raw)->surface.normal(5).z < -.99 &&
                  doc.bodies().at(sibling)->surface.normal(5).z < -.99,
              "One Undo restores both shared instances");
        view.selectEntities({{raw, SelectionKind::Face, 5}, {raw, SelectionKind::Face, 10}});
        action(window, "orientation.connected");
        const auto invalidReference = encodeDocument(doc);
        start(view);
        check(!view.previewValid() && message.contains("exactly one reference") &&
                  encodeDocument(doc) == invalidReference,
              "Multiple reference faces reject without mutation");
        view.leaveContext();
        std::cout << "Native orientation arrows, physical front/back pixels, reflection, "
                     "preview/orbit/cancel/apply, stale/no-change, Undo/persistence and shared "
                     "scope passed\n";
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
