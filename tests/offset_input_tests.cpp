#include "app/window.hpp"
#include "io/document_io.hpp"
#include <QAction>
#include <QApplication>
#include <QDir>
#include <QSurfaceFormat>
#include <QTemporaryDir>
#include <QTest>
#include <iostream>
using namespace sketchy;
namespace {
void check(bool ok, const char *message) {
    if (!ok)
        throw std::runtime_error(message);
}
void move(Viewport &view, QPointF point, Qt::MouseButtons buttons = Qt::NoButton) {
    QMouseEvent event(QEvent::MouseMove, point, view.mapToGlobal(point.toPoint()), Qt::NoButton,
                      buttons, Qt::NoModifier);
    QCoreApplication::sendEvent(&view, &event);
    QCoreApplication::processEvents();
}
bool hasArea(const Document &doc, Id body, double area, double eps = 1e-6) {
    for (const auto &[id, face] : doc.bodies().at(body)->surface.faces)
        if (std::abs(doc.worldArea(body, id) - area) < eps)
            return true;
    return false;
}
} // namespace
int main(int argc, char **argv) {
    QSurfaceFormat format;
    format.setVersion(3, 3);
    format.setProfile(QSurfaceFormat::CoreProfile);
    format.setDepthBufferSize(24);
    QSurfaceFormat::setDefaultFormat(format);
    QTemporaryDir preferences;
    qputenv("XDG_CONFIG_HOME", preferences.path().toUtf8());
    QApplication app(argc, argv);
    Window window;
    window.show();
    try {
        check(QTest::qWaitForWindowExposed(&window), "Offset window exposed");
        window.activateWindow();
        check(QTest::qWaitForWindowActive(&window, 5000), "Offset window active");
        auto &doc = window.document();
        auto *view = window.viewport();
        const auto body = doc.addFace({{{0, 0, 0}, {4, 0, 0}, {4, 3, 0}, {0, 3, 0}}});
        const auto face = doc.bodies().at(body)->surface.faces.begin()->first;
        view->refresh();
        view->standardView(1);
        view->fit();
        QTest::qWait(150);
        auto *field = window.findChild<QLineEdit *>("measurements");
        auto *action = window.findChild<QAction *>("tool.21");
        check(field && action, "Offset action and Measurements available");
        view->setFocus();
        QTest::keyClick(view, Qt::Key_F);
        check(view->tool() == Viewport::Tool::Offset && action->isChecked(), "F selects Offset");
        const auto before = encodeDocument(doc);
        QTest::mouseClick(view, Qt::LeftButton, {}, view->project({1, .2, 0}).toPoint());
        move(*view, view->project({1, .7, 0}));
        check(view->previewValid() && encodeDocument(doc) == before,
              "Pointer offset preview does not publish");
        QTest::qWait(100);
        const auto pixels = view->grabFramebuffer();
        size_t previewPixels{};
        for (int y = 0; y < pixels.height(); ++y)
            for (int x = 0; x < pixels.width(); ++x) {
                const auto color = pixels.pixelColor(x, y);
                if (std::abs(color.red() - 185) < 15 && std::abs(color.green() - 118) < 15 &&
                    std::abs(color.blue() - 47) < 15)
                    ++previewPixels;
            }
        check(previewPixels > 50, "Validated offset preview is visibly drawn in the framebuffer");
        const auto capture = qEnvironmentVariable("SKETCHYUP_OFFSET_CAPTURE");
        if (!capture.isEmpty()) {
            QDir().mkpath(capture);
            check(pixels.save(capture + "/preview.png"), "Save rendered offset preview evidence");
        }
        QTest::keyClick(view, Qt::Key_Escape);
        check(!view->operationAnchor() && !view->previewValid() && encodeDocument(doc) == before,
              "Escape discards preview");
        view->setSelection(body, face);
        view->setFocus();
        const auto count = doc.history().total;
        QTest::keyClick(view, Qt::Key_Minus);
        QTest::keyClicks(field, "500mm");
        QTest::keyClick(field, Qt::Key_Return);
        check(view->hasFocus() && hasArea(doc, body, 6) && doc.history().total == count + 1,
              "Selected face accepts exact keyboard inset and one history item");
        check(view->measurements("-250mm") && hasArea(doc, body, 8.75) &&
                  doc.history().total == count + 1,
              "Numeric re-entry replaces offset");
        check(view->selectionState().entities().size() == 2,
              "Both generated faces remain selected after amendment");
        const auto changed = encodeDocument(doc);
        if (!capture.isEmpty()) {
            QTest::qWait(100);
            check(window.grab().save(capture + "/applied.png"), "Save offset applied evidence");
        }
        check(!view->measurements("-2m") && encodeDocument(doc) == changed,
              "Collapsed revision preserves prior committed offset");
        QTest::keyClick(view, Qt::Key_Z, Qt::ControlModifier);
        check(doc.bodies().at(body)->surface.faces.size() == 1 && hasArea(doc, body, 12),
              "One Undo removes all amended offsets");
        QTest::keyClick(view, Qt::Key_Z, Qt::ControlModifier | Qt::ShiftModifier);
        check(hasArea(doc, body, 8.75), "Redo restores revised offset");
        const auto stale = encodeDocument(doc);
        check(!view->measurements("-.4m") && encodeDocument(doc) == stale,
              "Undo/redo invalidates offset amendment");
        view->setTool(Viewport::Tool::Offset);
        QTest::mouseClick(view, Qt::LeftButton, {}, view->project({1, 1, 0}).toPoint());
        move(*view, view->project({1, 1.2, 0}));
        doc.addFace({{{8, 0, 0}, {9, 0, 0}, {9, 1, 0}, {8, 1, 0}}});
        const auto intervened = encodeDocument(doc);
        check(!view->measurements("-.1m") && encodeDocument(doc) == intervened,
              "Intervening edit fences active offset");
        view->setTool(Viewport::Tool::Offset);
        auto reopened = decodeContainer(encodeContainer(doc));
        check(encodeDocument(reopened) == encodeDocument(doc), "Native offset persists");
        // Drag commits through the same command; mouse rounding has a pixel-sized budget.
        doc = Document();
        const auto dragBody = doc.addFace({{{0, 0, 0}, {4, 0, 0}, {4, 3, 0}, {0, 3, 0}}});
        view->refresh();
        view->standardView(1);
        view->fit();
        QTest::qWait(100);
        const auto start = view->project({1, .2, 0}).toPoint(),
                   end = view->project({1, .7, 0}).toPoint();
        QTest::mousePress(view, Qt::LeftButton, {}, start);
        move(*view, end, Qt::LeftButton);
        QTest::mouseRelease(view, Qt::LeftButton, {}, end);
        check(hasArea(doc, dragBody, 6, .15) && doc.history().total == 2,
              "Pointer drag commits inset once");
        view->setTool(Viewport::Tool::Offset);
        doc = Document();
        const auto ring = doc.addFace({{{0, 0, 0}, {10, 0, 0}, {10, 8, 0}, {0, 8, 0}},
                                       {{3, 2, 0}, {7, 2, 0}, {7, 6, 0}, {3, 6, 0}}});
        view->refresh();
        view->standardView(1);
        view->fit();
        QTest::qWait(100);
        QTest::mouseClick(view, Qt::LeftButton, {}, view->project({2.8, 4, 0}).toPoint());
        move(*view, view->project({3.3, 4, 0}));
        check(view->previewValid() && !field->text().trimmed().startsWith('-'),
              "Moving toward a hole previews positive outward material offset");
        const auto ringBefore = encodeDocument(doc);
        view->standardView(0);
        check(encodeDocument(doc) == ringBefore && view->operationAnchor().has_value(),
              "Camera navigation retains the proposal without publishing");
        check(view->measurements("500mm"), "Numeric distance commits the holed face after orbit");
        double ringArea{};
        for (const auto &[id, face] : doc.bodies().at(ring)->surface.faces)
            ringArea += doc.worldArea(ring, id);
        check(std::abs(ringArea - 83) < tolerance &&
                  doc.bodies().at(ring)->surface.wires.size() == 4,
              "Outset retains original hole and its contracted wire outline");
        std::cout << "Native Offset shortcut, preview/cancel, exact numeric amendment, stale "
                     "guards, Undo/Redo and drag passed\n";
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
