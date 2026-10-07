#include "app/surface_format.hpp"
#include "app/window.hpp"
#include "core/components.hpp"
#include "core/materials.hpp"
#include "io/assets.hpp"
#include "io/document_io.hpp"
#include <QAction>
#include <QApplication>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFile>
#include <QFileDialog>
#include <QPushButton>
#include <QTemporaryDir>
#include <QTest>
#include <QTimer>
#include <QWindow>
#include <iostream>
using namespace sketchy;
namespace {
void check(bool value, const char *message) {
    if (!value)
        throw std::runtime_error(message);
}
void sync(Window &window) { QMetaObject::invokeMethod(window.viewport(), "changed"); }
void focus(QWidget *widget) {
    widget->window()->activateWindow();
    check(QTest::qWaitFor(
              [&] { return QGuiApplication::focusWindow() == widget->window()->windowHandle(); }),
          "Native focus settled");
    widget->setFocus();
    check(QTest::qWaitFor([&] { return widget->hasFocus(); }), "Widget focused");
}
void click(Window &window, const char *name) {
    auto *button = window.findChild<QPushButton *>(name);
    check(button, "Material button exists");
    button->click();
}
void modal(Window &window, const char *button, const std::function<void(QDialog *)> &operation,
           bool file = false) {
    bool opened = false;
    QTimer timer;
    timer.setInterval(10);
    QObject::connect(&timer, &QTimer::timeout, &window, [&] {
        QDialog *dialog = file ? window.findChild<QFileDialog *>()
                               : window.findChild<QDialog *>("materialDialog");
        if (!dialog || !dialog->isVisible())
            return;
        timer.stop();
        dialog->activateWindow();
        if (!QTest::qWaitFor(
                [&] { return QGuiApplication::focusWindow() == dialog->windowHandle(); })) {
            dialog->reject();
            return;
        }
        opened = true;
        operation(dialog);
        if (dialog->isVisible())
            dialog->reject();
    });
    timer.start();
    click(window, button);
    check(opened, "Material modal opened");
}
void type(QDialog *dialog, const char *name, const QString &value) {
    auto *input = dialog->findChild<QLineEdit *>(name);
    input->setFocus();
    input->selectAll();
    QTest::keyClicks(input, value);
}
void accept(QDialog *dialog) {
    dialog->findChild<QDialogButtonBox *>()->button(QDialogButtonBox::Ok)->click();
}
void chooseFile(Window &window, const char *button, const QString &path) {
    modal(
        window, button,
        [&](QDialog *dialog) {
            auto *file = qobject_cast<QFileDialog *>(dialog);
            auto *name = file->findChild<QLineEdit *>("fileNameEdit");
            check(name, "File chooser filename field exists");
            name->setFocus();
            name->selectAll();
            QTest::keyClicks(name, path);
            QMetaObject::invokeMethod(file, "accept");
            check(file->result() == QDialog::Accepted, "File chooser accepted the resource path");
        },
        true);
}
} // namespace
int main(int argc, char **argv) {
    QSurfaceFormat format;
    format.setDepthBufferSize(24);
    sketchy::setDefaultViewportFormat(format);
    QTemporaryDir files;
    qputenv("XDG_CONFIG_HOME", files.path().toUtf8());
    QApplication::setAttribute(Qt::AA_DontUseNativeDialogs);
    QApplication app(argc, argv);
    Window window;
    window.resize(1280, 900);
    auto &doc = window.document();
    auto *view = window.viewport();
    try {
        const auto body = doc.addFace({{{-2, -2, 1}, {2, -2, 1}, {2, 2, 1}, {-2, 2, 1}}});
        const auto face = doc.bodies().at(body)->surface.faces.begin()->first;
        sync(window);
        window.show();
        check(QTest::qWaitForWindowExposed(&window), "Materials window exposed");
        view->standardView(1);
        view->fit();
        focus(view);
        QTest::keyClick(view, Qt::Key_B);
        check(view->tool() == Viewport::Tool::Paint &&
                  window.findChild<QTabWidget *>("organizationTabs")->currentIndex() == 3,
              "B activates Paint and opens Materials");
        modal(window, "materialNew", [&](QDialog *dialog) {
            dialog->findChild<QComboBox *>("materialLibrary")->setCurrentIndex(5);
            check(dialog->findChild<QLineEdit *>("materialColor")->text() == "#b7d9e5" &&
                      dialog->findChild<QDoubleSpinBox *>("materialOpacity")->value() == 35,
                  "Local swatch library populates editable color and opacity");
            type(dialog, "materialName", "Terracotta");
            type(dialog, "materialColor", "#cc4422");
            dialog->findChild<QDoubleSpinBox *>("materialOpacity")->setValue(75);
            accept(dialog);
        });
        check(doc.materials().size() == 1 && view->paintMaterial() == 1 &&
                  doc.materials().at(1)->opacity == .75f,
              "Native swatch creation selects persisted opacity/color");
        const auto original = encodeDocument(doc);
        modal(window, "materialEdit", [&](QDialog *dialog) {
            type(dialog, "materialColor", "invalid");
            accept(dialog);
            check(dialog->isVisible() &&
                      !dialog->findChild<QLabel *>("materialDialogError")->text().isEmpty(),
                  "Invalid material edit retains inline error");
        });
        check(encodeDocument(doc) == original, "Invalid/canceled material edit does not publish");
        view->selectEntities(
            {{body, SelectionKind::Face, face},
             {body, SelectionKind::Edge, doc.bodies().at(body)->topology.edges.begin()->first}});
        window.findChild<QComboBox *>("materialSide")->setCurrentIndex(0);
        click(window, "materialApply");
        check(faceMaterials(*doc.bodies().at(body), face) == MaterialSides{1, 0},
              "Apply changes only requested front side");
        doc.undo();
        sync(window);
        check(faceMaterials(*doc.bodies().at(body), face) == MaterialSides{}, "Apply undoes once");
        doc.redo();
        sync(window);
        const auto blue = createMaterial(doc, "Blue back", {.1f, .2f, .9f}, .33333334f);
        sync(window);
        view->setPaintMaterial(blue, 1);
        modal(window, "materialEdit", [&](QDialog *dialog) {
            type(dialog, "materialName", "Precise blue");
            accept(dialog);
        });
        check(doc.materials().at(blue)->opacity == .33333334f &&
                  doc.materials().at(blue)->color == std::array<float, 3>{.1f, .2f, .9f},
              "Renaming preserves untouched floating point color and opacity");
        click(window, "materialPaint");
        const Vec3 probe{-.63, .43, 1};
        focus(view);
        QTest::mouseClick(view, Qt::LeftButton, Qt::NoModifier, view->project(probe).toPoint());
        check(faceMaterials(*doc.bodies().at(body), face) == MaterialSides{1, blue},
              "Paint clicks use independent back assignment");
        view->standardView(6);
        doc.transform(body, Transform::scaling({-1, 1, 1}));
        view->refresh();
        const auto beforeSample = encodeDocument(doc);
        view->setPaintMaterial(1, 0);
        focus(view);
        QTest::mouseClick(view, Qt::LeftButton, Qt::AltModifier, view->project(probe).toPoint());
        check(view->paintMaterial() == blue && view->paintSide() == 1 &&
                  encodeDocument(doc) == beforeSample,
              "Alt-click samples physical mirrored back without a document edit");
        const auto component = createComponent(doc, body, "Panel");
        const auto placed =
            placeComponent(doc, component.definition, Transform::translation({5, 0, 0}));
        const auto member = component.movedGeometry.at(body);
        const auto other = doc.instances().at(placed.instance)->members.at(member);
        sync(window);
        view->fit();
        view->setPaintMaterial(1, 1);
        const auto closed = encodeDocument(doc);
        focus(view);
        QTest::mouseClick(view, Qt::LeftButton, Qt::NoModifier,
                          view->project(doc.worldTransform(member).point(probe)).toPoint());
        check(encodeDocument(doc) == closed, "Closed component paint cannot bypass edit scope");
        view->enterContext(body);
        view->fit();
        view->setPaintMaterial(1, 1);
        focus(view);
        QTest::mouseClick(view, Qt::LeftButton, Qt::NoModifier,
                          view->project(doc.worldTransform(member).point(probe)).toPoint());
        check(faceMaterials(*doc.bodies().at(member), face).back == 1 &&
                  faceMaterials(*doc.bodies().at(other), face).back == 1,
              "Opened component paint publishes shared member assignment");
        doc.undo();
        sync(window);
        check(faceMaterials(*doc.bodies().at(member), face).back == blue &&
                  faceMaterials(*doc.bodies().at(other), face).back == blue,
              "Shared material assignment undoes in one step");
        const auto source = files.filePath("resource.bin");
        QFile file(source);
        check(file.open(QIODevice::WriteOnly) && file.write("original resource") == 17,
              "Resource fixture written");
        file.close();
        const auto beforeCancel = encodeDocument(doc);
        modal(window, "materialAttach", [](QDialog *dialog) { dialog->reject(); }, true);
        check(encodeDocument(doc) == beforeCancel,
              "Canceling the file chooser leaves the document unchanged");
        chooseFile(window, "materialAttach", source);
        const auto asset = doc.materials().at(1)->asset;
        if (!asset)
            std::cerr << "Attachment error: "
                      << window.findChild<QLabel *>("materialError")->text().toStdString()
                      << "; selected=" << view->paintMaterial()
                      << "; assets=" << doc.assets().size() << '\n';
        check(asset && assetByteArray(doc.assets().at(asset)->payload) == "original resource",
              "Native attachment owns bytes inside component scope");
        doc.undo();
        sync(window);
        check(doc.materials().at(1)->asset == 0 && doc.assets().empty(),
              "Import and binding undo together");
        doc.redo();
        sync(window);
        view->editMaterials({QJsonObject{{"command", "asset.replace"},
                                         {"asset", QString::number(asset)},
                                         {"data", QJsonValue::Null}}});
        check(window.findChild<QLabel *>("materialDetails")->text().contains("Missing"),
              "Missing resource is explicit in panel");
        chooseFile(window, "materialResolve", source);
        check(doc.materials().at(1)->asset == asset && doc.assets().at(asset)->payload,
              "Resolve preserves resource identity");
        QFile::remove(source);
        const auto reopened = decodeContainer(encodeContainer(doc));
        check(assetByteArray(reopened.assets().at(asset)->payload) == "original resource",
              "Native imported bytes survive source deletion and reopen");
        click(window, "materialDelete");
        check(doc.materials().contains(1) &&
                  !window.findChild<QLabel *>("materialError")->text().isEmpty(),
              "Used swatch deletion rejects inline");
        click(window, "materialDetach");
        click(window, "materialPurgeAssets");
        check(doc.assets().empty(), "Detached unused file can be removed");
        doc.undo();
        sync(window);
        check(doc.assets().contains(asset), "Unused file cleanup is undoable");
        const auto beforeStale = doc.materials().at(1);
        modal(window, "materialEdit", [&](QDialog *dialog) {
            createMaterial(doc, "Concurrent", {.3f, .4f, .5f});
            type(dialog, "materialName", "Stale");
            accept(dialog);
            check(
                dialog->isVisible() &&
                    dialog->findChild<QLabel *>("materialDialogError")->text().contains("changed"),
                "Stale editor retains error");
        });
        check(doc.materials().at(1) == beforeStale, "Stale dialog does not overwrite material");
        sync(window);
        check(view->rendererReady(), "Material controls retain live renderer");
        if (app.arguments().contains("--capture"))
            window.grab().save("/capture/R036d-materials.png");
        std::cout << "Native swatches, retained errors, precision, side assignment, paint/sample, "
                     "shared scope, assets, undo and stale guards passed; DPR="
                  << view->devicePixelRatioF() << '\n';
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
