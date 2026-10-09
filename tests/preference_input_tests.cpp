#include "app/recovery_controller.hpp"
#include "app/surface_format.hpp"
#include "app/window.hpp"
#include "io/document_io.hpp"
#include <QAction>
#include <QApplication>
#include <QSettings>
#include <QTemporaryDir>
#include <QTest>
#include <iostream>
using namespace sketchy;
namespace {
void check(bool value, const char *message) {
    if (!value)
        throw std::runtime_error(message);
}
QAction *action(Window &window, const QString &id) {
    auto *result = window.findChild<QAction *>(id);
    check(result, "Preference action exists");
    return result;
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
        // Existing unversioned settings, including opaque future data, must survive.
        QVariantMap preserved{{"defaultUnits", "mm"},
                              {"fieldOfView", 78.0},
                              {"recoverySeconds", 0},
                              {"recentFiles", QStringList{files.filePath("old-model.sketchyup")}},
                              {"libraryFolder", files.filePath("library")},
                              {"render/blenderPath", files.filePath("unused-blender")},
                              {"futureNamespace/data", QByteArray("opaque\0payload", 14)}};
        {
            QSettings settings;
            for (auto it = preserved.cbegin(); it != preserved.cend(); ++it)
                settings.setValue(it.key(), it.value());
            settings.setValue("theme", 2);
            settings.setValue("trackpadNavigation", true);
            settings.setValue("reducedMotion", true);
            settings.sync();
            check(settings.status() == QSettings::NoError, "Write isolated legacy settings");
        }
        for (int restart = 0; restart < 4; ++restart) {
            Window window;
            window.startRecovery(files.filePath("recovery"));
            window.show();
            check(QTest::qWaitForWindowExposed(&window), "Preference window exposed");
            const int expectedTheme = restart == 0 ? 2 : (restart - 1) % 3;
            const bool expectedTrackpad = restart == 0 || (restart - 1) % 2 == 1;
            check(action(window, "view.theme." + QString::number(expectedTheme))->isChecked(),
                  "Selected theme survives window restart");
            check(
                window.viewport()->trackpadNavigation() == expectedTrackpad &&
                    action(window, expectedTrackpad ? "view.trackpad" : "view.mouse")->isChecked(),
                "Navigation preference survives restart");
            check(action(window, "view.reduced_motion")->isChecked() == expectedTrackpad,
                  "Reduced motion preference survives restart");
            check(window.document().displayUnits() == DisplayUnit::Millimeters &&
                      std::abs(window.viewport()->fieldOfView() - 78.0) < 1e-9,
                  "Legacy units and field of view remain effective");
            auto *recovery = window.findChild<RecoveryController *>("recoveryController");
            check(recovery && recovery->interval() == 0,
                  "Legacy recovery choice remains effective");
            const auto before = encodeContainer(window.document());
            if (restart < 3) {
                action(window, "view.theme." + QString::number(restart % 3))->trigger();
                action(window, restart % 2 ? "view.trackpad" : "view.mouse")->trigger();
                auto *motion = action(window, "view.reduced_motion");
                if (motion->isChecked() != bool(restart % 2))
                    motion->trigger();
            }
            check(encodeContainer(window.document()) == before && !window.document().canUndo(),
                  "Preference changes preserve model content and history");
            QSettings settings;
            settings.sync();
            check(settings.status() == QSettings::NoError, "Preference store remains writable");
            for (auto it = preserved.cbegin(); it != preserved.cend(); ++it)
                check(settings.value(it.key()) == it.value(),
                      "Unrelated legacy and opaque settings preserved");
        }
        std::cout << "Preferences: legacy values, three theme modes, navigation, reduced motion, "
                     "four window lifetimes and unrelated settings preservation passed\n";
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
