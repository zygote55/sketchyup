#include "app/viewport.hpp"
#include "core/assets.hpp"
#include "core/edge_appearance.hpp"
#include "core/materials.hpp"
#include "io/document_io.hpp"
#include "io/model_style_io.hpp"
#include <QApplication>
#include <QBuffer>
#include <QElapsedTimer>
#include <QFile>
#include <QJsonDocument>
#include <QSurfaceFormat>
#include <QTest>
#include <QVBoxLayout>
#include <iostream>
using namespace sketchy;
namespace {
void check(bool value, const char *message) {
    if (!value)
        throw std::runtime_error(message);
}
QImage frame(Viewport &view) {
    QCoreApplication::processEvents();
    auto image = view.grabFramebuffer();
    check(!image.isNull() && view.rendererReady() && view.renderStats().glError == 0,
          "Style framebuffer is valid");
    return image;
}
QColor sample(Viewport &view, Vec3 point) {
    const auto image = frame(view);
    const auto p = view.project(point);
    const QPoint pixel(qRound(p.x() * image.width() / view.width()),
                       qRound(p.y() * image.height() / view.height()));
    check(image.rect().contains(pixel), "Probe lies inside framebuffer");
    return image.pixelColor(pixel);
}
void picks(Viewport &view, Vec3 point, Id id) {
    check(view.pick(view.project(point)).first == id, "Style preserves CPU face picking");
    const auto selected = view.selectionAt(view.project(point));
    check(selected && selected->body == id, "Style preserves GPU selection and alpha cutouts");
}
void near(QColor actual, QColor expected, const char *message, int error = 5) {
    check(std::abs(actual.red() - expected.red()) <= error &&
              std::abs(actual.green() - expected.green()) <= error &&
              std::abs(actual.blue() - expected.blue()) <= error,
          message);
}
void settle(Viewport &view) {
    frame(view);
    QElapsedTimer timer;
    timer.start();
    while (view.texturesPending() && timer.elapsed() < 10000)
        QTest::qWait(10);
    check(!view.texturesPending(), "Image worker settles");
    QTest::qWait(40);
    frame(view);
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
    try {
        Document doc;
        const auto plane = [&](double z) {
            return doc.addFace({{{0, 0, z}, {1, 0, z}, {1, 1, z}, {0, 1, z}}});
        };
        const auto body = plane(1), behind = plane(.5);
        doc.paint(behind, {1, 1, 0});
        QImage source(8, 8, QImage::Format_RGBA8888);
        source.fill(Qt::red);
        for (int y = 0; y < 4; ++y)
            for (int x = 4; x < 8; ++x)
                source.setPixelColor(x, y, QColor(0, 255, 0, 0));
        QByteArray bytes;
        QBuffer buffer(&bytes);
        buffer.open(QIODevice::WriteOnly);
        check(source.save(&buffer, "PNG"), "Texture fixture encoded");
        const auto asset = createAsset(
            doc, "Cutout", "image/png",
            std::make_shared<AssetPayload>(std::vector<std::uint8_t>(bytes.begin(), bytes.end())));
        const auto material = createMaterial(doc, "White image tint", {1, 1, 1}, 1, asset);
        assignMaterial(doc, body, {}, material, true, false);
        QWidget host, second;
        host.resize(800, 680);
        second.resize(800, 680);
        QVBoxLayout layout(&host), secondLayout(&second);
        auto *view = new Viewport(doc);
        layout.addWidget(view);
        auto style = doc.style();
        style.gridVisible = style.axesVisible = style.edgesVisible = false;
        style.background = {.1f, .2f, .3f};
        style.front = {0, 1, 1};
        style.back = {1, 0, 1};
        doc.setStyle(style);
        doc.markSaved();
        host.show();
        check(QTest::qWaitForWindowExposed(&host), "Style viewport exposed");
        view->standardView(1);
        view->fit();
        settle(*view);
        const Vec3 opaque{.25, .25, 1}, hole{.75, .25, 1};
        const auto geometry = doc.bodies();
        const auto materials = doc.materials();
        const auto assets = doc.assets();
        const auto meshes = view->renderStats().bodyMeshBuilds;
        const auto red = sample(*view, opaque);
        check(red.red() > 200 && red.green() < 8 && red.blue() < 8,
              "Textured mode shows image RGB");
        picks(*view, opaque, body);
        picks(*view, hole, behind);
        for (auto mode :
             {ModelStyleMode::Shaded, ModelStyleMode::Monochrome, ModelStyleMode::Wireframe,
              ModelStyleMode::XRay, ModelStyleMode::Textured}) {
            style.mode = mode;
            doc.setStyle(style);
            const auto pixel = sample(*view, opaque);
            picks(*view, opaque, body);
            picks(*view, hole, behind);
            check(doc.bodies() == geometry && doc.materials() == materials &&
                      doc.assets() == assets && view->renderStats().bodyMeshBuilds == meshes,
                  "Style switch preserves records and triangulation");
            if (mode == ModelStyleMode::Shaded)
                check(pixel.red() > 200 && pixel.green() > 200 && pixel.blue() > 200,
                      "Shaded uses tint without image RGB");
            if (mode == ModelStyleMode::Monochrome)
                check(pixel.red() < 8 && pixel.green() > 200 && pixel.blue() > 200,
                      "Monochrome uses front style color");
            if (mode == ModelStyleMode::Wireframe)
                near(pixel, QColor::fromRgbF(.1, .2, .3), "Wireframe omits face fill");
            if (mode == ModelStyleMode::XRay)
                check(pixel.red() > 30 && pixel.red() < 220 && pixel.blue() > 30,
                      "X-ray blends material tint over background");
            if (mode == ModelStyleMode::Textured)
                near(pixel, red, "Returning to textured restores image appearance");
        }
        const auto saved = encodeContainer(doc);
        const auto light = frame(*view);
        view->setTheme(themeColors(true));
        const auto dark = frame(*view);
        check(light == dark && encodeContainer(doc) == saved,
              "Application theme does not change model presentation or bytes");
        style.mode = ModelStyleMode::Monochrome;
        doc.setStyle(style);
        doc.move(behind, {0, 0, 2});
        view->standardView(6);
        const auto back = sample(*view, opaque);
        check(back.red() > 200 && back.green() < 8 && back.blue() > 200,
              "Monochrome uses physical back color from below");
        doc.undo();
        view->standardView(1);
        style.mode = ModelStyleMode::Textured;
        style.groundVisible = true;
        style.ground = {.8f, .3f, .1f};
        doc.setStyle(style);
        near(sample(*view, {-0.1, .3, 0}), QColor::fromRgbF(.8, .3, .1),
             "Ground color appears outside geometry");
        picks(*view, opaque, body);
        picks(*view, hole, behind);
        style.groundVisible = false;
        style.mode = ModelStyleMode::Wireframe;
        style.edge = {0, 0, 0};
        style.background = {1, 1, 1};
        style.profiles = true;
        style.profileWidth = 6;
        doc.setStyle(style);
        frame(*view);
        check(view->renderStats().profileEdges == 8, "Open face boundaries become profiles");
        const auto face = doc.bodies().at(body)->surface.faces.begin()->first;
        SelectionSet edges;
        for (const auto &[id, edge] : doc.bodies().at(body)->topology.edges)
            edges.insert({body, SelectionKind::Edge, id});
        setEdgeAppearance(doc, edges, 0, {}, true, {});
        frame(*view);
        check(view->renderStats().profileEdges == 8,
              "Softened boundaries still produce silhouettes");
        const auto image = frame(*view);
        const auto projected = view->project({0, .5, 1});
        const double ratio = double(image.width()) / view->width();
        const int x = qRound(projected.x() * ratio), y = qRound(projected.y() * ratio);
        int darkPixels = 0;
        for (int dx = -qRound(8 * ratio); dx <= qRound(8 * ratio); ++dx)
            if (image.pixelColor(x + dx, y).lightness() < 40)
                ++darkPixels;
        check(darkPixels >= int(5 * ratio) && darkPixels <= int(8 * ratio),
              "Profile thickness follows logical pixels at display scale");
        setEdgeAppearance(doc, edges, 0, true, {}, {});
        frame(*view);
        check(view->renderStats().profileEdges == 4,
              "Explicitly hidden boundaries do not produce profiles");
        doc.undo();
        doc.undo();
        style.profiles = false;
        style.edgesVisible = false;
        style.mode = ModelStyleMode::Shaded;
        doc.setStyle(style);
        frame(*view);
        check(view->renderStats().profileEdges == 0, "Profile control disables profiles");
        const auto beforeGrid = frame(*view);
        style.gridVisible = true;
        style.axesVisible = true;
        doc.setStyle(style);
        check(frame(*view) != beforeGrid, "Grid and axes controls affect native drawing");
        style.gridVisible = style.axesVisible = false;
        doc.setStyle(style);
        view->selectEntities({{body, SelectionKind::Face, face}});
        const auto selected = frame(*view);
        view->selectEntities({});
        check(selected != frame(*view), "Selection remains visible with model edges disabled");
        style.mode = ModelStyleMode::Monochrome;
        style.background = {.04f, .04f, .04f};
        style.profiles = true;
        doc.setStyle(style);
        const auto beforeContext = sample(*view, opaque);
        const auto generation = view->renderStats().contextGeneration;
        layout.removeWidget(view);
        view->setParent(&second);
        secondLayout.addWidget(view);
        second.show();
        view->show();
        check(QTest::qWaitForWindowExposed(&second), "Reparented style viewport exposed");
        settle(*view);
        near(sample(*view, opaque), beforeContext, "Styles survive context recreation");
        check(view->renderStats().contextGeneration > generation, "New GL context allocated");
        picks(*view, hole, behind);
        view->setClipPlane(std::array<double, 4>{0, 0, -1, .7});
        frame(*view);
        picks(*view, opaque, behind);
        view->setClipPlane({});
        // A closed cube has six silhouette edges from an isometric view. Camera
        // motion and a mirrored placement change facing without retriangulation.
        doc = Document{};
        const auto cube = doc.addFace({{{0, 0, 0}, {1, 0, 0}, {1, 1, 0}, {0, 1, 0}}});
        doc.extrude(cube, doc.bodies().at(cube)->surface.faces.begin()->first, 1);
        auto cubeStyle = doc.style();
        cubeStyle.profiles = true;
        cubeStyle.profileWidth = 5;
        cubeStyle.gridVisible = cubeStyle.axesVisible = false;
        cubeStyle.background = {.04f, .04f, .04f};
        doc.setStyle(cubeStyle);
        view->standardView(0);
        view->fit();
        frame(*view);
        check(view->renderStats().profileEdges == 6,
              "Isometric closed cube has six silhouette edges");
        const auto cubeMeshes = view->renderStats().bodyMeshBuilds;
        view->setOrthographic(true);
        frame(*view);
        check(view->renderStats().profileEdges == 6 &&
                  view->renderStats().bodyMeshBuilds == cubeMeshes,
              "Projection changes update profiles without retriangulating geometry");
        doc.transform(cube, Transform::scaling({-1, 1, 1}));
        view->fit();
        frame(*view);
        check(view->renderStats().profileEdges == 6, "Mirrored cube retains geometric silhouettes");
        const auto capture = qEnvironmentVariable("SKETCHYUP_STYLE_VIEWPORT_EVIDENCE");
        if (!capture.isEmpty())
            check(frame(*view).save(capture), "Raw style framebuffer saved");
        QJsonObject result{{"passed", true},
                           {"platform", QGuiApplication::platformName()},
                           {"scale", view->devicePixelRatioF()},
                           {"modelStyle", encodeModelStyle(doc.style())},
                           {"profileEdges", double(view->renderStats().profileEdges)}};
        view->benchmark(100);
        frame(*view);
        check(view->renderStats().profileEdges == 0,
              "Synthetic benchmark does not report stale model profile counts");
        result["syntheticProfileEdges"] = double(view->renderStats().profileEdges);
        std::cout << QJsonDocument(result).toJson(QJsonDocument::Compact).toStdString() << '\n';
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
