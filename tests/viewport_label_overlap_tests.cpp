#include "app/interface_preferences.hpp"
#include "app/surface_format.hpp"
#include "app/window.hpp"
#include <QAction>
#include <QApplication>
#include <QComboBox>
#include <QDir>
#include <QInputDialog>
#include <QLabel>
#include <QPainter>
#include <QSettings>
#include <QTemporaryDir>
#include <QTest>
#include <QTimer>
#include <iostream>
using namespace sketchy;
namespace {
void check(bool value, const QString &message) {
    if (!value)
        throw std::runtime_error(message.toStdString());
}
// Window.grab() omits the painted viewport overlay, so composite the framebuffer (which
// carries the overlay text) and the context chips over the grabbed window chrome.
QImage captureWindow(Window &window) {
    auto *view = window.viewport();
    QImage image = window.grab().toImage();
    const QImage framebuffer = view->grabFramebuffer();
    QPainter painter(&image);
    painter.drawImage(QRectF(view->mapTo(&window, QPoint(0, 0)), QSizeF(view->size())),
                      framebuffer);
    for (const char *name : {"contextBreadcrumb", "componentScopeBanner"})
        if (auto *chip = window.findChild<QLabel *>(name); chip && chip->isVisibleTo(view))
            painter.drawPixmap(view->mapTo(&window, chip->pos()), chip->grab());
    return image;
}
void chooseTextSize(Window &window, const QString &percent) {
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
            dialog->accept();
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
struct Counters {
    int beside = 0, below = 0, states = 0;
};
void verify(Window &window, const QString &state, Counters &counters) {
    auto *view = window.viewport();
    auto *breadcrumb = window.findChild<QLabel *>("contextBreadcrumb");
    auto *banner = window.findChild<QLabel *>("componentScopeBanner");
    check(breadcrumb && banner, "Context chips exist");
    // Context labels resize on a queued event; let that and the repaint it requests settle.
    QTest::qWait(100);
    view->grabFramebuffer();
    const auto chip = breadcrumb->geometry();
    const auto label = view->paintedProjectionLabelRect();
    check(!label.isEmpty(), state + ": projection label painted");
    check(breadcrumb->isVisibleTo(view) && !chip.isEmpty(), state + ": breadcrumb shown");
    check(!label.intersects(chip), state + ": label overlaps breadcrumb");
    if (banner->isVisibleTo(view))
        check(!label.intersects(banner->geometry()), state + ": label overlaps scope banner");
    check(view->rect().contains(label), state + ": label stays inside viewport");
    check(view->rect().contains(chip), state + ": breadcrumb stays inside viewport");
    if (label.left() == chip.right() + 1 + 8) {
        check(label.top() < chip.bottom(), state + ": beside label keeps the first line");
        ++counters.beside;
    } else {
        check(label.left() == chip.left() && label.top() >= chip.bottom(),
              state + ": wrapped label sits left-aligned below the breadcrumb");
        const auto room = view->width() - 20 - (chip.right() + 1 + 8);
        check(room < label.width(), state + ": label wrapped although it fits beside");
        ++counters.below;
    }
    ++counters.states;
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
        auto &doc = window.document();
        doc.addFace({{{0, 0, 0}, {2, 0, 0}, {2, 2, 0}, {0, 2, 0}}});
        auto *view = window.viewport();
        QMetaObject::invokeMethod(view, "changed");
        window.show();
        check(QTest::qWaitForWindowExposed(&window), "Overlap window exposed");
        window.activateWindow();
        check(QTest::qWaitForWindowActive(&window), "Overlap window active");
        view->setFocus();
        view->fit();
        view->setSelection(1);
        view->makeComponent("Reception desk assembly with a deliberately long name");
        const auto component = view->selectedBody();
        check(doc.instances().contains(component), "Component created");
        // Enter the component, then group the face and enter each nested group in turn.
        Id nested = component;
        for (int depth = 0; depth < 3; ++depth) {
            view->enterContext(nested);
            QTest::qWait(60);
            const auto face = view->selectionAt(view->project({1, 1, 0}));
            check(face && face->kind == SelectionKind::Face, "Nested face is pickable");
            view->selectEntities({*face});
            view->makeGroup();
            const auto group = view->selectedBody();
            check(group && doc.bodies().at(group)->parent == nested, "Nested group created");
            nested = group;
        }
        view->enterContext(0);
        QTest::qWait(60);

        Counters counters;
        for (const char *percent : {"100%", "200%", "100%"}) {
            chooseTextSize(window, percent);
            for (int mode : {1, 2}) {
                window.findChild<QAction *>("view.theme." + QString::number(mode))->trigger();
                for (const bool nestedContext : {false, true}) {
                    view->enterContext(nestedContext ? nested : 0);
                    check(view->selectionState().context() == (nestedContext ? nested : Id{0}),
                          "Context entered");
                    for (int width : {640, 701, 1200}) {
                        window.setFixedSize(width, 600);
                        QTest::qWait(30);
                        check(window.width() == width, "Exact logical width exercised");
                        verify(window,
                               QString("%1 text, theme %2, %3, width %4")
                                   .arg(percent)
                                   .arg(mode)
                                   .arg(nestedContext ? "nested" : "model")
                                   .arg(width),
                               counters);
                    }
                }
            }
        }
        check(counters.beside > 0 && counters.below > 0,
              "Matrix exercised both the beside and the wrapped placement");

        // The reported hardware case: 701x418 at the default text size.
        chooseTextSize(window, "100%");
        view->enterContext(0);
        window.setFixedSize(701, 418);
        verify(window, "701x418 model", counters);
        if (argc == 2) {
            const auto folder = QString::fromLocal8Bit(argv[1]);
            check(QDir().mkpath(folder), "Create capture directory");
            // Light theme, as in the hardware screenshot.
            window.findChild<QAction *>("view.theme.1")->trigger();
            verify(window, "701x418 light model", counters);
            check(captureWindow(window).save(folder + "/701x418-model.png"),
                  "Capture model context");
            view->enterContext(nested);
            verify(window, "701x418 nested", counters);
            check(captureWindow(window).save(folder + "/701x418-nested.png"),
                  "Capture nested context");
            view->enterContext(0);
            window.findChild<QAction *>("view.theme.2")->trigger();
            verify(window, "701x418 dark model", counters);
            check(captureWindow(window).save(folder + "/701x418-model-dark.png"),
                  "Capture dark model context");
        }

        // With no context chip the label keeps its original top-left position.
        auto *breadcrumb = window.findChild<QLabel *>("contextBreadcrumb");
        breadcrumb->hide();
        QTest::qWait(60);
        view->grabFramebuffer();
        check(view->paintedProjectionLabelRect().left() == 20 &&
                  view->paintedProjectionLabelRect().top() < 28 &&
                  view->paintedProjectionLabelRect().bottom() >= 28,
              "Hidden breadcrumb leaves the label at its original position");
        breadcrumb->show();
        verify(window, "breadcrumb restored", counters);
        std::cout << "Viewport label placement: " << counters.states << " states, "
                  << counters.beside << " beside the breadcrumb, " << counters.below
                  << " below it; never overlapping and always inside the viewport\n";
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
