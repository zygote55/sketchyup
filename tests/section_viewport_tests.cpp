#include "app/surface_format.hpp"
#include "app/viewport.hpp"
#include "core/assets.hpp"
#include "core/groups.hpp"
#include "core/materials.hpp"
#include "core/sections.hpp"
#include "io/document_io.hpp"
#include <QApplication>
#include <QBuffer>
#include <QElapsedTimer>
#include <QFile>
#include <QMouseEvent>
#include <QTemporaryDir>
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
    const auto image = view.grabFramebuffer();
    check(!image.isNull() && view.rendererReady() && view.renderStats().glError == 0,
          "Section framebuffer is valid");
    return image;
}
QColor sample(Viewport &view, Vec3 point) {
    const auto image = frame(view);
    const auto at = view.project(point);
    const QPoint pixel(qRound(at.x() * image.width() / view.width()),
                       qRound(at.y() * image.height() / view.height()));
    check(image.rect().contains(pixel), "Section probe lies inside framebuffer");
    return image.pixelColor(pixel);
}
void near(QColor actual, QColor expected, const char *message, int error = 7) {
    if (std::abs(actual.red() - expected.red()) > error ||
        std::abs(actual.green() - expected.green()) > error ||
        std::abs(actual.blue() - expected.blue()) > error) {
        std::cerr << message << ": " << actual.name().toStdString() << " / "
                  << expected.name().toStdString() << '\n';
        throw std::runtime_error(message);
    }
}
Id box(Document &doc, double x) {
    const auto id = doc.addFace({{{x, 0, 0}, {x + 2, 0, 0}, {x + 2, 2, 0}, {x, 2, 0}}});
    doc.extrude(id, doc.bodies().at(id)->surface.faces.begin()->first, 2);
    doc.paint(id, {1, 0, 0});
    return id;
}
void configure(Document &doc) {
    auto style = doc.style();
    style.gridVisible = style.axesVisible = style.edgesVisible = false;
    style.background = {.1f, .2f, .3f};
    doc.setStyle(style);
}
void show(QWidget &host, Viewport &view) {
    host.resize(960, 720);
    host.show();
    check(QTest::qWaitForWindowExposed(&host), "Section viewport exposed");
    view.standardView(1);
    view.fit();
    frame(view);
}
void move(Viewport &view, Vec3 point) {
    const auto at = view.project(point);
    QMouseEvent event(QEvent::MouseMove, at, view.mapToGlobal(at.toPoint()), Qt::NoButton,
                      Qt::NoButton, Qt::NoModifier);
    QCoreApplication::sendEvent(&view, &event);
    QCoreApplication::processEvents();
}
void solidSections() {
    Document doc;
    const auto body = box(doc, 0), sibling = box(doc, 3.5);
    const auto group = createGroup(doc, {body}, "Mirrored section context");
    renameEntity(doc, body, "Nested solid");
    renameEntity(doc, sibling, "Sibling solid");
    const auto originalSurface = doc.bodies().at(body)->surface;
    configure(doc);
    const auto rootCut = createSection(doc, "Model horizontal", 0, {{0, 0, -1}, 1});
    auto record = *doc.sections().at(rootCut);
    record.color = {.1f, .8f, .2f};
    updateSection(doc, rootCut, record);
    const auto parentCut = createSection(doc, "Parent half", group, {{0, 1, 0}, -1});
    const auto childCut = createSection(doc, "Child half", body, {{1, 0, 0}, -1});
    QWidget host;
    QVBoxLayout layout(&host);
    auto *view = new Viewport(doc);
    layout.addWidget(view);
    show(host, *view);
    check(view->pick(view->project({1.5, 1.5, 2})).first == body, "Uncut native face is pickable");
    const auto meshBuilds = view->renderStats().bodyMeshBuilds;
    setActiveSection(doc, 0, rootCut);
    // Picking before repaint must see the current cut and its occluding cap.
    check(view->pick(view->project({1.5, 1.5, 1})).first == 0,
          "Fresh named cap blocks native face tools before repaint");
    const auto cap = sample(*view, {1.5, 1.5, 1});
    check(cap.green() > 170 && cap.red() < 80 && cap.blue() < 90,
          "Named section cap uses its fill color");
    auto selected = view->selectionAt(view->project({1.5, 1.5, 1}));
    check(selected && selected->body == group && selected->kind == SelectionKind::Body,
          "Generated cap selects its enclosing context without a fake native face");
    setActiveSection(doc, group, parentCut);
    setActiveSection(doc, body, childCut);
    near(sample(*view, {1.5, 1.5, 1}), cap, "Three ancestor cuts retain correct cap quadrant");
    near(sample(*view, {.4, 1.5, 1}), QColor::fromRgbF(.1, .2, .3),
         "Child cut removes left geometry");
    near(sample(*view, {1.5, .4, 1}), QColor::fromRgbF(.1, .2, .3),
         "Parent cut removes lower geometry");
    near(sample(*view, {3.9, .4, 1}), cap, "Sibling inherits only model cut");
    check(view->pick(view->project({.4, 1.5, 1})).first == 0 &&
              !view->selectionAt(view->project({.4, 1.5, 1})),
          "Removed geometry cannot be picked");
    check(doc.bodies().at(body)->surface == originalSurface &&
              view->renderStats().bodyMeshBuilds == meshBuilds,
          "Clipping preserves native surface and triangulation");
    const auto active = doc.activeSections();
    doc.undo();
    near(sample(*view, {.4, 1.5, 1}), cap, "Undo activation restores the visible half");
    doc.redo();
    check(doc.activeSections() == active, "Redo restores active planes exactly");
    doc.transform(group, Transform::translation({2, 0, 0}) * Transform::scaling({-1, 1, 1}));
    near(sample(*view, {.5, 1.5, 1}), cap, "Mirrored nested contexts retain the local half-space");
    near(sample(*view, {1.5, 1.5, 1}), QColor::fromRgbF(.1, .2, .3),
         "Mirrored cut clips the opposite world side");
    record = *doc.sections().at(rootCut);
    record.fill = false;
    updateSection(doc, rootCut, record);
    const auto bottom = sample(*view, {.5, 1.5, 0});
    check(bottom.red() > 150 && bottom.green() < 20,
          "Turning cap fill off exposes retained interior face");
    check(view->pick(view->project({.5, 1.5, 0})).first == body,
          "Interior native face remains pickable without cap fill");
    const auto behindCap = doc.addWire(0, {.25, 1.5, .25}, {.75, 1.5, .25});
    frame(*view);
    check(view->pickEdge(view->project({.5, 1.5, .25})).first == behindCap,
          "Wire behind an unfilled cut remains available to edge tools");
    record.fill = true;
    updateSection(doc, rootCut, record);
    frame(*view);
    check(view->pickEdge(view->project({.5, 1.5, .25})).first == 0,
          "Filled cap blocks CPU edge picking through its interior");
    record.fill = false;
    updateSection(doc, rootCut, record);
    view->enterContext(group);
    view->setTool(Viewport::Tool::Line);
    view->refresh();
    frame(*view);
    check(QTest::qWaitFor([&] { return view->inferenceReady(); }, 10000),
          "Section inference index becomes ready");
    move(*view, {.5, 1.5, 0});
    check(view->acquiredInference() && view->acquiredInference()->kind == InferenceKind::OnFace &&
              std::abs(view->acquiredInference()->point.z) < tolerance,
          "Removed top face does not occlude retained interior inference");
    record.fill = true;
    updateSection(doc, rootCut, record);
    view->refresh();
    frame(*view);
    check(QTest::qWaitFor([&] { return view->inferenceReady(); }, 10000),
          "Cap inference index becomes ready");
    move(*view, {.5, 1.5, 0});
    check(!view->acquiredInference(), "Filled derived cap blocks inference through the interior");
    view->setTool(Viewport::Tool::Select);
    view->leaveContext();
    const auto stored = encodeContainer(doc);
    check(encodeContainer(decodeContainer(stored)) == stored,
          "Native section state reopens exactly");
    check(doc.bodies().at(body)->surface == originalSurface,
          "Mirrored section view preserves authoritative geometry");
    const auto capture = qEnvironmentVariable("SKETCHYUP_SECTION_VIEW_EVIDENCE");
    if (!capture.isEmpty()) {
        record.fill = true;
        updateSection(doc, rootCut, record);
        auto style = doc.style();
        style.edgesVisible = true;
        doc.setStyle(style);
        view->standardView(0);
        view->fit();
        check(frame(*view).save(capture), "Raw section framebuffer saved");
    }
    const auto model = qEnvironmentVariable("SKETCHYUP_SECTION_MODEL_EVIDENCE");
    if (!model.isEmpty()) {
        QFile file(model);
        check(file.open(QIODevice::WriteOnly), "Section fixture opened");
        const auto bytes = encodeContainer(doc);
        check(file.write(bytes) == bytes.size(), "Native section fixture saved");
    }
    host.close();
}
void texturedCut() {
    Document doc;
    const auto body = doc.addFace({{{0, 0, 0}, {2, 0, 0}, {2, 2, 0}, {0, 2, 0}}});
    configure(doc);
    QImage pixels(8, 8, QImage::Format_RGBA8888);
    for (int y = 0; y < 8; ++y)
        for (int x = 0; x < 8; ++x)
            pixels.setPixelColor(x, y, x < 4 ? QColor(255, 30, 10) : QColor(10, 220, 240));
    QByteArray bytes;
    QBuffer buffer(&bytes);
    buffer.open(QIODevice::WriteOnly);
    check(pixels.save(&buffer, "PNG"), "Section texture encoded");
    const auto asset = createAsset(
        doc, "Section image", "image/png",
        std::make_shared<AssetPayload>(std::vector<std::uint8_t>(bytes.begin(), bytes.end())));
    const auto material = createMaterial(doc, "Section texture", {1, 1, 1}, 1, asset);
    assignMaterial(doc, body, {}, material, true, true);
    const auto plane = createSection(doc, "Local half", body, {{1, 0, 0}, -1});
    QWidget host;
    QVBoxLayout layout(&host);
    auto *view = new Viewport(doc);
    layout.addWidget(view);
    show(host, *view);
    QElapsedTimer timer;
    timer.start();
    while (view->texturesPending() && timer.elapsed() < 10000)
        QTest::qWait(10);
    check(!view->texturesPending(), "Section texture worker settles");
    QTest::qWait(40);
    const auto original = sample(*view, {1.3, .3, 0});
    setActiveSection(doc, body, plane);
    frame(*view);
    check(view->pickEdge(view->project({.3, 0, 0})).first == 0 &&
              view->pickEdge(view->project({1.3, 0, 0})).first == body,
          "Named clipping trims CPU edge picking while retaining the native edge identity");
    near(sample(*view, {1.3, .3, 0}), original,
         "Clipped UV interpolation preserves native texture sampling");
    near(sample(*view, {.3, .3, 0}), QColor::fromRgbF(.1, .2, .3),
         "Open textured face clips without invented cap fill");
    doc.addGuide(body, guidePoint({1.5, 1, 0}));
    const auto retainedGuide = doc.bodies().at(body)->guides.rbegin()->first;
    doc.addGuide(body, guidePoint({.3, 1, 0}));
    const auto removedGuide = doc.bodies().at(body)->guides.rbegin()->first;
    frame(*view);
    const SelectedEntity guide{body, SelectionKind::Guide, retainedGuide};
    check(view->selectionAt(view->project({1.5, 1, 0})) == guide &&
              !view->selectionAt(view->project({.3, 1, 0})),
          "Guide picking uses the context's retained half-space");
    const auto retainedBounds = QRectF(view->project({1, 0, 0}), view->project({2, 2, 0}))
                                    .normalized()
                                    .adjusted(-4, -4, 4, 4);
    const auto contained = view->windowSelection(retainedBounds, false);
    check(contained.contains(guide) &&
              !contained.contains({body, SelectionKind::Guide, removedGuide}) &&
              contained.contains(
                  {body, SelectionKind::Face, doc.bodies().at(body)->surface.faces.begin()->first}),
          "Window selection encloses retained face and guide without requiring clipped vertices");
    doc.transform(body, Transform::scaling({-1, 1, 1}));
    view->fit();
    near(sample(*view, {-1.3, .3, 0}), original,
         "Reflected local cut preserves independent side UVs");
    check(view->pick(view->project({-1.3, .3, 0})).first == body,
          "Retained textured native face is pickable");
    host.close();
}
} // namespace
int main(int argc, char **argv) {
    QSurfaceFormat format;
    format.setVersion(3, 3);
    format.setProfile(QSurfaceFormat::CoreProfile);
    format.setDepthBufferSize(24);
    format.setSamples(0);
    sketchy::setDefaultViewportFormat(format);
    QTemporaryDir isolated;
    qputenv("XDG_CONFIG_HOME", isolated.path().toUtf8());
    QApplication app(argc, argv);
    try {
        solidSections();
        texturedCut();
        std::cout << "Native scoped sections, caps, picking, Undo, reflection and UVs passed\n";
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
