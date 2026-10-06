#include "app/viewport.hpp"
#include "core/assets.hpp"
#include "core/components.hpp"
#include "core/face_textures.hpp"
#include "core/materials.hpp"
#include "io/document_io.hpp"
#include <QApplication>
#include <QBuffer>
#include <QElapsedTimer>
#include <QSurfaceFormat>
#include <QTest>
#include <QVBoxLayout>
#include <iostream>
using namespace sketchy;
namespace {
void check(bool value, const char *message) {
    if (!value)
        throw std::runtime_error(message);
}
AssetPayloadPtr pixels(const std::array<QColor, 4> &colors) {
    QImage image(8, 8, QImage::Format_RGBA8888);
    for (int y = 0; y < 8; ++y)
        for (int x = 0; x < 8; ++x)
            image.setPixelColor(x, y, colors[(y / 4) * 2 + x / 4]);
    QByteArray bytes;
    QBuffer buffer(&bytes);
    buffer.open(QIODevice::WriteOnly);
    check(image.save(&buffer, "PNG"), "Native texture fixture encoded");
    return std::make_shared<AssetPayload>(std::vector<std::uint8_t>(bytes.begin(), bytes.end()));
}
QImage frame(Viewport &view) {
    QCoreApplication::processEvents();
    auto image = view.grabFramebuffer();
    check(!image.isNull() && view.rendererReady(), "Textured framebuffer available");
    return image;
}
void settled(Viewport &view) {
    frame(view);
    QElapsedTimer timer;
    timer.start();
    while (view.texturesPending() && timer.elapsed() < 10000)
        QTest::qWait(10);
    check(!view.texturesPending(), "Image decoding finishes without blocking native events");
    // The timer queues the appearance rebuild when a complete cache is published.
    QTest::qWait(40);
    frame(view);
}
QColor sample(Viewport &view, Vec3 point) {
    const auto image = frame(view);
    const auto p = view.project(point);
    const QPoint pixel(qRound(p.x() * image.width() / view.width()),
                       qRound(p.y() * image.height() / view.height()));
    check(image.rect().contains(pixel), "Texture probe lies in framebuffer");
    return image.pixelColor(pixel);
}
void near(QColor a, QColor b, const char *message, int error = 6) {
    if (std::abs(a.red() - b.red()) > error || std::abs(a.green() - b.green()) > error ||
        std::abs(a.blue() - b.blue()) > error) {
        std::cerr << message << ": " << a.name().toStdString() << " / " << b.name().toStdString()
                  << '\n';
        throw std::runtime_error(message);
    }
}
void picks(Viewport &view, Vec3 point, Id body, const char *message) {
    check(view.pick(view.project(point)).first == body, message);
    const auto selected = view.selectionAt(view.project(point));
    check(selected && selected->body == body, message);
}
} // namespace
int main(int argc, char **argv) {
    QSurfaceFormat format;
    format.setVersion(3, 3);
    format.setProfile(QSurfaceFormat::CoreProfile);
    format.setDepthBufferSize(24);
    format.setSamples(0);
    QSurfaceFormat::setDefaultFormat(format);
    QApplication app(argc, argv);
    try {
        Document doc;
        const auto plane = [&](double z) {
            return doc.addFace({{{0, 0, z}, {1, 0, z}, {1, 1, z}, {0, 1, z}}});
        };
        const auto body = plane(1), behind = plane(.5);
        const auto face = doc.bodies().at(body)->surface.faces.begin()->first;
        doc.paint(behind, {1, 1, 0});
        const auto source = pixels({Qt::red, QColor(0, 255, 0, 0), Qt::blue, Qt::white});
        const auto image = createAsset(doc, "Front checker", "image/png", source);
        const auto backImage = createAsset(doc, "Back checker", "image/png",
                                           pixels({Qt::magenta, Qt::cyan, Qt::yellow, Qt::white}));
        const auto front = createMaterial(doc, "Front", {1, 1, 1}, 1, image);
        const auto back = createMaterial(doc, "Back", {1, 1, 1}, 1, backImage);
        assignMaterial(doc, body, {}, front, true, false);
        assignMaterial(doc, body, {}, back, false, true);
        QWidget host, second;
        second.resize(800, 680);
        QVBoxLayout secondLayout(&second);
        host.resize(800, 680);
        QVBoxLayout layout(&host);
        auto *view = new Viewport(doc);
        layout.addWidget(view);
        host.show();
        check(QTest::qWaitForWindowExposed(&host), "Texture viewport exposed");
        view->standardView(1);
        view->fit();
        settled(*view);
        check(view->textureSummary().isEmpty(), "Both images loaded successfully");
        const Vec3 redPoint{.25, .25, 1}, hole{.75, .25, 1}, bluePoint{.25, .75, 1};
        const auto red = sample(*view, redPoint), blue = sample(*view, bluePoint);
        check(red.red() > 200 && red.green() < 8 && red.blue() < 8,
              "Top-left UV and sRGB red render in native viewport");
        check(blue.blue() > 200 && blue.red() < 8 && blue.green() < 8,
              "+V samples image rows downward");
        const auto holeColor = sample(*view, hole);
        check(holeColor.red() > 200 && holeColor.green() > 200 && holeColor.blue() < 8,
              "Zero image alpha reveals backing material");
        picks(*view, hole, behind, "Image hole passes both CPU and GPU picking through");
        picks(*view, redPoint, body, "Opaque image pixels remain pickable");
        // The probe is not generally centered on a device pixel. Recover that
        // pixel's world coordinates to compare the four-texel filtering oracle.
        const auto filteredFrame = frame(*view);
        const Vec3 middle{.5, .5, 1};
        const auto projected = view->project(middle);
        const double ratio = double(filteredFrame.width()) / view->width();
        const QPoint device(qRound(projected.x() * ratio), qRound(projected.y() * ratio));
        const auto xUnit = view->project(middle + Vec3{1, 0, 0}) - projected;
        const auto yUnit = view->project(middle + Vec3{0, 1, 0}) - projected;
        const TextureCoordinate pixelUv{
            .5 + ((device.x() + .5) / ratio - projected.x()) / xUnit.x(),
            .5 + ((device.y() + .5) / ratio - projected.y()) / yUnit.y()};
        const auto decoded = decodeTextureImage(*doc.assets().at(image)).image;
        const auto linearSample = decoded->sampleLinear(pixelUv);
        const double previewLight =
            .64 + .36 * std::abs(dot(normalized({.3, -.5, .8}), Vec3{0, 0, 1}));
        auto filteredChannel = [&](int channel, int background) {
            const auto c = linearSample[channel];
            const auto srgb = c <= .0031308 ? 12.92 * c : 1.055 * std::pow(c, 1 / 2.4) - .055;
            return qRound(srgb * previewLight * 255 * linearSample[3] +
                          background * (1 - linearSample[3]));
        };
        near(filteredFrame.pixelColor(device),
             QColor(filteredChannel(0, holeColor.red()), filteredChannel(1, holeColor.green()),
                    filteredChannel(2, holeColor.blue())),
             "Native bilinear filtering decodes RGB before interpolation and keeps straight alpha");
        const auto before = view->renderStats();
        replaceAsset(doc, image, pixels({Qt::green, Qt::green, Qt::green, Qt::green}));
        // A changed asset can never use the previous payload's transparent hole.
        check(view->pick(view->project(hole)).first == body,
              "Pending replacement uses color preview");
        settled(*view);
        const auto changed = sample(*view, redPoint);
        check(changed.green() > 200 && changed.red() < 8 &&
                  view->renderStats().bodyMeshBuilds == before.bodyMeshBuilds,
              "Image replacement refreshes appearance without retriangulation");
        picks(*view, hole, body, "Replacement removes the old picking hole");
        doc.undo();
        settled(*view);
        near(sample(*view, redPoint), red, "Undo restores original image");
        picks(*view, hole, behind, "Undo restores image alpha picking");
        doc.redo();
        settled(*view);
        near(sample(*view, redPoint), changed, "Redo restores replacement pixels");
        doc.undo();
        settled(*view);
        replaceAsset(doc, image,
                     pixels({QColor(255, 0, 0, 128), QColor(255, 0, 0, 128), QColor(255, 0, 0, 128),
                             QColor(255, 0, 0, 128)}));
        editMaterial(doc, front, {}, {}, .5f);
        settled(*view);
        const double alpha = .5 * 128 / 255;
        near(sample(*view, redPoint),
             QColor(qRound(red.red() * alpha + holeColor.red() * (1 - alpha)),
                    qRound(holeColor.green() * (1 - alpha)), 0),
             "Image alpha multiplies material opacity exactly once");
        picks(*view, redPoint, body, "Fractional image coverage remains pickable");
        editMaterial(doc, front, {}, {}, 1.f);
        replaceAsset(doc, image, source);
        settled(*view);
        const auto generation = view->renderStats().contextGeneration;
        layout.removeWidget(view);
        view->setParent(&second);
        secondLayout.addWidget(view);
        second.show();
        view->show();
        check(QTest::qWaitForWindowExposed(&second), "Reparented textured viewport exposed");
        settled(*view);
        check(view->renderStats().contextGeneration > generation, "Texture GL context recreated");
        near(sample(*view, redPoint), red, "Texture pixels survive GL context recreation");
        picks(*view, hole, behind, "Image alpha picking survives GL context recreation");
        view->setClipPlane(std::array<double, 4>{0, 0, -1, .7});
        settled(*view);
        picks(*view, redPoint, behind, "Clipping applies before textured CPU and GPU picking");
        view->setClipPlane({});
        TextureMapping mapping;
        mapping.offset = {-.5, 0};
        assignTextureMapping(doc, body, face, mapping, true, false);
        settled(*view);
        picks(*view, redPoint, behind, "Negative repeat offset preserves alpha addressing");
        mapping.uGradient = {1e8, 0, 0};
        mapping.offset = {};
        assignTextureMapping(doc, body, face, mapping, true, false);
        settled(*view);
        check(!view->textureSummary().isEmpty(), "Unrepresentable UV span reports color preview");
        picks(*view, hole, body, "UV precision fallback agrees in drawing and both pickers");
        mapping = TextureMapping{};
        mapping.offset = {999999999.0, -999999999.0};
        assignTextureMapping(doc, body, face, mapping, true, false);
        settled(*view);
        near(sample(*view, redPoint), red, "Common billion-repeat offsets preserve native phase");
        mapping.uGradient = {-1, 0, 0};
        mapping.offset = {1, 0};
        assignTextureMapping(doc, body, face, mapping, false, true);
        doc.move(behind, {0, 0, 1});
        view->standardView(6);
        settled(*view);
        const auto cyan = sample(*view, redPoint);
        check(cyan.green() > 200 && cyan.blue() > 200 && cyan.red() < 8,
              "Independent back projection and back image render");
        const auto component = createComponent(doc, body, "Textured panel");
        const auto mirrored =
            placeComponent(doc, component.definition,
                           Transform::translation({2.5, 0, 0}) * Transform::scaling({-1, 1.5, 1}));
        const auto member = component.movedGeometry.at(body);
        const auto mirrorMember = doc.instances().at(mirrored.instance)->members.at(member);
        view->fit();
        settled(*view);
        near(sample(*view, doc.worldTransform(member).point(redPoint)),
             sample(*view, doc.worldTransform(mirrorMember).point(redPoint)),
             "Reflected nonuniform instance keeps physical back image and mapping");
        view->standardView(1);
        doc.erase(behind);
        settled(*view);
        const auto first = sample(*view, doc.worldTransform(member).point(redPoint));
        near(first, sample(*view, doc.worldTransform(mirrorMember).point(redPoint)),
             "Reflected instance keeps physical front image");
        const auto saved = encodeContainer(doc);
        doc = decodeContainer(saved);
        settled(*view);
        near(first, sample(*view, doc.worldTransform(member).point(redPoint)),
             "Relocated native container restores textures");
        check(encodeContainer(doc) == saved, "Viewport texture decoding is read-only");
        doc.move(component.instance, {999990, -999990, 200});
        doc.move(mirrored.instance, {999990, -999990, 200});
        view->fit();
        settled(*view);
        near(first, sample(*view, doc.worldTransform(member).point(redPoint)),
             "Site translation preserves body-local image phase across camera rebasing");
        check(view->pick(view->project(doc.worldTransform(member).point(hole))).first == 0,
              "Far-site image hole passes CPU picking through");
        check(!view->selectionAt(view->project(doc.worldTransform(member).point(hole))),
              "Far-site image hole passes GPU picking through");
        // Tint multiplication must happen in linear RGB before preview lighting.
        replaceAsset(doc, image,
                     pixels({QColor(128, 128, 128), QColor(128, 128, 128), QColor(128, 128, 128),
                             QColor(128, 128, 128)}));
        editMaterial(doc, front, {}, std::array<float, 3>{.5f, .5f, .5f}, {});
        settled(*view);
        const auto gray = sample(*view, doc.worldTransform(member).point(redPoint));
        const double light = .64 + .36 * std::abs(dot(normalized({.3, -.5, .8}), Vec3{0, 0, 1}));
        const double linear =
            std::pow((.5 + .055) / 1.055, 2.4) * std::pow((128. / 255 + .055) / 1.055, 2.4);
        const int expected = qRound((1.055 * std::pow(linear, 1 / 2.4) - .055) * light * 255);
        near(gray, QColor(expected, expected, expected),
             "Linear image/tint product has analytic oracle");
        replaceAsset(doc, image, source, "image/svg+xml");
        settled(*view);
        check(!view->textureSummary().isEmpty(), "Unsupported image exposes color-preview status");
        check(view->rendererReady() && view->renderStats().glError == 0,
              "Textures, fallback and selection do not generate GL errors");
        if (const auto path = qEnvironmentVariable("SKETCHYUP_TEXTURE_EVIDENCE"); !path.isEmpty()) {
            doc.undo();
            replaceAsset(doc, image, source, "image/png");
            editMaterial(doc, front, {}, std::array<float, 3>{1, 1, 1}, {});
            settled(*view);
            check(view->grabFramebuffer().save(path), "Native texture screenshot saved");
        }
        std::cout << "Native textures, independent sides, alpha picking, replacement, history, "
                     "reflection, tint and relocation passed; DPR="
                  << view->devicePixelRatioF() << '\n';
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
