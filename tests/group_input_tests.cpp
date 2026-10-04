#include "app/window.hpp"
#include "core/groups.hpp"
#include "io/document_io.hpp"
#include <QAction>
#include <QApplication>
#include <QLabel>
#include <QSurfaceFormat>
#include <QTest>
#include <iostream>
using namespace sketchy;
void check(bool value, const char *message) {
    if (!value)
        throw std::runtime_error(message);
}
int main(int argc, char **argv) {
    QSurfaceFormat format;
    format.setVersion(3, 3);
    format.setProfile(QSurfaceFormat::CoreProfile);
    format.setDepthBufferSize(24);
    QSurfaceFormat::setDefaultFormat(format);
    QApplication app(argc, argv);
    QString status;
    try {
        Window window;
        window.resize(1200, 800);
        auto &doc = window.document();
        doc.addFace({{{0, 0, 0}, {4, 0, 0}, {4, 4, 0}, {0, 4, 0}}});
        doc.insertEdges(1, {}, {0, 0, 1}, {{Vec3{2, 0, 0}, Vec3{2, 4, 0}}});
        doc.addGuide(1, guidePoint({1, 3, 0}));
        auto *view = window.viewport();
        QObject::connect(view, &Viewport::message, [&](const QString &text) { status = text; });
        QMetaObject::invokeMethod(view, "changed");
        window.show();
        check(QTest::qWaitForWindowExposed(&window), "Group window exposed");
        window.activateWindow();
        check(QTest::qWaitForWindowActive(&window), "Group window active");
        view->setFocus();
        view->standardView(1);
        view->fit();
        view->setTool(Viewport::Tool::Select);
        QTest::qWait(50);
        const auto face = view->selectionAt(view->project({.5, 1, 0}));
        check(face && face->kind == SelectionKind::Face, "Group source is a face");
        check(std::abs(doc.worldArea(face->body, face->entity) - 8) < 1e-9,
              "Group fixture divides the rectangle into equal halves");
        const auto guide = doc.bodies().at(1)->guides.begin()->first;
        view->selectEntities({*face, {1, SelectionKind::Guide, guide}});
        const auto before = *doc.bodies().at(1);
        const auto revision = doc.revision();
        QTest::keyClick(view, Qt::Key_G, Qt::ControlModifier);
        check(doc.revision() == revision + 1, "Ctrl+G creates one atomic group");
        const auto group = view->selectedBody();
        check(group && doc.bodies().at(group)->kind == BodyKind::Group,
              "Created group stays selected");
        Id member = 0;
        for (const auto &[id, body] : doc.bodies())
            if (body->parent == group)
                member = id;
        check(member && doc.bodies().at(1)->surface.faces.size() == 1 &&
                  doc.bodies().at(member)->surface.faces.contains(face->entity) &&
                  doc.bodies().at(member)->guides.contains(guide),
              "Raw face and guide transfer preserves neighboring geometry");
        window.findChild<QAction *>("edit.undo")->trigger();
        check(doc.bodies().size() == 1 && *doc.bodies().at(1) == before,
              "One native undo restores pre-group geometry");
        window.findChild<QAction *>("edit.redo")->trigger();
        const auto groupedHit = view->selectionAt(view->project({.5, 1, 0}));
        check(groupedHit == SelectedEntity{group, SelectionKind::Body, 0},
              "Closed face picks whole group");
        const auto guideHit = view->selectionAt(view->project({1, 3, 0}));
        check(guideHit == SelectedEntity{group, SelectionKind::Body, 0},
              "Closed guide picks whole group");
        const QRectF partial(view->project({.3, .3, 0}), view->project({1, 1, 0}));
        check(view->windowSelection(partial, true).contains({group, SelectionKind::Body, 0}) &&
                  !view->windowSelection(partial, false).contains({group, SelectionKind::Body, 0}),
              "Crossing selects group, enclosing window requires all members");
        const auto rootImage = view->grabFramebuffer();
        QTest::mouseDClick(view, Qt::LeftButton, {}, view->project({.5, 1, 0}).toPoint());
        check(view->selectionState().context() == group, "Double-click opens one group boundary");
        const auto dimmedImage = view->grabFramebuffer();
        const auto outsidePixel = (view->project({3, 1, 0}) * view->devicePixelRatioF()).toPoint();
        check(rootImage.pixelColor(outsidePixel) != dimmedImage.pixelColor(outsidePixel),
              "Inactive surroundings dim while editing the group");
        const auto rawHit = view->selectionAt(view->project({.5, 1, 0}));
        check(rawHit && rawHit->body == member && rawHit->kind == SelectionKind::Face,
              "Open group picks raw face");
        auto *breadcrumb = window.findChild<QLabel *>("contextBreadcrumb");
        check(breadcrumb && breadcrumb->text().contains(QString("href=\"%1\"").arg(group)),
              "Clickable breadcrumb shows group identity");
        const auto neighbor = *doc.bodies().at(1);
        view->setDrawingPlane(DrawingPlane::make({}, {0, 0, 1}, {1, 0, 0}), member);
        view->setTool(Viewport::Tool::Line);
        check(view->measurements("[0,2,0]") && view->measurements("[2,2,0]"),
              "Exact line draws in active raw context");
        check(doc.bodies().at(member)->surface.faces.size() == 2 && *doc.bodies().at(1) == neighbor,
              "Line splits active group geometry without merging into inactive neighbor");
        view->setTool(Viewport::Tool::Select);
        view->setDrawingPlane(std::nullopt);
        const auto inside = view->selectionAt(view->project({.5, 1, 0}));
        check(inside && inside->body == member, "Edited group face remains pickable");
        view->selectEntities({*inside});
        view->deleteSelection();
        check(doc.bodies().at(member)->surface.faces.size() == 1 && *doc.bodies().at(1) == neighbor,
              "Delete command retains the current group editing scope");
        window.findChild<QAction *>("edit.undo")->trigger();
        const auto toNest = view->selectionAt(view->project({.5, 1, 0}));
        check(toNest.has_value(), "Nested source is pickable after undo");
        view->selectEntities({*toNest});
        view->makeGroup();
        const auto nested = view->selectedBody();
        check(doc.bodies().at(nested)->parent == group,
              "Make Group inside a group creates nested boundary");
        QTest::keyClick(view, Qt::Key_Return);
        check(view->selectionState().context() == nested, "Enter opens nested group");
        const auto nestedHit = view->selectionAt(view->project({.5, 1, 0}));
        check(nestedHit.has_value(), "Nested raw face pick");
        view->selectEntities({*nestedHit});
        QTest::keyClick(view, Qt::Key_Escape);
        check(view->selectionState().context() == group,
              "Esc closes one level with geometry selected");
        QTest::keyClick(view, Qt::Key_Escape);
        check(view->selectionState().context() == 0, "Second Esc closes to model");
        view->enterContext(nested);
        QTest::mouseClick(view, Qt::LeftButton, {}, QPoint(view->width() - 20, view->height() / 2));
        check(view->selectionState().context() == group, "Clicking outside closes only one level");
        view->enterContext(nested);
        QTest::mouseClick(breadcrumb, Qt::LeftButton, {}, QPoint(10, 8));
        check(view->selectionState().context() == 0,
              "Clicking Model breadcrumb closes nested contexts");
        const auto closedMember = doc.bodies().at(member);
        const auto closedCount = doc.bodies().size();
        view->setTool(Viewport::Tool::Rectangle);
        view->setDrawingPlane(DrawingPlane::make({}, {0, 0, 1}, {1, 0, 0}));
        check(view->measurements("[0,0,0]") && view->measurements("1m,1m"),
              "Drawing over a closed group creates geometry in the model context");
        check(doc.bodies().size() == closedCount + 1 && doc.bodies().at(member) == closedMember,
              "New coincident drawing cannot merge into a closed group");
        view->setTool(Viewport::Tool::Select);
        view->setDrawingPlane(std::nullopt);
        window.findChild<QAction *>("edit.undo")->trigger();
        view->setSelection(group);
        window.findChild<QAction *>("group.hide")->trigger();
        check(doc.bodies().at(group)->hidden && !view->selectionAt(view->project({.5, 1, 0})),
              "Persistent hide removes whole group from picking");
        window.findChild<QAction *>("group.reveal")->trigger();
        view->setSelection(group);
        window.findChild<QAction *>("group.lock")->trigger();
        check(doc.bodies().at(group)->locked && !view->selectionAt(view->project({.5, 1, 0})),
              "Persistent lock prevents grouped picking");
        const auto unrelated = view->selectionAt(view->project({3, 1, 0}));
        check(unrelated && unrelated->body == 1, "Locked group leaves unrelated geometry editable");
        window.findChild<QAction *>("group.unlock")->trigger();
        const auto saved = encodeContainer(doc);
        const auto reopened = decodeContainer(saved);
        check(encodeContainer(reopened) == saved,
              "Native grouped model reopens without losing nested state");
        view->setSelection(group);
        const auto capture = app.arguments().indexOf("--capture");
        if (capture >= 0) {
            view->enterContext(group);
            view->setSelection(nested);
            QCoreApplication::processEvents();
            check(window.grab().save(app.arguments().value(capture + 1)), "Group capture saved");
            view->leaveContext();
            view->setSelection(group);
        }
        QTest::keyClick(view, Qt::Key_G, Qt::ControlModifier | Qt::ShiftModifier);
        check(!doc.bodies().contains(group) && doc.bodies().at(nested)->parent == 0,
              "Explode promotes nested groups one level");
        window.findChild<QAction *>("edit.undo")->trigger();
        check(doc.bodies().contains(group) && doc.bodies().at(nested)->parent == group,
              "One undo restores exploded hierarchy");
        const auto guideContext = doc.nextId();
        doc.addGuide(0, guidePoint({6, 1, 0}));
        QMetaObject::invokeMethod(view, "changed");
        view->selectEntities({{guideContext, SelectionKind::Guide,
                               doc.bodies().at(guideContext)->guides.begin()->first}});
        view->makeGroup();
        const auto guideGroup = view->selectedBody();
        view->fit();
        QCoreApplication::processEvents();
        check(view->selectionAt(view->project({6, 1, 0})) ==
                  SelectedEntity{guideGroup, SelectionKind::Body, 0},
              "A guide-only group is pickable as a whole");
        const auto highlighted = view->grabFramebuffer();
        const auto guidePixel = (view->project({6, 1, 0}) * view->devicePixelRatioF()).toPoint();
        bool blue = false;
        for (int y = guidePixel.y() - 6; y <= guidePixel.y() + 6; ++y)
            for (int x = guidePixel.x() - 6; x <= guidePixel.x() + 6; ++x)
                if (highlighted.rect().contains(x, y)) {
                    const auto color = highlighted.pixelColor(x, y);
                    blue |= color.blue() > 150 && color.blue() > color.red() + 60 &&
                            color.blue() > color.green() + 60;
                }
        check(blue, "Guide-only group selection has visible feedback");
        view->cancel();
        doc = Document{};
        for (double x : {0., 1., 3., 5., 7.})
            doc.addFace({{{x, 0, 0}, {x + 1, 0, 0}, {x + 1, 1, 0}, {x, 1, 0}}});
        doc.paint(1, {.9f, .1f, .1f});
        doc.paint(2, {.1f, .1f, .9f});
        const auto mergingGroup = createGroup(doc, {2});
        const auto protectedGroup = createGroup(doc, {5});
        const auto lockedRecord = doc.bodies().at(3), hiddenRecord = doc.bodies().at(4);
        QMetaObject::invokeMethod(view, "changed");
        view->setSelection(3);
        view->lockSelection();
        view->selectEntities({{4, SelectionKind::Face, 5}});
        view->hideSelection();
        view->setSelection(mergingGroup);
        const auto mergeRevision = doc.revision();
        view->explodeGroups();
        check(doc.revision() == mergeRevision + 1 && !doc.bodies().contains(mergingGroup) &&
                  !doc.bodies().contains(2) && doc.bodies().at(1)->surface.vertices.size() == 6 &&
                  doc.bodies().at(1)->surface.faces.size() == 2,
              "Native explode consolidates adjacent records in one edit");
        check(doc.bodies().at(3) == lockedRecord && doc.bodies().at(4) == hiddenRecord &&
                  doc.bodies().at(5)->parent == protectedGroup,
              "Temporary locks, partially hidden records and closed groups remain isolated");
        check(view->selectedBody() == 1, "Exploded selection follows transferred geometry");
        view->selectEntities({});
        view->standardView(1);
        view->fit();
        QCoreApplication::processEvents();
        const auto colors = view->grabFramebuffer();
        auto pixel = [&](const QImage &image, Vec3 point) {
            return image.pixelColor((view->project(point) * view->devicePixelRatioF()).toPoint());
        };
        const auto redPixel = pixel(colors, {.5, .5, 0});
        const auto bluePixel = pixel(colors, {1.5, .5, 0});
        check(redPixel.red() > redPixel.blue() + 70 && bluePixel.blue() > bluePixel.red() + 70,
              "Merged faces retain distinct visible colors");
        const auto mergeCapture = app.arguments().indexOf("--capture-consolidation");
        if (mergeCapture >= 0)
            check(window.grab().save(app.arguments().value(mergeCapture + 1)),
                  "Consolidated color capture saved");
        const auto builds = view->renderStats().bodyMeshBuilds;
        const auto beforeTint = doc.bodies().at(1);
        auto tinted = std::make_shared<Body>(*beforeTint);
        tinted->faceColors.begin()->second = {.1f, .9f, .1f};
        doc.apply({"Tint merged face", {{1, beforeTint, tinted}}}, doc.revision());
        QMetaObject::invokeMethod(view, "changed");
        const auto greenPixel = pixel(view->grabFramebuffer(), {1.5, .5, 0});
        check(greenPixel.green() > greenPixel.blue() + 70 &&
                  view->renderStats().bodyMeshBuilds == builds,
              "Per-face color upload changes pixels without rebuilding meshes");
        window.findChild<QAction *>("edit.undo")->trigger();
        window.findChild<QAction *>("edit.undo")->trigger();
        check(doc.bodies().contains(mergingGroup) && doc.bodies().at(2)->parent == mergingGroup &&
                  doc.bodies().at(1)->surface.faces.size() == 1,
              "One undo restores both the exploded boundary and separate colored records");
        view->unlockContexts();
        view->revealHiddenGeometry();
        window.findChild<QAction *>("geometry.merge_context")->trigger();
        check(!doc.bodies().contains(3) && !doc.bodies().contains(4) &&
                  doc.bodies().at(2)->parent == mergingGroup &&
                  doc.bodies().at(5)->parent == protectedGroup,
              "Explicit native merge combines eligible raw records and preserves groups");
        const auto outer = createGroup(doc, {mergingGroup});
        QMetaObject::invokeMethod(view, "changed");
        view->enterContext(outer);
        view->setSelection(mergingGroup);
        view->explodeGroups();
        check(!doc.bodies().contains(mergingGroup) && !doc.bodies().contains(2) &&
                  doc.bodies().at(outer)->surface.faces.size() == 1 &&
                  view->selectionState().entities().contains({outer, SelectionKind::Face, 5}),
              "Nested explode retains raw selection in the active destination group");
        check(!view->renderStats().glError, "Grouped picking and rendering leave no GL errors");
        std::cout << "Native raw grouping, protected picking, nested editing, drawing/deletion "
                     "isolation, breadcrumbs, locks, persistence and undo passed; DPR="
                  << view->devicePixelRatioF() << '\n';
    } catch (const std::exception &e) {
        std::cerr << e.what() << " · " << status.toStdString() << '\n';
        return 1;
    }
}
