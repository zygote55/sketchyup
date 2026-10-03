#include "app/viewport.hpp"
#include "io/document_io.hpp"
#include <QApplication>
#include <QLineEdit>
#include <QMouseEvent>
#include <QTest>
#include <QVBoxLayout>
#include <iostream>
using namespace sketchy;
void check(bool ok, const char *message) {
    if (!ok)
        throw std::runtime_error(message);
}
template <class F> void rejects(F fn) {
    try {
        fn();
    } catch (const std::exception &) {
        return;
    }
    throw std::runtime_error("Expected rejection");
}
void move(Viewport &view, QPointF point, Qt::MouseButtons buttons = Qt::NoButton) {
    QMouseEvent event(QEvent::MouseMove, point, view.mapToGlobal(point.toPoint()), Qt::NoButton,
                      buttons, Qt::NoModifier);
    QCoreApplication::sendEvent(&view, &event);
    QCoreApplication::processEvents();
}
int main(int argc, char **argv) {
    QApplication app(argc, argv);
    try {
        Document doc;
        ToolSession transaction(doc);
        QJsonObject command{
            {"command", "geometry.face"},
            {"loops", QJsonArray{QJsonArray{QJsonArray{0, 0, 0}, QJsonArray{2, 0, 0},
                                            QJsonArray{0, 2, 0}}}}};
        const auto initial = encodeDocument(doc);
        rejects([&] { transaction.commit(command); });
        transaction.begin();
        check(transaction.phase() == ToolSession::Phase::Anchored, "Session anchor state");
        transaction.preview(command);
        check(transaction.phase() == ToolSession::Phase::Preview, "Session preview state");
        check(encodeDocument(doc) == initial && !doc.canUndo() && !doc.dirty(),
              "Preview cannot dirty or add history");
        transaction.cancel();
        check(transaction.phase() == ToolSession::Phase::Ready && encodeDocument(doc) == initial,
              "Cancel leaves exact document");
        transaction.begin();
        transaction.commit(command);
        check(transaction.phase() == ToolSession::Phase::Committed && doc.canUndo(),
              "Commit state and one edit");
        doc.undo();
        check(doc.bodies().empty() && !doc.canUndo(), "Commit is one undo step");
        transaction.begin();
        doc.addWire(0, {0, 0, 0}, {1, 0, 0});
        const auto intervened = encodeDocument(doc);
        rejects([&] { transaction.commit(command); });
        check(encodeDocument(doc) == intervened, "Intervening edit invalidates anchored request");
        doc = Document();
        rejects([&] { transaction.commit(command); });
        check(doc.bodies().empty(), "Document replacement invalidates the old session identity");
        QWidget host;
        host.resize(900, 700);
        QVBoxLayout layout(&host);
        auto *view = new Viewport(doc);
        auto *field = new QLineEdit;
        layout.addWidget(view);
        layout.addWidget(field);
        host.show();
        check(QTest::qWaitForWindowExposed(&host), "Lifecycle viewport exposed");
        view->standardView(1);
        view->setTool(Viewport::Tool::Rectangle);
        auto start = view->project({0, 0, 0}).toPoint(), end = view->project({2, 1, 0}).toPoint();
        QTest::mouseClick(view, Qt::LeftButton, {}, start);
        check(view->interactionPhase() == ToolSession::Phase::Anchored, "Click anchors rectangle");
        const auto anchored = view->operationAnchor();
        const auto untouched = encodeDocument(doc);
        QTest::mousePress(view, Qt::MiddleButton, {}, start);
        move(*view, start + QPoint(25, 15), Qt::MiddleButton);
        QTest::mouseRelease(view, Qt::MiddleButton, {}, start + QPoint(25, 15));
        check(view->operationAnchor() == anchored && encodeDocument(doc) == untouched,
              "Temporary camera movement preserves anchor and document");
        field->setFocus();
        QCoreApplication::processEvents();
        check(view->operationAnchor() == anchored, "Measurements focus preserves anchor");
        view->setFocus();
        QTest::keyClick(view, Qt::Key_Escape);
        check(view->interactionPhase() == ToolSession::Phase::Ready &&
                  encodeDocument(doc) == untouched,
              "Escape cancels without dirtying");
        QTest::keyClick(view, Qt::Key_Escape);
        check(view->tool() == Viewport::Tool::Select, "Second Escape leaves tool");
        view->standardView(1);
        view->setTool(Viewport::Tool::Rectangle);
        start = view->project({0, 0, 0}).toPoint();
        end = view->project({2, 1, 0}).toPoint();
        QTest::mouseClick(view, Qt::LeftButton, {}, start);
        move(*view, end);
        check(view->previewValid() && doc.bodies().empty(),
              "Rectangle preview is validated and nonmutating");
        QTest::mouseClick(view, Qt::LeftButton, {}, end);
        check(view->interactionPhase() == ToolSession::Phase::Committed && doc.bodies().size() == 1,
              "Click/move/click commits");
        auto body = doc.bodies().begin()->second;
        check(std::abs(body->surface.area(body->surface.faces.begin()->first) - 2) < 1e-8,
              "Click rectangle dimensions");
        doc = Document();
        view->refresh();
        QTest::mousePress(view, Qt::LeftButton, {}, start);
        move(*view, end, Qt::LeftButton);
        QTest::mouseRelease(view, Qt::LeftButton, {}, end);
        check(doc.bodies().size() == 1, "Press/drag/release commits");
        body = doc.bodies().begin()->second;
        check(std::abs(body->surface.area(body->surface.faces.begin()->first) - 2) < 1e-8,
              "Drag equals click dimensions");
        doc = Document();
        view->refresh();
        view->setTool(Viewport::Tool::Line);
        QTest::mousePress(view, Qt::LeftButton, {}, start);
        move(*view, end, Qt::LeftButton);
        QEvent pointerCancel(QEvent::TouchCancel);
        QCoreApplication::sendEvent(view, &pointerCancel);
        QTest::mouseRelease(view, Qt::LeftButton, {}, end);
        check(doc.bodies().empty() && view->interactionPhase() == ToolSession::Phase::Ready,
              "Pointer cancellation prevents late-release commit");
        QTest::mousePress(view, Qt::LeftButton, {}, start);
        move(*view, end, Qt::LeftButton);
        field->setFocus();
        QCoreApplication::processEvents();
        QTest::mouseRelease(view, Qt::LeftButton, {}, end);
        check(doc.bodies().empty() && view->operationAnchor().has_value(),
              "Focus loss stops drag commit while preserving the editable anchor");
        QEvent deactivated(QEvent::WindowDeactivate);
        QCoreApplication::sendEvent(view, &deactivated);
        check(!view->operationAnchor() && doc.bodies().empty(),
              "Window deactivation cancels uncommitted geometry");
        view->setFocus();
        QTest::mouseClick(view, Qt::LeftButton, {}, start);
        move(*view, end);
        QTest::mouseClick(view, Qt::LeftButton, {}, end);
        check(doc.bodies().size() == 1 && doc.bodies().begin()->second->surface.wires.size() == 1,
              "Line tool uses planar edge command");
        doc = Document();
        view->refresh();
        view->setTool(Viewport::Tool::Rectangle);
        QTest::mouseClick(view, Qt::LeftButton, {}, start);
        move(*view, start);
        QTest::mouseClick(view, Qt::LeftButton, {}, start);
        check(doc.bodies().empty() && view->operationAnchor().has_value(),
              "Rejected rectangle keeps usable anchor");
        move(*view, end);
        QTest::mouseClick(view, Qt::LeftButton, {}, end);
        check(doc.bodies().size() == 1, "Valid endpoint recovers rejected operation");
        view->standardView(0);
        view->fit();
        view->setTool(Viewport::Tool::Extrude);
        auto center = view->project({1, .5, 0}).toPoint();
        QTest::mouseClick(view, Qt::LeftButton, {}, center);
        const auto beforeSweep = encodeDocument(doc);
        const auto destination = view->project({1, .5, 1.5}).toPoint();
        move(*view, destination);
        check(view->previewValid() && encodeDocument(doc) == beforeSweep,
              "Push/pull preview keeps authoritative document");
        QTest::mouseClick(view, Qt::LeftButton, {}, destination);
        check(doc.bodies().begin()->second->surface.faces.size() == 6,
              "Pointer push/pull commits prism");
        doc.undo();
        view->refresh();
        view->setTool(Viewport::Tool::Extrude);
        QTest::mousePress(view, Qt::LeftButton, {}, center);
        move(*view, destination, Qt::LeftButton);
        QTest::mouseRelease(view, Qt::LeftButton, {}, destination);
        check(doc.bodies().begin()->second->surface.faces.size() == 6,
              "Drag push/pull commits prism");
        view->setTool(Viewport::Tool::Rectangle);
        QTest::mouseClick(view, Qt::LeftButton, {}, start);
        const auto beforeExternal = doc.revision();
        doc.paint(doc.bodies().begin()->first, {.2f, .3f, .4f});
        view->refresh();
        check(view->interactionPhase() == ToolSession::Phase::Ready &&
                  doc.revision() == beforeExternal + 1,
              "External edit cancels stale preview without extra edit");
        std::cout << "Tool states, click/drag, camera interleaving, cancellation, rejection and "
                     "pointer push/pull passed\n";
        return 0;
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
