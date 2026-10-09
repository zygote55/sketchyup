#include "app/window.hpp"
#include "io/document_io.hpp"
#include <QAccessible>
#include <QAction>
#include <QApplication>
#include <QComboBox>
#include <QDialog>
#include <QJsonArray>
#include <QJsonDocument>
#include <QPushButton>
#include <QSettings>
#include <QTabWidget>
#include <QTemporaryDir>
#include <QTest>
#include <QTimer>
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
QJsonObject inspect(QWidget &window, const QString &page, const QString &theme) {
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
    AssistantPanel::HostServices services;
    services.outcomeRoot = settings.filePath("isolated-outcomes");
    services.credentialExecutable = "/bin/false";
    QSettings("SketchyUp", "SketchyUp").setValue("recoverySeconds", 0);
    Window window(nullptr, services);
    try {
        check(app.arguments().size() == 1 ||
                  (app.arguments().size() == 2 && (app.arguments()[1] == "--inventory-only" ||
                                                   app.arguments()[1] == "--dialogs-only")),
              "Usage: native_accessibility_audit [--inventory-only|--dialogs-only]");
        const bool dialogsOnly = app.arguments().contains("--dialogs-only");
        const bool keyboard = !dialogsOnly && !app.arguments().contains("--inventory-only");
        window.resize(1200, 900);
        window.demo();
        window.show();
        check(QTest::qWaitForWindowExposed(&window), "Audit window exposed");
        if (keyboard) {
            window.activateWindow();
            check(QTest::qWaitForWindowActive(&window), "Audit window owns keyboard focus");
        }
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
            if (dialogsOnly) {
                auto appendDialog = [&](QDialog &dialog, const QString &page) {
                    const auto context = inspect(dialog, page, theme);
                    check(context["unnamedControls"].toInt() == 0,
                          "Native dialog controls have descriptive accessible names");
                    contexts.append(context);
                };
                const std::pair<const char *, const char *> dialogs[] = {
                    {"drawing.plane.custom", "drawingPlaneDialog"},
                    {"edit.shortcuts", "shortcutDialog"},
                    {"view.commands", "commandPalette"}};
                for (const auto &[actionId, dialogId] : dialogs) {
                    auto *entry = window.findChild<QAction *>(actionId);
                    check(entry && entry->isEnabled(), "Dialog action available in demo");
                    QString failure;
                    bool captured{};
                    QTimer::singleShot(0, &window, [&] {
                        auto *dialog = qobject_cast<QDialog *>(QApplication::activeModalWidget());
                        try {
                            check(dialog && dialog->objectName() == dialogId,
                                  "Expected owned modal dialog opened");
                            check(QTest::qWaitForWindowExposed(dialog), "Audit dialog exposed");
                            appendDialog(*dialog, dialog->windowTitle());
                            captured = true;
                        } catch (const std::exception &error) {
                            failure = QString::fromUtf8(error.what());
                        }
                        if (dialog)
                            dialog->reject();
                    });
                    entry->trigger();
                    if (!failure.isEmpty())
                        throw std::runtime_error(failure.toStdString());
                    check(captured, "Modal dialog inventory completed");
                }
                if (!window.assistantPanel()->isVisible())
                    window.findChild<QAction *>("view.assistant")->trigger();
                auto *preferences =
                    window.assistantPanel()->findChild<QPushButton *>("assistantPreferences");
                check(preferences, "Assistant preferences action available");
                preferences->click();
                auto *dialog = window.findChild<QDialog *>("assistantPreferencesDialog");
                check(dialog && QTest::qWaitForWindowExposed(dialog),
                      "Synthetic assistant preferences exposed");
                auto *provider = dialog->findChild<QComboBox *>("assistantProviderChoice");
                auto *auth = dialog->findChild<QComboBox *>("assistantOpenAIAuth");
                check(provider && auth, "Provider and authentication controls exist");
                for (const auto &providerName :
                     {QStringLiteral("None"), QStringLiteral("Ollama"), QStringLiteral("OpenAI")}) {
                    provider->setCurrentText(providerName);
                    if (providerName == "OpenAI") {
                        for (int mode = 0; mode < auth->count(); ++mode) {
                            auth->setCurrentIndex(mode);
                            QTest::qWait(25);
                            appendDialog(*dialog, "Assistant: " + auth->currentText());
                        }
                    } else {
                        QTest::qWait(25);
                        appendDialog(*dialog, "Assistant: " + providerName);
                    }
                }
                dialog->reject();
                continue;
            }
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
        if (keyboard)
            window.viewport()->setFocus();
        for (int i = 0; keyboard && i < 12; ++i) {
            auto *focused = QApplication::focusWidget();
            check(focused, "Keyboard region has a focus owner");
            regions.append(identity(focused));
            QTest::keyClick(focused, Qt::Key_F6);
            QTest::qWait(25);
        }
        check(encodeContainer(window.document()) == before, "Audit preserves model content");
        QJsonObject report{
            {"auditVersion", dialogsOnly ? 2 : 1},
            {"dialogsOnly", dialogsOnly},
            {"platform", QGuiApplication::platformName()},
            {"qtVersion", qVersion()},
            {"logicalWidth", window.width()},
            {"logicalHeight", window.height()},
            {"scale", window.devicePixelRatioF()},
            {"contexts", contexts},
            {"actions", actions},
            {"f6FocusOwners", regions},
            {"keyboardRoutingExercised", keyboard},
            {"activeWindowAtCapture", window.isActiveWindow()},
            {"scope",
             dialogsOnly
                 ? "Visible enabled tab-focusable controls in Drawing plane, Keyboard shortcuts, "
                   "Commands and model search, and all synthetic Assistant preference modes, "
                   "in two themes; Qt accessibility interfaces. No keyboard-routing claim."
                 : "Visible enabled tab-focusable widgets in all model-panel tabs and two "
                   "themes; public QAction inventory and optional F6 routing. Names come "
                   "from Qt accessibility interfaces."},
            {"limitations",
             "Does not prove screen-reader operation, contrast, focus visibility, all dialogs, "
             "independent text scaling or complete mouse-free modeling. Provider preference modes "
             "use isolated unsaved settings and a failing synthetic credential helper; no sign-in, "
             "key operation, provider submission or document-changing button is activated."},
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
