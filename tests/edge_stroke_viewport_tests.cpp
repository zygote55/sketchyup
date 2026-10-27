// Viewport tests for the edge stroke (R100.f1, UX_DESIGN §18.3): the dash geometry, the opacity,
// the texture tiling and clipping, the hatch for a missing texture, and a stroke that equals the
// profile width drawing exactly as the profile does. They read the grabbed framebuffer, so they
// need a GL context. The native CI jobs run them on X11 and Wayland at 1x and 2x.
#include "app/surface_format.hpp"
#include "app/viewport.hpp"
#include "core/assets.hpp"
#include "core/model_style.hpp"
#include <QApplication>
#include <QBuffer>
#include <QElapsedTimer>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTest>
#include <QVBoxLayout>
#include <cmath>
#include <iostream>
#include <limits>
#include <memory>
#include <optional>
#include <vector>
using namespace sketchy;
namespace {
void check(bool value, const char *message) {
    if (!value)
        throw std::runtime_error(message);
}
// The square's edges are 2 metres long; the view is orthographic and top-down, so every edge has
// the same screen length, and the bottom edge runs horizontally on screen.
struct Square {
    QPointF from, to;
    double length{};
    QPointF unit;
};
// A document with one square face and a viewport over it, sized so the square fills the view.
struct Scene {
    Document doc;
    QWidget host;
    QVBoxLayout layout{&host};
    Viewport *view{new Viewport(doc)};
    Scene() {
        layout.addWidget(view);
        host.resize(800, 680);
        doc.addFace({{{0, 0, 0}, {2, 0, 0}, {2, 2, 0}, {0, 2, 0}}});
        auto style = doc.style();
        // Monochrome white faces on a white background: the stroke is the only dark thing drawn.
        // Wireframe would also draw the model's own edge lines, which lie on the stroke's centre.
        style.mode = ModelStyleMode::Monochrome;
        style.front = style.back = {1, 1, 1};
        style.background = {1, 1, 1};
        style.edge = {0, 0, 0};
        style.gridVisible = style.axesVisible = style.groundVisible = style.edgesVisible = false;
        style.profiles = true;
        style.profileWidth = 4;
        doc.setStyle(style);
        doc.markSaved();
        host.show();
        check(QTest::qWaitForWindowExposed(&host), "Edge stroke viewport exposed");
        view->standardView(1);
        view->setOrthographic(true);
        view->fit();
    }
};
QImage frame(Viewport &view) {
    QCoreApplication::processEvents();
    auto image = view.grabFramebuffer();
    check(!image.isNull() && view.rendererReady() && view.renderStats().glError == 0,
          "Edge stroke framebuffer is valid");
    return image;
}
// Waits for the edge texture to decode, then draws the settled frame.
QImage settle(Viewport &view) {
    frame(view);
    QElapsedTimer timer;
    timer.start();
    while (view.texturesPending() && timer.elapsed() < 10000)
        QTest::qWait(10);
    check(!view.texturesPending(), "Edge texture decodes");
    QTest::qWait(40);
    return frame(view);
}
Square bottomEdge(const Viewport &view) {
    Square square;
    square.from = view.project({0, 0, 0});
    square.to = view.project({2, 0, 0});
    const double dx = square.to.x() - square.from.x(), dy = square.to.y() - square.from.y();
    square.length = std::hypot(dx, dy);
    square.unit = {dx / square.length, dy / square.length};
    return square;
}
// The colour under a logical point of the bottom edge, `along` logical pixels from its start.
QColor sampleAlong(const QImage &image, const Viewport &view, const Square &square, double along) {
    const double ratio = double(image.width()) / view.width();
    const QPointF p = square.from + square.unit * along;
    const QPoint pixel(qRound(p.x() * ratio), qRound(p.y() * ratio));
    check(image.rect().contains(pixel), "Sample lies inside the framebuffer");
    return image.pixelColor(pixel);
}
bool near(QColor color, int r, int g, int b, int error) {
    return std::abs(color.red() - r) <= error && std::abs(color.green() - g) <= error &&
           std::abs(color.blue() - b) <= error;
}
bool dark(QColor color) { return color.red() < 60 && color.green() < 60 && color.blue() < 60; }
// Samples the bottom edge every half logical pixel, away from the corners where the vertical
// edges meet it.
template <class F>
void alongEdge(const QImage &image, const Viewport &view, const Square &square, F visit) {
    for (double along = 6; along <= square.length - 6; along += .5)
        visit(along, sampleAlong(image, view, square, along));
}
ModelStyle edgeStyle(const Document &doc, std::optional<EdgeStroke> stroke) {
    auto style = doc.style();
    style.edgeStroke = stroke;
    return style;
}
// The 8 by 8 texture: on the left, red above dark green; on the right, blue above yellow. A stroke
// 4 logical pixels wide shows the top four rows only, and an 8 pixel tile repeats along the edge.
QByteArray tileTexture() {
    QImage source(8, 8, QImage::Format_RGBA8888);
    for (int y = 0; y < 8; ++y)
        for (int x = 0; x < 8; ++x) {
            QColor color;
            if (x < 4)
                color = y < 4 ? QColor(255, 0, 0) : QColor(0, 128, 0);
            else
                color = y < 4 ? QColor(0, 0, 255) : QColor(255, 255, 0);
            source.setPixelColor(x, y, color);
        }
    QByteArray bytes;
    QBuffer buffer(&bytes);
    buffer.open(QIODevice::WriteOnly);
    check(source.save(&buffer, "PNG"), "Texture fixture encoded");
    return bytes;
}
} // namespace
int main(int argc, char **argv) {
    QSurfaceFormat format;
    format.setVersion(3, 3);
    format.setProfile(QSurfaceFormat::CoreProfile);
    format.setDepthBufferSize(24);
    format.setSamples(0);
    sketchy::setDefaultViewportFormat(format);
    QApplication app(argc, argv);
    try {
        QJsonObject result{{"passed", true}, {"platform", QGuiApplication::platformName()}};
        // 1. A stroke that equals the profile width (opaque, solid, untextured) draws exactly as
        // the profile does, and the stroke off draws one quad per profile.
        {
            Scene scene;
            auto &doc = scene.doc;
            auto *view = scene.view;
            result["scale"] = view->devicePixelRatioF();
            doc.setStyle(edgeStyle(doc, std::nullopt));
            const auto off = settle(*view);
            const auto offStats = view->renderStats();
            check(offStats.profileEdges == 4, "The square has four profiles");
            check(offStats.profileSegments == offStats.profileEdges,
                  "A solid profile is one quad, as before");
            doc.setStyle(edgeStyle(doc, EdgeStroke{4, 1, EdgeDash::Solid, 0}));
            check(settle(*view) == off,
                  "An opaque solid stroke equal to Profile width draws the same pixels");
            check(view->renderStats().profileSegments == offStats.profileSegments,
                  "The same stroke makes the same quads");
            check(!view->edgeTextureMissing(), "No texture is not a missing texture");
        }
        // 2. Opacity blends the stroke over the faces: half opacity of black over white is grey.
        {
            Scene scene;
            auto &doc = scene.doc;
            auto *view = scene.view;
            doc.setStyle(edgeStyle(doc, EdgeStroke{4, .5, EdgeDash::Solid, 0}));
            const auto image = settle(*view);
            const auto square = bottomEdge(*view);
            const auto middle = sampleAlong(image, *view, square, square.length / 2);
            check(near(middle, 128, 128, 128, 8),
                  "Half opacity blends the edge colour over the faces");
            doc.setStyle(edgeStyle(doc, EdgeStroke{4, 1, EdgeDash::Solid, 0}));
            check(near(sampleAlong(settle(*view), *view, square, square.length / 2), 0, 0, 0, 8),
                  "Full opacity draws the edge colour");
        }
        // 3. Dash geometry: the runs follow the proposed lengths, in logical pixels, at any scale.
        {
            Scene scene;
            auto &doc = scene.doc;
            auto *view = scene.view;
            doc.setStyle(edgeStyle(doc, EdgeStroke{4, 1, EdgeDash::Dashed, 0}));
            const auto image = settle(*view);
            const auto square = bottomEdge(*view);
            const auto runs = edgeDashRuns(square.length, EdgeDash::Dashed);
            check(view->renderStats().profileSegments == 4 * runs.size(),
                  "Each dash run is one quad on every edge");
            // Each sample is on a dash when it lies in a run. Samples near a run's ends are
            // skipped, because the edge's anti-aliasing and the pixel grid move them by about a
            // pixel. The pattern starts at the edge's own start, which the topology sets, so it may
            // run from either corner: one of the two readings must match every sample.
            int bestMismatches = std::numeric_limits<int>::max(), bestSamples = 0;
            for (const bool reversed : {false, true}) {
                int samples = 0, mismatches = 0;
                alongEdge(image, *view, square, [&](double along, QColor color) {
                    const double s = reversed ? square.length - along : along;
                    bool onDash = false, nearEnd = false;
                    for (const auto &[from, to] : runs) {
                        onDash |= s >= from && s <= to;
                        nearEnd |= std::abs(s - from) < 1.5 || std::abs(s - to) < 1.5;
                    }
                    if (nearEnd)
                        return;
                    ++samples;
                    mismatches += dark(color) != onDash;
                });
                if (mismatches < bestMismatches) {
                    bestMismatches = mismatches;
                    bestSamples = samples;
                }
            }
            check(bestSamples > 200, "The dashed edge has enough samples");
            check(bestMismatches == 0, "Dashes land where the proposed lengths put them");
            // Dotted and dash-dot use the same geometry, so one more pattern at this scale.
            doc.setStyle(edgeStyle(doc, EdgeStroke{4, 1, EdgeDash::DashDot, 0}));
            check(settle(*view) != image, "Dash-dot draws a different pattern");
            check(view->renderStats().profileSegments ==
                      4 * edgeDashRuns(square.length, EdgeDash::DashDot).size(),
                  "Dash-dot runs are one quad each");
        }
        // 4. Texture tiling: the texture repeats every its own width in logical pixels along the
        // edge, and the stroke shows only the top rows it covers (the four rows of its width).
        {
            Scene scene;
            auto &doc = scene.doc;
            auto *view = scene.view;
            const auto bytes = tileTexture();
            const auto texture =
                createAsset(doc, "Weave", "image/png",
                            std::make_shared<AssetPayload>(
                                std::vector<std::uint8_t>(bytes.begin(), bytes.end())));
            doc.setStyle(edgeStyle(doc, EdgeStroke{4, 1, EdgeDash::Solid, texture}));
            const auto image = settle(*view);
            check(!view->edgeTextureMissing(), "A present texture is not missing");
            check(view->renderStats().profileSegments == 4,
                  "A textured stroke is one quad per edge");
            const auto square = bottomEdge(*view);
            int red = 0, blue = 0, green = 0, yellow = 0, samples = 0, repeats = 0, compared = 0;
            std::vector<int> classes;
            alongEdge(image, *view, square, [&](double, QColor color) {
                ++samples;
                const bool isRed = color.red() > 200 && color.green() < 60 && color.blue() < 60;
                const bool isBlue = color.blue() > 200 && color.red() < 60 && color.green() < 60;
                green += color.green() > 100 && color.green() < 200 && color.red() < 60 &&
                         color.blue() < 60;
                yellow += color.red() > 200 && color.green() > 200 && color.blue() < 60;
                red += isRed;
                blue += isBlue;
                classes.push_back(isRed ? 1 : isBlue ? 2 : 0);
            });
            check(green == 0 && yellow == 0,
                  "Rows below the stroke's width never show the texture");
            // Filtering blends one pixel at each colour change, so those samples are neither
            // colour; the pure ones are most of the edge.
            check(red > samples / 5 && blue > samples / 5 && red + blue > samples * 6 / 10,
                  "The texture's left and right halves both draw");
            // The period is the texture's own width, 8 logical pixels: a pure sample and the one 8
            // pixels further on agree.
            const int shift = 16; // 8 logical pixels at half-pixel sampling
            for (size_t i = 0; i + shift < classes.size(); ++i) {
                if (classes[i] == 0 || classes[i + shift] == 0)
                    continue;
                ++compared;
                repeats += classes[i] == classes[i + shift];
            }
            check(compared > 100 && repeats > compared * 9 / 10,
                  "The texture repeats every 8 logical pixels along the edge");
        }
        // 5. A missing texture draws hatched in the warning colour, never in the edge colour.
        {
            Scene scene;
            auto &doc = scene.doc;
            auto *view = scene.view;
            const auto lost = createAsset(doc, "Lost weave", "image/png");
            doc.setStyle(edgeStyle(doc, EdgeStroke{4, 1, EdgeDash::Solid, lost}));
            const auto image = settle(*view);
            check(view->edgeTextureMissing(), "A missing texture shows the status chip");
            const auto square = bottomEdge(*view);
            int warning = 0, background = 0, black = 0, samples = 0;
            alongEdge(image, *view, square, [&](double, QColor color) {
                ++samples;
                // The light theme's warning colour (theme.hpp), on the white background.
                warning += near(color, 122, 79, 0, 24);
                background += near(color, 255, 255, 255, 6);
                black += dark(color);
            });
            check(black == 0, "A missing texture never draws the edge colour");
            check(warning > samples / 4 && background > samples / 4,
                  "The hatch alternates the warning colour with gaps");
            check(view->renderStats().profileSegments == 4,
                  "A hatched stroke is one quad per edge");
        }
        // 6. The chip's state: a texture that is present, or no texture, is not missing.
        {
            Scene scene;
            auto &doc = scene.doc;
            auto *view = scene.view;
            const auto lost = createAsset(doc, "Lost weave", "image/png");
            doc.setStyle(edgeStyle(doc, EdgeStroke{4, 1, EdgeDash::Solid, lost}));
            check(view->edgeTextureMissing(), "The chip shows for a missing texture");
            doc.setStyle(edgeStyle(doc, std::nullopt));
            check(!view->edgeTextureMissing(), "The chip hides when the stroke is off");
        }
        std::cout << QJsonDocument(result).toJson(QJsonDocument::Compact).toStdString() << '\n';
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
    return 0;
}
