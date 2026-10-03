#include "app/window.hpp"
#include "io/document_io.hpp"
#include <QApplication>
#include <QAction>
#include <QDir>
#include <QTest>
#include <iostream>
using namespace sketchy;
void check(bool ok, const char *message) { if (!ok) throw std::runtime_error(message); }
int main(int argc, char **argv) {
    QApplication app(argc, argv);
    QWidget host;
    Window window(&host);
    window.setWindowFlags(Qt::Widget);
    host.show();
    window.show();
    if (!QTest::qWaitForWindowExposed(&host,5000)) return 2;
    try {
        window.demo();
        const auto original = encodeDocument(window.document());
        auto *field = window.findChild<QLineEdit *>("measurements");
        auto *tray = window.findChild<QWidget *>("tray");
        const int widths[]{640,900,1200,1600};
        const char *classes[]{"compact","standard","expanded","wide"};
        for (int i=0; i<4; ++i) {
            window.setFixedSize(widths[i],600);
            host.resize(widths[i],600);
            QTest::qWait(100);
            check(window.width() == widths[i], "Exact logical width tested");
            check(window.property("layoutClass") == classes[i], "Responsive class matches boundary");
            check(tray->isVisible() == (widths[i] >= 800), "Tray follows responsive boundary");
            check(field->isVisible() &&
                  window.rect().contains(field->mapTo(&window, field->rect().bottomRight())),
                  "Measurements remain on screen at every width");
            check(window.viewport()->width() >= 300, "Viewport retains usable width");
            if (argc == 2) {
                QDir().mkpath(argv[1]);
                check(window.grab().save(QString::fromLocal8Bit(argv[1]) +
                    QString("/shell-%1.png").arg(widths[i])), "Save native shell capture");
            }
        }
        for (const QString &name : {QString("Dark theme"), QString("Light theme")}) {
            for (auto *action : window.findChildren<QAction *>())
                if (action->text() == name) action->trigger();
            QTest::qWait(100);
            auto frame = window.viewport()->grabFramebuffer();
            check(!frame.isNull(), "Themed viewport rendered");
            if (argc == 2)
                check(window.grab().save(QString::fromLocal8Bit(argv[1]) + "/" + name + ".png"),
                      "Save themed shell capture");
        }
        check(encodeDocument(window.document()) == original, "Layout and theme preserve document");
        window.document().markSaved();
        std::cout << "Native shell: 640/900/1200/1600 logical widths, theme, measurements passed\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        window.document().markSaved();
        return 1;
    }
}
