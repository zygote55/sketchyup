#include "app/window.hpp"
#include "core/components.hpp"
#include "core/face_textures.hpp"
#include "core/groups.hpp"
#include "io/document_io.hpp"
#include <QApplication>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFile>
#include <QJsonDocument>
#include <QLineEdit>
#include <QPushButton>
#include <QSurfaceFormat>
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
void click(Window &window, const char *name) {
    auto *button = window.findChild<QPushButton *>(name);
    check(button, "Texture action exists");
    button->click();
}
void type(QDialog *dialog, const char *name, const QString &value) {
    auto *field = dialog->findChild<QLineEdit *>(name);
    check(field, "Texture field exists");
    field->setFocus();
    field->selectAll();
    QTest::keyClicks(field, value);
}
void accept(QDialog *dialog) {
    dialog->findChild<QDialogButtonBox *>()->button(QDialogButtonBox::Ok)->click();
}
void modal(Window &window, const std::function<void(QDialog *)> &operation) {
    bool opened = false;
    QTimer timer;
    timer.setInterval(10);
    QObject::connect(&timer, &QTimer::timeout, &window, [&] {
        auto *dialog = window.findChild<QDialog *>("textureDialog");
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
    click(window, "materialTexture");
    check(opened, "Texture editor opened");
}
void near(Vec3 actual, Vec3 expected, const char *message) {
    check(length(actual - expected) < 1e-9, message);
}
} // namespace
int main(int argc, char **argv) {
    QSurfaceFormat format;
    format.setDepthBufferSize(24);
    QSurfaceFormat::setDefaultFormat(format);
    QTemporaryDir isolated;
    qputenv("XDG_CONFIG_HOME", isolated.path().toUtf8());
    QApplication app(argc, argv);
    Window window;
    window.resize(1280, 950);
    auto &doc = window.document();
    auto *view = window.viewport();
    try {
        QFile example(QStringLiteral(SOURCE_DIR "/examples/texture-mapping.json"));
        check(example.open(QIODevice::ReadOnly), "Packaged texture example opens");
        executeBatch(doc, {{"apiVersion", 1},
                           {"documentId", QString::fromStdString(doc.identity())},
                           {"expectedRevision", QString::number(doc.revision())},
                           {"commands", QJsonDocument::fromJson(example.readAll()).array()}});
        const Id body = 1, face = 5;
        TextureMapping front{
            {.123456789012345, .25, 1}, {2, .5, 0}, {0, 4, 0}, {.123456789012345, -.25}};
        assignTextureMapping(doc, body, face, front, true, false);
        const auto back = faceTextureMappings(*doc.bodies().at(body), face).back;
        sync(window);
        window.show();
        check(QTest::qWaitForWindowExposed(&window), "Window exposed");
        view->setSelection(body, face);
        view->standardView(1);
        view->fit();
        view->setPaintMaterial(1, 0);
        sync(window);
        const auto original = encodeContainer(doc);
        const auto history = doc.history().position;
        modal(window, [&](QDialog *dialog) { accept(dialog); });
        check(encodeContainer(doc) == original && doc.history().position == history,
              "Untouched mapping preserves exact precision and history");
        view->setPaintMaterial(1, 2);
        modal(window, [&](QDialog *dialog) { accept(dialog); });
        check(encodeContainer(doc) == original,
              "Untouched Both sides preserves two distinct original projections");
        modal(window, [&](QDialog *dialog) {
            dialog->findChild<QComboBox *>("textureSourceSide")->setCurrentIndex(1);
            type(dialog, "textureOffsetU", "0.75");
            accept(dialog);
        });
        const auto copied = faceTextureMappings(*doc.bodies().at(body), face);
        check(copied.front == copied.back && copied.front->offset.u == .75,
              "Explicit back source and Both target copy one edited projection");
        doc.undo();
        view->setPaintMaterial(1, 0);
        sync(window);
        const auto beforeInvalid = encodeContainer(doc);
        modal(window, [&](QDialog *dialog) {
            type(dialog, "textureWidth", "0 mm");
            accept(dialog);
            check(dialog->isVisible() &&
                      !dialog->findChild<QLabel *>("textureError")->text().isEmpty(),
                  "Zero repeat retains inline error");
        });
        check(encodeContainer(doc) == beforeInvalid, "Invalid and canceled mapping never publishes");
        doc.setDisplayUnits(DisplayUnit::Millimeters);
        sync(window);
        const auto beforeEdit = doc.bodies().at(body);
        modal(window, [&](QDialog *dialog) {
            check(dialog->findChild<QLineEdit *>("textureWidth")->text().contains("mm"),
                  "Document units populate repeat lengths");
            type(dialog, "textureWidth", "-1000");
            type(dialog, "textureHeight", "500 mm");
            type(dialog, "textureRotation", "90");
            type(dialog, "textureOriginX", "250 mm");
            type(dialog, "textureOffsetV", "0.75");
            accept(dialog);
            check(!dialog->isVisible(), "Valid texture edit accepts");
        });
        const auto changed = faceTextureMappings(*doc.bodies().at(body), face);
        check(changed.back == back, "Front editor leaves back projection exact");
        check(changed.front->origin == Vec3{.25, .25, 1} &&
                  changed.front->offset.u == front.offset.u && changed.front->offset.v == .75,
              "Edited origin/offset retain untouched floating point channels");
        near(changed.front->uGradient, {.25, -1, 0},
             "Signed width and quarter-turn match independent gradient");
        near(changed.front->vGradient, {-std::sqrt(4.25), 0, 0},
             "Height scales sheared repeat edge before rotation");
        const auto afterEdit = doc.bodies().at(body);
        doc.undo();
        check(*doc.bodies().at(body) == *beforeEdit, "Texture edit is one Undo");
        doc.redo();
        check(*doc.bodies().at(body) == *afterEdit, "Texture Redo exact");
        sync(window);
        const auto roundtrip = decodeContainer(encodeContainer(doc));
        check(*roundtrip.bodies().at(body) == *doc.bodies().at(body),
              "Native authored mapping survives container round trip");
        const auto resetHistory = doc.history().position;
        click(window, "materialResetTexture");
        check(!faceTextureMappings(*doc.bodies().at(body), face).front &&
                  faceTextureMappings(*doc.bodies().at(body), face).back == back,
              "Reset respects selected physical side");
        click(window, "materialResetTexture");
        check(doc.history().position == resetHistory + 1,
              "Repeated implicit reset does not add history");
        doc.undo();
        sync(window);
        const auto second = doc.addFace({{{3, 0, 1}, {5, 0, 1}, {5, 2, 1}, {3, 2, 1}}});
        sync(window);
        view->selectEntities(
            {{body, SelectionKind::Face, face}, {second, SelectionKind::Face, face}});
        modal(window, [&](QDialog *dialog) {
            type(dialog, "textureOffsetU", "0.5");
            accept(dialog);
        });
        check(faceTextureMappings(*doc.bodies().at(body), face).front ==
                  faceTextureMappings(*doc.bodies().at(second), face).front,
              "One world projection applies across selected faces");
        doc.undo();
        sync(window);
        view->setSelection(body, face);
        auto saved = doc.bodies().at(body);
        modal(window, [&](QDialog *dialog) {
            view->setSelection(second, face);
            type(dialog, "textureWidth", "2 m");
            accept(dialog);
            check(dialog->isVisible() &&
                      dialog->findChild<QLabel *>("textureError")->text().contains("changed"),
                  "Changed selection rejects editor commit");
        });
        check(doc.bodies().at(body) == saved, "Changed selection preserves original face");
        view->setSelection(body, face);
        modal(window, [&](QDialog *dialog) {
            view->lockSelection();
            type(dialog, "textureWidth", "2 m");
            accept(dialog);
            check(dialog->isVisible() &&
                      !dialog->findChild<QLabel *>("textureError")->text().isEmpty(),
                  "Editor lock rejects mapping commit");
        });
        check(doc.bodies().at(body) == saved, "Editor lock leaves document unchanged");
        view->unlockContexts();
        view->setSelection(body, face);
        modal(window, [&](QDialog *dialog) {
            doc.setDisplayUnits(DisplayUnit::Meters);
            type(dialog, "textureWidth", "2 m");
            accept(dialog);
            check(dialog->isVisible() &&
                      dialog->findChild<QLabel *>("textureError")->text().contains("changed"),
                  "Changed document rejects mapping commit");
        });
        sync(window);
        const auto made = createComponent(doc, body, "Textured panel");
        auto placement = Transform::translation({10, 0, 0}) * Transform::scaling({-2, 3, 1});
        placement.m[4] = .4;
        const auto placed = placeComponent(doc, made.definition, placement);
        const auto member = made.movedGeometry.at(body);
        const auto other = doc.instances().at(placed.instance)->members.at(member);
        sync(window);
        view->enterContext(placed.instance);
        view->setSelection(other, face);
        const auto sharedBefore = doc.bodies().at(member);
        modal(window, [&](QDialog *dialog) {
            type(dialog, "textureWidth", "750 mm");
            accept(dialog);
        });
        check(faceTextureMappings(*doc.bodies().at(member), face) ==
                  faceTextureMappings(*doc.bodies().at(other), face),
              "Opened component mapping propagates shared local records");
        const auto world = transformTextureMapping(
            *faceTextureMappings(*doc.bodies().at(other), face).front, doc.worldTransform(other));
        const auto repeatWidth =
            length(world.vGradient) / length(cross(world.uGradient, world.vGradient));
        check(std::abs(repeatWidth - .75) < 1e-9,
              "World repeat length accounts for reflected sheared placement");
        doc.undo();
        check(*doc.bodies().at(member) == *sharedBefore, "Shared mapping undoes atomically");
        sync(window);
        modal(window, [&](QDialog *dialog) {
            dialog->findChild<QComboBox *>("textureSpace")->setCurrentIndex(1);
            type(dialog, "textureWidth", "1 m");
            accept(dialog);
        });
        const auto local = *faceTextureMappings(*doc.bodies().at(other), face).front;
        check(std::abs(length(local.vGradient) / length(cross(local.uGradient, local.vGradient)) -
                       1) < 1e-9,
              "Explicit local coordinates measure repeat lengths in the geometry body frame");
        doc.undo();
        sync(window);
        view->leaveContext();
        view->setSelection(body);
        saved = doc.bodies().at(member);
        click(window, "materialTexture");
        check(!window.findChild<QLabel *>("materialError")->text().isEmpty() &&
                  doc.bodies().at(member) == saved,
              "Closed component needs explicit face editing context");
        view->enterContext(body);
        view->setSelection(member, face);
        view->fit();
        sync(window);
        check(view->rendererReady(), "Texture editing retains live renderer");
        if (const auto path = qEnvironmentVariable("SKETCHYUP_TEXTURE_EDITOR_EVIDENCE");
            !path.isEmpty())
            modal(window, [&](QDialog *dialog) {
                check(QTest::qWaitFor([&] { return dialog->isVisible(); }),
                      "Editor capture visible");
                check(dialog->grab().save(path), "Raw editor capture written");
            });
        std::cout << "Native texture units, mirror, rotation, shear, scopes, reset, precision, "
                     "history and stale guards passed; DPR="
                  << view->devicePixelRatioF() << '\n';
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
