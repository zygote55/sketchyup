#include "app/window.hpp"
#include "core/components.hpp"
#include "core/tags.hpp"
#include "io/document_io.hpp"
#include <QAction>
#include <QApplication>
#include <QDialog>
#include <QDialogButtonBox>
#include <QPushButton>
#include <QSurfaceFormat>
#include <QTest>
#include <QTimer>
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
        auto &doc = window.document();
        doc.addFace({{{0, 0, 0}, {2, 0, 0}, {2, 2, 0}, {0, 2, 0}}});
        auto *view = window.viewport();
        QObject::connect(view, &Viewport::message, [&](const QString &text) { status = text; });
        QMetaObject::invokeMethod(view, "changed");
        window.show();
        check(QTest::qWaitForWindowExposed(&window), "Component window exposed");
        window.activateWindow();
        check(QTest::qWaitForWindowActive(&window), "Component window active");
        view->setFocus();
        view->setSelection(1);
        QTimer::singleShot(50, [&] {
            auto *dialog = window.findChild<QDialog *>("componentDialog");
            check(dialog, "Component creation dialog opens");
            dialog->findChild<QLineEdit *>("componentName")->setText("Panel");
            QTest::mouseClick(dialog->findChild<QDialogButtonBox *>()->button(QDialogButtonBox::Ok),
                              Qt::LeftButton);
        });
        QTest::keyClick(view, Qt::Key_G);
        check(doc.instances().contains(1) && doc.definitions().at(1)->name == "Panel",
              "Native create uses the named shared definition");
        view->placeComponent(1, {6, 0, 0});
        const auto peer = view->selectedBody();
        doc.transform(peer, Transform::translation({6, 0, 0}) * Transform::scaling({-1, 1, 1}));
        const auto member = doc.instances().at(peer)->members.at(2);
        view->refresh();
        view->standardView(1);
        view->fit();
        view->setTool(Viewport::Tool::Select);
        QTest::qWait(60);
        const auto folder = createTag(doc, "Assembly tags", 0, true);
        const auto tag = createTag(doc, "Original", folder);
        assignTag(doc, 1, tag);
        view->refresh();
        view->grabFramebuffer();
        const auto geometryBuilds = view->renderStats().bodyMeshBuilds;
        const auto geometryRecords = doc.bodies();
        editTag(doc, folder, {}, {}, false);
        view->refresh();
        view->grabFramebuffer();
        check(!view->selectionAt(view->project({1, 1, 0})) && doc.bodies() == geometryRecords &&
                  view->renderStats().bodyMeshBuilds == geometryBuilds,
              "Tag folder visibility updates native picking without rebuilding or replacing "
              "geometry");
        editTag(doc, folder, {}, {}, true);
        view->refresh();
        check(view->selectionAt(view->project({1, 1, 0})).has_value(),
              "Showing a tag folder restores picking");
        QTest::mouseDClick(view, Qt::LeftButton, {}, view->project({5, 1, 0}).toPoint());
        check(view->componentScope() == peer, "Double-click opens the mirrored component scope");
        auto *banner = window.findChild<QLabel *>("componentScopeBanner");
        check(banner->isVisible() && banner->text().contains("2 instances") &&
                  banner->text().contains("Panel"),
              "Persistent banner names the shared definition and affected instance count");
        view->setDrawingPlane(DrawingPlane::make({}, {0, 0, 1}, {1, 0, 0}), member);
        view->setTool(Viewport::Tool::Line);
        check(view->measurements("[5,0,0]") && view->measurements("[5,2,0]"),
              "Native line draws in the mirrored instance frame");
        check(doc.bodies().at(2)->surface.faces.size() == 2 &&
                  doc.bodies().at(member)->surface.faces.size() == 2,
              "Native line updates both component instances");
        window.findChild<QAction *>("edit.undo")->trigger();
        view->setTool(Viewport::Tool::Select);
        view->setSelection(member, 5);
        view->setTool(Viewport::Tool::Extrude);
        QTest::qWait(50);
        QTest::mouseClick(view, Qt::LeftButton, {}, view->project({5, 1, 0}).toPoint());
        check(view->measurements("0.5m") && view->measurements("0.75m"),
              "Shared push/pull supports numeric amendment");
        for (auto id : {Id{2}, member}) {
            double height = 0;
            for (const auto &[vertex, point] : doc.bodies().at(id)->surface.vertices)
                height = std::max(height, point.z);
            check(std::abs(height - .75) < tolerance, "Amended push/pull reaches all peers");
        }
        window.findChild<QAction *>("edit.undo")->trigger();
        check(doc.bodies().at(member)->surface.faces.size() == 1,
              "One undo restores the shared pre-push face");
        view->setTool(Viewport::Tool::Rectangle);
        view->setDrawingPlane(DrawingPlane::make({0, 0, 1}, {0, 0, 1}, {1, 0, 0}), peer);
        check(view->measurements("[5,0,1]") && view->measurements("0.5m,0.5m"),
              "New geometry draws in the component root");
        check(view->selectedBody() && doc.bodies().at(view->selectedBody())->parent == peer,
              "Native drawing selects the initiating placement's new geometry");
        check(doc.instances().at(1)->members.size() == 3 &&
                  doc.instances().at(peer)->members.size() == 3,
              "New root geometry becomes a canonical member in all instances");
        window.findChild<QAction *>("edit.undo")->trigger();
        view->setTool(Viewport::Tool::Select);
        view->setSelection(member, 5);
        view->deleteSelection();
        check(doc.bodies().at(2)->surface.faces.empty() &&
                  doc.bodies().at(member)->surface.faces.empty(),
              "Native delete uses the active shared component scope");
        window.findChild<QAction *>("edit.undo")->trigger();
        view->setSelection(member, 5);
        view->setTool(Viewport::Tool::Move);
        view->setTransformCopy(true);
        check(view->measurements("[5,1,0]") && view->measurements("0,0,1m") &&
                  view->measurements("x3"),
              "Native shared copy arrays support count amendment");
        check(doc.bodies().at(member)->surface.faces.size() == 4 &&
                  doc.bodies().at(2)->surface.faces.size() == 4 &&
                  view->selectionState().entities().size() == 3,
              "Shared arrays update both instances and select only the initiating copies");
        window.findChild<QAction *>("edit.undo")->trigger();
        view->setTransformCopy(false);
        view->setTool(Viewport::Tool::Select);
        view->setSelection(member, 5);
        view->makeGroup();
        check(view->selectedBody() &&
                  doc.bodies().at(view->selectedBody())->kind == BodyKind::Group,
              "Grouping shared raw geometry selects the initiating placement's new group");
        view->explodeGroups();
        check(view->selectedBody() && doc.bodies().at(view->selectedBody())->parent == peer &&
                  !view->selectionState().entities().empty(),
              "Shared explode and merge retain selected geometry after root normalization");
        window.findChild<QAction *>("edit.undo")->trigger();
        window.findChild<QAction *>("edit.undo")->trigger();
        view->setSelection(member, 5);
        view->makeComponent("Inset");
        const auto nested = view->selectedBody();
        check(doc.instances().contains(nested) && doc.instances().size() == 4,
              "Make component from a raw face creates nested reusable content in both peers");
        view->enterContext(nested);
        check(banner->text().contains("Inset") && banner->text().contains("2 instances"),
              "Nested editing banner follows the innermost definition");
        view->makeComponentUnique(true);
        check(doc.instances().at(peer)->definition != doc.instances().at(1)->definition,
              "Native nested make-unique isolates the ancestor ownership path");
        window.findChild<QAction *>("edit.undo")->trigger();
        view->leaveContext();
        window.findChild<QAction *>("edit.undo")->trigger();
        QMetaObject::invokeMethod(banner, "linkActivated", Q_ARG(QString, "unique"));
        check(doc.instances().at(peer)->definition != doc.instances().at(1)->definition &&
                  banner->text().contains("1 instance"),
              "Banner make-unique isolates the active placement and refreshes scope count");
        view->setSelection(member, 5);
        view->paintSelection({.2f, .4f, .8f});
        check(doc.bodies().at(member)->color != doc.bodies().at(2)->color,
              "Painting a unique component leaves peers untouched");
        const auto world = doc.worldTransform(member);
        const auto beforeAxes = encodeDocument(doc);
        QTimer::singleShot(50, [&] {
            auto *dialog = window.findChild<QDialog *>("componentDialog");
            check(dialog, "Component axes dialog opens");
            auto *origin = dialog->findChild<QLineEdit *>("componentOrigin");
            auto *ok = dialog->findChild<QDialogButtonBox *>()->button(QDialogButtonBox::Ok);
            origin->setText("invalid");
            QTest::mouseClick(ok, Qt::LeftButton);
            check(dialog->isVisible() &&
                      !dialog->findChild<QLabel *>("componentDialogError")->text().isEmpty() &&
                      encodeDocument(doc) == beforeAxes,
                  "Invalid axes input stays in the form without changing the document");
            origin->setText("0.5,0.5,0");
            dialog->findChild<QLineEdit *>("componentXAxis")->setText("0,1,0");
            QTest::mouseClick(ok, Qt::LeftButton);
        });
        window.findChild<QAction *>("component.axes")->trigger();
        for (const auto &[id, point] : doc.bodies().at(member)->surface.vertices)
            check(length(doc.worldTransform(member).point(point) - world.point(point)) < tolerance,
                  "Native local-axis change preserves world geometry");
        const auto saved = encodeContainer(doc);
        check(encodeContainer(decodeContainer(saved)) == saved,
              "Native component edits retain identity through save/reopen");
        const auto capture = app.arguments().indexOf("--capture");
        if (capture >= 0) {
            view->setTool(Viewport::Tool::Select);
            QCoreApplication::processEvents();
            check(window.grab().save(app.arguments().value(capture + 1)),
                  "Component capture saved");
        }
        QTest::keyClick(view, Qt::Key_Escape);
        check(!view->componentScope() && !banner->isVisible(),
              "Esc closes component editing and its scope banner");
        check(!view->renderStats().glError, "Native component workflow has no GL errors");
        std::cout << "Native components, shared banner, mirrored editing, numeric amendment, "
                     "unique, axes and persistence passed; DPR="
                  << view->devicePixelRatioF() << '\n';
    } catch (const std::exception &error) {
        std::cerr << error.what() << " | status: " << status.toStdString() << '\n';
        return 1;
    }
}
