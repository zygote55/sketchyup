#include "app/surface_format.hpp"
#include "app/viewport.hpp"
#include "core/assets.hpp"
#include "core/components.hpp"
#include "core/groups.hpp"
#include "core/materials.hpp"
#include "core/sections.hpp"
#include "core/tags.hpp"
#include <QApplication>
#include <QBuffer>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTest>
#include <QVBoxLayout>
#include <functional>
#include <iostream>
#include <numbers>
using namespace sketchy;
namespace {
void check(bool value, const std::string &message) {
    if (!value)
        throw std::runtime_error(message);
}
QImage frame(Viewport &view) {
    QCoreApplication::processEvents();
    auto image = view.grabFramebuffer();
    check(!image.isNull() && view.rendererReady(), "Placement framebuffer available");
    return image.convertToFormat(QImage::Format_RGBA8888);
}
// Painter overlays (focus, pending-work notices) can change between frames
// independently of geometry. Compare only frames that repeat exactly.
QImage stableFrame(Viewport &view) {
    auto image = frame(view);
    for (int attempt = 0; attempt < 10; ++attempt) {
        QTest::qWait(20);
        auto next = frame(view);
        if (next == image)
            return image;
        image = std::move(next);
    }
    throw std::runtime_error("Placement framebuffer did not become stable");
}
struct Difference {
    qint64 pixels{};
    int maximum{};
};
Difference compare(const QImage &a, const QImage &b) {
    check(a.size() == b.size(), "Compared framebuffers have one size");
    Difference result;
    for (int y = 0; y < a.height(); ++y) {
        const auto *p = a.constScanLine(y), *q = b.constScanLine(y);
        for (int x = 0; x < a.width(); ++x) {
            int delta = 0;
            for (int c = 0; c < 4; ++c)
                delta = std::max(delta, std::abs(int(p[x * 4 + c]) - int(q[x * 4 + c])));
            if (delta) {
                ++result.pixels;
                result.maximum = std::max(result.maximum, delta);
            }
        }
    }
    return result;
}
Id prism(Document &doc, double radius, int sides) {
    std::vector<Vec3> loop;
    for (int i = 0; i < sides; ++i) {
        const auto angle = 2 * std::numbers::pi * i / sides;
        loop.push_back({radius * std::cos(angle), radius * std::sin(angle), 0});
    }
    const auto body = doc.addFace({loop});
    doc.extrude(body, doc.bodies().at(body)->surface.faces.begin()->first, 1.3);
    return body;
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
    QJsonArray results;
    qint64 differingStates{};
    try {
        QWidget host;
        host.resize(720, 540);
        QVBoxLayout layout(&host);
        Document doc;
        auto *view = new Viewport(doc);
        layout.addWidget(view);
        host.show();
        check(QTest::qWaitForWindowExposed(&host), "Placement viewport exposed");
        // Compare the shared placement path against every body drawing its own
        // world-space mesh, in one state. CPU/GPU picking must be identical.
        // Framebuffer differences are measured and reported, not asserted: the
        // shared path rounds placement-space positions and the draw offset
        // separately, which can move rasterized edges by a sample.
        auto state = [&](const QString &name, const std::function<void()> &prepare = {}) {
            if (prepare)
                prepare();
            view->setPlacementMeshSharing(true);
            const auto shared = stableFrame(*view);
            const auto stats = view->renderStats();
            std::vector<std::pair<Id, Id>> picks, edges;
            std::vector<std::optional<SelectedEntity>> selected;
            for (int y = 1; y < 8; ++y)
                for (int x = 1; x < 10; ++x) {
                    const QPointF point(view->width() * x / 10., view->height() * y / 8.);
                    picks.push_back(view->pick(point));
                    edges.push_back(view->pickEdge(point, 8));
                    if (x % 4 == 0 && y % 3 == 0)
                        selected.push_back(view->selectionAt(point));
                }
            view->setPlacementMeshSharing(false);
            const auto own = stableFrame(*view);
            const auto ownStats = view->renderStats();
            size_t index = 0, selection = 0;
            bool picksEqual = true;
            for (int y = 1; y < 8; ++y)
                for (int x = 1; x < 10; ++x, ++index) {
                    const QPointF point(view->width() * x / 10., view->height() * y / 8.);
                    picksEqual &= view->pick(point) == picks[index];
                    picksEqual &= view->pickEdge(point, 8) == edges[index];
                    if (x % 4 == 0 && y % 3 == 0)
                        picksEqual &= view->selectionAt(point) == selected[selection++];
                }
            view->setPlacementMeshSharing(true);
            const auto difference = compare(shared, own);
            if (const auto dump = qEnvironmentVariable("SKETCHYUP_PLACEMENT_DUMP");
                !dump.isEmpty() && difference.pixels) {
                const auto prefix =
                    dump + "/" + name + "-" + QString::number(view->devicePixelRatioF()) + "-";
                shared.save(prefix + "shared.png");
                own.save(prefix + "own.png");
            }
            differingStates += difference.pixels > 0 || !picksEqual;
            check(ownStats.placementMeshes == 0 && ownStats.sharedPlacements == 0,
                  "Disabled sharing draws every body from its own mesh");
            results.append(QJsonObject{{"state", name},
                                       {"differentPixels", difference.pixels},
                                       {"maximumChannelDelta", difference.maximum},
                                       {"picksEqual", picksEqual},
                                       {"placementMeshes", qint64(stats.placementMeshes)},
                                       {"sharedPlacements", qint64(stats.sharedPlacements)},
                                       {"bodies", qint64(doc.bodies().size())}});
            check(picksEqual, "Shared placements pick identically: " + name.toStdString());
        };
        // A colored, partly translucent prism with irrational vertex coordinates.
        const auto body = prism(doc, .9, 26);
        const auto glass = createMaterial(doc, "Glass", {.2f, .5f, .9f}, .45f);
        const auto clay = createMaterial(doc, "Clay", {.8f, .4f, .3f});
        assignMaterial(doc, body, {}, clay);
        assignMaterial(doc, body, doc.bodies().at(body)->surface.faces.begin()->first, glass);
        const auto component = createComponent(doc, body, "Placement prism");
        std::vector<Id> placements{component.instance};
        for (int i = 1; i < 12; ++i)
            placements.push_back(
                placeComponent(
                    doc, component.definition,
                    Transform::translation({2.137 * (i % 4), 2.911 * (i / 4), .173 * (i % 3)}))
                    .instance);
        view->fit();
        state("translated");
        frame(*view);
        check(view->renderStats().placementMeshes == 1 &&
                  view->renderStats().sharedPlacements == 12,
              "Twelve translated placements draw one shared mesh");
        state("xray", [&] {
            auto style = doc.style();
            style.mode = ModelStyleMode::XRay;
            doc.setStyle(style);
        });
        state("monochrome-profiles-ground", [&] {
            auto style = doc.style();
            style.mode = ModelStyleMode::Monochrome;
            style.profiles = style.groundVisible = true;
            doc.setStyle(style);
        });
        state("wireframe", [&] {
            auto style = doc.style();
            style.mode = ModelStyleMode::Wireframe;
            style.profiles = style.groundVisible = false;
            doc.setStyle(style);
        });
        state("solar-shadows", [&] {
            doc.setStyle(ModelStyle{});
            SolarSettings solar;
            solar.enabled = true;
            solar.latitude = 40;
            solar.time = {2026, 6, 21, 15, 0, 0, 0};
            doc.setSolar(solar);
        });
        state("hidden-shown", [&] {
            doc.setSolar(SolarSettings{});
            view->selectEntities({{placements[3], SelectionKind::Body, 0}});
            view->hideSelection();
            view->showHiddenGeometry(true);
        });
        state("tag-hidden", [&] {
            view->showHiddenGeometry(false);
            const auto tag = createTag(doc, "Hidden placements");
            assignTag(doc, placements[5], tag);
            editTag(doc, tag, {}, {}, false);
            view->refresh();
        });
        state("context-dim-locked-selection", [&] {
            view->enterContext(placements[7]);
            const auto member = doc.instances().at(placements[7])->members.rbegin()->second;
            view->selectEntities({{member, SelectionKind::Face,
                                   doc.bodies().at(member)->surface.faces.begin()->first}});
        });
        state("face-session-hidden", [&] {
            const auto member = doc.instances().at(placements[7])->members.rbegin()->second;
            const auto face = std::next(doc.bodies().at(member)->surface.faces.begin(), 2)->first;
            view->selectEntities({{member, SelectionKind::Face, face}});
            view->hideSelection();
            view->leaveContext();
        });
        state("opacity-clip", [&] {
            const auto member = doc.instances().at(placements[2])->members.rbegin()->second;
            view->setBodyOpacity(member, .5f);
            view->setClipPlane(std::array<double, 4>{.3, .2, -1, 1.1});
        });
        state("section", [&] {
            view->setClipPlane(std::nullopt);
            const auto section =
                createSection(doc, "Cut", 0, SectionPlane::through({0, 0, .6}, {.2, .1, -1}));
            setActiveSection(doc, 0, section);
        });
        state("orthographic-top", [&] {
            setActiveSection(doc, 0, std::nullopt);
            view->standardView(1);
            view->fit();
        });
        // Rotated, mirrored and scaled placements: equal linear parts share.
        state("linear-placements", [&] {
            view->standardView(0);
            for (int i = 0; i < 6; ++i) {
                const auto rotation = Transform::rotation({0, 0, 1}, i < 3 ? .7 : -1.1);
                const auto scale = Transform::scaling({i % 2 ? -1. : 1., 1, i == 5 ? 1.7 : 1.});
                placements.push_back(
                    placeComponent(doc, component.definition,
                                   Transform::translation({-3.3 - 2.2 * i, .7 * i, 0}) * rotation *
                                       scale)
                        .instance);
            }
            view->fit();
        });
        state("definition-edit",
              [&] { editMaterial(doc, clay, {}, std::array<float, 3>{.3f, .7f, .2f}, {}); });
        state("make-unique", [&] { makeComponentUnique(doc, placements[1]); });
        // Distant placements keep camera-relative precision in the draw offset.
        state("far", [&] {
            const auto group = createGroup(doc, {placements.begin(), placements.end()}, "Far");
            doc.transform(group, Transform::translation({900000.37, -900000.21, 900000.11}));
            view->fit();
        });
        state("far-zoomed", [&] {
            const auto member = doc.instances().at(placements[4])->members.rbegin()->second;
            const auto bounds = view->bodyBounds(member);
            view->frameBounds(bounds.minimum, bounds.maximum);
        });
        // A small decoded texture with transparency on the shared definition.
        state("textured", [&] {
            QImage image(16, 16, QImage::Format_RGBA8888);
            for (int y = 0; y < 16; ++y)
                for (int x = 0; x < 16; ++x)
                    image.setPixelColor(
                        x, y, QColor((x * 16) % 256, (y * 16) % 256, 128, (x + y) % 5 ? 255 : 90));
            QByteArray bytes;
            QBuffer buffer(&bytes);
            buffer.open(QIODevice::WriteOnly);
            image.save(&buffer, "PNG");
            const auto asset =
                createAsset(doc, "Checker", "image/png",
                            std::make_shared<const AssetPayload>(
                                std::vector<std::uint8_t>(bytes.begin(), bytes.end())));
            editMaterial(doc, clay, {}, {}, {}, asset);
            view->fit();
            frame(*view);
            for (int i = 0; i < 300 && view->texturesPending(); ++i)
                QTest::qWait(10);
            check(!view->texturesPending(), "Placement texture decoded");
        });
        check(view->renderStats().glError == 0, "Placement comparisons leave no GL error");
        std::cout << QJsonDocument(QJsonObject{{"platform", QGuiApplication::platformName()},
                                               {"scale", view->devicePixelRatioF()},
                                               {"graphics", view->graphicsDescription()},
                                               {"differingStates", differingStates},
                                               {"states", results}})
                         .toJson(QJsonDocument::Compact)
                         .toStdString()
                  << '\n';
        std::cout << "Shared placement meshes pick identically to per-body meshes across styles, "
                     "shadows, visibility, context, sections, linear and distant placements and "
                     "textures; pixel differences reported above; DPR="
                  << view->devicePixelRatioF() << '\n';
    } catch (const std::exception &error) {
        std::cerr << QJsonDocument(results).toJson(QJsonDocument::Compact).toStdString() << '\n'
                  << error.what() << '\n';
        return 1;
    }
}
