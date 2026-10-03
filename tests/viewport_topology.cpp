#include "app/viewport.hpp"
#include "io/document_io.hpp"
#include <QApplication>
#include <QTest>
#include <QVBoxLayout>
#include <iostream>
using namespace sketchy;
namespace {
void check(bool value, const char *message) {
    if (!value)
        throw std::runtime_error(message);
}
QImage frame(Viewport *view) {
    QCoreApplication::processEvents();
    auto image = view->grabFramebuffer();
    check(!image.isNull() && view->rendererReady(), "Topology frame available");
    return image;
}
QColor sample(Viewport *view, Vec3 point) {
    const auto image = frame(view);
    const auto p = view->project(point);
    const QPoint pixel(qRound(p.x() * image.width() / view->width()),
                       qRound(p.y() * image.height() / view->height()));
    check(image.rect().contains(pixel), "Topology probe in framebuffer");
    return image.pixelColor(pixel);
}
Id edgeBetween(const Body &body, Vec3 a, Vec3 b) {
    for (const auto &[id, edge] : body.topology.edges) {
        const auto pa = body.surface.vertices.at(edge.a), pb = body.surface.vertices.at(edge.b);
        if ((pa == a && pb == b) || (pa == b && pb == a))
            return id;
    }
    throw std::runtime_error("Fixture edge missing");
}
} // namespace
void checkTopologyViewport() {
    Document doc;
    const auto back = doc.addFace({{{-6, -4, -.5}, {6, -4, -.5}, {6, 4, -.5}, {-6, 4, -.5}}});
    doc.paint(back, {.1f, .1f, .9f});
    const auto front =
        doc.addFace({{{-4, -3, 0}, {4, -3, 0}, {4, 0, 0}, {0, 0, 0}, {0, 3, 0}, {-4, 3, 0}},
                     {{-3, -2, 0}, {-3, -1, 0}, {-2, -1, 0}, {-2, -2, 0}},
                     {{-3, 1, 0}, {-3, 2, 0}, {-2, 2, 0}, {-2, 1, 0}}});
    doc.paint(front, {.9f, .1f, .1f});
    const auto frontFace = doc.bodies().at(front)->surface.faces.begin()->first;
    const auto edge = edgeBetween(*doc.bodies().at(front), {-3, -2, 0}, {-2, -2, 0});
    const auto hidden = doc.addWire(0, {-1.5, -1.5, -.2}, {-.5, -1.5, -.2});
    const auto loose = doc.addWire(0, {1, 3.5, 0}, {3, 3.5, 0});
    QWidget host;
    host.resize(900, 700);
    QVBoxLayout layout(&host);
    auto *view = new Viewport(doc);
    layout.addWidget(view);
    host.show();
    check(QTest::qWaitForWindowExposed(&host), "Topology viewport exposed");
    view->standardView(1);
    view->fit();
    frame(view);
    const auto original = encodeDocument(doc);
    for (auto point : {Vec3{-2.5, -1.5, 0}, Vec3{-2.5, 1.5, 0}, Vec3{2, 2, 0}}) {
        check(view->pick(view->project(point)).first == back,
              "Hole/concavity picks underlying face");
        const auto color = sample(view, point);
        check(color.blue() > color.red() * 3, "Hole/concavity is not filled by tessellation");
    }
    const auto solid = sample(view, {-1.2, -1.3, 0});
    const auto onGrid = sample(view, {-1, -1.3, 0});
    check(solid.red() > solid.blue() * 3, "Concave face interior is filled");
    check(std::abs(solid.red() - onGrid.red()) < 5 && std::abs(solid.green() - onGrid.green()) < 5,
          "Reference grid cannot shine through coplanar opaque face");
    check(view->pickEdge(view->project({-2.5, -2, 0})) == std::pair<Id, Id>{front, edge},
          "Hole boundary picks persistent edge ID");
    check(view->pickEdge(view->project({-1, -1.5, -.2})).first == 0,
          "Hidden edge cannot be picked through opaque face");
    check(view->pickEdge(view->project({2, 3.5, 0})).first == loose,
          "Visible loose wire is pickable");
    view->setBodyOpacity(front, 0);
    check(view->pickEdge(view->project({-1, -1.5, -.2})).first == hidden,
          "Hiding occluder exposes edge before repaint");
    check(view->pickEdge(view->project({-2.5, -2, 0})).first == 0,
          "Invisible face edges are not pickable");
    view->setBodyOpacity(front, 1);
    view->setClipPlane(std::array<double, 4>{1, 0, 0, -2});
    check(view->pickEdge(view->project({1.5, 3.5, 0})).first == 0,
          "Clipped wire portion cannot be picked");
    check(view->pickEdge(view->project({2.5, 3.5, 0})).first == loose,
          "Unclipped wire portion keeps identity");
    view->setClipPlane(std::nullopt);
    frame(view);
    check(encodeDocument(doc) == original, "Tessellation, bounds and picking never alter topology");
    auto bounds = view->bodyBounds(front);
    check(bounds.valid && bounds.minimum == Vec3{-4, -3, 0} && bounds.maximum == Vec3{4, 3, 0},
          "World bounds derive from authoritative vertices");
    const auto transform = Transform::translation({1, 0, .2}) * Transform::scaling({-1, 1, 1});
    doc.transform(front, transform);
    bounds = view->bodyBounds(front);
    check(bounds.minimum == Vec3{-3, -3, .2} && bounds.maximum == Vec3{5, 3, .2},
          "Bounds update before repaint after mirror/translation");
    check(view->pickEdge(view->project(transform.point({-2.5, -2, 0}))) ==
              std::pair<Id, Id>{front, edge},
          "Mirrored edge picking uses current transform before repaint");
    view->refresh();
    view->fit();
    frame(view);
    check(view->pick(view->project(transform.point({-1.2, -1.3, 0}))) ==
              std::pair<Id, Id>{front, frontFace},
          "Mirrored triangles retain face association");
    // Changing loop orientation retriangulates without changing authoritative IDs.
    const auto before = doc.bodies().at(front);
    auto reversed = std::make_shared<Body>(*before);
    for (auto &loop : reversed->surface.faces.at(frontFace).loops)
        std::reverse(loop.begin(), loop.end());
    doc.apply({"Reverse fixture orientation", {{front, before, reversed}}}, doc.revision());
    view->refresh();
    frame(view);
    check(view->pick(view->project(transform.point({-1.2, -1.3, 0}))) ==
              std::pair<Id, Id>{front, frontFace},
          "Orientation/cache rebuild preserves face ID");
    check(view->pickEdge(view->project(transform.point({-2.5, -2, 0}))) ==
              std::pair<Id, Id>{front, edge},
          "Orientation change preserves edge ID");
    doc.splitEdge(front, edge, .5);
    const auto splitHit = view->pickEdge(view->project(transform.point({-2.75, -2, 0})));
    check(splitHit.first == front && splitHit.second != edge &&
              doc.bodies().at(front)->topology.edges.contains(splitHit.second),
          "Prepaint pick cannot return a retired edge after split");
    view->refresh();
    frame(view);
    check(view->pickEdge(view->project(transform.point({-2.75, -2, 0}))) == splitHit,
          "Cached pick matches current split identity");
    view->setSelection(front, frontFace);
    doc.insertEdges(front, {0, 0, 0}, {0, 0, 1}, {{{{-1, -3, 0}, {-1, 3, 0}}}});
    view->refresh();
    frame(view);
    check(view->selectedBody() == front && view->selectedFace() == 0,
          "Retired face selection clears after subdivision");
    const auto left = view->pick(view->project(transform.point({-3.5, 0, 0}))),
               right = view->pick(view->project(transform.point({-.5, -1, 0})));
    check(left.first == front && right.first == front && left.second != right.second &&
              left.second != frontFace && right.second != frontFace,
          "Subdivided regions pick their distinct persistent face IDs");
    const auto saved = encodeDocument(doc);
    frame(view);
    frame(view);
    check(encodeDocument(doc) == saved && view->renderStats().glError == 0,
          "Repeated new topology frames remain immutable and valid");
    // Replacement can have the same revision; cached identity and bounds must still reset.
    Document replacement;
    replacement.addFace({{{-1, -1, 0}, {1, -1, 0}, {0, 1, 0}}});
    doc = std::move(replacement);
    view->fit();
    frame(view);
    check(view->bodyBounds(1).valid && view->pick(view->project({0, 0, 0})).first == 1,
          "Document replacement resets bounds and picking records");
    check(view->selectedBody() == 0,
          "Document identity change clears selection before IDs can be reused");
    const auto overlay = doc.addFace({{{-1, -1, 0}, {1, -1, 0}, {0, 1, 0}}});
    doc.paint(1, {.9f, .1f, .1f});
    doc.paint(overlay, {.1f, .9f, .1f});
    view->refresh();
    frame(view);
    const auto coplanar = sample(view, {0, 0, 0});
    check(coplanar.green() > coplanar.red() * 3 &&
              view->pick(view->project({0, 0, 0})).first == overlay,
          "Coincident face picks match last visible opaque draw");
    check(view->pickEdge(view->project({0, -1, 0})).first == overlay,
          "Coincident edge picks match last visible line draw");
    std::cout << "Topology viewport: concavity, holes, stable edge/face IDs, clipping, occlusion, "
                 "bounds and mirror checks passed\n";
}
