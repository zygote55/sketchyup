#include "app/window.hpp"
#include "core/assets.hpp"
#include "core/materials.hpp"
#include "core/scenes.hpp"
#include "core/sections.hpp"
#include "io/document_io.hpp"
#include <QAction>
#include <QApplication>
#include <QBuffer>
#include <QCheckBox>
#include <QDateEdit>
#include <QDialog>
#include <QDialogButtonBox>
#include <QLineEdit>
#include <QPushButton>
#include <QSurfaceFormat>
#include <QTemporaryDir>
#include <QTest>
#include <QTimeEdit>
#include <QTimer>
#include <iostream>
using namespace sketchy;
namespace {
void check(bool value, const char *message) {
    if (!value)
        throw std::runtime_error(message);
}
void sync(Window &window) {
    window.viewport()->refresh();
    QMetaObject::invokeMethod(window.viewport(), "changed");
    QCoreApplication::processEvents();
    window.viewport()->repaint();
}
void save(QDialog *dialog) {
    dialog->findChild<QDialogButtonBox *>()->button(QDialogButtonBox::Save)->click();
}
void modal(Window &window, const std::function<void(QDialog *)> &operation) {
    bool opened{};
    std::exception_ptr failure;
    QTimer timer;
    timer.setInterval(10);
    QObject::connect(&timer, &QTimer::timeout, &window, [&] {
        auto *dialog = window.findChild<QDialog *>("solarDialog");
        if (!dialog || !dialog->isVisible())
            return;
        timer.stop();
        opened = true;
        dialog->activateWindow();
        try {
            operation(dialog);
        } catch (...) {
            failure = std::current_exception();
        }
        if (dialog->isVisible())
            dialog->reject();
    });
    timer.start();
    window.findChild<QPushButton *>("solarEditButton")->click();
    check(opened, "Native sun editor opens");
    if (failure)
        std::rethrow_exception(failure);
}
QColor sample(Viewport &view, Vec3 point) {
    QCoreApplication::processEvents();
    view.repaint();
    const auto image = view.grabFramebuffer();
    check(!image.isNull() && view.rendererReady() && view.renderStats().glError == 0,
          "Solar framebuffer available without GL errors");
    const auto p = view.project(point);
    const QPoint pixel(qRound(p.x() * image.width() / view.width()),
                       qRound(p.y() * image.height() / view.height()));
    check(image.rect().contains(pixel), "Solar probe in viewport");
    return image.pixelColor(pixel);
}
} // namespace
int main(int argc, char **argv) {
    QSurfaceFormat format;
    format.setVersion(3, 3);
    format.setProfile(QSurfaceFormat::CoreProfile);
    format.setDepthBufferSize(24);
    QSurfaceFormat::setDefaultFormat(format);
    QTemporaryDir isolated;
    qputenv("XDG_CONFIG_HOME", isolated.path().toUtf8());
    QApplication app(argc, argv);
    try {
        Window window;
        window.resize(1280, 950);
        auto &doc = window.document();
        auto &view = *window.viewport();
        const auto body = doc.addFace({{{0, 0, 0}, {2, 0, 0}, {2, 3, 0}, {0, 3, 0}}});
        doc.extrude(body, 5, 3);
        auto style = doc.style();
        style.groundVisible = true;
        style.axesVisible = style.gridVisible = style.edgesVisible = false;
        doc.setStyle(style);
        doc.markSaved();
        window.show();
        check(QTest::qWaitForWindowExposed(&window), "Sun study window exposed");
        auto *action = window.findChild<QAction *>("view.solar");
        check(action, "View menu exposes sun study");
        action->trigger();
        check(window.findChild<QWidget *>("solarPanel")->isVisible(), "Sun panel opens");
        const auto history = doc.history().total;
        modal(window, [&](QDialog *dialog) {
            dialog->findChild<QCheckBox *>("solarEnabled")->setChecked(true);
            dialog->findChild<QLineEdit *>("solarLatitude")->setText("91");
            save(dialog);
            check(dialog->isVisible() && !doc.solar().enabled, "Invalid latitude does not publish");
            dialog->findChild<QLineEdit *>("solarLatitude")->setText("40");
            dialog->findChild<QLineEdit *>("solarLongitude")->setText("-105");
            dialog->findChild<QDateEdit *>("solarDate")->setDate(QDate(2010, 6, 21));
            dialog->findChild<QTimeEdit *>("solarTime")->setTime(QTime(8, 0));
            dialog->findChild<QLineEdit *>("solarOffset")->setText("-07:90");
            save(dialog);
            check(dialog->isVisible(), "Invalid UTC offset rejected");
            dialog->findChild<QLineEdit *>("solarOffset")->setText("-07:00");
            save(dialog);
            check(!dialog->isVisible(), "Explicit sun study saves");
        });
        check(doc.solar().enabled && doc.solar().time.utcOffsetMinutes == -420 &&
                  doc.history().total == history + 1,
              "Sun editor publishes one exact history edit");
        const auto settings = doc.solar();
        modal(window, [&](QDialog *dialog) { save(dialog); });
        check(doc.history().total == history + 1, "Unchanged editor has no history");
        doc.undo();
        check(!doc.solar().enabled, "Sun editor Undo");
        doc.redo();
        sync(window);
        check(view.captureSceneSnapshot(false, false, false, false, true).solar == settings,
              "Native scene capture opts into solar alone");
        auto precise = settings;
        precise.latitude = 40.12345678912345;
        view.applyModelSolar(precise);
        const auto preciseRevision = doc.revision();
        modal(window, [&](QDialog *dialog) { save(dialog); });
        check(doc.solar() == precise && doc.revision() == preciseRevision,
              "Untouched sun editor preserves exact coordinate precision");
        view.applyModelSolar(settings);
        modal(window, [&](QDialog *dialog) {
            dialog->findChild<QLineEdit *>("solarOffset")->setText("+05:45");
            save(dialog);
        });
        check(doc.solar().time.utcOffsetMinutes == 345,
              "Native UTC input accepts quarter-hour offsets");
        doc.undo();
        sync(window);
        view.standardView(1);
        view.frameBounds({-9, -8, 0}, {9, 9, 4});
        sync(window);
        const auto sun = solarPosition(settings);
        check(sun.aboveHorizon, "Morning fixture has sun");
        const Vec3 top{1, 1.5, 3};
        const auto probe = top - sun.direction * (top.z / sun.direction.z);
        const auto shadow = sample(view, probe);
        auto unshadowed = settings;
        unshadowed.shadows = false;
        view.applyModelSolar(unshadowed);
        const auto lit = sample(view, probe);
        std::cout << "Ground probe " << probe.x << "," << probe.y
                  << " shadow=" << shadow.lightness() << " lit=" << lit.lightness() << '\n';
        check(lit.lightness() > shadow.lightness() + 25,
              "Sun casts a visible ground shadow at geometric projection");
        check(view.pick(view.project(probe)).first == 0 && !view.selectionAt(view.project(probe)),
              "Shadow plane does not become pickable geometry");
        view.applyModelSolar(settings);
        view.setBodyOpacity(body, .25f);
        check(sample(view, probe).lightness() > shadow.lightness() + 25,
              "Translucent fragments below the shadow cutoff do not cast opaque shadows");
        view.setBodyOpacity(body, 1.f);
        QImage cutout(4, 4, QImage::Format_RGBA8888);
        cutout.fill(QColor(255, 255, 255, 0));
        QByteArray imageBytes;
        QBuffer imageBuffer(&imageBytes);
        imageBuffer.open(QIODevice::WriteOnly);
        check(cutout.save(&imageBuffer, "PNG"), "Shadow cutout fixture encodes");
        const auto asset = createAsset(doc, "Transparent shadow fixture", "image/png",
                                       std::make_shared<AssetPayload>(std::vector<std::uint8_t>(
                                           imageBytes.begin(), imageBytes.end())));
        const auto material =
            createMaterial(doc, "Transparent shadow fixture", {1, 1, 1}, 1, asset);
        assignMaterial(doc, body, {}, material, true, true);
        sync(window);
        sample(view, probe);
        check(QTest::qWaitFor([&] { return !view.texturesPending(); }, 10000),
              "Shadow texture worker settles");
        check(sample(view, probe).lightness() > shadow.lightness() + 25,
              "Texture alpha cutouts do not cast solid shadows");
        doc.undo();
        doc.undo();
        doc.undo();
        sync(window);
        view.setClipPlane(std::array<double, 4>{0, 0, -1, 1});
        check(sample(view, probe).lightness() > shadow.lightness() + 25,
              "Free clipping removes the clipped-away shadow caster");
        view.setClipPlane({});
        const auto section = createSection(doc, "Solar cut", 0, {{0, 0, -1}, 1});
        setActiveSection(doc, 0, section);
        sync(window);
        check(sample(view, probe).lightness() > shadow.lightness() + 25,
              "Named section caps and clipped geometry produce the shorter shadow");
        setActiveSection(doc, 0, {});
        sync(window);
        const auto beforeHidden = doc.bodies().at(body);
        auto hidden = std::make_shared<Body>(*beforeHidden);
        hidden->hidden = true;
        doc.apply({"Hide shadow caster", {{body, beforeHidden, hidden}}}, doc.revision());
        sync(window);
        check(sample(view, probe).lightness() > shadow.lightness() + 25,
              "Hidden bodies do not cast shadows");
        doc.undo();
        sync(window);
        auto rotated = settings;
        rotated.northDegrees = 90;
        view.applyModelSolar(rotated);
        const auto rotatedSun = solarPosition(rotated);
        const auto rotatedProbe = top - rotatedSun.direction * (top.z / rotatedSun.direction.z);
        const auto rotatedShadow = sample(view, rotatedProbe);
        rotated.shadows = false;
        view.applyModelSolar(rotated);
        check(sample(view, rotatedProbe).lightness() > rotatedShadow.lightness() + 25,
              "Model north rotates the projected shadow");
        view.applyModelSolar(settings);
        const Vec3 shift{250000, 250000, 0};
        doc.transform(body, Transform::translation(shift));
        view.frameBounds(Vec3{-9, -8, 0} + shift, Vec3{9, 9, 4} + shift);
        sync(window);
        const auto distantShadow = sample(view, probe + shift);
        view.applyModelSolar(unshadowed);
        check(sample(view, probe + shift).lightness() > distantShadow.lightness() + 25,
              "Camera-relative shadows survive distant placement");
        doc.undo();
        doc.undo();
        view.frameBounds({-9, -8, 0}, {9, 9, 4});
        sync(window);
        auto night = settings;
        night.time.hour = 0;
        view.applyModelSolar(night);
        const auto nightShadow = sample(view, probe);
        night.shadows = false;
        view.applyModelSolar(night);
        const auto nightClear = sample(view, probe);
        check(std::abs(nightShadow.lightness() - nightClear.lightness()) <= 2,
              "Night disables directional shadowing");
        view.applyModelSolar(settings);
        const auto evidence = qEnvironmentVariable("SKETCHYUP_SOLAR_EVIDENCE");
        if (!evidence.isEmpty()) {
            view.standardView(0);
            view.frameBounds({-5, -1, 0}, {3, 4, 4});
            sync(window);
            check(view.grabFramebuffer().save(evidence), "Solar viewport evidence saved");
        }
        const auto editorEvidence = qEnvironmentVariable("SKETCHYUP_SOLAR_EDITOR_EVIDENCE");
        if (!editorEvidence.isEmpty())
            modal(window, [&](QDialog *dialog) {
                check(dialog->grab().save(editorEvidence), "Native sun editor evidence saved");
            });
        const auto path = isolated.filePath("sun.sketchyup");
        saveDocument(doc, path);
        check(loadDocument(path).solar() == settings, "Native solar file reopens exactly");
        modal(window, [&](QDialog *dialog) {
            doc.setDisplayUnits(DisplayUnit::Millimeters);
            save(dialog);
            check(dialog->isVisible() && doc.solar() == settings,
                  "Stale editor cannot overwrite current model");
        });
        doc.markSaved();
        window.close();
        std::cout << "Native sun controls, history, scene capture, lighting, shadows and picking "
                     "passed\n";
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
