#include "app/window.hpp"
#include "io/document_io.hpp"
#include <QAccessible>
#include <QAction>
#include <QApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QTabWidget>
#include <QTemporaryDir>
#include <QTest>
#include <iostream>
using namespace sketchy;
namespace {
void check(bool value, const char *message) {
    if (!value)
        throw std::runtime_error(message);
}
QString identity(const QObject *object) {
    if (!object)
        return {};
    return QString::fromLatin1(object->metaObject()->className()) + ':' + object->objectName();
}
QJsonObject inspect(Window &window, const QString &page, const QString &theme) {
    QJsonArray controls;
    int unnamed{}, focusable{};
    for (auto *widget : window.findChildren<QWidget *>()) {
        if (!widget->isVisibleTo(&window) || !widget->isEnabled() ||
            !(widget->focusPolicy() & Qt::TabFocus))
            continue;
        ++focusable;
        auto *accessible = QAccessible::queryAccessibleInterface(widget);
        const auto name = accessible ? accessible->text(QAccessible::Name) : QString{};
        if (name.trimmed().isEmpty())
            ++unnamed;
        controls.append(
            QJsonObject{{"identity", identity(widget)},
                        {"accessibleInterface", accessible && accessible->isValid()},
                        {"accessibleName", name},
                        {"accessibleDescription",
                         accessible ? accessible->text(QAccessible::Description) : QString{}},
                        {"role", accessible ? int(accessible->role()) : -1},
                        {"toolTip", widget->toolTip()},
                        {"focusPolicy", int(widget->focusPolicy())},
                        {"logicalFontPointSize", widget->font().pointSizeF()}});
    }
    return {{"page", page},
            {"theme", theme},
            {"focusableControls", focusable},
            {"unnamedControls", unnamed},
            {"controls", controls}};
}
} // namespace
int main(int argc, char **argv) {
    QTemporaryDir settings;
    if (!settings.isValid())
        return 2;
    qputenv("XDG_CONFIG_HOME", settings.path().toUtf8());
    qputenv("XDG_DATA_HOME", settings.path().toUtf8());
    QApplication app(argc, argv);
    app.setOrganizationName("SketchyUp");
    app.setApplicationName("SketchyUp");
    Window window;
    try {
        window.resize(1200, 900);
        window.demo();
        window.show();
        check(QTest::qWaitForWindowExposed(&window), "Audit window exposed");
        window.activateWindow();
        check(QTest::qWaitForWindowActive(&window), "Audit window owns keyboard focus");
        const auto before = encodeContainer(window.document());
        QJsonArray actions;
        for (auto *action : window.findChildren<QAction *>()) {
            if (!action->property("category").isValid())
                continue;
            actions.append(
                QJsonObject{{"id", action->objectName()},
                            {"text", action->text()},
                            {"shortcut", action->shortcut().toString(QKeySequence::PortableText)},
                            {"shortcutContext", int(action->shortcutContext())},
                            {"enabled", action->isEnabled()}});
        }
        QJsonArray contexts;
        for (const QString &theme : {QStringLiteral("Dark theme"), QStringLiteral("Light theme")}) {
            bool found{};
            for (auto *action : window.findChildren<QAction *>())
                if (action->text() == theme) {
                    action->trigger();
                    found = true;
                    break;
                }
            check(found, "Audit theme action exists");
            auto *tabs = window.findChild<QTabWidget *>("organizationTabs");
            check(tabs && tabs->count() > 0, "Audit model-panel tabs exist");
            for (int page = 0; page < tabs->count(); ++page) {
                tabs->setCurrentIndex(page);
                QTest::qWait(50);
                contexts.append(inspect(window, tabs->tabText(page), theme));
            }
        }
        // Observe real keyboard routing. This inventory does not assert that the
        // present order or labels are accessible; those are the subsequent audit.
        QJsonArray regions;
        window.viewport()->setFocus();
        for (int i = 0; i < 12; ++i) {
            auto *focused = QApplication::focusWidget();
            check(focused, "Keyboard region has a focus owner");
            regions.append(identity(focused));
            QTest::keyClick(focused, Qt::Key_F6);
            QTest::qWait(25);
        }
        check(encodeContainer(window.document()) == before, "Audit preserves model content");
        QJsonObject report{
            {"auditVersion", 1},
            {"platform", QGuiApplication::platformName()},
            {"qtVersion", qVersion()},
            {"logicalWidth", window.width()},
            {"logicalHeight", window.height()},
            {"scale", window.devicePixelRatioF()},
            {"contexts", contexts},
            {"actions", actions},
            {"f6FocusOwners", regions},
            {"scope", "Visible enabled tab-focusable widgets in all model-panel tabs and two "
                      "themes; public QAction inventory and F6 routing. Names come from Qt "
                      "accessibility interfaces."},
            {"limitations",
             "Does not prove screen-reader operation, contrast, focus visibility, all dialogs, "
             "independent text scaling or complete mouse-free modeling. The fixture uses isolated "
             "application settings and does not open provider preferences or request credentials."},
            {"releaseAcceptance", false}};
        std::cout << QJsonDocument(report).toJson().toStdString();
        window.document().markSaved();
        return 0;
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        window.document().markSaved();
        return 1;
    }
}
