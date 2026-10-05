#include "app/viewport.hpp"
#include "automation/commands.hpp"
#include "io/document_io.hpp"
#include <QApplication>
#include <QSurfaceFormat>
#include <QTest>
#include <iostream>
using namespace sketchy;
void check(bool ok, const char *message) {
    if (!ok)
        throw std::runtime_error(message);
}
QJsonObject face(double x, double z = 0) {
    return {{"command", "geometry.face"},
            {"loops",
             QJsonArray{QJsonArray{QJsonArray{x - 0.8, -0.8, z}, QJsonArray{x + 0.8, -0.8, z},
                                   QJsonArray{x + 0.8, 0.8, z}, QJsonArray{x - 0.8, 0.8, z}}}}};
}
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
        auto add = [&](double x, double z = 0) {
            return doc.addFace(
                {{{x - .8, -.8, z}, {x + .8, -.8, z}, {x + .8, .8, z}, {x - .8, .8, z}}});
        };
        const auto removed = add(-4), modified = add(-1), occluder = add(4, 2);
        const auto before = encodeDocument(doc);
        const auto history = doc.history().total;
        auto edit =
            std::make_shared<const Document::PreparedEdit>(doc.prepareEdit([&](Document &draft) {
                executeBatch(
                    draft, {{"apiVersion", 1},
                            {"documentId", QString::fromStdString(draft.identity())},
                            {"expectedRevision", QString::number(draft.revision())},
                            {"commands", QJsonArray{QJsonObject{{"command", "geometry.delete"},
                                                                {"body", QString::number(removed)}},
                                                    QJsonObject{{"command", "geometry.translate"},
                                                                {"body", QString::number(modified)},
                                                                {"delta", QJsonArray{0, .5, 0}}},
                                                    face(2), face(4)}}});
            }));
        Viewport view(doc);
        view.resize(900, 600);
        view.show();
        check(QTest::qWaitForWindowExposed(&view), "Viewport exposed");
        check(QTest::qWaitFor([&] { return view.rendererReady(); }), "Renderer ready");
        view.standardView(1);
        view.fit();
        view.setAssistantPreview(edit);
        auto frame = view.grabFramebuffer();
        auto green = [](QColor c) { return c.green() > 130 && c.red() < 70 && c.blue() < 100; };
        auto red = [](QColor c) { return c.red() > 180 && c.green() < 90 && c.blue() < 100; };
        auto amber = [](QColor c) {
            return c.red() > 180 && c.green() > 100 && c.green() < 160 && c.blue() < 50;
        };
        int greens{}, reds{}, ambers{};
        for (int y = 0; y < frame.height(); ++y)
            for (int x = 0; x < frame.width(); ++x) {
                auto c = frame.pixelColor(x, y);
                greens += green(c);
                reds += red(c);
                ambers += amber(c);
            }
        check(greens > 100 && reds > 100 && ambers > 20,
              "Green/red hatch and amber changed outlines appear");
        auto region = [&](Vec3 center, auto predicate) {
            const auto a = view.project(center + Vec3{-.4, -.4, 0}),
                       b = view.project(center + Vec3{.4, .4, 0});
            const double ratio = double(frame.width()) / view.width();
            auto rect = QRect(QPoint(qRound(a.x() * ratio), qRound(a.y() * ratio)),
                              QPoint(qRound(b.x() * ratio), qRound(b.y() * ratio)))
                            .normalized();
            int count{}, total{};
            for (int y = rect.top(); y <= rect.bottom(); ++y)
                for (int x = rect.left(); x <= rect.right(); ++x) {
                    check(frame.rect().contains(x, y), "Sample lies inside frame");
                    count += predicate(frame.pixelColor(x, y));
                    ++total;
                }
            return double(count) / total;
        };
        const auto coverage = region({2, 0, 0}, green);
        check(coverage > .2 && coverage < .55, "Hatching retains visible gaps at current DPR");
        check(region({4, 0, 2}, green) == 0, "Preview does not shine through opaque live geometry");
        check(view.pick(view.project({4, 0, 2})).first == occluder,
              "Picking still refers to live model");
        check(view.hasAssistantPreview() && encodeDocument(doc) == before &&
                  doc.history().total == history,
              "Displaying exact proposal does not edit bytes or history");
        view.setAssistantPreviewFocus(modified);
        check(view.accessibleDescription().contains("1.6 m × 1.6 m × 0 m") &&
                  !view.accessibleDescription().contains("³"),
              "Preview bounds use length units and expose accessible measurement text");
        frame = view.grabFramebuffer();
        if (argc > 1)
            check(frame.save(QString::fromLocal8Bit(argv[1])), "Save preview evidence");
        view.setAssistantPreview({});
        check(!view.hasAssistantPreview(), "Discard clears display");
        view.setAssistantPreview(edit);
        view.setSelection(removed);
        view.hideSelection();
        frame = view.grabFramebuffer();
        check(region({-4, 0, 0}, red) == 0, "Temporary hidden state removes preview geometry");
        view.revealHiddenGeometry();
        doc.move(modified, {0, 1, 0});
        view.refresh();
        view.grabFramebuffer();
        check(!view.hasAssistantPreview(), "Human edit invalidates display before another frame");
        bool rejected{};
        try {
            view.setAssistantPreview(edit);
        } catch (const std::exception &) {
            rejected = true;
        }
        check(rejected, "Stale proposal cannot be displayed again");
        check(view.renderStats().glError == 0, "Preview passes leave clean OpenGL state");
        std::cout
            << "Immutable assistant preview, hatching, depth, visibility and staleness passed\n";
        return 0;
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
