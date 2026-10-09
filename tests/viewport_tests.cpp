#include "app/surface_format.hpp"
#include "app/viewport.hpp"
#include "io/document_io.hpp"
#include <QApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QOpenGLContext>
#include <QPointer>
#include <QProcess>
#include <QScreen>
#include <QTest>
#include <QVBoxLayout>
#include <QWheelEvent>
#include <QWindow>
#include <iostream>
using namespace sketchy;
void checkTopologyViewport();
namespace {
void check(bool value, const char *message) {
    if (!value)
        throw std::runtime_error(message);
}
QImage frame(Viewport *view) {
    QCoreApplication::processEvents();
    auto image = view->grabFramebuffer();
    check(!image.isNull(), "Framebuffer capture failed");
    return image;
}
QColor sample(Viewport *view, Vec3 p) {
    auto image = frame(view);
    auto logical = view->project(p);
    auto physical = QPoint(qRound(logical.x() * image.width() / view->width()),
                           qRound(logical.y() * image.height() / view->height()));
    check(image.rect().contains(physical), "Sample is inside viewport");
    return image.pixelColor(physical);
}
void nearColor(QColor a, QColor b, const char *message) {
    if (std::abs(a.red() - b.red()) >= 6 || std::abs(a.green() - b.green()) >= 6 ||
        std::abs(a.blue() - b.blue()) >= 6)
        std::cerr << message << ": actual " << a.name().toStdString() << " expected "
                  << b.name().toStdString() << '\n';
    check(std::abs(a.red() - b.red()) < 6 && std::abs(a.green() - b.green()) < 6 &&
              std::abs(a.blue() - b.blue()) < 6,
          message);
}
QColor blend(QColor front, QColor back, double alpha) {
    return QColor(qRound(front.red() * alpha + back.red() * (1 - alpha)),
                  qRound(front.green() * alpha + back.green() * (1 - alpha)),
                  qRound(front.blue() * alpha + back.blue() * (1 - alpha)));
}
} // namespace
int main(int argc, char **argv) {
    QSurfaceFormat format;
    format.setVersion(3, 3);
    format.setProfile(QSurfaceFormat::CoreProfile);
    format.setDepthBufferSize(24);
    format.setSamples(0);
    sketchy::setDefaultViewportFormat(format);
    QApplication app(argc, argv);
    try {
        Document doc;
        auto makeFace = [&](double z, std::array<float, 3> color) {
            auto id = doc.addFace({{{-2, -2, z}, {2, -2, z}, {2, 2, z}, {-2, 2, z}}});
            doc.paint(id, color);
            return id;
        };
        // Deliberately insert the nearest layer first, opposite the needed blend order.
        auto front = makeFace(1, {.9f, .1f, .1f});
        auto back = makeFace(.4, {.1f, .1f, .9f});
        auto saved = encodeDocument(doc);
        QWidget first, second;
        first.resize(720, 640);
        second.resize(720, 640);
        QVBoxLayout firstLayout(&first), secondLayout(&second);
        auto *view = new Viewport(doc);
        firstLayout.addWidget(view);
        first.show();
        check(QTest::qWaitForWindowExposed(&first), "First viewport window exposed");
        QTest::qWait(100);
        view->standardView(1);
        view->fit();
        frame(view);
        check(view->rendererReady(), "Renderer initialized");
        const auto colorFormat = view->context()->format();
        check(colorFormat.redBufferSize() >= 8 && colorFormat.greenBufferSize() >= 8 &&
                  colorFormat.blueBufferSize() >= 8,
              "Viewport context preserves eight-bit RGB channels");
        Vec3 probe{-.6, .4, 1};
        auto opaque = sample(view, probe);
        check(opaque.red() > opaque.blue() * 3, "Near opaque face occludes the far face");
        check(view->pick(view->project(probe)).first == front, "Nearest opaque face is picked");
        view->setBodyOpacity(front, 0);
        check(view->pick(view->project(probe)).first == back,
              "Opacity immediately affects picking before repaint");
        auto behind = sample(view, probe);
        check(behind.blue() > behind.red() * 3, "Zero opacity exposes far face");
        check(view->pick(view->project(probe)).first == back, "Invisible front body is not picked");
        view->setBodyOpacity(front, .5);
        nearColor(sample(view, probe), blend(opaque, behind, .5),
                  "Transparency blends over opaque geometry");
        check(view->pick(view->project(probe)).first == front,
              "Visible transparent front face is picked");
        view->setBodyOpacity(front, 0);
        view->setBodyOpacity(back, 0);
        auto background = sample(view, probe);
        view->setBodyOpacity(front, .5);
        view->setBodyOpacity(back, .5);
        nearColor(sample(view, probe), blend(opaque, blend(behind, background, .5), .5),
                  "Transparent layers sort back to front");
        auto uploads = view->renderStats();
        frame(view);
        frame(view);
        check(view->renderStats().geometryUploads == uploads.geometryUploads &&
                  view->renderStats().transparencyUploads == uploads.transparencyUploads,
              "Stationary frames reuse GPU buffers");
        view->standardView(0);
        frame(view);
        auto navigated = view->renderStats();
        check(navigated.geometryUploads == uploads.geometryUploads &&
                  navigated.transparencyUploads > uploads.transparencyUploads,
              "Camera movement only reorders transparency");
        view->standardView(1);
        view->setBodyOpacity(front, 1);
        view->setBodyOpacity(back, 1);
        view->setClipPlane(std::array<double, 4>{0, 0, -1, .7});
        nearColor(sample(view, probe), behind, "Clipping removes front fragments");
        check(view->pick(view->project(probe)).first == back,
              "Clipped front face cannot intercept picking");
        view->setClipPlane(std::array<double, 4>{1, 0, 0, 0});
        frame(view);
        check(view->pick(view->project(probe)).first == 0,
              "Half-face clipping rejects the removed half");
        check(view->pick(view->project({.6, .4, 1})).first == front,
              "Half-face clipping keeps the visible half selectable");
        view->setClipPlane(std::nullopt);
        nearColor(sample(view, probe), opaque, "Disabling clipping restores geometry");
        bool rejected = false;
        try {
            view->setClipPlane(std::array<double, 4>{0, 0, 0, 0});
        } catch (const std::exception &) {
            rejected = true;
        }
        check(rejected, "Invalid clipping normal is rejected");
        check(encodeDocument(doc) == saved, "Viewport controls never mutate the model");
        const auto generation = view->renderStats().contextGeneration;
        const auto meshesBeforeContext = view->renderStats().bodyMeshBuilds;
        QPointer<QOpenGLContext> oldContext = view->context();
        firstLayout.removeWidget(view);
        view->setParent(&second);
        secondLayout.addWidget(view);
        second.show();
        view->show();
        check(QTest::qWaitForWindowExposed(&second), "Reparented viewport exposed");
        QTest::qWait(100);
        frame(view);
        check(oldContext.isNull(), "Reparenting destroyed the old GL context");
        check(view->renderStats().contextGeneration > generation && view->rendererReady(),
              "Reparenting recreated GL resources");
        check(view->renderStats().bodyMeshBuilds == meshesBeforeContext,
              "Context recreation reuses immutable CPU meshes");
        nearColor(sample(view, probe), opaque, "Context recreation preserves rendered pixels");
        check(view->pick(view->project(probe)).first == front,
              "Picking survives context recreation");
        second.hide();
        second.show();
        check(QTest::qWaitForWindowExposed(&second), "Viewport reshown");
        nearColor(sample(view, probe), opaque, "Hide/show preserves geometry");
        for (int width : {640, 900, 1200, 1600}) {
            view->setFixedSize(width, 480);
            frame(view);
            check(view->pick(view->project(probe)).first == front,
                  "Picking survives logical viewport resize");
        }
        check(view->renderStats().glError == 0,
              "No OpenGL errors after lifecycle and render tests");
        // Optional hardware run: ask the compositor to place a fullscreen test
        // window on each output and verify the actual output and effective scale.
        // An optional helper may move this test's own window on compositors
        // that ignore Qt's output request. Report that assistance explicitly.
        // No monitor settings or window rules are modified.
        QJsonArray transitions;
        if (app.arguments().contains("--screens")) {
            check(QGuiApplication::screens().size() >= 2,
                  "Output transition checks require at least two connected outputs");
            view->setMinimumSize(160, 160);
            view->setMaximumSize(QWIDGETSIZE_MAX, QWIDGETSIZE_MAX);
            const auto placementHelper = qEnvironmentVariable("SKETCHYUP_TEST_OUTPUT_HELPER");
            for (auto *screen : QGuiApplication::screens()) {
                QWidget output;
                output.setWindowTitle("SketchyUp output test " + screen->name());
                QVBoxLayout layout(&output);
                secondLayout.removeWidget(view);
                view->setParent(&output);
                layout.addWidget(view);
                output.winId();
                output.windowHandle()->setScreen(screen);
                // X11 outputs share a virtual desktop. setScreen alone does not
                // move a window's coordinates into the requested output.
                if (QGuiApplication::platformName() == "xcb")
                    output.setGeometry(screen->geometry());
                output.showFullScreen();
                view->show();
                check(QTest::qWaitForWindowExposed(&output), "Output test window exposed");
                if (!placementHelper.isEmpty()) {
                    QProcess helper;
                    helper.setProcessChannelMode(QProcess::ForwardedErrorChannel);
                    helper.start(
                        placementHelper,
                        {QString::number(QCoreApplication::applicationPid()), screen->name()});
                    if (!helper.waitForFinished(5000)) {
                        helper.kill();
                        helper.waitForFinished(1000);
                        check(false, "Output placement helper timed out or failed to start");
                    }
                    check(helper.exitStatus() == QProcess::NormalExit && helper.exitCode() == 0,
                          "Output placement helper failed");
                }
                QTest::qWait(300);
                const auto outputFrame = frame(view);
                const auto outputFormat = view->context()->format();
                check(outputFormat.redBufferSize() >= 8 && outputFormat.greenBufferSize() >= 8 &&
                          outputFormat.blueBufferSize() >= 8,
                      "Each output context preserves eight-bit RGB channels");
                check(output.windowHandle()->screen() == screen,
                      "Compositor used the requested output");
                nearColor(sample(view, probe), opaque, "Pixels preserved across output transition");
                check(view->pick(view->project(probe)).first == front,
                      "Picking aligned on target output");
                check(view->renderStats().glError == 0, "No GL error across output transition");
                transitions.append(QJsonObject{
                    {"screen", screen->name()},
                    {"placement",
                     placementHelper.isEmpty() ? "Qt fullscreen request" : "external test helper"},
                    {"effectiveScale", view->devicePixelRatioF()},
                    {"logicalWidth", view->width()},
                    {"logicalHeight", view->height()},
                    {"framebufferWidth", outputFrame.width()},
                    {"framebufferHeight", outputFrame.height()},
                    {"graphics", view->graphicsDescription()},
                    {"contextGeneration", qint64(view->renderStats().contextGeneration)},
                    {"colorBits",
                     QJsonArray{outputFormat.redBufferSize(), outputFormat.greenBufferSize(),
                                outputFormat.blueBufferSize(), outputFormat.alphaBufferSize()}}});
                layout.removeWidget(view);
                view->setParent(&second);
                secondLayout.addWidget(view);
                view->show();
            }
        }
        doc.move(front, {10, 0, 0});
        check(view->pick(view->project(probe)).first == back,
              "Edits immediately invalidate picking before repaint");
        frame(view);
        check(view->renderStats().glError == 0, "Edit uploads succeed");
        const auto beforePaint = view->renderStats();
        doc.paint(back, {.2f, .2f, .8f});
        frame(view);
        check(view->renderStats().bodyUploads == beforePaint.bodyUploads + 1 &&
                  view->renderStats().bodyMeshBuilds == beforePaint.bodyMeshBuilds,
              "Painting updates one body GPU cache without retriangulating unchanged geometry");
        const auto beforeMove = view->renderStats();
        doc.move(back, {1, 0, 0});
        frame(view);
        check(view->renderStats().bodyUploads == beforeMove.bodyUploads + 1 &&
                  view->renderStats().bodyWorldUpdates == beforeMove.bodyWorldUpdates + 1 &&
                  view->renderStats().bodyMeshBuilds == beforeMove.bodyMeshBuilds,
              "Transform updates only affected world/GPU cache");
        const auto beforeZoom = view->renderStats();
        const auto beforeProjection = view->project(probe);
        const auto center = view->rect().center();
        QWheelEvent wheel(center, view->mapToGlobal(center), QPoint(0, 15), QPoint(), Qt::NoButton,
                          Qt::NoModifier, Qt::ScrollUpdate, false);
        QApplication::sendEvent(view, &wheel);
        frame(view);
        check(view->project(probe) != beforeProjection &&
                  view->renderStats().bodyUploads == beforeZoom.bodyUploads,
              "Pixel-delta wheel zoom changes camera without body uploads");
        const auto beforeAdd = view->renderStats();
        auto extra = doc.addFace({{{0, 0, 2}, {1, 0, 2}, {0, 1, 2}}});
        frame(view);
        check(view->renderStats().bodyMeshBuilds == beforeAdd.bodyMeshBuilds + 1 &&
                  view->renderStats().bodyUploads == beforeAdd.bodyUploads + 1,
              "Adding geometry builds only its body cache");
        doc.erase(extra);
        frame(view);
        check(view->renderStats().cachedBodies == doc.bodies().size() &&
                  view->renderStats().bodyUploads == beforeAdd.bodyUploads + 1,
              "Deleting a body releases its cache without reuploading survivors");
        auto mirrored = doc.addFace({{{0, 0, 0}, {2, 0, 0}, {0, 2, 0}}});
        doc.paint(mirrored, {.1f, .9f, .1f});
        doc.transform(mirrored,
                      Transform::translation({-3, 0, 3}) * Transform::scaling({-1, 1, 1}));
        auto nested = doc.addFace({{{0, 0, 0}, {1, 0, 0}, {0, 1, 0}}});
        doc.paint(nested, {.1f, .1f, .9f});
        doc.transform(nested, Transform::translation({0, 0, 1}), mirrored);
        view->standardView(1);
        view->fit();
        const Vec3 nestedProbe{-3.2, .2, 4};
        check(view->pick(view->project(nestedProbe)).first == nested,
              "Immediate picking uses nested mirrored world transform");
        const auto nestedPixel = sample(view, nestedProbe);
        check(nestedPixel.blue() > nestedPixel.red() * 3,
              "Nested mirrored geometry renders at picked position");
        doc.undo();
        check(view->pick(view->project(nestedProbe)).first == mirrored,
              "Undo of parent transform invalidates picking immediately");
        frame(view);
        doc.redo();
        frame(view);
        const auto beforeAncestorMove = view->renderStats();
        doc.move(mirrored, {1, 0, 0});
        frame(view);
        check(view->renderStats().bodyWorldUpdates == beforeAncestorMove.bodyWorldUpdates + 2 &&
                  view->renderStats().bodyUploads == beforeAncestorMove.bodyUploads + 2 &&
                  view->renderStats().bodyMeshBuilds == beforeAncestorMove.bodyMeshBuilds,
              "Parent motion updates parent and descendant caches only");
        auto tiny = doc.addFace({{{0, 0, 0}, {1, 0, 0}, {0, 1, 0}}});
        doc.transform(tiny, Transform::scaling({.0001, .0001, .0001}));
        frame(view);
        check(view->rendererReady() && view->renderStats().glError == 0,
              "Small transformed faces do not disable renderer");
        QJsonArray screens;
        for (auto *screen : QGuiApplication::screens())
            screens.append(
                QJsonObject{{"name", screen->name()}, {"scale", screen->devicePixelRatio()}});
        checkTopologyViewport();
        QJsonObject result{
            {"passed", true},
            {"platform", QGuiApplication::platformName()},
            {"graphics", view->graphicsDescription()},
            {"scale", view->devicePixelRatioF()},
            {"colorBits", QJsonArray{colorFormat.redBufferSize(), colorFormat.greenBufferSize(),
                                     colorFormat.blueBufferSize(), colorFormat.alphaBufferSize()}},
            {"contextGenerations", int(view->renderStats().contextGeneration)},
            {"bodyMeshBuilds", qint64(view->renderStats().bodyMeshBuilds)},
            {"bodyUploads", qint64(view->renderStats().bodyUploads)},
            {"cachedBodies", int(view->renderStats().cachedBodies)},
            {"outputTransitions", transitions},
            {"screens", screens},
            {"checks", "depth, transparency, sort order, clipping/picking, buffer reuse, context "
                       "recreation, hide/show, resize, immutable document, incremental body "
                       "caches, pixel-delta zoom"}};
        std::cout << QJsonDocument(result).toJson(QJsonDocument::Compact).toStdString() << '\n';
        return 0;
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
