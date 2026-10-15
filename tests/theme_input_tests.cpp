#include "app/surface_format.hpp"
#include "app/window.hpp"
#include "io/document_io.hpp"
#include <QAction>
#include <QApplication>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QPushButton>
#include <QTemporaryDir>
#include <QTest>
#include <cmath>
#include <iostream>
#include <map>
using namespace sketchy;
namespace {
double luminance(const QColor &color) {
    auto linear = [](double v) {
        return v <= .04045 ? v / 12.92 : std::pow((v + .055) / 1.055, 2.4);
    };
    return .2126 * linear(color.redF()) + .7152 * linear(color.greenF()) +
           .0722 * linear(color.blueF());
}
double contrast(const QColor &a, const QColor &b) {
    const auto x = luminance(a), y = luminance(b);
    return (std::max(x, y) + .05) / (std::min(x, y) + .05);
}
void check(bool value, const char *message) {
    if (!value)
        throw std::runtime_error(message);
}
} // namespace
int main(int argc, char **argv) {
    QTemporaryDir files;
    if (!files.isValid())
        return 2;
    qputenv("XDG_CONFIG_HOME", files.path().toUtf8());
    qputenv("XDG_DATA_HOME", files.path().toUtf8());
    QSurfaceFormat format;
    format.setVersion(3, 3);
    format.setProfile(QSurfaceFormat::CoreProfile);
    format.setDepthBufferSize(24);
    sketchy::setDefaultViewportFormat(format);
    QApplication app(argc, argv);
    app.setOrganizationName("SketchyUp");
    app.setApplicationName("SketchyUp");
    try {
        Window window;
        window.show();
        check(QTest::qWaitForWindowExposed(&window), "Contrast window exposed");
        const auto content = encodeContainer(window.document());
        QJsonArray rows;
        bool passed = true;
        for (int mode : {1, 2}) {
            window.findChild<QAction *>("view.theme." + QString::number(mode))->trigger();
            auto *field = window.findChild<QLineEdit *>("measurements");
            field->setText("Measured text");
            field->setFocus();
            field->selectAll();
            QTest::qWait(50);
            auto record = [&](QWidget *widget, const char *state, QPalette::ColorRole fg,
                              QPalette::ColorRole bg) {
                check(widget, "Contrast control exists");
                widget->ensurePolished();
                const auto palette = widget->palette();
                const auto foreground = palette.color(QPalette::Active, fg);
                const auto background = palette.color(QPalette::Active, bg);
                const auto ratio = contrast(foreground, background);
                passed &= ratio >= 4.5;
                rows.append(QJsonObject{{"theme", mode == 1 ? "light" : "dark"},
                                        {"control", widget->objectName()},
                                        {"state", state},
                                        {"foreground", foreground.name()},
                                        {"background", background.name()},
                                        {"contrast", ratio},
                                        {"passes", ratio >= 4.5}});
            };
            record(field, "text", QPalette::Text, QPalette::Base);
            record(field, "selected text", QPalette::HighlightedText, QPalette::Highlight);
            record(window.findChild<QPushButton *>("commandSearch"), "button", QPalette::ButtonText,
                   QPalette::Button);
            record(window.findChild<QLabel *>("hint"), "secondary text", QPalette::WindowText,
                   QPalette::Window);
            // QLabel rich-text links may ignore the palette under a stylesheet.
            // Measure the dominant painted non-background color, including solid
            // glyph interiors/underline, rather than trusting QPalette::Link.
            auto *breadcrumb = window.findChild<QLabel *>("contextBreadcrumb");
            check(breadcrumb, "Context breadcrumb exists");
            const auto background = breadcrumb->palette().color(QPalette::Window);
            const auto pixels = breadcrumb->grab().toImage().convertToFormat(QImage::Format_RGB32);
            std::map<QRgb, int> histogram;
            for (int y = 0; y < pixels.height(); ++y)
                for (int x = 0; x < pixels.width(); ++x) {
                    const auto rgb = pixels.pixel(x, y);
                    if (rgb != background.rgb())
                        ++histogram[rgb];
                }
            check(!histogram.empty(), "Context link has painted foreground pixels");
            const auto dominant =
                std::max_element(histogram.begin(), histogram.end(),
                                 [](const auto &a, const auto &b) { return a.second < b.second; });
            const QColor foreground(dominant->first);
            const auto ratio = contrast(foreground, background);
            passed &= ratio >= 4.5;
            rows.append(QJsonObject{{"theme", mode == 1 ? "light" : "dark"},
                                    {"control", breadcrumb->objectName()},
                                    {"state", "painted rich-text link"},
                                    {"foreground", foreground.name()},
                                    {"background", background.name()},
                                    {"contrast", ratio},
                                    {"passes", ratio >= 4.5}});
        }
        // ADR 0164 decision 5 (R091.e): the warning, failure and selection tokens in both
        // themes. Text needs 4.5:1; the selection stroke is a graphic and needs 3:1. Canvas
        // decorations are drawn on paper, which is white in both themes.
        for (const bool dark : {false, true}) {
            const auto colors = themeColors(dark);
            auto token = [&](const char *name, const QColor &foreground, const QColor &background,
                             const char *on, double minimum) {
                const auto ratio = contrast(foreground, background);
                passed &= ratio >= minimum;
                rows.append(QJsonObject{{"theme", dark ? "dark" : "light"},
                                        {"control", QString("token.") + name},
                                        {"state", on},
                                        {"foreground", foreground.name()},
                                        {"background", background.name()},
                                        {"contrast", ratio},
                                        {"minimum", minimum},
                                        {"passes", ratio >= minimum}});
            };
            token("warning", colors.warning, colors.warningSurface, "warning banner", 4.5);
            token("warning", colors.warning, colors.surface, "Pages state text", 4.5);
            token("warning", colors.warning, colors.selected, "selected Pages row", 4.5);
            token("failure", colors.failure, colors.surface, "Pages state text", 4.5);
            token("failure", colors.failure, colors.selected, "selected Pages row", 4.5);
            token("selectionStroke", colors.selectionStroke, colors.surface, "selection on surface",
                  3);
            token("paper", colors.ink, colors.pasteboard, "text on pasteboard", 4.5);
            check(colors.paper == QColor(Qt::white), "Paper is white in both themes");
        }
        const auto paper = themeColors(false);
        auto onPaper = [&](const char *name, const QColor &foreground, const QColor &background,
                           const char *on, double minimum) {
            const auto ratio = contrast(foreground, background);
            passed &= ratio >= minimum;
            rows.append(QJsonObject{{"theme", "paper"},
                                    {"control", QString("token.") + name},
                                    {"state", on},
                                    {"foreground", foreground.name()},
                                    {"background", background.name()},
                                    {"contrast", ratio},
                                    {"minimum", minimum},
                                    {"passes", ratio >= minimum}});
        };
        onPaper("warning", paper.warning, paper.warningSurface, "Changed chip", 4.5);
        onPaper("failure", paper.failure, paper.paper, "broken card", 4.5);
        onPaper("failure", paper.failure, paper.failureSurface, "broken hatch fill", 4.5);
        onPaper("selectionStroke", paper.selectionStroke, paper.paper, "selected view outline", 3);
        const auto bytes =
            QJsonDocument(
                QJsonObject{{"rows", rows},
                            {"passes", passed},
                            {"scope",
                             "Effective active Qt palette for measurements, "
                             "selection, command button, secondary text and painted context link; "
                             "Sheets warning, failure and selection tokens"},
                            {"releaseAcceptance", false}})
                .toJson();
        if (argc == 2) {
            QFile report(QString::fromLocal8Bit(argv[1]));
            check(report.open(QIODevice::WriteOnly) && report.write(bytes) == bytes.size(),
                  "Write contrast report");
        }
        std::cout << bytes.constData();
        check(encodeContainer(window.document()) == content && !window.document().canUndo(),
              "Theme preserves content/history");
        check(passed, "Active text contrast meets 4.5:1 in both themes");
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
