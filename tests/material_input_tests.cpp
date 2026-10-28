#include "app/surface_format.hpp"
#include "app/window.hpp"
#include "core/components.hpp"
#include "core/materials.hpp"
#include "io/assets.hpp"
#include "io/document_io.hpp"
#include "io/procedural_textures.hpp"
#include "io/texture_presets.hpp"
#include <QAction>
#include <QApplication>
#include <QCheckBox>
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
#include <array>
#include <functional>
#include <iostream>
#include <optional>
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
            auto *library = dialog->findChild<QComboBox *>("materialLibrary");
            library->setCurrentIndex(library->findText("Glass"));
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
        // Textured presets (UX_DESIGN 18.4, ADR 0174 Q1): one undo step adds the texture and its
        // material, an identical texture is reused, and the six colour presets are unchanged.
        const auto textured = [&](const char *preset,
                                  const std::function<void(QDialog *)> &check_) {
            modal(window, "materialNew", [&](QDialog *dialog) {
                auto *library = dialog->findChild<QComboBox *>("materialLibrary");
                library->setCurrentIndex(library->findText(preset));
                check_(dialog);
                accept(dialog);
            });
        };
        const auto assetsBefore = doc.assets().size();
        const auto materialsBefore = doc.materials().size();
        modal(window, "materialNew", [&](QDialog *dialog) {
            auto *library = dialog->findChild<QComboBox *>("materialLibrary");
            const auto heading = library->findText("Textured");
            check(heading > library->findText("Terrain") && heading >= 0,
                  "Textured heading follows the colour presets");
            check(
                !(library->model()->flags(library->model()->index(heading, 0)) & Qt::ItemIsEnabled),
                "Group headings are disabled, so the arrow keys skip them");
            library->setCurrentIndex(library->findText("Wood, light"));
            check(dialog->findChild<QLabel *>("materialTexturePreview")->isVisible() &&
                      dialog->findChild<QLabel *>("materialTextureCaption")
                          ->text()
                          .startsWith("Adds 1 texture") &&
                      dialog->findChild<QLabel *>("materialTextureLicence")->text().contains("CC0"),
                  "A textured preset shows its tile, caption and licence");
            check(!dialog->findChild<QComboBox *>("materialAsset")->isEnabled(),
                  "The asset chooser is held to the preset's texture");
            check(dialog->findChild<QLineEdit *>("materialName")->text() == "Wood, light",
                  "A textured preset fills the name");
            // R100.b2: the preset's signed-off tile size fills the size field, which is on.
            check(dialog->findChild<QCheckBox *>("materialTextureSized")->isChecked() &&
                      dialog->findChild<QDoubleSpinBox *>("materialTextureWidth")->value() == 1 &&
                      dialog->findChild<QDoubleSpinBox *>("materialTextureHeight")->value() == 1,
                  "A textured preset shows its tile size");
            accept(dialog);
        });
        const auto woodId = doc.materials().rbegin()->first;
        const auto wood = doc.materials().at(woodId);
        check(doc.materials().size() == materialsBefore + 1 &&
                  doc.assets().size() == assetsBefore + 1,
              "A textured preset adds one material and one texture");
        check(wood->color == std::array<float, 3>{1, 1, 1} && wood->opacity == 1 && wood->asset,
              "Wood, light has a white tint, full opacity and its texture");
        check(wood->textureSize == TextureSize{1, 1},
              "The created material stores the preset's tile size");
        check(doc.assets().at(wood->asset)->mediaType == "image/jpeg" &&
                  assetByteArray(doc.assets().at(wood->asset)->payload) ==
                      texturePresetBytes(texturePresets()[0]),
              "The texture is the embedded Wood, light file, byte for byte");
        check(window.findChild<QLabel *>("materialDetails")->text().contains("Stored"),
              "The new textured material shows its texture as stored");
        // Undo removes the material and its texture together. The encoding is not compared: the
        // retired asset and material IDs stay used, so the next IDs advance.
        doc.undo();
        sync(window);
        check(doc.materials().size() == materialsBefore && doc.assets().size() == assetsBefore &&
                  !doc.materials().contains(woodId) && !doc.assets().contains(wood->asset),
              "A textured preset is one undo step");
        doc.redo();
        sync(window);
        check(doc.materials().contains(woodId) && doc.assets().contains(wood->asset),
              "Redo restores the textured preset");
        textured("Wood, light", [&](QDialog *dialog) {
            check(dialog->findChild<QLabel *>("materialTextureCaption")
                      ->text()
                      .startsWith("Reuses the texture"),
                  "An identical texture in the model is reused, and the caption says so");
        });
        check(doc.assets().size() == assetsBefore + 1 &&
                  doc.materials().rbegin()->second->asset == wood->asset,
              "Identical texture bytes are reused, not copied");
        // R100.b2: the editor sets one width as one undo step, and unchecking the size clears it.
        // The reused texture's material is the paint material now, so the edit targets it.
        const auto reusedId = doc.materials().rbegin()->first;
        modal(window, "materialEdit", [&](QDialog *dialog) {
            dialog->findChild<QDoubleSpinBox *>("materialTextureWidth")->setValue(2.5);
            accept(dialog);
        });
        check(doc.materials().at(reusedId)->textureSize == TextureSize{2.5, 1},
              "The editor sets the width of an existing material");
        doc.undo();
        sync(window);
        check(doc.materials().at(reusedId)->textureSize == TextureSize{1, 1},
              "Undoing the editor's size change restores it");
        modal(window, "materialEdit", [&](QDialog *dialog) {
            dialog->findChild<QCheckBox *>("materialTextureSized")->setChecked(false);
            accept(dialog);
        });
        check(!doc.materials().at(reusedId)->textureSize,
              "Unchecking the size clears it, back to one metre");
        doc.undo();
        sync(window);
        textured("Floor tile, 600 mm", [&](QDialog *dialog) {
            check(dialog->findChild<QLabel *>("materialTextureCaption")
                      ->text()
                      .startsWith("Adds 1 texture"),
                  "A new texture is added");
        });
        const auto floor = doc.materials().rbegin()->second;
        check(floor->textureSize == TextureSize{.6, .6},
              "The floor tile stores its 600 mm tile size");
        check(floor->asset && floor->asset != wood->asset &&
                  floor->color == std::array<float, 3>{float(QColor("#d6d1c7").redF()),
                                                       float(QColor("#d6d1c7").greenF()),
                                                       float(QColor("#d6d1c7").blueF())},
              "The floor tile carries its own texture and tint");
        check(assetByteArray(doc.assets().at(floor->asset)->payload) ==
                  proceduralTextureJpeg(ProceduralTexture::FloorTile600),
              "The generated floor tile is stored as its generator's bytes");
        textured("Glass, reeded", [](QDialog *) {});
        check(doc.materials().rbegin()->second->opacity == .35f,
              "Glass, reeded keeps the flat Glass opacity");
        // The six colour presets are byte-unchanged: the same colour and opacity as before, and
        // no texture.
        // The colour and opacity as the dialog has always written them (ADR 0174 section 3).
        struct ColourPreset {
            const char *name, *color;
            float opacity;
        };
        constexpr std::array<ColourPreset, 6> colours{{{"White", "#e6e6e6", 1},
                                                       {"Concrete", "#b0ada6", 1},
                                                       {"Terracotta", "#c45f42", 1},
                                                       {"Steel", "#7c8a96", 1},
                                                       {"Glass", "#b7d9e5", .35f},
                                                       {"Terrain", "#8a9a6b", 1}}};
        const auto assetsForColours = doc.assets().size();
        // The model keeps material names unique, and the first swatch of this test is Terracotta.
        std::optional<Id> earlier;
        for (const auto &[id, material] : doc.materials())
            if (material->name == "Terracotta")
                earlier = id;
        if (earlier)
            view->editMaterials({QJsonObject{{"command", "material.edit"},
                                             {"material", QString::number(*earlier)},
                                             {"name", "Terracotta swatch"}}});
        for (const auto &preset : colours) {
            modal(window, "materialNew", [&](QDialog *dialog) {
                auto *library = dialog->findChild<QComboBox *>("materialLibrary");
                library->setCurrentIndex(library->findText(preset.name));
                check(!dialog->findChild<QLabel *>("materialTexturePreview")->isVisible(),
                      "A colour preset shows no texture tile");
                accept(dialog);
            });
            const auto &created = doc.materials().rbegin()->second;
            const QColor rgb(preset.color);
            check(created->name == preset.name && created->asset == 0 &&
                      created->color == std::array<float, 3>{float(rgb.redF()), float(rgb.greenF()),
                                                             float(rgb.blueF())} &&
                      created->opacity == preset.opacity,
                  "A colour preset is unchanged");
        }
        check(doc.assets().size() == assetsForColours, "Colour presets add no texture");
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
