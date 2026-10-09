#include "app/surface_format.hpp"
#include "app/window.hpp"
#include "core/groups.hpp"
#include "io/document_io.hpp"
#include <QAction>
#include <QApplication>
#include <QTest>
#include <iostream>
using namespace sketchy;
void check(bool ok, const char *message) {
    if (!ok)
        throw std::runtime_error(message);
}
void move(Viewport &view, QPointF point, Qt::MouseButtons buttons = Qt::NoButton) {
    QMouseEvent event(QEvent::MouseMove, point, view.mapToGlobal(point.toPoint()), Qt::NoButton,
                      buttons, Qt::NoModifier);
    QCoreApplication::sendEvent(&view, &event);
    QCoreApplication::processEvents();
}
void click(Viewport &view, Vec3 point, Qt::KeyboardModifiers modifiers = Qt::NoModifier) {
    QTest::mouseClick(&view, Qt::LeftButton, modifiers, view.project(point).toPoint());
    QCoreApplication::processEvents();
}
void drag(Viewport &view, QRectF rect, bool crossing) {
    const auto first = crossing ? rect.topRight() : rect.topLeft();
    const auto last = crossing ? rect.bottomLeft() : rect.bottomRight();
    QTest::mousePress(&view, Qt::LeftButton, {}, first.toPoint());
    move(view, last, Qt::LeftButton);
    QTest::mouseRelease(&view, Qt::LeftButton, {}, last.toPoint());
    QCoreApplication::processEvents();
}
bool has(const SelectionSet &entities, Id body, SelectionKind kind) {
    return std::any_of(entities.begin(), entities.end(),
                       [&](auto e) { return e.body == body && e.kind == kind; });
}
int main(int argc, char **argv) {
    QSurfaceFormat format;
    format.setVersion(3, 3);
    format.setProfile(QSurfaceFormat::CoreProfile);
    format.setDepthBufferSize(24);
    sketchy::setDefaultViewportFormat(format);
    QApplication app(argc, argv);
    QString lastMessage;
    try {
        Window window;
        window.resize(1200, 800);
        auto &doc = window.document();
        doc.addFace({{{0, 0, 0}, {4, 0, 0}, {4, 4, 0}, {0, 4, 0}}});
        doc.insertEdges(1, {}, {0, 0, 1}, {{Vec3{.8, 0, 0}, Vec3{.8, 4, 0}}});
        doc.addGuide(1, guidePoint({-.5, 0, 0}));
        doc.addFace({{{1, 1, 1}, {3, 1, 1}, {3, 3, 1}, {1, 3, 1}},
                     {{1.5, 1.5, 1}, {1.5, 2.5, 1}, {2.5, 2.5, 1}, {2.5, 1.5, 1}}});
        doc.addWire(0, {-2, 0, 0}, {-2, 2, 0});
        auto *view = window.viewport();
        QObject::connect(view, &Viewport::message,
                         [&](const QString &text) { lastMessage = text; });
        QMetaObject::invokeMethod(view, "changed");
        window.show();
        check(QTest::qWaitForWindowExposed(&window), "Selection window exposed");
        window.activateWindow();
        check(QTest::qWaitForWindowActive(&window), "Selection window active");
        view->setFocus();
        view->fit();
        view->standardView(1);
        view->setTool(Viewport::Tool::Select);
        QTest::qWait(50);
        check(view->rendererReady(), "Selection renderer ready");
        const auto front = view->selectionAt(view->project({2, 1.25, 1}));
        check(front && front->body == 2 && front->kind == SelectionKind::Face,
              "Depth ID picking selects the visible front face");
        const auto throughHole = view->selectionAt(view->project({2, 2, 1}));
        check(throughHole && throughHole->body == 1 && throughHole->kind == SelectionKind::Face,
              "A face hole exposes the actual rear entity");
        const auto face = view->selectionAt(view->project({.4, .4, 0}));
        check(face && face->body == 1 && face->kind == SelectionKind::Face,
              "Back face is selectable in its visible region");
        check(std::abs(doc.worldArea(face->body, face->entity) - 3.2) < 1e-9,
              "Selection fixture has its intended vertical divider");
        const auto edge = view->selectionAt(view->project({-2, 1, 0}));
        check(edge && edge->body == 3 && edge->kind == SelectionKind::Edge,
              "Loose edge has a typed selection ID");
        auto original = encodeDocument(doc);
        move(*view, view->project({.4, .4, 0}));
        check(view->hoveredEntity() == face, "Hover resolves exactly the click target");
        click(*view, {.4, .4, 0});
        check(view->selectionState().entities() == SelectionSet{*face}, "Click selects one face");
        click(*view, {-2, 1, 0}, Qt::ControlModifier);
        check(view->selectionState().entities().size() == 2 &&
                  view->selectionSummary() == "1 face, 1 edge",
              "Ctrl adds across editing contexts and reports typed counts");
        click(*view, {.4, .4, 0}, Qt::ShiftModifier);
        check(view->selectionState().entities() == SelectionSet{*edge},
              "Shift toggles a single entity");
        auto *outliner = window.findChild<QTreeWidget *>("outlinerTree");
        check(outliner && outliner->selectedItems().size() == 1 &&
                  outliner->selectedItems()[0]->data(0, Qt::UserRole).toULongLong() == 3,
              "Outliner mirrors viewport entity selection");
        QTest::mouseDClick(view, Qt::LeftButton, {}, view->project({.4, .4, 0}).toPoint());
        check(view->selectionState().entities() == view->selectionState().boundary(doc, *face) &&
                  view->selectionState().context() == 0,
              "Double-click a face adds its boundary without entering the context");
        click(*view, {.4, .4, 0});
        check(view->selectionState().entities().size() ==
                      doc.bodies().at(1)->surface.faces.size() +
                          doc.bodies().at(1)->topology.edges.size() &&
                  !has(view->selectionState().entities(), 1, SelectionKind::Guide),
              "Triple-click selects connected model geometry but excludes guides");
        const auto wireBounds = QRectF(view->project({-2, 0, 0}), view->project({-2, 2, 0}))
                                    .normalized()
                                    .adjusted(-3, -3, 3, 3);
        drag(*view, wireBounds, false);
        check(view->selectionState().entities() == SelectionSet{*edge},
              "Left-to-right window selects a fully enclosed edge");
        const auto middle = view->project({-2, 1, 0});
        const QRectF partial(middle - QPointF(8, 8), QSizeF(16, 16));
        drag(*view, partial, false);
        check(view->selectionState().entities().empty(),
              "Window excludes a partially enclosed edge");
        drag(*view, partial, true);
        check(view->selectionState().entities() == SelectionSet{*edge},
              "Right-to-left crossing selects a touched edge");
        const auto hole = view->project({2, 2, 1});
        const auto holeSelection =
            view->windowSelection(QRectF(hole - QPointF(4, 4), QSizeF(8, 8)), true);
        check(has(holeSelection, 1, SelectionKind::Face) &&
                  !has(holeSelection, 2, SelectionKind::Face),
              "Crossing within a hole does not select the face surrounding it");
        const auto solid = view->project({2, 1.25, 1});
        const auto solidSelection =
            view->windowSelection(QRectF(solid - QPointF(3, 3), QSizeF(6, 6)), true);
        check(has(solidSelection, 2, SelectionKind::Face) &&
                  !has(solidSelection, 1, SelectionKind::Face),
              "Crossing selection respects occlusion instead of projected bounds alone");
        view->selectEntities({*edge});
        view->hideSelection();
        check(!view->selectionAt(middle), "Hidden edge is absent from picking");
        view->setTool(Viewport::Tool::Line);
        QElapsedTimer waiting;
        waiting.start();
        while (!view->inferenceReady() && waiting.elapsed() < 10000)
            QTest::qWait(10);
        check(view->inferenceReady(), "Selection policy inference index ready");
        move(*view, middle);
        check(!view->acquiredInference(), "Hidden edge cannot be acquired by drawing tools");
        view->setTool(Viewport::Tool::Select);
        view->showHiddenGeometry(true);
        check(view->selectionAt(middle) == edge,
              "Hidden geometry mode exposes the same stable edge");
        view->selectEntities({*edge});
        view->lockSelection();
        check(!view->selectionAt(middle) && view->windowSelection(partial, true).empty(),
              "Hidden mode and crossing selection cannot bypass a context lock");
        view->setTool(Viewport::Tool::Line);
        move(*view, middle);
        check(!view->acquiredInference(),
              "Visible locked edge cannot be acquired by drawing tools");
        view->setTool(Viewport::Tool::Select);
        view->selectAll();
        check(std::none_of(view->selectionState().entities().begin(),
                           view->selectionState().entities().end(),
                           [](auto e) { return e.body == 3; }),
              "Select all excludes locked contexts");
        view->unlockContexts();
        view->revealHiddenGeometry();
        view->showHiddenGeometry(false);
        view->enterContext(1);
        check(!view->selectionAt(solid) && view->selectionAt(view->project({.4, .4, 0})) == face,
              "Inactive foreground occludes picking and active context remains selectable");
        view->setTool(Viewport::Tool::Line);
        move(*view, solid);
        check(!view->acquiredInference(),
              "Inactive foreground cannot be acquired through inference");
        bool inactivePlaneRejected = false;
        try {
            view->setDrawingPlane(DrawingPlane{}, 2);
        } catch (const std::exception &) {
            inactivePlaneRejected = true;
        }
        check(inactivePlaneRejected, "Explicit plane cannot bypass active editing context");
        view->setTool(Viewport::Tool::Select);
        const auto active = view->windowSelection(QRectF(view->rect()), true);
        check(!active.empty() &&
                  std::all_of(active.begin(), active.end(), [](auto e) { return e.body == 1; }),
              "Window selection stays in the active editing context");
        check(encodeDocument(doc) == original, "Selection policies do not edit saved state");
        view->setTool(Viewport::Tool::Rectangle);
        const auto contextCount = doc.bodies().size();
        check(view->measurements("[5,0,0]") && view->measurements("1,1"),
              "Typed-only drawing completes inside the active context");
        check(doc.bodies().size() == contextCount && doc.bodies().at(1)->surface.faces.size() == 3,
              "Typed coordinate entry adopts active editing context");
        doc.undo();
        original = encodeDocument(doc);
        view->refresh();
        view->setTool(Viewport::Tool::Select);
        view->leaveContext();
        view->setSelection(1);
        QTest::keyClick(view, Qt::Key_Return);
        check(view->selectionState().context() == 1, "Enter on a whole context opens it");
        QTest::keyClick(view, Qt::Key_Escape);
        check(view->selectionState().context() == 0, "Escape closes an empty context selection");
        view->selectEntities({});
        QTest::keyClick(view, Qt::Key_Tab);
        check(view->hasFocus() && view->selectionState().entities().size() == 1,
              "Tab traverses visible entities without escaping focus");
        const auto firstKeyboard = view->selectionState().entities();
        QTest::keyClick(view, Qt::Key_Tab);
        check(view->selectionState().entities() != firstKeyboard, "Tab advances through typed IDs");
        QTest::keyClick(view, Qt::Key_Backtab, Qt::ShiftModifier);
        check(view->selectionState().entities() == firstKeyboard, "Shift-Tab reverses traversal");
        check(view->accessibleDescription() == view->selectionSummary(),
              "Viewport exposes an accessible selection summary");
        check(encodeDocument(doc) == original,
              "Selection, hiding, locking and context navigation do not edit saved state");
        view->selectEntities({*face, *edge});
        move(*view, view->project({2, 2, 1}));
        QTest::qWait(30);
        const auto image = view->grabFramebuffer();
        const auto at = view->project({-2, 1, 0}) * view->devicePixelRatioF();
        bool blue = false;
        for (int y = int(at.y()) - 6; y <= int(at.y()) + 6; ++y)
            for (int x = int(at.x()) - 6; x <= int(at.x()) + 6; ++x)
                if (image.rect().contains(x, y)) {
                    const auto c = image.pixelColor(x, y);
                    blue |= c.blue() > 150 && c.blue() > c.red() + 60 && c.blue() > c.green() + 60;
                }
        check(blue, "Selected edge is visibly highlighted in the framebuffer");
        const auto capture = app.arguments().indexOf("--capture");
        if (capture >= 0)
            check(image.save(app.arguments().value(capture + 1)), "Selection capture saved");
        const auto revision = doc.revision();
        const auto oldFaces = doc.bodies().at(1)->surface.faces.size();
        window.findChild<QAction *>("edit.delete")->trigger();
        check(doc.revision() == revision + 1 &&
                  doc.bodies().at(1)->surface.faces.size() == oldFaces - 1 &&
                  doc.bodies().at(3)->topology.edges.empty() &&
                  doc.bodies().at(1)->guides.size() == 1,
              "Delete uses typed multi-selection as one edit without deleting unrelated context "
              "geometry");
        window.findChild<QAction *>("edit.undo")->trigger();
        check(doc.bodies().at(1)->surface.faces.size() == oldFaces &&
                  doc.bodies().at(3)->topology.edges.size() == 1,
              "One native undo restores the mixed selection deletion");
        view->setSelection(0);
        QTreeWidgetItem *first = nullptr, *second = nullptr;
        for (int i = 0; i < outliner->topLevelItemCount(); ++i) {
            if (outliner->topLevelItem(i)->data(0, Qt::UserRole).toULongLong() == 1)
                first = outliner->topLevelItem(i);
            if (outliner->topLevelItem(i)->data(0, Qt::UserRole).toULongLong() == 2)
                second = outliner->topLevelItem(i);
        }
        check(first && second, "Outliner contexts present");
        const auto firstRow = outliner->visualItemRect(first).center();
        QTest::mouseClick(outliner->viewport(), Qt::LeftButton, {}, firstRow);
        // Synchronization rebuilds the list, so resolve the second row afresh.
        for (int i = 0; i < outliner->topLevelItemCount(); ++i)
            if (outliner->topLevelItem(i)->data(0, Qt::UserRole).toULongLong() == 2)
                second = outliner->topLevelItem(i);
        QTest::mouseClick(outliner->viewport(), Qt::LeftButton, Qt::ControlModifier,
                          outliner->visualItemRect(second).center());
        check(view->selectionState().entities().size() == 2 &&
                  view->selectionSummary() == "2 contexts",
              "Outliner additive selection updates the typed viewport model");
        view->setSelection(0);
        const auto visibleImage = view->grabFramebuffer();
        setEntityState(doc, 2, true, {});
        QMetaObject::invokeMethod(view, "changed");
        const auto hiddenImage = view->grabFramebuffer();
        const auto afterHide = view->selectionAt(view->project({2, 1.25, 1}));
        check(afterHide && afterHide->body == 1 && hiddenImage != visibleImage,
              "Persistent visibility updates appearance and picking without a camera change");
        setEntityState(doc, 1, {}, true);
        QMetaObject::invokeMethod(view, "changed");
        view->selectEntities({*face});
        check(view->selectionState().entities().empty() &&
                  !view->selectionAt(view->project({.4, .4, 0})),
              "Persistent lock updates native selection eligibility");
        setEntityState(doc, 1, {}, false);
        setEntityState(doc, 2, false, {});
        const auto group = createGroup(doc, {1, 2, 3}, "Assembly");
        QMetaObject::invokeMethod(view, "changed");
        view->selectEntities({*face});
        check(view->selectionState().entities().empty(),
              "Closed group protects native raw selection");
        view->enterContext(group);
        view->selectEntities({*face});
        check(view->selectionState().entities() == SelectionSet{*face},
              "Open group exposes native raw selection");
        view->leaveContext();
        check(view->selectionState().context() == 0, "Group closes to the model");
        check(!view->renderStats().glError, "Selection rendering and ID passes leave no GL errors");
        std::cout << "Native hover, typed/modifier/window/crossing selection, holes, occlusion, "
                     "click expansion, view guards, keyboard/Outliner, framebuffer and atomic "
                     "delete passed; DPR="
                  << view->devicePixelRatioF() << '\n';
    } catch (const std::exception &error) {
        std::cerr << error.what() << " · last status: " << lastMessage.toStdString() << '\n';
        return 1;
    }
}
