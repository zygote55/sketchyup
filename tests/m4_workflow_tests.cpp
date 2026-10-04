#include "app/window.hpp"
#include "core/components.hpp"
#include "core/entity_measure.hpp"
#include "core/groups.hpp"
#include "core/materials.hpp"
#include "core/tags.hpp"
#include "io/assets.hpp"
#include "io/document_io.hpp"
#include <QAction>
#include <QApplication>
#include <QDialog>
#include <QFile>
#include <QFileDialog>
#include <QJsonDocument>
#include <QMessageBox>
#include <QProcess>
#include <QPushButton>
#include <QSettings>
#include <QSurfaceFormat>
#include <QTemporaryDir>
#include <QTest>
#include <QTimer>
#include <QWindow>
#include <iostream>
using namespace sketchy;
void check(bool value, const char *message) {
    if (!value)
        throw std::runtime_error(message);
}
QByteArray read(const QString &path) {
    QFile file(path);
    check(file.open(QIODevice::ReadOnly), "Read M4 evidence");
    return file.readAll();
}
void write(const QString &path, const QByteArray &bytes) {
    QFile file(path);
    check(file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size(),
          "Write M4 evidence");
}
void focus(QWidget *widget) {
    widget->window()->activateWindow();
    check(QTest::qWaitFor(
              [&] { return QGuiApplication::focusWindow() == widget->window()->windowHandle(); }),
          "M4 focus settled");
    widget->setFocus();
}
void modal(Window &window, const QString &name, const std::function<void()> &open,
           const std::function<void(QDialog *)> &operation) {
    bool handled = false;
    std::exception_ptr failure;
    QTimer timer;
    QObject::connect(&timer, &QTimer::timeout, &window, [&] {
        auto *dialog = window.findChild<QDialog *>(name);
        if (handled || !dialog || !dialog->isVisible())
            return;
        handled = true;
        try {
            operation(dialog);
        } catch (...) {
            failure = std::current_exception();
        }
        if (dialog->isVisible())
            dialog->reject();
    });
    timer.start(10);
    open();
    timer.stop();
    if (failure)
        std::rethrow_exception(failure);
    check(handled, "Expected M4 lifecycle dialog opened");
}
Vec3 dimensions(const Document &doc, Id id) {
    return measureEntity(doc, {id, SelectionKind::Body, 0}).world.bounds->dimensions();
}
Id memberNamed(const Document &doc, Id instance, const std::string &name) {
    for (auto [canonical, live] : doc.instances().at(instance)->members)
        if (doc.bodies().at(live)->name == name)
            return live;
    throw std::runtime_error("Missing named M4 component member");
}
int main(int argc, char **argv) {
    // The child publishes a durable recovery image and stays alive until the parent kills it.
    if (argc > 1 && QString::fromLocal8Bit(argv[1]) == "--crash-writer") {
        QCoreApplication app(argc, argv);
        try {
            auto doc = loadDocument(app.arguments()[2]);
            auto saved = loadDocument(app.arguments()[4]);
            doc.markRecovered();
            RecoveryWriter writer(app.arguments()[3], QString::fromStdString(doc.identity()));
            writer.write(captureRecovery(
                doc, {app.arguments()[4], saved.revision(), QDateTime::currentDateTimeUtc()}));
            std::cout << writer.key().toStdString() << std::endl;
            return app.exec();
        } catch (const std::exception &error) {
            std::cerr << error.what() << '\n';
            return 1;
        }
    }
    QSurfaceFormat format;
    format.setVersion(3, 3);
    format.setProfile(QSurfaceFormat::CoreProfile);
    format.setDepthBufferSize(24);
    QSurfaceFormat::setDefaultFormat(format);
    QTemporaryDir files;
    qputenv("XDG_CONFIG_HOME", files.path().toUtf8());
    QApplication::setAttribute(Qt::AA_DontUseNativeDialogs);
    QApplication app(argc, argv);
    QSettings preferences("SketchyUp", "SketchyUp");
    preferences.setValue("defaultUnits", "mm");
    preferences.setValue("recoverySeconds", 0);
    Window window;
    window.resize(1280, 850);
    auto &doc = window.document();
    auto *view = window.viewport();
    QString status;
    QObject::connect(view, &Viewport::message, [&](const QString &text) { status = text; });
    try {
        window.show();
        check(QTest::qWaitForWindowExposed(&window), "M4 model window exposed");
        focus(view);
        view->setDrawingPlane(DrawingPlane{});
        view->setTool(Viewport::Tool::Rectangle);
        check(view->measurements("[0,0,0]") && view->measurements("6000,4000"),
              "Native measured room footprint");
        const auto wall = view->selectedBody();
        view->enterContext(wall);
        view->setTool(Viewport::Tool::Rectangle);
        check(view->measurements("[200,200,0]") && view->measurements("5600,3600"),
              "Native room wall thickness");
        Id inside = 0;
        for (auto [id, face] : doc.bodies().at(wall)->surface.faces)
            if (std::abs(doc.bodies().at(wall)->surface.area(id) - 20.16) < tolerance)
                inside = id;
        check(inside != 0, "Room interior is independently editable");
        view->setSelection(wall, inside);
        view->deleteSelection();
        view->leaveContext();
        view->setTool(Viewport::Tool::Extrude);
        view->standardView(0);
        view->fit();
        view->grabFramebuffer();
        QTest::mouseClick(view, Qt::LeftButton, {}, view->project({.1, 2, 0}).toPoint());
        check(view->measurements("2700"), "Native room height in millimeters");
        check(std::abs(dimensions(doc, wall).z - 2.7) < tolerance, "Room height is 2.7 meters");
        renameEntity(doc, wall, "Room walls");
        // Draw and push each region through the front wall to its single opposing face.
        view->enterContext(wall);
        for (double x : {1., 4.}) {
            view->setDrawingPlane(DrawingPlane::make({x, 0, .9}, {0, -1, 0}, {1, 0, 0}), wall);
            view->setTool(Viewport::Tool::Rectangle);
            check(view->measurements(QString("[%1m,0m,0.9m]").arg(x)) &&
                      view->measurements("1200,1000"),
                  "Native window opening outline");
            const auto patch = view->selectedFace();
            check(patch != 0, "Opening region selected");
            view->setTool(Viewport::Tool::Extrude);
            view->grabFramebuffer();
            QTest::mouseClick(view, Qt::LeftButton, {}, view->project({x + .6, 0, 1.4}).toPoint());
            check(view->measurements("-200"), "Native through-wall window opening");
        }
        view->leaveContext();
        view->setDrawingPlane({});
        view->setTool(Viewport::Tool::Select);
        auto wallMeasure = measureEntity(doc, {wall, SelectionKind::Body, 0});
        check(wallMeasure.world.volume && std::abs(*wallMeasure.world.volume - 9.888) < 1e-6,
              "Two 1.2 by 1.0 meter openings preserve closed wall volume");
        view->setSelection(wall);
        view->makeGroup();
        const auto room = view->selectedBody();
        renameEntity(doc, room, "Room study");
        const auto wallPaint = createMaterial(doc, "Warm plaster", {.84f, .80f, .71f});
        assignMaterial(doc, wall, {}, wallPaint);
        const auto framePaint = createMaterial(doc, "Window frame", {.25f, .31f, .32f});
        const auto missing = createAsset(doc, "Missing glass texture.png", "image/png");
        createAsset(doc, "Study notes.txt", "text/plain",
                    assetPayload("M4: frame outer size 1200 x 1000 mm; sill 900 mm."));
        const auto glassPaint = createMaterial(doc, "Glass", {.2f, .55f, .8f}, .35f, missing);
        const auto frame = doc.addFace(
            {{{1, .15, .9}, {2.2, .15, .9}, {2.2, .15, 1.9}, {1, .15, 1.9}},
             {{1.08, .15, .98}, {1.08, .15, 1.82}, {2.12, .15, 1.82}, {2.12, .15, .98}}},
            "Frame");
        doc.extrude(frame, doc.bodies().at(frame)->surface.faces.begin()->first, .1);
        doc.transform(frame, {}, room);
        assignMaterial(doc, frame, {}, framePaint);
        const auto frameGroup = createGroup(doc, {frame}, "Frame parts");
        const auto glass =
            doc.addFace({{{1.08, .1, .98}, {2.12, .1, .98}, {2.12, .1, 1.82}, {1.08, .1, 1.82}}},
                        "Glass panel");
        doc.transform(glass, {}, room);
        assignMaterial(doc, glass, {}, glassPaint);
        const auto floor =
            doc.addFace({{{.2, .2, 0}, {5.8, .2, 0}, {5.8, 3.8, 0}, {.2, 3.8, 0}}}, "Floor");
        doc.transform(floor, {}, room);
        view->refresh();
        view->enterContext(room);
        view->selectEntities(
            {{frameGroup, SelectionKind::Body, 0}, {glass, SelectionKind::Body, 0}});
        view->makeGroup();
        view->makeComponent("Window assembly");
        const auto first = view->selectedBody();
        const auto definition = doc.instances().at(first)->definition;
        view->placeComponent(definition, {3, 0, 0});
        const auto second = view->selectedBody();
        renameEntity(doc, first, "Window A");
        renameEntity(doc, second, "Window B");
        const auto windows = createTag(doc, "Windows");
        assignTag(doc, first, windows);
        assignTag(doc, second, windows);
        const auto firstFrame = memberNamed(doc, first, "Frame"),
                   secondFrame = memberNamed(doc, second, "Frame");
        const auto nested = memberNamed(doc, first, "Frame parts");
        const auto firstGlass = memberNamed(doc, first, "Glass panel");
        const auto wallBefore = doc.bodies().at(wall), glassBefore = doc.bodies().at(firstGlass);
        view->refresh();
        view->enterContext(first);
        view->enterContext(nested);
        view->setSelection(firstFrame);
        view->setTool(Viewport::Tool::Scale);
        check(view->measurements("[1m,0.15m,0.9m]") && view->measurements("1,2,1"),
              "Native nested frame edit");
        check(std::abs(dimensions(doc, firstFrame).y - .2) < tolerance &&
                  std::abs(dimensions(doc, secondFrame).y - .2) < tolerance &&
                  doc.bodies().at(wall) == wallBefore && doc.bodies().at(firstGlass) == glassBefore,
              "Shared nested edit propagates to both frames while walls and glazing stay isolated");
        view->leaveContext();
        view->makeComponentUnique(true);
        check(doc.instances().at(first)->definition != doc.instances().at(second)->definition,
              "Native make-unique separates definitions");
        const auto savedPath = files.filePath("Room study.sketchyup");
        saveDocument(doc, savedPath);
        const auto savedBytes = read(savedPath);
        auto reopened = loadDocument(savedPath);
        check(encodeContainer(reopened) == savedBytes &&
                  reopened.displayUnits() == DisplayUnit::Millimeters &&
                  reopened.instances().size() == 2,
              "Room save/reopen retains units, shared/unique bindings, materials, assets and "
              "identities");
        const auto peerBefore = doc.bodies().at(secondFrame);
        view->enterContext(nested);
        view->setSelection(firstFrame);
        view->setTool(Viewport::Tool::Scale);
        check(view->measurements("[1m,0.15m,0.9m]") && view->measurements("1,1.5,1"),
              "Native unique frame edit");
        check(std::abs(dimensions(doc, firstFrame).y - .3) < tolerance &&
                  doc.bodies().at(secondFrame) == peerBefore,
              "Unique edit changes only selected window and keeps sibling frame exact");
        const auto target = doc.history().position;
        window.findChild<QAction *>("edit.undo")->trigger();
        check(std::abs(dimensions(doc, firstFrame).y - .2) < tolerance && !doc.dirty(),
              "Menu undo restores explicitly saved room");
        focus(view);
        QTest::keyClick(view, Qt::Key_Z, Qt::ControlModifier | Qt::ShiftModifier);
        check(std::abs(dimensions(doc, firstFrame).y - .3) < tolerance &&
                  doc.history().position == target,
              "Keyboard redo restores unique edit");
        window.findChild<QAction *>("view.history")->trigger();
        auto *history = window.findChild<QTreeWidget *>("historySteps");
        QTreeWidgetItem *row = nullptr;
        for (int i = 0; i < history->topLevelItemCount(); ++i)
            if (history->topLevelItem(i)->data(0, Qt::UserRole).toULongLong() == target - 1)
                row = history->topLevelItem(i);
        check(row, "Saved room position is in History");
        history->setCurrentItem(row);
        focus(history);
        QTest::keyClick(history, Qt::Key_Return);
        check(QTest::qWaitFor([&] { return doc.history().position == target - 1; }) && !doc.dirty(),
              "History agrees with room undo");
        window.findChild<QPushButton *>("historyRedo")->click();
        check(QTest::qWaitFor([&] { return doc.history().position == target; }),
              "History redo restores unsaved room edit");
        const auto expected = encodeContainer(doc);
        // Actual unclean process exit: parent kills a live writer after its durable
        // acknowledgement.
        const auto input = files.filePath("crash-input.sketchyup"),
                   recoveryRoot = files.filePath("recovery");
        write(input, expected);
        QProcess child;
        child.start(QCoreApplication::applicationFilePath(),
                    {"--crash-writer", input, recoveryRoot, savedPath});
        check(child.waitForStarted(5000) && child.waitForReadyRead(10000),
              "Crash writer durably acknowledged checkpoint");
        const auto key = QString::fromUtf8(child.readLine()).trimmed();
        check(!key.isEmpty(), "Crash writer returned recovery identity");
        child.kill();
        check(child.waitForFinished(5000) && child.exitStatus() == QProcess::CrashExit,
              "Writer terminated uncleanly");
        const auto recovered = readRecovery(recoveryRoot, key);
        check(recovered.verified && !recovered.busy && recovered.document &&
                  encodeContainer(*recovered.document) == expected && recovered.document->dirty() &&
                  read(savedPath) == savedBytes,
              "Crash recovery releases stale lock and restores exact unsaved model without "
              "touching explicit save");
        doc.markSaved();
        window.findChild<QAction *>("file.new")->trigger();
        window.startRecovery(recoveryRoot);
        modal(
            window, "recoveryDialog", [&] { window.showRecovery(); },
            [&](QDialog *dialog) {
                auto *list = dialog->findChild<QTreeWidget *>("recoveryList");
                check(list->topLevelItemCount() == 1, "Room recovery listed once");
                list->setCurrentItem(list->topLevelItem(0));
                dialog->findChild<QPushButton *>("openRecovered")->click();
            });
        check(encodeContainer(doc) == expected && doc.dirty() && !doc.canUndo(),
              "Native recovery opens exact dirty room with no fabricated history");
        QTimer closeCancel;
        bool canceled = false, nonstandardDismissal = false;
        QObject::connect(&closeCancel, &QTimer::timeout, &window, [&] {
            if (auto *box = qobject_cast<QMessageBox *>(QApplication::activeModalWidget())) {
                canceled = true;
                if (nonstandardDismissal)
                    box->reject();
                else
                    box->button(QMessageBox::Cancel)->click();
            }
        });
        closeCancel.start(10);
        check(!window.close(), "Cancel close keeps recovered room open");
        closeCancel.stop();
        check(canceled && encodeContainer(doc) == expected && doc.dirty(),
              "Canceled close preserves recovered model");
        nonstandardDismissal = true;
        closeCancel.start(10);
        check(!window.close(), "Dismissal without an explicit choice preserves recovered work");
        closeCancel.stop();
        check(encodeContainer(doc) == expected && doc.dirty() &&
                  listRecoveries(recoveryRoot).size() == 1,
              "Nonstandard dismissal cannot implicitly discard model or recovery data");
        const auto copy = files.filePath("Recovered room copy.sketchyup");
        modal(
            window, "saveModelDialog", [&] { window.findChild<QAction *>("file.save")->trigger(); },
            [&](QDialog *dialog) {
                auto *chooser = qobject_cast<QFileDialog *>(dialog);
                focus(chooser);
                auto *name = chooser->findChild<QLineEdit *>("fileNameEdit");
                check(name, "Recovered copy filename field");
                name->setFocus();
                name->selectAll();
                QTest::keyClicks(name, copy);
                QMetaObject::invokeMethod(chooser, "accept", Qt::DirectConnection);
            });
        check(
            !doc.dirty() && read(copy) == expected && read(savedPath) == savedBytes &&
                listRecoveries(recoveryRoot).empty(),
            "Recovered Save writes explicit copy and retires recovery without overwriting source");
        view->setTool(Viewport::Tool::Select);
        view->setDrawingPlane({});
        view->standardView(0);
        view->fit();
        window.findChild<QAction *>("view.history")->trigger();
        window.findChild<QTabWidget *>("organizationTabs")->setCurrentIndex(0);
        const auto capture = app.arguments().indexOf("--capture");
        if (capture >= 0) {
            QTest::qWait(100);
            check(window.grab().save(app.arguments().value(capture + 1)), "M4 capture saved");
            write(app.arguments().value(capture + 1) + ".sketchyup", expected);
        }
        check(!view->renderStats().glError, "M4 native workflow has no GL errors");
        std::cout << "M4 measured room, two openings, shared/unique nested windows, History, "
                     "save/reopen, killed-writer recovery and canceled close passed; DPR="
                  << window.devicePixelRatioF() << '\n';
    } catch (const std::exception &error) {
        doc.markSaved();
        std::cerr << error.what() << " | status: " << status.toStdString() << '\n';
        return 1;
    }
}
