#include "app/reference_images_panel.hpp"
#include "app/surface_format.hpp"
#include "app/window.hpp"
#include "core/assets.hpp"
#include "core/components.hpp"
#include "core/reference_images.hpp"
#include "core/sections.hpp"
#include "io/document_io.hpp"
#include <QAction>
#include <QApplication>
#include <QBuffer>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFile>
#include <QLineEdit>
#include <QPushButton>
#include <QTemporaryDir>
#include <QTest>
#include <QTimer>
#include <iostream>
using namespace sketchy;
namespace {
void check(bool okay, const char *message) {
    if (!okay)
        throw std::runtime_error(message);
}
void settled(Viewport &view) {
    view.refresh();
    QCoreApplication::processEvents();
    view.grabFramebuffer();
    QElapsedTimer timer;
    timer.start();
    while (view.texturesPending() && timer.elapsed() < 10000)
        QTest::qWait(10);
    check(!view.texturesPending(), "Reference pixels decode asynchronously");
    QTest::qWait(50);
    view.grabFramebuffer();
}
QColor sample(Viewport &view, Vec3 p) {
    const auto image = view.grabFramebuffer();
    const auto point = view.project(p);
    const QPoint pixel(qRound(point.x() * image.width() / view.width()),
                       qRound(point.y() * image.height() / view.height()));
    check(image.rect().contains(pixel), "Reference pixel probe in frame");
    return image.pixelColor(pixel);
}
void red(Viewport &view, Vec3 p) {
    const auto color = sample(view, p);
    if (!(color.red() > 245 && color.green() < 8 && color.blue() < 8)) {
        std::cerr << color.name().toStdString() << '\n';
        throw std::runtime_error("Reference raster color remains unlit");
    }
}
void picks(Viewport &view, Vec3 p, Id expected) {
    check(view.pick(view.project(p)).first == expected, "CPU reference coverage pick");
    const auto selected = view.selectionAt(view.project(p));
    check(expected ? selected && selected->body == expected && selected->kind == SelectionKind::Body
                   : !selected,
          "GPU reference coverage selects whole image");
}
void save(QDialog *dialog) {
    dialog->findChild<QDialogButtonBox *>()->button(QDialogButtonBox::Save)->click();
}
void modal(Window &window, const char *name, const std::function<void()> &open,
           const std::function<void(QDialog *)> &operation) {
    bool opened{};
    std::exception_ptr failure;
    QTimer timer;
    timer.setInterval(10);
    QObject::connect(&timer, &QTimer::timeout, &window, [&] {
        auto *dialog = window.findChild<QDialog *>(name);
        if (!dialog || !dialog->isVisible())
            return;
        timer.stop();
        opened = true;
        try {
            operation(dialog);
        } catch (...) {
            failure = std::current_exception();
        }
        if (dialog->isVisible())
            dialog->reject();
    });
    timer.start();
    open();
    check(opened, "Native reference dialog opens");
    if (failure)
        std::rethrow_exception(failure);
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
        Window window;
        window.resize(1280, 950);
        auto &doc = window.document();
        auto &view = *window.viewport();
        QImage pixels(16, 16, QImage::Format_RGBA8888);
        const std::array<QColor, 4> colors{Qt::red, Qt::green, Qt::blue, QColor(255, 255, 255, 0)};
        for (int y = 0; y < 16; ++y)
            for (int x = 0; x < 16; ++x)
                pixels.setPixelColor(x, y, colors[(y / 8) * 2 + x / 8]);
        QByteArray png;
        QBuffer buffer(&png);
        buffer.open(QIODevice::WriteOnly);
        check(pixels.save(&buffer, "PNG"), "Reference fixture PNG");
        const auto path = isolated.filePath("reference.png");
        QFile file(path);
        check(file.open(QIODevice::WriteOnly) && file.write(png) == png.size(),
              "Write import fixture");
        file.close();
        auto style = doc.style();
        style.axesVisible = style.gridVisible = style.edgesVisible = style.groundVisible = false;
        doc.setStyle(style);
        window.show();
        check(QTest::qWaitForWindowExposed(&window), "Reference window exposed");
        window.findChild<QAction *>("view.reference_images")->trigger();
        auto *panel = dynamic_cast<ReferenceImagesPanel *>(
            window.findChild<QWidget *>("referenceImagesPanel"));
        check(panel && panel->isVisible(), "Reference panel accessible from View menu");
        const auto history = doc.history().total;
        modal(
            window, "referenceImportDialog", [&] { panel->importFile(path); },
            [&](QDialog *dialog) {
                dialog->findChild<QLineEdit *>("referenceWidth")->setText("4 m");
                save(dialog);
            });
        check(doc.bodies().size() == 1 && doc.assets().size() == 1 &&
                  doc.history().total == history + 1,
              "Image and embedded asset import in one history step");
        const auto id = doc.bodies().begin()->first;
        check(doc.bodies().at(id)->referenceImage->width == 4 &&
                  doc.bodies().at(id)->surface.faces.empty(),
              "Reference has dimensions without modeled faces");
        doc.undo();
        check(doc.bodies().empty() && doc.assets().empty(), "Import Undo removes image and asset");
        doc.redo();
        view.setSelection(0);
        view.standardView(1);
        view.fit();
        settled(view);
        const Vec3 redPoint{1, 3, 0}, bluePoint{1, 1, 0}, hole{3, 1, 0};
        red(view, redPoint);
        const auto blue = sample(view, bluePoint);
        check(blue.blue() > 245 && blue.red() < 8,
              "Top-left pixel coordinates orient image correctly");
        picks(view, redPoint, id);
        picks(view, hole, 0);
        const QRectF smallBox(view.project({.5, 3.5, 0}), view.project({1.5, 2.5, 0}));
        check(
            view.windowSelection(smallBox, false).empty() &&
                view.windowSelection(smallBox, true).contains({id, SelectionKind::Body, 0}),
            "Inside window selection requires the whole plane; crossing accepts partial coverage");
        const auto wholeBox = QRectF(view.project({0, 4, 0}), view.project({4, 0, 0}))
                                  .normalized()
                                  .adjusted(-3, -3, 3, 3);
        check(view.windowSelection(wholeBox, false).contains({id, SelectionKind::Body, 0}),
              "A fully enclosed image is window-selected as a whole entity");
        if (const auto capture = qEnvironmentVariable("SKETCHYUP_REFERENCE_CAPTURE");
            !capture.isEmpty())
            check(view.grabFramebuffer().save(capture + ".png"), "Reference evidence image saved");
        const auto beforeFaceTools = encodeContainer(doc);
        view.setTool(Viewport::Tool::Paint);
        QTest::mouseClick(&view, Qt::LeftButton, Qt::AltModifier, view.project(redPoint).toPoint());
        QTest::mouseClick(&view, Qt::LeftButton, Qt::NoModifier, view.project(redPoint).toPoint());
        view.setTool(Viewport::Tool::Extrude);
        QTest::mouseClick(&view, Qt::LeftButton, Qt::NoModifier, view.project(redPoint).toPoint());
        view.setTool(Viewport::Tool::Select);
        check(encodeContainer(doc) == beforeFaceTools && !view.selectedFace(),
              "Material sampling, painting and push/pull cannot treat an image as a modeled face");
        for (auto mode : {ModelStyleMode::Shaded, ModelStyleMode::Monochrome,
                          ModelStyleMode::Wireframe, ModelStyleMode::XRay}) {
            style.mode = mode;
            doc.setStyle(style);
            settled(view);
            red(view, redPoint);
        }
        style.mode = ModelStyleMode::Textured;
        doc.setStyle(style);
        auto sun = doc.solar();
        sun.enabled = true;
        sun.latitude = 40;
        sun.longitude = -105;
        sun.time = {2010, 6, 21, 8, 0, 0, -420};
        doc.setSolar(sun);
        settled(view);
        red(view, redPoint);
        view.setClipPlane(std::array<double, 4>{1, 0, 0, -2});
        settled(view);
        picks(view, redPoint, 0);
        view.setClipPlane({});
        const auto section =
            createSection(doc, "Reference cut", 0, SectionPlane::through({2, 0, 0}, {1, 0, 0}));
        setActiveSection(doc, 0, section);
        settled(view);
        picks(view, redPoint, 0);
        picks(view, {3, 3, 0}, id);
        check(view.renderStats().meshTriangles == 2,
              "Reference display quad does not grow model topology");
        setActiveSection(doc, 0, {});
        view.setSelection(id);
        settled(view);
        panel->refresh();
        auto openEdit = [&] { window.findChild<QPushButton *>("referenceImageEdit")->click(); };
        const auto revision = doc.revision();
        modal(window, "referenceEditDialog", openEdit, [&](QDialog *dialog) { save(dialog); });
        check(doc.revision() == revision, "Unchanged image editor preserves history");
        modal(window, "referenceEditDialog", openEdit, [&](QDialog *dialog) {
            auto *field = dialog->findChild<QLineEdit *>("referenceOpacity");
            field->setText("2");
            save(dialog);
            check(dialog->isVisible() && doc.revision() == revision,
                  "Invalid opacity does not mutate");
            field->setText("0.5");
            save(dialog);
        });
        check(doc.bodies().at(id)->referenceImage->opacity == .5, "Native opacity saved");
        doc.undo();
        settled(view);
        panel->refresh();
        Transform scaled;
        scaled.m[0] = scaled.m[5] = 2;
        doc.transform(id, scaled);
        settled(view);
        panel->refresh();
        const auto scaledRevision = doc.revision();
        modal(
            window, "referenceCalibrationDialog",
            [&] { window.findChild<QPushButton *>("referenceImageCalibrate")->click(); },
            [&](QDialog *dialog) {
                save(dialog);
                check(!dialog->isVisible(), "Unchanged calibration closes");
            });
        check(
            doc.revision() == scaledRevision && doc.bodies().at(id)->referenceImage->width == 4,
            "Calibration defaults to the placed world length and unchanged Save does not rescale");
        doc.undo();
        settled(view);
        panel->refresh();
        modal(
            window, "referenceCalibrationDialog",
            [&] { window.findChild<QPushButton *>("referenceImageCalibrate")->click(); },
            [&](QDialog *dialog) {
                dialog->findChild<QLineEdit *>("referenceLength")->setText("8 m");
                save(dialog);
            });
        check(doc.bodies().at(id)->referenceImage->width == 8 &&
                  doc.bodies().at(id)->referenceImage->height == 8 &&
                  doc.worldTransform(id).point(
                      referenceImagePoint(*doc.bodies().at(id)->referenceImage, {0, 1})) == Vec3{},
              "Native calibration doubles size and retains anchor");
        doc.undo();
        settled(view);
        panel->refresh();
        modal(window, "referenceEditDialog", openEdit, [&](QDialog *dialog) {
            auto settings = doc.style();
            settings.gridVisible = true;
            doc.setStyle(settings);
            dialog->findChild<QLineEdit *>("referenceOpacity")->setText("0.25");
            save(dialog);
            check(dialog->isVisible() && doc.bodies().at(id)->referenceImage->opacity == 1,
                  "Stale reference editor rejects");
        });
        doc.undo();
        const auto visibleRecord = doc.bodies().at(id);
        auto hiddenRecord = std::make_shared<Body>(*visibleRecord);
        hiddenRecord->hidden = true;
        doc.apply({"Hide reference", {{id, visibleRecord, hiddenRecord}}}, doc.revision());
        settled(view);
        picks(view, redPoint, 0);
        doc.undo();
        settled(view);
        view.setSelection(id);
        view.setPersistentState(false, true);
        settled(view);
        panel->refresh();
        check(!window.findChild<QPushButton *>("referenceImageEdit")->isEnabled() &&
                  !view.selectionAt(view.project(redPoint)),
              "Locked image cannot be edited or GPU selected");
        doc.undo();
        settled(view);
        const auto saved = encodeContainer(doc);
        file.remove();
        auto restored = decodeContainer(saved);
        check(encodeContainer(restored) == saved &&
                  restored.assets().at(1)->payload->bytes() ==
                      std::vector<std::uint8_t>(png.begin(), png.end()),
              "Pixels survive save and source removal");
        view.setSelection(0);
        doc = decodeContainer(saved);
        settled(view);
        red(view, redPoint);
        view.standardView(6);
        view.fit();
        settled(view);
        red(view, redPoint);
        picks(view, redPoint, id);
        auto image = *doc.bodies().at(id)->referenceImage;
        image.opacity = 0;
        setReferenceImage(doc, id, image);
        settled(view);
        picks(view, redPoint, 0);
        doc.undo();
        const auto missing = createAsset(doc, "Missing pixels", "image/png", {});
        image.asset = missing;
        image.opacity = 1;
        setReferenceImage(doc, id, image);
        settled(view);
        const auto placeholder = sample(view, redPoint);
        check(placeholder.red() > placeholder.green() + 40 &&
                  placeholder.blue() > placeholder.green() + 40,
              "Missing image has visible purple placeholder");
        picks(view, redPoint, id);
        doc.undo();
        doc.transform(id, Transform::translation({0, 0, 3}));
        const auto caster = doc.addFace({{{6, 6, 0}, {7, 6, 0}, {7, 7, 0}, {6, 7, 0}}});
        doc.extrude(caster, 5, 1);
        style.groundVisible = true;
        doc.setStyle(style);
        view.standardView(1);
        view.frameBounds({-10, -8, 0}, {10, 10, 4});
        settled(view);
        const Vec3 top{1, 3, 3};
        const auto direction = solarPosition(doc.solar()).direction;
        const auto probe = top - direction * (top.z / direction.z);
        const auto withShadows = sample(view, probe);
        auto noShadows = doc.solar();
        noShadows.shadows = false;
        doc.setSolar(noShadows);
        settled(view);
        const auto withoutShadows = sample(view, probe);
        check(std::abs(withShadows.lightness() - withoutShadows.lightness()) < 4,
              "Reference plane does not cast shadows alongside modeled geometry");
        const auto component = createComponent(doc, caster, "Reference import scope");
        placeComponent(doc, component.definition, Transform::translation({20, 0, 0}));
        settled(view);
        view.enterContext(component.instance);
        const auto assetCount = doc.assets().size(), bodyCount = doc.bodies().size();
        const auto beforeImport = doc.history().total;
        view.importReferenceImage(png, "image/png", "Shared plan", 2, 2);
        check(doc.assets().size() == assetCount + 1 && doc.bodies().size() == bodyCount + 2 &&
                  doc.history().total == beforeImport + 1 && view.selectedBody() &&
                  doc.bodies().at(view.selectedBody())->referenceImage,
              "Native component import shares one asset and atomically adds an image to both "
              "placements");
        doc.undo();
        settled(view);
        check(doc.assets().size() == assetCount && doc.bodies().size() == bodyCount,
              "Component image import Undo restores all placements and asset storage");
        view.leaveContext();
        check(view.renderStats().glError == 0, "Reference drawing leaves no GL errors");
        std::cout << "Reference image native display, picking, import, calibration and persistence "
                     "passed\n";
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
