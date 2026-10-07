#include "app/surface_format.hpp"
#include "app/window.hpp"
#include "io/document_io.hpp"
#include <QApplication>
#include <QTemporaryDir>
#include <QTest>
#include <iostream>
#include <set>
using namespace sketchy;
void check(bool value, const char *message) {
    if (!value)
        throw std::runtime_error(message);
}
std::set<double> starts(const Document &doc) {
    std::set<double> result;
    const auto &surface = doc.bodies().at(1)->surface;
    for (const auto &[id, face] : surface.faces)
        result.insert(surface.vertices.at(face.loops[0][0]).x);
    return result;
}
int main(int argc, char **argv) {
    QSurfaceFormat format;
    format.setVersion(3, 3);
    format.setProfile(QSurfaceFormat::CoreProfile);
    format.setDepthBufferSize(24);
    sketchy::setDefaultViewportFormat(format);
    QTemporaryDir settings;
    qputenv("XDG_CONFIG_HOME", settings.path().toUtf8());
    QApplication app(argc, argv);
    QString status;
    try {
        Window window;
        window.resize(1200, 800);
        window.show();
        auto &doc = window.document();
        auto *view = window.viewport();
        QObject::connect(view, &Viewport::message, [&](const QString &text) { status = text; });
        check(QTest::qWaitForWindowExposed(&window), "Array window exposed");
        window.activateWindow();
        check(QTest::qWaitForWindowActive(&window), "Array window active");
        doc.addFace({{{0, 0, 0}, {1, 0, 0}, {1, 1, 0}, {0, 1, 0}}});
        view->refresh();
        view->setSelection(1, 5);
        view->setTool(Viewport::Tool::Move);
        view->setTransformCopy(true);
        view->setFocus();
        check(view->measurements("[0,0,0]") && view->measurements("2m,0,0"), "Initial linear copy");
        auto *field = window.findChild<QLineEdit *>("measurements");
        QTest::keyClick(view, Qt::Key_X);
        check(field->hasFocus() && field->text() == "x",
              "x transfers keyboard input to Measurements");
        QTest::keyClicks(field, "3");
        QTest::keyClick(field, Qt::Key_Return);
        check(starts(doc) == std::set<double>{0, 2, 4, 6} &&
                  view->selectionState().entities().size() == 3,
              "x3 means three new copies and selects all copies");
        check(view->measurements("/4") && starts(doc) == std::set<double>{0, .5, 1, 1.5, 2},
              "/4 makes four equal intervals including original endpoint");
        check(view->measurements("4m,0,0") && starts(doc) == std::set<double>{0, 1, 2, 3, 4},
              "Changing distance preserves the current array count and division mode");
        const auto good = encodeDocument(doc);
        check(!view->measurements("x101") && encodeDocument(doc) == good,
              "Huge count rejection preserves current array");
        doc.undo();
        view->refresh();
        check(doc.bodies().at(1)->surface.faces.size() == 1,
              "Copies, divisions and spacing revisions share one undo item");
        check(!view->measurements("x2"), "Undo invalidates array amendment");
        view->setSelection(1);
        view->setTool(Viewport::Tool::Rotate);
        check(view->measurements("[2,0,0]") && view->measurements("90deg") &&
                  view->measurements("x3"),
              "Radial array from a rotation copy");
        check(doc.bodies().size() == 4 && view->selectionState().entities().size() == 3,
              "Radial count and copied selection");
        bool at90 = false, at180 = false, at270 = false;
        for (const auto &[id, body] : doc.bodies())
            if (id != 1) {
                auto point = doc.worldTransform(id).point({});
                at90 |= length(point - Vec3{2, -2, 0}) < tolerance;
                at180 |= length(point - Vec3{4, 0, 0}) < tolerance;
                at270 |= length(point - Vec3{2, 2, 0}) < tolerance;
            }
        check(at90 && at180 && at270, "Radial instances use exact pivot and angular spacing");
        check(view->measurements("540deg") && view->measurements("/3"),
              "Multi-turn sweep is retained when dividing");
        check(doc.bodies().size() == 4,
              "Multi-turn division retains explicit count even when placements coincide");
        unsigned opposite = 0;
        for (const auto &[id, body] : doc.bodies())
            if (id != 1)
                opposite += length(doc.worldTransform(id).point({}) - Vec3{4, 0, 0}) < tolerance;
        check(opposite == 2,
              "Division uses full 540-degree sweep rather than wrapped matrix angle");
        doc.undo();
        view->refresh();
        check(doc.bodies().size() == 1, "Radial array and sweep amendments undo once");
        std::cout << "Native linear/radial xN and /N arrays, exact spacing/counts, multi-turn "
                     "sweep, selection and guarded one-step amendment passed; DPR="
                  << view->devicePixelRatioF() << '\n';
        return 0;
    } catch (const std::exception &error) {
        std::cerr << error.what() << " (status: " << status.toStdString() << ")\n";
        return 1;
    }
}
