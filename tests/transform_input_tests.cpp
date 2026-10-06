#include "app/window.hpp"
#include "io/document_io.hpp"
#include <QAction>
#include <QApplication>
#include <QSurfaceFormat>
#include <QTemporaryDir>
#include <QTest>
#include <iomanip>
#include <iostream>
#include <numbers>
using namespace sketchy;
void check(bool value, const char *message) {
    if (!value)
        throw std::runtime_error(message);
}
bool sameContent(const Body &a, const Body &b) {
    auto expected = b;
    expected.surface.nextId = a.surface.nextId;
    expected.topology.nextId = a.topology.nextId;
    return a == expected;
}
void click(Viewport &view, Vec3 point) {
    // Keep fractional logical pixels: rounding the baseline changes the scale ratio,
    // especially at DPR 2. Deliver the same widget events as pointer motion below.
    const auto at = view.project(point);
    const auto global = view.mapToGlobal(at);
    QMouseEvent press(QEvent::MouseButtonPress, at, global, Qt::LeftButton, Qt::LeftButton, {});
    QCoreApplication::sendEvent(&view, &press);
    QMouseEvent release(QEvent::MouseButtonRelease, at, global, Qt::LeftButton, Qt::NoButton, {});
    QCoreApplication::sendEvent(&view, &release);
    QCoreApplication::processEvents();
}
void movePointer(Viewport &view, Vec3 point) {
    const auto at = view.project(point);
    QMouseEvent event(QEvent::MouseMove, at, view.mapToGlobal(at.toPoint()), Qt::NoButton,
                      Qt::NoButton, {});
    QCoreApplication::sendEvent(&view, &event);
    QCoreApplication::processEvents();
}
int main(int argc, char **argv) {
    QSurfaceFormat format;
    format.setVersion(3, 3);
    format.setProfile(QSurfaceFormat::CoreProfile);
    format.setDepthBufferSize(24);
    QSurfaceFormat::setDefaultFormat(format);
    QTemporaryDir preferences;
    qputenv("XDG_CONFIG_HOME", preferences.path().toUtf8());
    QApplication app(argc, argv);
    QString status;
    try {
        Window window;
        window.resize(1200, 800);
        auto &doc = window.document();
        auto *view = window.viewport();
        QObject::connect(view, &Viewport::message, [&](const QString &value) { status = value; });
        window.show();
        check(QTest::qWaitForWindowExposed(&window), "Transform window exposed");
        window.activateWindow();
        check(QTest::qWaitForWindowActive(&window), "Transform window active");
        doc.addFace({{{0, 0, 0}, {4, 0, 0}, {4, 3, 0}, {0, 3, 0}}});
        view->refresh();
        view->setDrawingPlane(DrawingPlane{});
        view->standardView(1);
        view->fit();
        view->setSelection(1, 5);
        view->setFocus();
        QTest::keyClick(view, Qt::Key_M);
        check(view->tool() == Viewport::Tool::Move, "M activates native Move");
        const auto original = doc.bodies().at(1);
        check(view->measurements("[0,0,0]") && view->measurements("2m,0,0") &&
                  view->measurements("250cm,0,0"),
              "Move and amend displacement");
        check(doc.bodies().at(1)->surface.vertices.at(1) == Vec3{2.5, 0, 0},
              "Numeric move uses exact units");
        doc.undo();
        view->refresh();
        check(sameContent(*doc.bodies().at(1), *original), "Move amendment is one undo step");
        check(!view->measurements("3m,0,0"), "Undo invalidates transform amendment");
        view->setTool(Viewport::Tool::Move);
        view->setSelection(1, 5);
        check(view->measurements("[0,0,0]"), "Copy pivot");
        QTest::keyClick(view, Qt::Key_Control);
        check(view->transformCopy(), "Standalone Ctrl enables copy");
        check(view->measurements("5m,0,0"), "Copy face inside context");
        const auto firstCopy = view->selectedFace();
        check(doc.bodies().size() == 1 && doc.bodies().at(1)->surface.faces.size() == 2 &&
                  firstCopy != 5,
              "Copied face selected with fresh identity");
        check(view->measurements("6m,0,0") && view->selectedFace() > firstCopy &&
                  doc.bodies().at(1)->surface.faces.size() == 2,
              "Copy amendment replaces prior copy and updates selection");
        QTest::keyClick(view, Qt::Key_Z, Qt::ControlModifier);
        check(view->transformCopy() && doc.bodies().at(1)->surface.faces.size() == 1,
              "Ctrl+Z does not toggle copy and removes one copied result");
        view->setTransformCopy(false);
        view->setSelection(1, 5);
        QTest::keyClick(view, Qt::Key_Q);
        check(view->tool() == Viewport::Tool::Rotate && view->measurements("[2,1.5,0]") &&
                  view->measurements("90deg"),
              "Rotate shortcut, pivot and angle");
        check(length(doc.bodies().at(1)->surface.vertices.at(1) - Vec3{3.5, -.5, 0}) < tolerance,
              "Rotation around explicit center");
        check(view->measurements("180deg"), "Angle amendment");
        doc.undo();
        view->refresh();
        check(sameContent(*doc.bodies().at(1), *original), "Rotation amendment undoes once");
        view->setSelection(1, 5);
        QTest::keyClick(view, Qt::Key_S);
        check(view->tool() == Viewport::Tool::Scale && view->measurements("[0,0,0]") &&
                  view->measurements("-2,3,1"),
              "Nonuniform reflected scale");
        check(std::abs(doc.bodies().at(1)->surface.area(5) - 72) < tolerance &&
                  doc.bodies().at(1)->surface.normal(5).z > .99,
              "Scale preserves face orientation under reflection");
        const auto scaled = encodeDocument(doc);
        check(!view->measurements("0,1,1") && encodeDocument(doc) == scaled,
              "Singular scale rejection preserves committed result");
        doc.undo();
        view->refresh();
        view->setSelection(1, 5);
        window.findChild<QAction *>("transform.flip.0")->trigger();
        check(doc.bodies().at(1)->surface.vertices.at(1) == Vec3{4, 0, 0},
              "Flip uses selection center");
        doc.undo();
        view->refresh();
        view->setSelection(1, 5);
        view->setTool(Viewport::Tool::Move);
        check(view->measurements("[0,0,0]") && !view->measurements("0,0,0"),
              "Identity move reports no committed change");
        check(doc.bodies().at(1)->surface.vertices.at(1) == Vec3{} && view->measurements("1,0,0"),
              "A rejected identity can be corrected without amending older history");
        doc.undo();
        view->refresh();
        check(sameContent(*doc.bodies().at(1), *original), "Correction is one new undo item");
        // Pointer and numeric paths share the same command, including drag and cancel.
        view->setTool(Viewport::Tool::Move);
        view->setSelection(1, 5);
        click(*view, {0, 0, 0});
        movePointer(*view, {2, 0, 0});
        QTest::qWait(30);
        check(view->previewValid() && sameContent(*doc.bodies().at(1), *original),
              "Move preview is private");
        click(*view, {2, 0, 0});
        check(doc.bodies().at(1)->surface.vertices.at(1) == Vec3{2, 0, 0},
              "Pointer move equals typed displacement");
        doc.undo();
        view->refresh();
        view->setTool(Viewport::Tool::Rotate);
        view->setSelection(1, 5);
        click(*view, {0, 0, 0});
        click(*view, {4, 0, 0});
        click(*view, {0, 3, 0});
        check(length(doc.bodies().at(1)->surface.vertices.at(2) - Vec3{0, 4, 0}) < tolerance,
              "Pointer rotation uses pivot, baseline and destination");
        doc.undo();
        view->refresh();
        view->setTool(Viewport::Tool::Scale);
        view->setSelection(1, 5);
        click(*view, {0, 0, 0});
        click(*view, {2, 0, 0});
        click(*view, {4, 0, 0});
        const auto pointerScaled = doc.bodies().at(1)->surface.vertices.at(2);
        if (length(pointerScaled - Vec3{8, 0, 0}) >= tolerance)
            std::cerr << std::setprecision(17) << "Pointer scale result: " << pointerScaled.x
                      << ", " << pointerScaled.y << ", " << pointerScaled.z << '\n';
        check(length(pointerScaled - Vec3{8, 0, 0}) < tolerance,
              "Pointer reference sets uniform scale");
        doc.undo();
        view->refresh();
        view->setTool(Viewport::Tool::Move);
        view->setSelection(1, 5);
        click(*view, {0, 0, 0});
        QTest::keyClick(view, Qt::Key_Escape);
        check(!view->operationAnchor() && sameContent(*doc.bodies().at(1), *original),
              "Escape cancels transform without edit");
        // Local axes follow a rotated and scaled context; the matrix is captured before amendment.
        doc.transform(1, Transform::rotation({0, 0, 1}, std::numbers::pi / 2) *
                             Transform::scaling({2, 1, 1}));
        view->refresh();
        view->setTransformLocal(true);
        view->setSelection(1, 5);
        view->setTool(Viewport::Tool::Move);
        check(view->measurements("[0,0,0]") && view->measurements("1,0,0") &&
                  view->measurements("2,0,0"),
              "Local-axis move and amendment");
        check(length(doc.worldTransform(1).point(doc.bodies().at(1)->surface.vertices.at(1)) -
                     Vec3{0, 4, 0}) < tolerance,
              "Local displacement follows scaled rotated axes");
        doc.undo();
        view->refresh();
        view->setTransformLocal(false);
        view->setSelection(1, 5);
        view->lockSelection();
        view->setTool(Viewport::Tool::Move);
        const auto locked = encodeDocument(doc);
        check(!view->measurements("[0,0,0]") && encodeDocument(doc) == locked,
              "Locked geometry rejects manipulation");
        // A selected cap carries attached walls through the same native command path.
        doc = Document();
        doc.addFace({{{0, 0, 0}, {4, 0, 0}, {4, 3, 0}, {0, 3, 0}}});
        doc.extrude(1, 5, 2);
        Id cap = 0;
        for (const auto &[id, face] : doc.bodies().at(1)->surface.faces)
            if (doc.bodies().at(1)->surface.normal(id).z > .99 &&
                doc.bodies().at(1)->surface.vertices.at(face.loops[0][0]).z == 2)
                cap = id;
        check(cap, "Connected cap fixture");
        view->refresh();
        view->setSelection(1, cap);
        view->setTool(Viewport::Tool::Move);
        check(view->measurements("[0,0,2]") && view->measurements("0,0,1m"), "Connected cap move");
        const auto &movedBox = *doc.bodies().at(1);
        for (const auto &[edge, incident] : movedBox.topology.adjacency(movedBox.surface).edgeFaces)
            check(incident.size() == 2, "Connected cap retains closed walls");
        check(movedBox.surface.vertices.at(movedBox.surface.faces.at(cap).loops[0][0]).z == 3,
              "Connected cap uses the requested displacement");
        // With no preselection, Move can manipulate an inferred vertex.
        doc = Document();
        doc.addFace({{{0, 0, 0}, {4, 0, 0}, {4, 3, 0}, {0, 3, 0}}});
        view->refresh();
        view->setDrawingPlane(DrawingPlane{});
        view->standardView(1);
        view->fit();
        view->setTool(Viewport::Tool::Move);
        view->selectEntities({});
        for (int i = 0; i < 100 && !view->inferenceReady(); ++i) {
            movePointer(*view, {0, 0, 0});
            QTest::qWait(10);
        }
        click(*view, {0, 0, 0});
        const auto beforeVertex = encodeDocument(doc);
        check(!view->measurements("0,0,1m") && encodeDocument(doc) == beforeVertex,
              "Single-vertex nonplanar move rejects atomically");
        check(view->measurements("1m,0,0") &&
                  doc.bodies().at(1)->surface.vertices.at(1) == Vec3{1, 0, 0} &&
                  doc.bodies().at(1)->surface.vertices.at(2) == Vec3{4, 0, 0},
              "Inferred vertex moves without moving other vertices");
        doc.undo();
        view->refresh();
        // Drag releases commit once and use the same inferred displacement.
        view->setSelection(1, 5);
        view->setTool(Viewport::Tool::Move);
        const auto start = view->project({0, 0, 0}).toPoint();
        const auto end = view->project({2, 0, 0}).toPoint();
        QTest::mousePress(view, Qt::LeftButton, {}, start);
        QMouseEvent move(QEvent::MouseMove, end, view->mapToGlobal(end), Qt::NoButton,
                         Qt::LeftButton, {});
        QCoreApplication::sendEvent(view, &move);
        QTest::mouseRelease(view, Qt::LeftButton, {}, end);
        check(doc.bodies().at(1)->surface.vertices.at(1) == Vec3{2, 0, 0},
              "Move drag commits inferred displacement");
        doc.undo();
        view->refresh();
        check(doc.bodies().at(1)->surface.vertices.at(1) == Vec3{}, "One drag is one undo");
        // Whole-context copy creates and selects a new hierarchy, including amendment.
        doc.addFace({{{0, 0, 0}, {1, 0, 0}, {1, 1, 0}, {0, 1, 0}}});
        doc.transform(2, Transform::translation({5, 0, 0}), 1);
        view->refresh();
        view->setSelection(1);
        view->setTool(Viewport::Tool::Move);
        view->setTransformCopy(true);
        check(view->measurements("[0,0,0]") && view->measurements("4,0,0"), "Hierarchy copy");
        const auto root = view->selectedBody();
        check(root > 2 && doc.bodies().size() == 4, "New copied root selected");
        for (const auto &[id, body] : doc.bodies())
            if (body->parent == root)
                check(doc.worldTransform(id).point({}) == Vec3{9, 0, 0},
                      "Copied child inherits moved frame");
        check(view->measurements("6,0,0") && view->selectedBody() > root &&
                  doc.bodies().size() == 4,
              "Hierarchy amendment replaces copied identities");
        if (argc > 1) {
            view->standardView(0);
            view->fit();
            QTest::qWait(120);
            check(window.grab().save(QString::fromLocal8Bit(argv[1])), "Transform capture saved");
        }
        doc.undo();
        view->refresh();
        check(doc.bodies().size() == 2 && doc.worldTransform(2).point({}) == Vec3{5, 0, 0},
              "Hierarchy copy undoes once");
        std::cout << "Native move/rotate/scale/flip, pivots, copy amendment, local axes, pointer "
                     "parity, cancel and lock guards passed; DPR="
                  << view->devicePixelRatioF() << '\n';
        return 0;
    } catch (const std::exception &error) {
        std::cerr << error.what() << " (status: " << status.toStdString() << ")\n";
        return 1;
    }
}
