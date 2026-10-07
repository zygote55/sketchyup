#include "app/window.hpp"
#include "io/document_io.hpp"
#include <QAction>
#include <QApplication>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QPushButton>
#include <QSurfaceFormat>
#include <QTemporaryDir>
#include <QTest>
#include <cmath>
#include <iostream>
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
    QSurfaceFormat::setDefaultFormat(format);
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
        }
        const auto bytes =
            QJsonDocument(QJsonObject{{"rows", rows},
                                      {"passes", passed},
                                      {"scope", "Effective active Qt palette for measurements, "
                                                "selection, command button and secondary text"},
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
