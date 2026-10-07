#include "app/window.hpp"
#include "core/entity_measure.hpp"
#include "io/document_io.hpp"
#include <QApplication>
#include <QDialog>
#include <QLineEdit>
#include <QListWidget>
#include <QSettings>
#include <QSurfaceFormat>
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
QJsonArray records(const Document &doc) {
    auto result = encodeBodies(doc.bodies());
    for (int i = 0; i < result.size(); ++i) {
        auto body = result[i].toObject();
        // Undo restores content while allocation floors remain monotonic.
        body.remove("nextId");
        body.remove("nextEdgeId");
        result[i] = body;
    }
    return result;
}
void key(Qt::Key key, Qt::KeyboardModifiers modifiers = Qt::NoModifier) {
    auto *focused = QApplication::focusWidget();
    check(focused, "Keyboard workflow has a focus owner");
    QTest::keyClick(focused, key, modifiers);
    QCoreApplication::processEvents();
}
void measurement(Window &window, const QString &text) {
    auto *field = window.findChild<QLineEdit *>("measurements");
    check(field && field->isEnabled(), "Measurements is available");
    for (int i = 0; !field->hasFocus() && i < 12; ++i)
        key(Qt::Key_F6);
    check(field->hasFocus(), "F6 reaches Measurements without pointer or direct focus assignment");
    key(Qt::Key_A, Qt::ControlModifier);
    QTest::keyClicks(field, text);
    check(field->text() == text, "Typing units does not trigger modeling shortcuts");
    key(Qt::Key_Return);
    if (!QTest::qWaitFor([&] { return window.viewport()->hasFocus(); }, 1000))
        throw std::runtime_error(
            ("Measurement " + text + ": " + field->accessibleDescription() + "; focus=" +
             (QApplication::focusWidget() ? QApplication::focusWidget()->objectName()
                                          : QString("none")))
                .toStdString());
}
void palette(Window &window, const QString &text) {
    bool handled{};
    std::exception_ptr failure;
    QTimer timer;
    QObject::connect(&timer, &QTimer::timeout, &window, [&] {
        auto *dialog = window.findChild<QDialog *>("commandPalette");
        if (handled || !dialog || !dialog->isVisible())
            return;
        handled = true;
        try {
            auto *query = dialog->findChild<QLineEdit *>("paletteQuery");
            check(query && QTest::qWaitFor([&] { return query->hasFocus(); }),
                  "Palette gives its search field keyboard focus");
            QTest::keyClicks(query, text);
            auto *results = dialog->findChild<QListWidget *>("paletteResults");
            check(results && results->count() == 1, "Face query has one unambiguous result");
            key(Qt::Key_Return);
        } catch (...) {
            failure = std::current_exception();
            dialog->reject();
        }
    });
    timer.start(10);
    key(Qt::Key_K, Qt::ControlModifier);
    timer.stop();
    if (failure)
        std::rethrow_exception(failure);
    check(handled && QTest::qWaitFor([&] { return window.viewport()->hasFocus(); }),
          "Palette selection returns to modeling");
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
    QSettings().setValue("defaultUnits", "m");
    QSettings().setValue("recoverySeconds", 0);
    Window window;
    try {
        window.resize(1200, 850);
        window.show();
        check(QTest::qWaitForWindowExposed(&window), "Keyboard modeling window exposed");
        window.activateWindow();
        check(QTest::qWaitForWindowActive(&window), "Keyboard modeling window activated");
        auto *view = window.viewport();
        // Initial focus is fixture setup. Every subsequent user operation uses keys.
        view->setFocus();
        check(QTest::qWaitFor([&] { return view->hasFocus(); }), "Initial viewport focus");
        key(Qt::Key_R);
        check(view->tool() == Viewport::Tool::Rectangle, "Rectangle shortcut selects the tool");
        measurement(window, "[0,0,0]");
        measurement(window, "2m,3m");
        auto &doc = window.document();
        check(doc.bodies().size() == 1, "Typed rectangle creates one body");
        const auto body = doc.bodies().begin()->first;
        const auto face = doc.bodies().begin()->second->surface.faces.begin()->first;
        check(std::abs(doc.worldArea(body, face) - 6) < 1e-9,
              "Typed rectangle measures six square metres");
        const auto planar = records(doc);
        const auto planarPosition = doc.history(0, 1).position;
        palette(window, QString("face %1/%2").arg(body).arg(face));
        key(Qt::Key_P);
        check(view->tool() == Viewport::Tool::Extrude, "Push/pull shortcut selects the tool");
        measurement(window, "4m");
        const auto result = measureEntity(doc, {body, SelectionKind::Body, 0});
        check(result.world.bounds && result.world.volume &&
                  length(result.world.bounds->dimensions() - Vec3{2, 3, 4}) < 1e-9 &&
                  std::abs(*result.world.volume - 24) < 1e-9,
              "Keyboard-only construction produces a two by three by four metre solid");
        const auto solid = records(doc);
        check(doc.history(0, 1).position == planarPosition + 1, "Typed extrusion is one edit");
        key(Qt::Key_Z, Qt::ControlModifier);
        check(records(doc) == planar && doc.history(0, 1).position == planarPosition,
              "Keyboard undo restores the rectangle");
        key(Qt::Key_Z, Qt::ControlModifier | Qt::ShiftModifier);
        check(records(doc) == solid && doc.history(0, 1).position == planarPosition + 1,
              "Keyboard redo restores the exact solid");
        doc.markSaved();
        std::cout << "Keyboard modeling: rectangle, F6 Measurements, unit suffix typing, palette "
                     "face selection, push/pull, measured solid and undo/redo passed\n";
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        window.document().markSaved();
        return 1;
    }
}
