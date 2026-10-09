#include "app/inspection_service.hpp"
#include "app/surface_format.hpp"
#include "app/window.hpp"
#include "io/document_io.hpp"
#include <QApplication>
#include <QCryptographicHash>
#include <QFile>
#include <QJsonDocument>
#include <QOpenGLContext>
#include <QSettings>
#include <QTemporaryDir>
#include <QTest>
#include <QWindow>
#include <future>
#include <iostream>
#include <numbers>
using namespace sketchy;
void check(bool ok, const char *message) {
    if (!ok)
        throw std::runtime_error(message);
}
template <class F> void rejects(const char *code, F operation) {
    try {
        operation();
    } catch (const InspectionError &error) {
        check(error.code() == code,
              (std::string("Unexpected inspection error ") + error.code() + ": " + error.what())
                  .c_str());
        return;
    }
    throw std::runtime_error(std::string("Expected ") + code);
}
QJsonObject request(const Document &doc, QString query, QJsonObject fields = {}) {
    fields["apiVersion"] = 1;
    fields["documentId"] = QString::fromStdString(doc.identity());
    fields["expectedRevision"] = QString::number(doc.revision());
    fields["query"] = query;
    return fields;
}
void focus(QWidget &widget) {
    widget.window()->activateWindow();
    check(QTest::qWaitFor(
              [&] { return QGuiApplication::focusWindow() == widget.window()->windowHandle(); }),
          "Native inspection test window focus");
    widget.setFocus();
    check(QTest::qWaitFor([&] { return widget.hasFocus(); }), "Viewport keyboard focus");
}
int main(int argc, char **argv) {
    QSurfaceFormat format;
    format.setVersion(3, 3);
    format.setProfile(QSurfaceFormat::CoreProfile);
    format.setDepthBufferSize(24);
    sketchy::setDefaultViewportFormat(format);
    QTemporaryDir files;
    qputenv("XDG_CONFIG_HOME", files.path().toUtf8());
    QApplication app(argc, argv);
    if (app.arguments().contains("--print-capabilities")) {
        std::cout << QJsonDocument(desktopInspectionCapabilities()).toJson().toStdString();
        return 0;
    }
    QSettings("SketchyUp", "SketchyUp").setValue("recoverySeconds", 0);
    Window window;
    window.resize(1280, 850);
    auto &doc = window.document();
    auto &view = *window.viewport();
    try {
        QFile schema("docs/api/inspection-desktop-v1.json");
        check(schema.open(QIODevice::ReadOnly) &&
                  QJsonDocument::fromJson(schema.readAll()).object() ==
                      desktopInspectionCapabilities(),
              "Published desktop registry must match executable discovery");
        window.openPath("examples/m4-room-study.sketchyup");
        Id room{}, selectedWindow{};
        for (const auto &[id, body] : doc.bodies()) {
            if (body->name == "Room study")
                room = id;
            if (body->name == "Window A")
                selectedWindow = id;
        }
        check(room && selectedWindow, "Room fixture targets missing");
        window.show();
        check(QTest::qWaitForWindowExposed(&window), "Native inspection window exposed");
        focus(view);
        check(QTest::qWaitFor([&] { return view.rendererReady(); }), "Renderer initialized");
        view.enterContext(room);
        view.selectEntities({{selectedWindow, SelectionKind::Body, 0}});
        view.fit();
        QCoreApplication::processEvents();
        const auto original = encodeContainer(doc);
        const auto stamp = doc.saveStamp();
        const auto selection = view.selectionState().entities();
        const auto selected = window.inspect(request(doc, "selection.get"))["data"].toObject();
        check(selected["total"] == 1, "Desktop API must expose actual native selection");
        const auto ref = selected["items"].toArray()[0].toObject()["ref"].toObject();
        const auto measures = window
                                  .inspect(request(doc, "measure.entity",
                                                   {{"target", ref}, {"space", "local"}}))["data"]
                                  .toObject();
        check(std::abs(measures["bounds"].toObject()["dimensions"].toArray()[0].toDouble() - 1.2) <
                  tolerance,
              "Native selected window measurement mismatch");
        const auto camera = window.inspect(request(doc, "view.describe"))["data"].toObject();
        check(camera["clipFromWorld"].toArray().size() == 16 && camera["selectedCount"] == 1 &&
                  camera["activeContext"].toObject()["body"] == QString::number(room),
              "Camera metadata lacks model context");
        check(camera["angleUnits"] == "rad" &&
                  std::abs(camera["fieldOfView"].toDouble() -
                           view.fieldOfView() * std::numbers::pi / 180.0) < 1e-12,
              "Camera angles must use canonical radians");
        const auto captureRequest =
            request(doc, "view.capture", {{"maxWidth", 640}, {"maxHeight", 480}});
        const auto result = window.inspect(captureRequest);
        const auto capture = result["data"].toObject();
        const auto png = QByteArray::fromBase64(capture["image"].toString().toLatin1());
        const auto image = QImage::fromData(png, "PNG");
        check(!image.isNull() && image.width() <= 640 && image.height() <= 480 &&
                  image.width() == capture["width"] && image.height() == capture["height"],
              "Capture dimensions and PNG disagree");
        check(capture["sha256"] ==
                      QString::fromLatin1(
                          QCryptographicHash::hash(png, QCryptographicHash::Sha256).toHex()) &&
                  png.size() <= 3 * 1024 * 1024 &&
                  QJsonDocument(result).toJson(QJsonDocument::Compact).size() <=
                      4 * 1024 * 1024 + 64 * 1024,
              "Capture hash or byte bounds incorrect");
        check(capture["clipFromWorld"] == camera["clipFromWorld"] &&
                  capture["devicePixelRatio"] == view.devicePixelRatioF(),
              "Returned camera/DPR must match the captured frame");
        std::set<QRgb> colors;
        for (int y = 0; y < image.height(); y += 3)
            for (int x = 0; x < image.width(); x += 3)
                colors.insert(image.pixel(x, y));
        check(colors.size() > 30,
              "Capture must contain rendered room detail, not an empty response");
        if (const auto argument = app.arguments().indexOf("--capture");
            argument >= 0 && argument + 1 < app.arguments().size())
            check(image.save(app.arguments()[argument + 1]),
                  "Cannot write requested capture artifact");
        check(doc.isCurrentSnapshot(stamp) && encodeContainer(doc) == original &&
                  view.selectionState().entities() == selection && !doc.dirty(),
              "Inspection must not edit model or native selection");
        view.standardView(1);
        view.fit();
        QCoreApplication::processEvents();
        const auto top = window.inspect(captureRequest)["data"].toObject();
        check(top["projection"] == "orthographic" &&
                  top["clipFromWorld"] != capture["clipFromWorld"] &&
                  top["sha256"] != capture["sha256"],
              "Camera changes must produce new image and projection metadata without model edits");
        check(doc.isCurrentSnapshot(stamp), "View changes must leave document revision alone");
        auto wrong = captureRequest;
        wrong["documentId"] = "wrong";
        rejects("WRONG_DOCUMENT", [&] { window.inspect(wrong); });
        wrong = captureRequest;
        wrong["maxWidth"] = 2048;
        rejects("LIMIT_EXCEEDED", [&] { window.inspect(wrong); });
        wrong = captureRequest;
        wrong["path"] = "/tmp/forbidden-inspection-output.png";
        rejects("INVALID_REQUEST", [&] { window.inspect(wrong); });
        const auto snapshot = window.inspect(request(doc, "snapshot.begin"));
        wrong = captureRequest;
        wrong["snapshotId"] = snapshot["snapshotId"];
        rejects("INVALID_REQUEST", [&] { window.inspect(wrong); });
        auto worker = std::async(std::launch::async, [&] {
            rejects("WRONG_THREAD", [&] { window.inspect(captureRequest); });
        });
        worker.get();
        view.setTool(Viewport::Tool::Rectangle);
        view.setDrawingPlane(DrawingPlane::make({}, {0, 0, 1}, {1, 0, 0}));
        focus(view);
        QTest::mouseClick(&view, Qt::LeftButton, Qt::NoModifier, view.project({2, 2, 0}).toPoint());
        check(QTest::qWaitFor([&] { return view.inspectionBusy(); }),
              "Rectangle anchor must create a pending gesture");
        rejects("VIEW_BUSY", [&] { window.inspect(captureRequest); });
        check(view.inspectionBusy() && doc.isCurrentSnapshot(stamp),
              "Capture rejection must preserve the pending gesture");
        view.cancel();
        view.setTool(Viewport::Tool::Select);
        view.setClipPlane(std::array<double, 4>{1, 0, 0, -2});
        rejects("UNSUPPORTED_VIEW", [&] { window.inspect(captureRequest); });
        view.setClipPlane({});
        doc.move(selectedWindow, {.2, 0, 0});
        rejects("STALE_REVISION", [&] { window.inspect(captureRequest); });
        view.refresh();
        auto capturedQuery =
            request(doc, "measure.entity",
                    {{"target", ref}, {"space", "local"}, {"snapshotId", snapshot["snapshotId"]}});
        capturedQuery["expectedRevision"] = snapshot["revision"];
        check(window.inspect(capturedQuery)["data"] == measures,
              "Desktop capture session must retain the original selected measurement");
        window.hide();
        QCoreApplication::processEvents();
        rejects("VIEW_UNAVAILABLE", [&] { window.inspect(request(doc, "view.capture")); });
        doc.markSaved();
        std::cout << "Desktop selection, snapshots and bounded PNG capture passed at DPR "
                  << view.devicePixelRatioF() << '\n';
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        doc.markSaved();
        return 1;
    }
}
