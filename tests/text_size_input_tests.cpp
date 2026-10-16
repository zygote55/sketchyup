#include "app/component_parameters_section.hpp"
#include "app/interface_preferences.hpp"
#include "app/parameter_value_field.hpp"
#include "app/surface_format.hpp"
#include "app/window.hpp"
#include "component_parameter_fixture.hpp"
#include "io/document_io.hpp"
#include <QAction>
#include <QApplication>
#include <QComboBox>
#include <QDir>
#include <QInputDialog>
#include <QPushButton>
#include <QScrollArea>
#include <QSettings>
#include <QTabWidget>
#include <QTemporaryDir>
#include <QTest>
#include <QToolBar>
#include <iostream>
using namespace sketchy;
namespace {
void check(bool value, const char *message) {
    if (!value)
        throw std::runtime_error(message);
}
void choose(Window &window, const QString &percent, bool accept) {
    bool handled = false;
    std::exception_ptr failure;
    QTimer timer;
    QObject::connect(&timer, &QTimer::timeout, &window, [&] {
        auto *dialog = window.findChild<QInputDialog *>();
        if (handled || !dialog || !dialog->isVisible())
            return;
        handled = true;
        try {
            check(QTest::qWaitForWindowActive(dialog), "Text size dialog activates");
            auto *combo = dialog->findChild<QComboBox *>();
            check(combo && combo->findText(percent) >= 0, "Requested text size offered");
            combo->setCurrentText(percent);
            if (accept)
                dialog->accept();
            else
                dialog->reject();
        } catch (...) {
            failure = std::current_exception();
            dialog->reject();
        }
    });
    timer.start(10);
    window.findChild<QAction *>("view.textSize")->trigger();
    timer.stop();
    if (failure)
        std::rethrow_exception(failure);
    check(handled, "Text size dialog opens");
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
        QSettings settings;
        settings.setValue("interfaceTextPercent", 900);
        settings.setValue("futureNamespace/data", QByteArray("preserve"));
        settings.sync();
        {
            Window window;
            window.show();
            check(QTest::qWaitForWindowExposed(&window), "Text size window exposed");
            auto *field = window.findChild<QLineEdit *>("measurements");
            check(field && field->font().pixelSize() == 12,
                  "Invalid persisted scale falls back to 100 percent");
            // R094.c: a model with a parametric placement for the Info tab's parameter rows.
            auto table = parameter_fixture::makeTable();
            const auto model = QDir(files.path()).filePath("parameters.sketchyup");
            saveDocument(table.doc, model);
            window.openPath(model);
            const auto document = encodeContainer(window.document());
            const auto displayScale = window.devicePixelRatioF();
            choose(window, "200%", false);
            check(field->font().pixelSize() == 12, "Cancel leaves text size unchanged");
            settings.sync();
            check(settings.value("interfaceTextPercent").toInt() == 900,
                  "Cancel preserves original setting");
            for (int percent : {75, 125, 150, 175, 200, 100, 200}) {
                choose(window, QString::number(percent) + '%', true);
                check(QTest::qWaitFor([&] {
                          return field->font().pixelSize() == interfaceExtent(12, percent);
                      }),
                      "Actual font size follows preference");
                check(window.devicePixelRatioF() == displayScale,
                      "Text preference does not change display scale");
            }
            for (int mode : {1, 2}) {
                window.findChild<QAction *>("view.theme." + QString::number(mode))->trigger();
                for (int width : {640, 900, 1200, 1600}) {
                    window.setFixedSize(width, 900);
                    QTest::qWait(50);
                    check(window.width() == width, "Exact logical width exercised");
                    check(field->font().pixelSize() == 24, "Theme preserves enlarged text");
                    check(field->isVisible() && window.rect().contains(field->mapTo(
                                                    &window, field->rect().bottomRight())),
                          "Measurements stays inside every tested window width");
                    check(window.viewport()->width() >= 300, "Enlarged UI retains usable viewport");
                    auto *tools = window.findChild<QToolBar *>("toolRail");
                    check(tools && tools->width() == 156, "Tool rail grows with text");
                    auto *search = window.findChild<QPushButton *>("commandSearch");
                    check(search->width() >= search->sizeHint().width(),
                          "Command label fits its button");
                    auto *breadcrumb = window.findChild<QLabel *>("contextBreadcrumb");
                    check(breadcrumb->height() >= breadcrumb->fontMetrics().height() &&
                              breadcrumb->width() >=
                                  breadcrumb->fontMetrics().horizontalAdvance("Model"),
                          "Model breadcrumb fits enlarged font");
                    auto *tabs = window.findChild<QTabWidget *>("organizationTabs");
                    if (tabs->isVisible()) {
                        for (int tab : {0, 1}) {
                            tabs->setCurrentIndex(tab);
                            check(QTest::qWaitFor([&] {
                                      for (auto *button :
                                           tabs->currentWidget()->findChildren<QPushButton *>())
                                          if (button->isVisible() &&
                                              button->width() < button->sizeHint().width())
                                              return false;
                                      return true;
                                  }),
                                  "Outliner and tag action labels fit after reflow");
                        }
                        // R094.c (ADR 0166 §8): parameter rows lay out from font metrics; at
                        // 200% text in a narrow Tray their labels wrap above the fields and no
                        // control runs past the Info tab or clips its text.
                        tabs->setCurrentIndex(2);
                        window.viewport()->setSelection(table.wide);
                        auto *parameters = window.findChild<ComponentParametersSection *>(
                            "componentParametersSection");
                        auto *scroll = window.findChild<QScrollArea *>("entityInfoScroll");
                        check(QTest::qWaitFor([&] {
                                  return parameters->isVisible() &&
                                         parameters->fields().size() == 8;
                              }),
                              "Parameters show in the Info tab");
                        QTest::qWait(50);
                        const int right = scroll->viewport()->width();
                        bool overflow = false;
                        for (auto *widget : parameters->findChildren<QWidget *>()) {
                            if (!widget->isVisible())
                                continue;
                            const auto end =
                                widget->mapTo(scroll->viewport(), QPoint(widget->width(), 0));
                            if (end.x() > right + 1)
                                std::cerr << "Overflow: " << widget->metaObject()->className()
                                          << "/" << widget->objectName().toStdString() << " in "
                                          << widget->parentWidget()->objectName().toStdString()
                                          << " ends at " << end.x() << " of " << right << " (min "
                                          << widget->minimumSizeHint().width() << ")\n";
                            overflow |= end.x() > right + 1;
                        }
                        check(!overflow, "Parameter controls stay inside the Info tab");
                        for (auto *label : parameters->findChildren<QLabel *>())
                            if (label->isVisible() && label->wordWrap())
                                check(label->height() >= label->heightForWidth(label->width()),
                                      "Wrapped parameter text is not clipped");
                        for (auto *button : parameters->findChildren<QPushButton *>())
                            if (button->isVisible() && button->objectName() != "sectionHeader")
                                check(button->width() >= button->sizeHint().width(),
                                      "Parameter button labels fit");
                        window.viewport()->setSelection(0);
                        tabs->setCurrentIndex(0);
                        auto *hint = window.findChild<QLabel *>("hint");
                        check(hint->height() >= hint->heightForWidth(hint->width()),
                              "Wrapped navigation hint remains readable");
                    }
                    if (argc == 2) {
                        const auto folder = QString::fromLocal8Bit(argv[1]);
                        check(QDir().mkpath(folder), "Create text-size capture directory");
                        check(window.grab().save(
                                  folder + QString("/theme-%1-width-%2.png").arg(mode).arg(width)),
                              "Capture enlarged native interface");
                    }
                    std::cout << "Text size 200%, theme " << mode << ", logical width " << width
                              << ", viewport " << window.viewport()->width() << '\n';
                }
            }
            check(encodeContainer(window.document()) == document && !window.document().canUndo(),
                  "Text size preserves model and history");
        }
        {
            Window restarted;
            restarted.show();
            check(QTest::qWaitForWindowExposed(&restarted), "Restarted scaled window exposed");
            check(restarted.findChild<QLineEdit *>("measurements")->font().pixelSize() == 24,
                  "Text size survives restart");
        }
        settings.sync();
        check(settings.value("futureNamespace/data").toByteArray() == "preserve",
              "Unrelated preference preserved");
        std::cout << "Independent text size: bounded choices, cancel, themes, widths, display "
                     "scale, restart and unchanged model passed\n";
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
