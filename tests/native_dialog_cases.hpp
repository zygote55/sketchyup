#pragma once
// R084.u dialog inventory. Every dialog the desktop application can show is either a case
// here (audited by `native_accessibility_audit --dialogs-keyboard=GROUP`) or listed in
// `unexercised()` with the reason it cannot run in the isolated fixture.
#include "app/render_panel.hpp"
#include "app/sheet_export_dialog.hpp"
#include "app/sheet_workspace.hpp"
#include "app/terrain_actions.hpp"
#include "app/terrain_import_dialog.hpp"
#include "automation/commands.hpp"
#include "automation/extension_store.hpp"
#include "automation/report_schedules.hpp"
#include "core/component_parameters.hpp"
#include "core/components.hpp"
#include "core/drawing_sheets.hpp"
#include "core/scenes.hpp"
#include "core/terrain.hpp"
#include "integrations/chatgpt_auth.hpp"
#include "native_dialog_audit.hpp"
#include <QBuffer>
#include <QDoubleSpinBox>
#include <QImage>
#include <QListWidget>
#include <QSpinBox>
#include <QStackedWidget>
#include <QStandardPaths>
#include <QTabWidget>
#include <QTest>
#include <QTimer>
namespace dialog_audit {
// Wide enough for the docked assistant (1100 px) and the Model panel; small enough to keep
// software rendering of the 2x Wayland window cheap.
inline const QSize kAuditWindow(1100, 560);
// The demo model, unsaved-changes free, with nothing selected. Rebuilt only when a case
// changed it (rebuilding re-uploads the viewport's geometry).
inline void fresh(Context &context) {
    auto &window = context.window;
    if (window.size() != kAuditWindow)
        window.resize(kAuditWindow); // A case may have resized the window.
    static QByteArray demo;
    if (demo.isEmpty() || encodeContainer(window.document()) != demo) {
        window.document().markSaved();
        window.demo();
        window.document().markSaved();
        demo = encodeContainer(window.document());
    }
    window.viewport()->enterContext(0);
    window.viewport()->setSelection(0);
}
inline Id bodyNamed(Window &window, const std::string &name) {
    for (const auto &[id, body] : window.document().bodies())
        if (body->name == name)
            return id;
    throw Failure("Demo body exists: " + name);
}
inline QString source(const QString &relative) {
    // Fixtures resolve against the working directory (the checkout root in CI and containers).
    return QDir::current().absoluteFilePath(relative);
}
inline void load(Context &context, const QString &path) {
    context.window.document().markSaved();
    context.window.openPath(path);
    context.window.document().markSaved();
}
// R101.e2 (UX_DESIGN §19.6, §19.10): a saved schedule in the audit's isolated store, for the rename
// and delete dialogs. Its name is valid, so Rename is enabled as the default. The store is shared
// by the cases, so an earlier case's copy is removed first.
inline void quantitiesSchedule(Context &context) {
    fresh(context);
    auto &doc = context.window.document();
    const auto store = reportScheduleStorePath();
    try {
        deleteReportSchedule(doc, store, "Walls old");
    } catch (const InspectionError &) {
        // None to remove: the first case to run here.
    }
    const QJsonObject request{
        {"metrics", "all"}, {"groupBy", "none"}, {"filters", QJsonObject{{"visibility", "all"}}}};
    saveReportSchedule(doc, store, "Walls old", request);
}
// Opens the Quantities section from the palette, then focuses its schedule list with the first row
// selected and presses `key` there: F2 renames, Delete asks first (§19.10). The key is sent after
// the trigger returns, because the dialog it opens is modal.
inline std::function<void(Context &)> keyOnSchedule(int key) {
    return [key](Context &context) {
        palette(context, "Quantities");
        auto *list = context.window.findChild<QListWidget *>("quantitiesScheduleList");
        require(list != nullptr && list->count() == 1, "The saved schedule is listed");
        list->setCurrentRow(0);
        list->setFocus(Qt::OtherFocusReason);
        context.opener = list;
        context.openerId = identity(list);
        // Focus and the key are sent together, after the palette has released focus.
        QTimer::singleShot(0, list, [&context, list, key] {
            context.window.activateWindow();
            QTest::qWaitForWindowActive(&context.window, 2000);
            list->setFocus(Qt::OtherFocusReason);
            QTest::keyClick(list, Qt::Key(key));
        });
    };
}
inline void selectTable(Context &context) {
    fresh(context);
    context.window.viewport()->setSelection(bodyNamed(context.window, "Table"));
}
struct ComponentFixture {
    Id root{}, member{}, face{};
};
inline ComponentFixture component(Context &context) {
    fresh(context);
    auto &doc = context.window.document();
    const auto table = bodyNamed(context.window, "Table");
    const auto made = createComponent(doc, table, "Audit component");
    ComponentFixture result{
        made.instance, doc.instances().at(made.instance)->members.at(made.movedGeometry.at(table))};
    result.face = doc.bodies().at(result.member)->surface.faces.begin()->first;
    doc.markSaved();
    context.window.viewport()->refresh();
    context.window.viewport()->setSelection(made.instance);
    return result;
}
// R094.e (ADR 0166 §4.1): a placement of a parametric definition with its own Width, selected,
// so Enter on it asks "… has its own values".
inline void parametricPlacement(Context &context) {
    const auto made = component(context);
    auto &doc = context.window.document();
    ComponentParameterSpec width;
    width.name = "width";
    width.label = "Width";
    width.type = ParameterType::Length;
    width.mode = ComponentParameterMode::Input;
    width.expression = "1 m";
    defineComponentParameter(doc, doc.instances().at(made.root)->definition, width);
    overrideComponentParameters(
        doc, made.root, {{"width", ParameterQuantity{quantizeParameterLength(1.5), {1, 0}}}});
    doc.markSaved();
    context.window.viewport()->refresh();
    emit context.window.viewport()->changed();
    context.window.viewport()->setSelection(made.root);
}
inline void selectTableFace(Context &context) {
    fresh(context);
    const auto table = bodyNamed(context.window, "Table");
    context.window.viewport()->setSelection(
        table, context.window.document().bodies().at(table)->surface.faces.begin()->first);
}
inline void namedMaterial(Context &context) {
    load(context, source("tests/fixtures/container-materials-v10.sketchyup"));
    const auto &materials = context.window.document().materials();
    require(!materials.empty(), "Materials fixture has a named material");
    context.window.viewport()->setPaintMaterial(materials.begin()->first, 2);
}
inline void firstTag(Context &context) {
    load(context, source("tests/fixtures/container-tags-v9.sketchyup"));
    auto *tree = context.window.findChild<QTreeWidget *>("tagTree");
    require(tree && tree->topLevelItemCount() > 1, "Tags fixture lists tags");
    tree->setCurrentItem(tree->topLevelItem(1));
}
inline void listRow(Context &context, const char *name, int row) {
    auto *list = context.window.findChild<QListWidget *>(name);
    require(list && list->count() > row, QString("Fixture list has rows: ") + name);
    list->setCurrentRow(row);
}
inline std::function<void(Context &)> loadRow(QString file, const char *list) {
    return [file, list](Context &context) {
        load(context, source(file));
        listRow(context, list, 0);
    };
}
inline QString pngFile(Context &context) {
    const auto path = QDir(context.files).filePath("reference.png");
    if (!QFile::exists(path)) {
        QImage image(16, 8, QImage::Format_RGB32);
        image.fill(Qt::darkCyan);
        require(image.save(path, "PNG"), "Write reference PNG fixture");
    }
    return path;
}
inline void referenceImage(Context &context) {
    fresh(context);
    QFile file(pngFile(context));
    require(file.open(QIODevice::ReadOnly), "Read reference PNG fixture");
    auto &view = *context.window.viewport();
    view.importReferenceImage(file.readAll(), "image/png", "Audit image", 1, .5);
    context.window.document().markSaved();
    Id image{};
    for (const auto &[id, body] : context.window.document().bodies())
        if (body->referenceImage)
            image = id;
    view.setSelection(image);
}
// Writes the source files that import cases open.
inline void writeImportSources(Context &context) {
    fresh(context);
    const auto &doc = context.window.document();
    const QDir dir(context.files);
    QFile glb(dir.filePath("source.glb"));
    const auto bytes = exportGlb(RenderSnapshot::capture(doc)).glb;
    require(glb.open(QIODevice::WriteOnly) && glb.write(bytes) == bytes.size(),
            "Write glTF import fixture");
    glb.close();
    QFile stl(dir.filePath("source.stl"));
    require(stl.open(QIODevice::WriteOnly) &&
                stl.write("solid audit\nfacet normal 0 0 1\nouter loop\nvertex 0 0 0\nvertex 1 0 "
                          "0\nvertex 0 1 0\nendloop\nendfacet\nendsolid audit\n") > 0,
            "Write STL import fixture");
    stl.close();
    QFile obj(dir.filePath("source.obj"));
    require(obj.open(QIODevice::WriteOnly) && obj.write("v 0 0 0\nv 1 0 0\nv 0 1 0\nf 1 2 3\n") > 0,
            "Write OBJ import fixture");
    obj.close();
    // R095.d terrain sources: a point file (no units, so Import waits for them) and a DXF that
    // declares metres.
    QFile points(dir.filePath("survey.xyz"));
    require(points.open(QIODevice::WriteOnly) &&
                points.write("0 0 10\n10 0 11\n0 10 12\n10 10 13\n5 5 12.5\n") > 0,
            "Write terrain point fixture");
    points.close();
    QFile contours(dir.filePath("contours.dxf"));
    QStringList dxf{"0", "SECTION", "2", "HEADER", "9", "$ACADVER", "1", "AC1015", "9",
                    "$INSUNITS", "70", "6", "0", "ENDSEC", "0", "SECTION", "2", "ENTITIES"};
    for (const auto &[handle, half, z] : {std::tuple{"100", 20, 10}, std::tuple{"101", 10, 11}}) {
        dxf << "0" << "LWPOLYLINE" << "5" << handle << "8" << "CONTOURS" << "90" << "4" << "70"
            << "1" << "38" << QString::number(z);
        for (const auto &[x, y] : {std::pair{-1, -1}, std::pair{1, -1}, std::pair{1, 1},
                                   std::pair{-1, 1}})
            dxf << "10" << QString::number(x * half) << "20" << QString::number(y * half);
    }
    dxf << "0" << "ENDSEC" << "0" << "EOF";
    require(contours.open(QIODevice::WriteOnly) &&
                contours.write((dxf.join('\n') + '\n').toUtf8()) > 0,
            "Write terrain DXF fixture");
    contours.close();
    QFile::remove(dir.filePath("existing.sketchyup"));
    QFile existing(dir.filePath("existing.sketchyup"));
    require(existing.open(QIODevice::WriteOnly) && existing.write("placeholder") > 0,
            "Write existing-file fixture");
}
// R095.e1: a small terrain made from three nested square contours (terrain.create_from_edges),
// selected. `paint` gives one tile face its own colour, so Regenerate asks first. With
// `crossing`, two crossing open lines in one tag are selected as edges instead (Create terrain
// skips the R095.h roles step and refuses). With `select`, the contours' edges are selected
// and no terrain is made (closed loops: Create terrain opens the roles step).
inline void terrainFixture(Context &context, bool paint, bool crossing = false,
                           bool select = false) {
    fresh(context);
    auto &window = context.window;
    auto &doc = window.document();
    auto wire = [&](double half, double z, double c) {
        const std::vector<Vec3> p{{c - half, c - half, z}, {c + half, c - half, z},
                                  {c + half, c + half, z}, {c - half, c + half, z}};
        Id body = 0;
        for (std::size_t i = 0; i < p.size(); ++i)
            body = doc.addWire(body, p[i], p[(i + 1) % p.size()]);
        return body;
    };
    auto line = [&](const std::vector<Vec3> &p) {
        Id body = 0;
        for (std::size_t i = 0; i + 1 < p.size(); ++i)
            body = doc.addWire(body, p[i], p[i + 1]);
        return body;
    };
    const auto bodies =
        crossing ? std::vector<Id>{line({{-10, 0, 0}, {0, 0.5, 0}, {10, 0, 0}}),
                                   line({{0, -10, 1}, {0.5, 0, 1}, {0, 10, 1}})}
                 : std::vector<Id>{wire(30, 10, 0), wire(20, 11, 0), wire(10, 12, 0)};
    SelectionSet edges;
    QJsonArray references;
    for (const auto body : bodies)
        for (const auto &[id, edge] : doc.bodies().at(body)->topology.edges) {
            edges.insert({body, SelectionKind::Edge, id});
            references.append(
                QJsonObject{{"body", QString::number(body)}, {"edge", QString::number(id)}});
        }
    window.viewport()->refresh();
    if (crossing || select) {
        window.viewport()->selectEntities(edges);
        return;
    }
    const QJsonObject request{
        {"apiVersion", 1},
        {"documentId", QString::fromStdString(doc.identity())},
        {"expectedRevision", QString::number(doc.revision())},
        {"commands", QJsonArray{QJsonObject{{"command", "terrain.create_from_edges"},
                                            {"edges", references}}}}};
    const auto terrain = executeBatch(doc, request)["terrainOperations"]
                             .toArray()[0]
                             .toObject()["terrain"]
                             .toString()
                             .toULongLong();
    if (paint) {
        const auto tileId = terrainTiles(doc, terrain).front();
        const auto before = doc.bodies().at(tileId);
        auto tile = std::make_shared<Body>(*before);
        tile->faceColors[tile->surface.faces.begin()->first] = {1, 0, 0};
        doc.apply({"Paint", {{tileId, before, tile}}}, doc.revision());
    }
    window.viewport()->refresh();
    window.viewport()->setSelection(terrain);
}
inline std::function<void(Context &)> viaPalette(const QString &label) {
    return [label](Context &context) { palette(context, label); };
}
inline std::function<void(Context &)> viaPanel(const QString &view, const QString &button) {
    return [view, button](Context &context) { panelButton(context, view, button); };
}
// A new output path for every attempt: exports never replace an existing file.
inline std::function<QString()> newFile(Context &context, const QString &stem,
                                        const QString &suffix = {}) {
    return [&context, stem, suffix] {
        static int counter = 0;
        return QDir(context.files)
            .filePath(QString("%1-%2%3").arg(stem).arg(++counter).arg(suffix));
    };
}

// A new tag needs a name before OK can create it; type one (keyboard) into the empty field.
inline void typeName(QWidget &dialog) {
    auto *name = dialog.findChild<QLineEdit *>("organizationName");
    require(name, "Tag dialog has a name field");
    waitDialogFocus(dialog);
    if (name->text().isEmpty())
        QTest::keyClicks(name, "Audit tag");
}
// Ctrl+J toggles the assistant; start each assistant case with it closed.
inline void hideAssistant(Context &context) {
    fresh(context);
    QSettings settings("SketchyUp", "SketchyUp");
    for (const auto *key :
         {"assistant/provider", "assistant/openaiModel", "assistant/consentOpenAI"})
        settings.remove(key);
    auto *tabs = context.window.findChild<QTabWidget *>("assistantSideTabs");
    if (tabs && tabs->indexOf(context.window.assistantPanel()) >= 0)
        context.window.findChild<QAction *>("view.assistant")->trigger();
}
inline void installExtension(Context &context) {
    fresh(context);
    QFile manifest(source("examples/extensions/panel.sketchyext"));
    require(manifest.open(QIODevice::ReadOnly), "Read the example extension");
    ExtensionStore store(QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) +
                         "/extensions");
    if (store.entries().empty())
        store.install(manifest.readAll());
    store.setEnabled(store.entries().begin()->first, true);
}
// A launcher for the project's fake Blender worker (blender_job_tests --fake MODE).
// "hold-handoff" renders like "success" but keeps a scene handoff running until cancelled.
inline QString fakeBlender(Context &context, const QString &mode) {
    const auto path = QDir(context.files).filePath("blender-" + mode);
    if (!QFile::exists(path)) {
        QFile file(path);
        require(file.open(QIODevice::WriteOnly), "Create fake Blender launcher");
        const auto worker = "'" + QCoreApplication::applicationDirPath() + "/blender_job_tests'";
        QString script = "#!/bin/sh\n";
        if (mode == "hold-handoff")
            script += "for request; do :; done\n"
                      "if grep -Eq '\"operation\" *: *\"handoff\"' \"$request\" 2>/dev/null; then\n"
                      "  exec " +
                      worker + " --fake hang \"$@\"\nfi\n";
        script += "exec " + worker + " --fake " +
                  (mode == "hold-handoff" ? QString("success") : mode) + " \"$@\"\n";
        file.write(script.toUtf8());
        file.close();
        require(file.setPermissions(QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner),
                "Fake Blender launcher is executable");
    }
    return path;
}
inline void cameraScenes(Context &context) {
    fresh(context);
    auto &doc = context.window.document();
    SceneSnapshot snapshot;
    snapshot.camera = SceneCamera{};
    createScene(doc, "Audit A", snapshot);
    snapshot.camera->yaw = 90;
    createScene(doc, "Audit B", snapshot);
    doc.markSaved();
}
// In Render with Blender: point at the fake worker (preparation), then check it by keyboard
// (Tab to Check Blender and devices, Space) and wait until rendering is available.
inline std::function<void(QWidget &)> verifiedBlender(Context &context,
                                                      const QString &mode = "success") {
    return [&context, mode](QWidget &setup) {
        setup.findChild<QLineEdit *>("blenderPath")->setText(fakeBlender(context, mode));
        setup.findChild<QComboBox *>("renderBackend")->setCurrentText("CPU");
        press(tabTo(setup, "probeBlender"));
        require(
            QTest::qWaitFor(
                [&] { return setup.findChild<QPushButton *>("startRender")->isEnabled(); }, 20000),
            "Fake Blender check enables Render");
    };
}
// R091.f: a page with one plan view of a top scene, for the Sheets workspace dialogs.
inline void sheetView(Context &context) {
    fresh(context);
    require(context.window.sheetWorkspace() != nullptr, "The Sheets workspace exists");
    auto &doc = context.window.document();
    SceneSnapshot plan;
    plan.camera = SceneCamera{};
    plan.camera->pitch = 90;
    plan.camera->orthographic = true;
    const auto scene = createScene(doc, "Audit plan", plan);
    plan.camera->yaw = -90;
    plan.camera->pitch = 0;
    createScene(doc, "Audit front", plan);
    const auto sheet = createDrawingSheet(doc, "Audit page", {420, 297}, "A-1", 10);
    DrawingViewRecord view;
    view.name = "Audit view";
    view.sheet = sheet;
    view.source = {DrawingViewSourceKind::Scene, scene};
    view.frame = {20, 20, 150, 100};
    view.scaleDenominator = 50;
    createDrawingView(doc, view);
    doc.markSaved();
    emit context.window.viewport()->changed();
}
// Shows the page and selects its view; with `edit`, opens edit-view (Enter on the canvas).
inline void selectSheetView(Context &context, bool edit) {
    auto &window = context.window;
    window.setWorkspace(Window::Workspace::Sheets);
    auto *canvas = window.sheetWorkspace()->canvas();
    const auto sheets = orderedDrawingSheets(window.document());
    require(!sheets.empty(), "The audit page exists");
    window.sheetWorkspace()->setCurrentSheet(sheets.back());
    canvas->setFocus(Qt::OtherFocusReason);
    require(QTest::qWaitFor([&] { return canvas->hasFocus(); }), "The page canvas has focus");
    QTest::keyClick(canvas, Qt::Key_Tab); // With Select, Tab selects the first view.
    require(canvas->selectedView() != 0, "Tab selects the audit view");
    if (edit) {
        QTest::keyClick(canvas, Qt::Key_Return);
        require(canvas->editView() != 0, "Enter opens edit-view");
    }
}
inline void exportSettings(QWidget &sheet, Context &context) {
    sheet.findChild<QSpinBox *>("animationFps")->setValue(1);
    sheet.findChild<QDoubleSpinBox *>("animationHold")->setValue(0);
    sheet.findChild<QLineEdit *>("animationDestination")->setText(newFile(context, "frames")());
}
inline std::vector<Case> cases(Context &ctx) {
    std::vector<Case> all;
    const auto add = [&](Case c) { all.push_back(std::move(c)); };
    const auto fileDialog = [](const QString &title) { return kind<QFileDialog>(title); };
    // ---------------------------------------------------------------- app ----------
    add({"drawing-plane", "settings", "Drawing plane",
         "Draw > Drawing plane > Custom plane… (palette)", fresh, viaPalette("Custom plane…"),
         named("drawingPlaneDialog"), Enter::Accept, "OK"});
    add({"keyboard-shortcuts", "settings", "Keyboard shortcuts",
         "Edit > Keyboard shortcuts… (palette)", fresh, viaPalette("Keyboard shortcuts…"),
         named("shortcutDialog"), Enter::Accept, "Save"});
    add({"command-palette",
         "settings",
         "Commands and model search",
         "Ctrl+K (View > Commands…)",
         fresh,
         [](Context &context) {
             context.opener = focused();
             context.openerId = identity(focused());
             QTest::keyClick(focused(), Qt::Key_K, Qt::ControlModifier);
         },
         named("commandPalette"),
         Enter::DefaultOnly,
         "",
         Return::Viewport,
         {},
         {},
         {},
         "Enter runs the highlighted command; every palette-opened case exercises it."});
    add({"document-units", "settings", "Document units", "File > Document units… (palette)", fresh,
         viaPalette("Document units…"), named("documentUnitsDialog"), Enter::Accept, "OK"});
    add({"welcome-units", "settings", "Welcome to SketchyUp (first-run units)",
         "Opens itself at first start with keyboard focus", fresh,
         [](Context &context) {
             QSettings("SketchyUp", "SketchyUp").remove("defaultUnits");
             context.opener = focused();
             context.openerId = identity(focused());
             QTimer::singleShot(0, &context.window, [&context] { context.window.startUnits(); });
         },
         namedTitle("documentUnitsDialog", "Welcome to SketchyUp"), Enter::Accept, "OK"});
    add({"recovery-settings", "settings", "Recovery settings",
         "File > Recovery settings… (palette)", fresh, viaPalette("Recovery settings…"),
         named("recoverySettings"), Enter::Accept, "OK"});
    add({"recover-work",
         "settings",
         "Recover work",
         "File > Recover work… (palette)",
         fresh,
         viaPalette("Recover work…"),
         named("recoveryDialog"),
         Enter::DefaultOnly,
         "Open recovered version",
         Return::Viewport,
         {},
         {},
         {},
         "No inactive recovery copy exists in the isolated profile, so its actions are disabled."});
    add({"field-of-view", "settings", "Field of view", "View > Field of view… (palette)", fresh,
         viaPalette("Field of view…"), kind<QInputDialog>("Field of view"), Enter::Accept, "OK"});
    add({"text-size", "settings", "Interface text size", "View > Interface text size… (palette)",
         fresh, viaPalette("Interface text size…"), kind<QInputDialog>("Interface text size"),
         Enter::Accept, "OK"});
    add({"extensions", "app", "Extensions", "Extensions > Manage extensions… (palette)", fresh,
         viaPalette("Manage extensions…"), named("extensionsDialog"), Enter::DefaultOnly,
         "Install…"});
    add({"extensions-installed",
         "app",
         "Extensions (with an enabled extension)",
         "Extensions > Manage extensions… (palette)",
         installExtension,
         viaPalette("Manage extensions…"),
         named("extensionsDialog"),
         Enter::DefaultOnly,
         "Install…",
         Return::Viewport,
         {},
         [](QWidget &dialog) {
             auto *list = dialog.findChild<QListWidget *>("extensionsList");
             require(list && list->count() > 0, "Installed extension is listed");
             list->setCurrentRow(0);
         },
         {},
         "Shows the action and its parameter editors."});
    add({"extension-install",
         "app",
         "Install extension (file chooser)",
         "Extensions dialog > Install… (Tab, Space)",
         fresh,
         viaPalette("Manage extensions…"),
         [](QWidget *w) {
             return qobject_cast<QFileDialog *>(w) && w->parentWidget() &&
                    w->parentWidget()->objectName() == "extensionsDialog";
         },
         Enter::DefaultOnly,
         "Open",
         Return::Opener,
         {},
         {},
         {childOf(ctx, named("extensionsDialog"), "extensionInstall")}});
    add({"face-color", "app", "Set original face color (color chooser)",
         "Edit > Set original face color… (palette)", selectTable,
         viaPalette("Set original face color…"), kind<QColorDialog>("Material color"),
         Enter::Accept, "OK"});
    add({"edit-error", "app", "Could not complete edit (error message)",
         "Any command that fails, e.g. Edit > Save component to library… with no component", fresh,
         viaPalette("Save component to library…"), kind<QMessageBox>("Could not complete edit"),
         Enter::Closes, "OK"});
    // --------------------------------------------------------------- files ---------
    add({"open-model", "app", "Open model (file chooser)", "File > Open… (Ctrl+O)", fresh,
         [](Context &context) {
             context.opener = focused();
             context.openerId = identity(focused());
             QTest::keyClick(focused(), Qt::Key_O, Qt::ControlModifier);
         },
         fileDialog("Open model"), Enter::DefaultOnly, "Open"});
    add({"save-model", "app", "Save model (file chooser)",
         "File > Save as… (palette; also Ctrl+Shift+S)", fresh, viaPalette("Save as…"),
         fileDialog("Save model"), Enter::DefaultOnly, "Save"});
    add({"replace-model",
         "app",
         "Replace model? (confirmation)",
         "Save as… to an existing file name",
         writeImportSources,
         viaPalette("Save as…"),
         kind<QMessageBox>("Replace model?"),
         Enter::Closes,
         "Cancel",
         Return::Viewport,
         {},
         {},
         {chooseFile("Save model", QDir(ctx.files).filePath("existing.sketchyup"))},
         "Cancel is the default, so Enter never overwrites."});
    add({"unsaved-changes",
         "app",
         "Unsaved changes (confirmation)",
         "File > New (Ctrl+N) with unsaved edits",
         [](Context &context) {
             fresh(context);
             auto &doc = context.window.document();
             doc.paint(bodyNamed(context.window, "Table"), {.2f, .3f, .4f});
             context.window.viewport()->refresh();
         },
         [](Context &context) {
             context.opener = focused();
             context.openerId = identity(focused());
             QTest::keyClick(focused(), Qt::Key_N, Qt::ControlModifier);
         },
         kind<QMessageBox>("Unsaved changes"),
         Enter::Next,
         "Save",
         Return::Viewport,
         fileDialog("Save model"),
         {},
         {},
         "Save (default) opens Save model; cancelling it keeps the model and its edits."});
    add({"save-failed",
         "files",
         "Save failed (error message)",
         "Save as… to a location that cannot be written",
         fresh,
         viaPalette("Save as…"),
         kind<QMessageBox>("Save failed"),
         Enter::Closes,
         "OK",
         Return::Viewport,
         {},
         {},
         {chooseFile("Save model", "/proc/sketchyup-audit.sketchyup")},
         "The model stays open and unsaved; the save-failure banner remains."});
    add({"new-from-template",
         "files",
         "New from template",
         "File > New from template… (palette)",
         fresh,
         viaPalette("New from template…"),
         namedTitle("libraryDialog", "New from template"),
         Enter::Disabled,
         "Create model",
         Return::Viewport,
         {},
         {},
         {},
         "The isolated library folder is empty, so Create model stays disabled."});
    add({"library-folder",
         "files",
         "Choose library folder (folder chooser)",
         "New from template > Choose folder… (Tab, Space)",
         fresh,
         viaPalette("New from template…"),
         fileDialog("Choose library folder"),
         Enter::DefaultOnly,
         "Choose",
         Return::Opener,
         {},
         {},
         {childOf(ctx, named("libraryDialog"), "libraryChooseFolder")}});
    add({"save-template", "files", "Save template to library",
         "File > Save template to library… (palette)", fresh,
         viaPalette("Save template to library…"), named("saveLibraryDialog"), Enter::Child, "Save",
         Return::Viewport, fileDialog("Save new library bundle")});
    // ------------------------------------------------------------ exchange ---------
    add({"dxf-export-options", "dxf", "DXF export options", "File > Export 2D DXF… (palette)",
         fresh, viaPalette("Export 2D DXF…"), namedTitle("dxfOptionsDialog", "DXF export options"),
         Enter::Next, "OK", Return::Viewport, fileDialog("New 2D DXF drawing")});
    add({"dxf-export-file",
         "dxf",
         "New 2D DXF drawing (file chooser)",
         "Export 2D DXF… > OK",
         fresh,
         viaPalette("Export 2D DXF…"),
         fileDialog("New 2D DXF drawing"),
         Enter::DefaultOnly,
         "Save",
         Return::Viewport,
         {},
         {},
         {enterThrough(named("dxfOptionsDialog"))}});
    add({"dxf-export-report",
         "dxf",
         "DXF export report",
         "Export 2D DXF… > OK > file name",
         [](Context &context) {
             // DXF export accepts planar models only.
             load(context, source("tests/fixtures/display-precision-v25.sketchyup"));
         },
         viaPalette("Export 2D DXF…"),
         named("dxfReport"),
         Enter::Closes,
         "Close",
         Return::Viewport,
         {},
         {},
         {enterThrough(named("dxfOptionsDialog")),
          chooseFile("New 2D DXF drawing", newFile(ctx, "export", ".dxf"))}});
    add({"dxf-import-file", "dxf", "Import 2D DXF (file chooser)",
         "File > Import 2D DXF… (palette)", fresh, viaPalette("Import 2D DXF…"),
         fileDialog("Import 2D DXF"), Enter::DefaultOnly, "Open"});
    add({"dxf-import-options",
         "dxf",
         "DXF import options",
         "Import 2D DXF… > file name",
         fresh,
         viaPalette("Import 2D DXF…"),
         namedTitle("dxfOptionsDialog", "DXF import options"),
         Enter::Accept,
         "OK",
         Return::Viewport,
         named("dxfReport"),
         [](QWidget &dialog) {
             // The fixture declares no units; choose them so the import can complete.
             dialog.findChild<QComboBox *>("dxfUnits")->setCurrentText("Meters");
         },
         {chooseFile("Import 2D DXF", source("tests/fixtures/dxf/ezdxf-r12.dxf")),
          {named("dxfReport"), [](QWidget &report) {
               waitDialogFocus(report);
               QTest::keyClick(focused(), Qt::Key_Escape);
           }}}});
    add({"stl-export-options", "dxf", "STL export options", "File > Export STL… (palette)", fresh,
         viaPalette("Export STL…"), named("stlOptionsDialog"), Enter::Next, "OK", Return::Viewport,
         fileDialog("New STL file")});
    add({"stl-export-report",
         "stl-obj",
         "STL export report",
         "Export STL… > OK > file name",
         fresh,
         viaPalette("Export STL…"),
         named("stlReport"),
         Enter::Closes,
         "Close",
         Return::Viewport,
         {},
         {},
         {enterThrough(named("stlOptionsDialog")),
          chooseFile("New STL file", newFile(ctx, "export", ".stl"))}});
    add({"stl-import-file", "stl-obj", "Import STL (file chooser)", "File > Import STL… (palette)",
         fresh, viaPalette("Import STL…"), fileDialog("Import STL"), Enter::DefaultOnly, "Open"});
    add({"stl-import-options",
         "stl-obj",
         "STL import options",
         "Import STL… > file name",
         writeImportSources,
         viaPalette("Import STL…"),
         namedTitle("stlOptionsDialog", "STL import options"),
         Enter::Accept,
         "OK",
         Return::Viewport,
         {},
         {},
         {chooseFile("Import STL", QDir(ctx.files).filePath("source.stl")),
          {named("stlReport"), [](QWidget &report) {
               waitDialogFocus(report);
               QTest::keyClick(focused(), Qt::Key_Escape);
           }}}});
    add({"obj-export-options", "stl-obj", "OBJ export options",
         "File > Export OBJ package… (palette)", fresh, viaPalette("Export OBJ package…"),
         namedTitle("objOptionsDialog", "OBJ export options"), Enter::Next, "OK", Return::Viewport,
         fileDialog("New OBJ package folder")});
    add({"obj-export-report",
         "stl-obj",
         "OBJ export report",
         "Export OBJ package… > OK > name",
         fresh,
         viaPalette("Export OBJ package…"),
         named("objReport"),
         Enter::Closes,
         "Close",
         Return::Viewport,
         {},
         {},
         {enterThrough(named("objOptionsDialog")),
          chooseFile("New OBJ package folder", newFile(ctx, "obj-package"))}});
    add({"obj-import-file", "import", "Import OBJ/MTL (file chooser)",
         "File > Import OBJ/MTL… (palette)", fresh, viaPalette("Import OBJ/MTL…"),
         fileDialog("Import OBJ/MTL"), Enter::DefaultOnly, "Open"});
    add({"obj-import-options",
         "import",
         "OBJ import options",
         "Import OBJ/MTL… > file name",
         writeImportSources,
         viaPalette("Import OBJ/MTL…"),
         namedTitle("objOptionsDialog", "OBJ import options"),
         Enter::Accept,
         "OK",
         Return::Viewport,
         {},
         {},
         {chooseFile("Import OBJ/MTL", QDir(ctx.files).filePath("source.obj")),
          {named("objReport"), [](QWidget &report) {
               waitDialogFocus(report);
               QTest::keyClick(focused(), Qt::Key_Escape);
           }}}});
    add({"gltf-import-file", "import", "Import GLB/glTF (file chooser)",
         "File > Import GLB/glTF… (palette)", fresh, viaPalette("Import GLB/glTF…"),
         fileDialog("Import GLB/glTF"), Enter::DefaultOnly, "Open"});
    add({"gltf-import-report",
         "import",
         "GLB/glTF import report",
         "Import GLB/glTF… > file name",
         writeImportSources,
         viaPalette("Import GLB/glTF…"),
         named("gltfImportReport"),
         Enter::Closes,
         "Close",
         Return::Viewport,
         {},
         {},
         {chooseFile("Import GLB/glTF", QDir(ctx.files).filePath("source.glb"))}});
    add({"formline-import-file", "import", "Import Formline (file chooser)",
         "File > Import Formline… (palette)", fresh, viaPalette("Import Formline…"),
         fileDialog("Import Formline v1"), Enter::DefaultOnly, "Open"});
    add({"formline-import-report",
         "import",
         "Formline import report",
         "Import Formline… > file name",
         fresh,
         viaPalette("Import Formline…"),
         named("formlineImportReport"),
         Enter::Closes,
         "Close",
         Return::Viewport,
         {},
         {},
         {chooseFile("Import Formline v1", source("tests/fixtures/formline-v1.formline"))}});
    // R095.d: File > Import terrain…. The dialog checks the source on a worker; its default
    // button imports, and stays disabled until units are chosen (a point file has none).
    const auto terrainSettles = [](QWidget &dialog) {
        auto *terrain = dynamic_cast<TerrainImportDialog *>(&dialog);
        require(terrain && QTest::qWaitFor([&] { return !terrain->checking(); }, 20000),
                "The terrain dry run settles");
    };
    add({"terrain-import-file", "import", "Import terrain (file chooser)",
         "File > Import terrain… (palette)", fresh, viaPalette("Import terrain…"),
         fileDialog("Import terrain"), Enter::DefaultOnly, "Open"});
    add({"terrain-import-points",
         "import",
         "Import terrain (point file, units not chosen)",
         "Import terrain… > file name",
         writeImportSources,
         viaPalette("Import terrain…"),
         named("terrainImportDialog"),
         Enter::Disabled,
         "Import",
         Return::Viewport,
         {},
         terrainSettles,
         {chooseFile("Import terrain", QDir(ctx.files).filePath("survey.xyz"))}});
    add({"terrain-import-dxf",
         "import",
         "Import terrain (DXF with declared units)",
         "Import terrain… > file name",
         writeImportSources,
         viaPalette("Import terrain…"),
         named("terrainImportDialog"),
         Enter::Accept,
         "Import",
         Return::Viewport,
         {},
         terrainSettles,
         {chooseFile("Import terrain", QDir(ctx.files).filePath("contours.dxf"))},
         "Import adds the terrain to the open model in one undoable edit."});
    // R095.e1: the terrain actions on the selected terrain (command search; the Entity info
    // Terrain section's buttons trigger the same actions).
    const auto settingsSettle = [](QWidget &dialog) {
        auto *settings = dynamic_cast<TerrainSettingsDialog *>(&dialog);
        require(settings && QTest::qWaitFor([&] { return !settings->checking(); }, 20000),
                "The terrain settings dry run settles");
    };
    add({"terrain-settings",
         "terrain",
         "Terrain settings (current settings)",
         "Terrain settings… (palette)",
         [](Context &context) { terrainFixture(context, false); },
         viaPalette("Terrain settings…"),
         named("terrainSettingsDialog"),
         Enter::Disabled,
         "Apply terrain settings",
         Return::Viewport,
         {},
         settingsSettle,
         {},
         "Apply waits for a change; it rebuilds the terrain in one undoable edit."});
    add({"terrain-explode", "terrain", "Explode terrain (confirmation)",
         "Explode terrain… (palette)", [](Context &context) { terrainFixture(context, false); },
         viaPalette("Explode terrain…"), named("terrainExplodeDialog"), Enter::Closes, "Cancel",
         Return::Viewport, {}, {}, {}, "Cancel is the default."});
    add({"terrain-regenerate-paint", "terrain", "Regenerate terrain (paint would be dropped)",
         "Regenerate terrain (palette)", [](Context &context) { terrainFixture(context, true); },
         viaPalette("Regenerate terrain"), named("terrainRegenerateDialog"), Enter::Closes,
         "Cancel", Return::Viewport, {}, {}, {}, "Cancel is the default."});
    // R096.f1: Reset edits… on a terrain with one sculpt; Cancel is the default.
    add({"terrain-reset-edits", "terrain", "Reset terrain edits (confirmation)",
         "Terrain > Reset edits… (palette)",
         [](Context &context) {
             terrainFixture(context, false);
             auto &doc = context.window.document();
             const auto terrain = context.window.viewport()->selectedBody();
             const QJsonObject request{
                 {"apiVersion", 1},
                 {"documentId", QString::fromStdString(doc.identity())},
                 {"expectedRevision", QString::number(doc.revision())},
                 {"commands", QJsonArray{QJsonObject{{"command", "terrain.sculpt"},
                                                     {"terrain", QString::number(terrain)},
                                                     {"mode", "raise"},
                                                     {"path", QJsonArray{QJsonArray{0.3, 0.2}}},
                                                     {"radius", 15},
                                                     {"amount", 1}}}}};
             executeBatch(doc, request);
             context.window.viewport()->refresh();
             context.window.viewport()->setSelection(0);
             context.window.viewport()->setSelection(terrain);
         },
         viaPalette("Reset edits…"), named("terrainResetEditsDialog"), Enter::Closes, "Cancel",
         Return::Viewport, {}, {}, {}, "Cancel is the default."});
    add({"terrain-create-refused", "terrain", "Create terrain from selection (refusal report)",
         "Create terrain from selection (palette)",
         [](Context &context) { terrainFixture(context, false, true); },
         viaPalette("Create terrain from selection"), named("terrainReportDialog"), Enter::Closes,
         "Close", Return::Viewport, {}, {}, {}, "Crossing lines: nothing was changed."});
    // R095.h: closed loops open the roles step first; Create (the default) creates with the
    // roles shown, and the summit's flat-triangle notice follows in the report.
    add({"terrain-create-roles",
         "terrain",
         "Create terrain from selection (roles step)",
         "Create terrain from selection (palette)",
         [](Context &context) { terrainFixture(context, false, false, true); },
         viaPalette("Create terrain from selection"),
         named("terrainSelectionRolesDialog"),
         Enter::Next,
         "Create terrain",
         Return::Viewport,
         named("terrainReportDialog"),
         [](QWidget &dialog) {
             auto *roles = dynamic_cast<TerrainSelectionRolesDialog *>(&dialog);
             require(roles && QTest::qWaitFor([&] { return !roles->checking(); }, 20000),
                     "The roles dry run settles");
         },
         {},
         "Each tag gets a role; Create makes the terrain in one undoable edit."});
    // R095.e2: File > Site settings… without and with a site origin, and its Re-anchor… and
    // Set origin… dialogs (the action waits for a different origin; Set origin would edit).
    const auto siteFixture = [](Context &context) {
        terrainFixture(context, false);
        setSiteOrigin(context.window.document(),
                      SiteOrigin{512300, 5234500, 0, "EPSG:32633", {"hill.dxf", std::string(64, 'a')}});
        context.window.viewport()->refresh();
    };
    add({"site-settings-none", "terrain", "Site settings (no site origin)",
         "File > Site settings… (palette)", fresh, viaPalette("Site settings…"),
         named("siteSettingsDialog"), Enter::Closes, "Close", Return::Viewport, {}, {}, {},
         "Explains that the first terrain import sets the origin and offers Set origin…."});
    add({"site-settings", "terrain", "Site settings (origin set)", "File > Site settings… (palette)",
         siteFixture, viaPalette("Site settings…"), named("siteSettingsDialog"), Enter::Closes,
         "Close", Return::Viewport, {}, {}, {}, "Origin, CRS label and provenance are read-only."});
    add({"dxf-export-options-site", "dxf", "DXF export options (site origin set)",
         "File > Export 2D DXF… (palette)", siteFixture, viaPalette("Export 2D DXF…"),
         namedTitle("dxfOptionsDialog", "DXF export options"), Enter::Next, "OK", Return::Viewport,
         fileDialog("New 2D DXF drawing"), {}, {},
         "Write site coordinates is enabled and unchecked."});
    add({"site-reanchor",
         "terrain",
         "Re-anchor site origin",
         "Site settings… > Re-anchor… (Tab, Space)",
         siteFixture,
         viaPalette("Site settings…"),
         named("siteReanchorDialog"),
         Enter::Disabled,
         "Re-anchor site origin",
         Return::Opener,
         {},
         {},
         {childOf(ctx, named("siteSettingsDialog"), "siteReanchor")},
         "Re-anchor waits for a different origin; it is one undoable edit."});
    add({"site-set-origin",
         "terrain",
         "Set site origin",
         "Site settings… > Set origin… (Tab, Space)",
         fresh,
         viaPalette("Site settings…"),
         named("siteSetOriginDialog"),
         Enter::DefaultOnly,
         "Set site origin",
         Return::Opener,
         {},
         {},
         {childOf(ctx, named("siteSettingsDialog"), "siteSetOrigin")},
         "Enter would set the origin (site.set)."});
    add({"measured-export", "components", "Export measured PDF / SVG",
         "File > Export measured PDF/SVG… (palette)", fresh, viaPalette("Export measured PDF/SVG…"),
         named("measuredExportDialog"), Enter::Child, "Save", Return::Viewport,
         fileDialog("New measured drawing")});
    add({"measured-report",
         "import",
         "Measured drawing exported (report)",
         "Export measured PDF/SVG… > Save > file name",
         fresh,
         viaPalette("Export measured PDF/SVG…"),
         named("measuredExportReport"),
         Enter::Closes,
         "Close",
         Return::Viewport,
         {},
         {},
         {{named("measuredExportDialog"),
           [](QWidget &dialog) {
               waitDialogFocus(dialog);
               QTest::keyClick(focused(), Qt::Key_Return); // Save; the dialog closes after report.
           }},
          chooseFile("New measured drawing", newFile(ctx, "measured", ".pdf"))}});
    add({"raster-export", "components", "Export view as PNG",
         "File > Export view as PNG… (palette)", fresh, viaPalette("Export view as PNG…"),
         named("rasterExportDialog"), Enter::Child, "Save", Return::Viewport,
         fileDialog("Save PNG")});
    // ---------------------------------------------------------- components ---------
    add({"make-component", "components", "Make component", "Edit > Make component… (G, palette)",
         selectTable, viaPalette("Make component…"),
         namedTitle("componentDialog", "Make component"), Enter::Accept, "OK"});
    add({"place-component", "components", "Place component", "Edit > Place component… (palette)",
         [](Context &context) { component(context); }, viaPalette("Place component…"),
         namedTitle("componentDialog", "Place component"), Enter::Accept, "OK"});
    add({"replace-component", "components", "Replace component",
         "Edit > Replace component… (palette)", [](Context &context) { component(context); },
         viaPalette("Replace component…"), namedTitle("componentDialog", "Replace component"),
         Enter::Accept, "OK"});
    add({"component-axes", "components", "Change component axes",
         "Edit > Change component axes… (palette)", [](Context &context) { component(context); },
         viaPalette("Change component axes…"),
         namedTitle("componentDialog", "Change component axes"), Enter::Accept, "OK"});
    add({"parameters-scope", "components", "Placement has its own values",
         "Enter on a selected placement with its own values (also double-click and Edit "
         "definition…)",
         parametricPlacement,
         [](Context &context) { QTest::keyClick(context.window.viewport(), Qt::Key_Return); },
         named("parametersScopeDialog"), Enter::Accept, "Edit definition at defaults"});
    add({"component-library",
         "files",
         "Insert library component",
         "Edit > Browse component library… (palette)",
         fresh,
         viaPalette("Browse component library…"),
         namedTitle("libraryDialog", "Insert library component"),
         Enter::Disabled,
         "Insert component",
         Return::Viewport,
         {},
         {},
         {},
         "The isolated library folder is empty, so Insert component stays disabled."});
    add({"save-component", "files", "Save component to library",
         "Edit > Save component to library… (palette)",
         [](Context &context) { component(context); }, viaPalette("Save component to library…"),
         namedTitle("saveLibraryDialog", "Save component to library"), Enter::Child, "Save",
         Return::Viewport, fileDialog("Save new library bundle")});
    add({"glue-face", "components", "Set component glue face", "Edit > Set glue face… (palette)",
         [](Context &context) {
             const auto f = component(context);
             auto &view = *context.window.viewport();
             view.enterContext(f.root);
             view.setSelection(f.member, f.face);
         },
         viaPalette("Set glue face…"), named("hostedGlueDialog"), Enter::Accept, "OK"});
    add({"placement-options", "components", "Attachment placement options",
         "Edit > Attachment placement options… (palette)", fresh,
         viaPalette("Attachment placement options…"), named("hostedPlacementOptions"),
         Enter::Accept, "OK"});
    add({"geometry-diagnostics",
         "components",
         "Geometry diagnostics",
         "Edit > Geometry diagnostics… (palette)",
         selectTable,
         viaPalette("Geometry diagnostics…"),
         named("geometryDiagnosticsReport"),
         Enter::DefaultOnly,
         "Refresh",
         Return::Viewport,
         {},
         {},
         {},
         "Refresh (read-only) is Qt's automatic default; Enter never edits the model."});
    // -------------------------------------------------------------- panels ---------
    add({"material-new", "materials", "New material",
         "View > Materials (palette), then New… (Tab, Space)", fresh,
         viaPanel("Materials", "materialNew"), namedTitle("materialDialog", "New material"),
         Enter::Accept, "OK", Return::OpenerPanel});
    add({"material-color",
         "materials",
         "Material color (color chooser)",
         "New material > Choose… (Tab, Space)",
         fresh,
         viaPanel("Materials", "materialNew"),
         kind<QColorDialog>("Material color"),
         Enter::Accept,
         "OK",
         Return::Opener,
         {},
         {},
         {childOf(ctx, named("materialDialog"), "materialChooseColor")}});
    add({"entity-edit", "materials", "Edit entity",
         "View > Entity info (palette), then Edit entity… (Tab, Space)", selectTable,
         viaPanel("Entity info", "entityInfoEdit"), named("entityInfoDialog"), Enter::Accept, "OK",
         Return::OpenerPanel});
    add({"style-edit", "styles", "Model style",
         "View > Model styles (palette), then Colors and details… (Tab, Space)", fresh,
         viaPanel("Model styles", "styleCustomize"), named("styleDialog"), Enter::Accept, "OK",
         Return::OpenerPanel});
    add({"solar-edit", "materials", "Sun and shadows",
         "View > Sun and shadows (palette), then Edit sun study… (Tab, Space)", fresh,
         viaPanel("Sun and shadows", "solarEditButton"), named("solarDialog"), Enter::Accept,
         "Save", Return::OpenerPanel});
    add({"scene-new", "styles", "New saved scene",
         "View > Saved scenes (palette), then New… (Tab, Space; or Ctrl+Shift+N)", fresh,
         viaPanel("Saved scenes", "sceneNewButton"), named("savedSceneDialog"), Enter::Accept,
         "Save", Return::OpenerPanel});
    add({"section-new", "sections", "New section plane",
         "View > Section planes (palette), then New… (Tab, Space)", fresh,
         viaPanel("Section planes", "sectionNewButton"), named("sectionPlaneDialog"), Enter::Accept,
         "Save", Return::OpenerPanel});
    add({"text-create", "sections", "Create 3D text",
         "View > 3D text (palette), then Create 3D text… (Tab, Space)", fresh,
         viaPanel("3D text", "textCreateButton"), named("textDialog"), Enter::Accept, "Save",
         Return::OpenerPanel});
    add({"reference-import", "styles", "Import reference image (file chooser)",
         "View > Reference images (palette), then Import reference image… (Tab, Space)", fresh,
         viaPanel("Reference images", "referenceImageImport"), fileDialog("Import reference image"),
         Enter::DefaultOnly, "Open", Return::OpenerPanel});
    add({"reference-place",
         "annotations",
         "Place reference image",
         "Import reference image… > file",
         fresh,
         viaPanel("Reference images", "referenceImageImport"),
         named("referenceImportDialog"),
         Enter::Accept,
         "Save",
         Return::OpenerPanel,
         {},
         {},
         {chooseFile("Import reference image", pngFile(ctx))}});
    add({"reference-edit", "annotations", "Edit reference image",
         "View > Reference images (palette), then Edit selected image… (Tab, Space)",
         referenceImage, viaPanel("Reference images", "referenceImageEdit"),
         named("referenceEditDialog"), Enter::Accept, "Save", Return::OpenerPanel});
    add({"reference-calibrate", "sections", "Calibrate reference image",
         "View > Reference images (palette), then Calibrate selected image… (Tab, Space)",
         referenceImage, viaPanel("Reference images", "referenceImageCalibrate"),
         named("referenceCalibrationDialog"), Enter::Accept, "Save", Return::OpenerPanel});
    add({"outliner-rename", "outliner", "Rename (outliner)",
         "View > Outliner (palette), then Rename (Tab, Space; or F2)", selectTable,
         viaPanel("Outliner", "outliner.renameButton"), namedTitle("organizationDialog", "Rename"),
         Enter::Accept, "OK", Return::OpenerPanel});
    add({"outliner-move", "outliner", "Move to parent (outliner)",
         "View > Outliner (palette), then Move to (Tab, Space; or Ctrl+Shift+M)", selectTable,
         viaPanel("Outliner", "outliner.moveButton"),
         namedTitle("organizationDialog", "Move to parent"), Enter::Accept, "OK",
         Return::OpenerPanel});
    add({"outliner-tag", "outliner", "Assign tag",
         "View > Outliner (palette), then Tag (Tab, Space; or Ctrl+Alt+T)", selectTable,
         viaPanel("Outliner", "outliner.tagButton"), namedTitle("organizationDialog", "Assign tag"),
         Enter::Accept, "OK", Return::OpenerPanel});
    add({"tag-new",
         "outliner",
         "New tag",
         "View > Tags and folders (palette), then New tag (Tab, Space; or Ctrl+Alt+N)",
         fresh,
         viaPanel("Tags and folders", "tags.newButton"),
         namedTitle("organizationDialog", "New tag"),
         Enter::Accept,
         "OK",
         Return::OpenerPanel,
         {},
         typeName});
    add({"tag-folder",
         "outliner",
         "New tag folder",
         "View > Tags and folders (palette), then Folder (Tab, Space; or Ctrl+Alt+F)",
         fresh,
         viaPanel("Tags and folders", "tags.folderButton"),
         namedTitle("organizationDialog", "New tag folder"),
         Enter::Accept,
         "OK",
         Return::OpenerPanel,
         {},
         typeName});
    add({"material-edit", "materials", "Edit material",
         "View > Materials (palette), then Edit… (Tab, Space)", namedMaterial,
         viaPanel("Materials", "materialEdit"), namedTitle("materialDialog", "Edit material"),
         Enter::Accept, "OK", Return::OpenerPanel});
    add({"material-attach", "materials", "Attach file (file chooser)",
         "View > Materials (palette), then Attach file… (Tab, Space)", namedMaterial,
         viaPanel("Materials", "materialAttach"), fileDialog("Attach file"), Enter::DefaultOnly,
         "Open", Return::OpenerPanel});
    add({"material-replace-file", "materials", "Replace stored file (file chooser)",
         "View > Materials (palette), then Replace file… (Tab, Space)",
         [](Context &context) {
             load(context, source("tests/fixtures/m4-complete-v11.sketchyup"));
             for (const auto &[id, material] : context.window.document().materials())
                 if (material->asset) {
                     context.window.viewport()->setPaintMaterial(id, 2);
                     return;
                 }
             throw Failure("Fixture has a material with a stored file");
         },
         viaPanel("Materials", "materialResolve"),
         [](QWidget *w) {
             return qobject_cast<QFileDialog *>(w) && (w->windowTitle() == "Replace stored file" ||
                                                       w->windowTitle() == "Attach file");
         },
         Enter::DefaultOnly, "Open", Return::OpenerPanel});
    add({"texture-mapping", "materials", "Texture mapping",
         "View > Materials (palette), then Texture mapping… (Tab, Space)", selectTableFace,
         viaPanel("Materials", "materialTexture"), named("textureDialog"), Enter::Accept, "Apply",
         Return::OpenerPanel});
    add({"style-color",
         "styles",
         "Model style color (color chooser)",
         "Model style > Background (Tab, Space)",
         fresh,
         viaPanel("Model styles", "styleCustomize"),
         kind<QColorDialog>("Background"),
         Enter::Accept,
         "OK",
         Return::Opener,
         {},
         {},
         {childOf(ctx, named("styleDialog"), "styleBackgroundColor")}});
    add({"scene-update", "styles", "Update saved scene",
         "View > Saved scenes (palette), then Update… (Tab, Space)",
         loadRow("tests/fixtures/saved-scenes-v18.sketchyup", "savedScenesList"),
         viaPanel("Saved scenes", "sceneUpdateButton"),
         namedTitle("savedSceneDialog", "Update saved scene"), Enter::Accept, "Save",
         Return::OpenerPanel});
    add({"scene-rename", "styles", "Rename saved scene",
         "View > Saved scenes (palette), then Rename… (Tab, Space; or F2)",
         loadRow("tests/fixtures/saved-scenes-v18.sketchyup", "savedScenesList"),
         viaPanel("Saved scenes", "sceneRenameButton"),
         namedTitle("savedSceneDialog", "Rename saved scene"), Enter::Accept, "Save",
         Return::OpenerPanel});
    add({"section-edit", "sections", "Edit section plane",
         "View > Section planes (palette), then Edit… (Tab, Space)",
         loadRow("tests/fixtures/section-planes-v19.sketchyup", "sectionPlanesList"),
         viaPanel("Section planes", "sectionEditButton"),
         namedTitle("sectionPlaneDialog", "Edit section plane"), Enter::Accept, "Save",
         Return::OpenerPanel});
    add({"section-color",
         "sections",
         "Section fill color (color chooser)",
         "Section plane dialog > fill color (Tab, Space)",
         fresh,
         viaPanel("Section planes", "sectionNewButton"),
         kind<QColorDialog>(""),
         Enter::Accept,
         "OK",
         Return::Opener,
         {},
         {},
         {childOf(ctx, named("sectionPlaneDialog"), "sectionColor")}});
    add({"annotation-dimension", "annotations", "New dimension",
         "View > Dimensions and labels (palette), then Dimension… (Tab, Space; or Ctrl+Shift+D)",
         fresh, viaPanel("Dimensions and labels", "annotationDistanceButton"),
         named("annotationDialog"), Enter::Accept, "Save", Return::OpenerPanel});
    add({"annotation-label", "annotations", "New label",
         "View > Dimensions and labels (palette), then Label… (Tab, Space; or Ctrl+Shift+L)",
         selectTableFace, viaPanel("Dimensions and labels", "annotationLabelButton"),
         named("annotationDialog"), Enter::Accept, "Save", Return::OpenerPanel});
    add({"annotation-edit", "annotations", "Edit annotation",
         "View > Dimensions and labels (palette), then Edit / rebind… (Tab, Space)",
         loadRow("tests/fixtures/annotations-v21.sketchyup", "annotationsList"),
         viaPanel("Dimensions and labels", "annotationEditButton"), named("annotationDialog"),
         Enter::Accept, "Save", Return::OpenerPanel});
    add({"annotation-color",
         "annotations",
         "Annotation color (color chooser)",
         "Annotation dialog > color (Tab, Space)",
         loadRow("tests/fixtures/annotations-v21.sketchyup", "annotationsList"),
         viaPanel("Dimensions and labels", "annotationEditButton"),
         kind<QColorDialog>(""),
         Enter::Accept,
         "OK",
         Return::Opener,
         {},
         {},
         {childOf(ctx, named("annotationDialog"), "annotationColor")}});
    add({"text-edit", "sections", "Edit 3D text",
         "View > 3D text (palette), then Edit text… (Tab, Space; or F2)",
         loadRow("tests/fixtures/editable-text-v22.sketchyup", "textList"),
         viaPanel("3D text", "textEditButton"), named("textDialog"), Enter::Accept, "Save",
         Return::OpenerPanel});
    add({"tag-rename", "outliner", "Rename tag",
         "View > Tags and folders (palette), then Rename (Tab, Space; or F2)", firstTag,
         viaPanel("Tags and folders", "tags.renameButton"),
         namedTitle("organizationDialog", "Rename"), Enter::Accept, "OK", Return::OpenerPanel});
    add({"tag-move", "outliner", "Move tag to folder",
         "View > Tags and folders (palette), then Move to (Tab, Space; or Ctrl+Shift+M)", firstTag,
         viaPanel("Tags and folders", "tags.moveButton"),
         namedTitle("organizationDialog", "Move to parent"), Enter::Accept, "OK",
         Return::OpenerPanel});
    // ------------------------------------------------------------- blender ----------
    add({"animation-setup",
         "animation",
         "Export saved scenes as PNG frames",
         "Render with Blender > Export scene animation… (Tab, Space)",
         cameraScenes,
         viaPalette("Render…"),
         named("animationSetup"),
         Enter::Next,
         "Export frames",
         Return::Opener,
         named("animationExportProgress"),
         [&ctx](QWidget &sheet) { exportSettings(sheet, ctx); },
         {childOf(ctx, named("renderSetup"), "exportSceneAnimation", verifiedBlender(ctx))},
         "Uses the project's fake Blender worker; Export frames starts a real frame batch."});
    add({"animation-folder",
         "animation",
         "Choose animation parent folder (folder chooser)",
         "Export saved scenes > Choose parent folder… (Tab, Space)",
         cameraScenes,
         viaPalette("Render…"),
         fileDialog("Choose animation parent folder"),
         Enter::DefaultOnly,
         "Choose",
         Return::Opener,
         {},
         {},
         {{named("renderSetup"),
           [&ctx](QWidget &setup) {
               waitDialogFocus(setup);
               verifiedBlender(ctx)(setup);
               press(tabTo(setup, "exportSceneAnimation"));
           }},
          childOf(ctx, named("animationSetup"), "text:Choose parent folder…")}});
    add({"animation-progress",
         "blender",
         "Export scene animation (progress)",
         "Export saved scenes > Export frames (Enter)",
         cameraScenes,
         viaPalette("Render…"),
         named("animationExportProgress"),
         Enter::Closes,
         "Close",
         Return::Viewport,
         {},
         [](QWidget &progress) {
             // Audit the finished batch so the button label is stable.
             require(QTest::qWaitFor(
                         [&] {
                             auto *button = progress.findChild<QPushButton *>();
                             return button && button->text() == "Close";
                         },
                         20000),
                     "Fake animation batch finishes");
         },
         {{named("renderSetup"),
           [&ctx](QWidget &setup) {
               waitDialogFocus(setup);
               verifiedBlender(ctx)(setup);
               press(tabTo(setup, "exportSceneAnimation"));
           }},
          enterThrough(named("animationSetup"),
                       [&ctx](QWidget &sheet) { exportSettings(sheet, ctx); })}});
    // Render result page: render with the fake worker by keyboard (Enter on Render), then
    // reach the result page's buttons from wherever focus lands.
    const auto renderedResult = [&ctx](const QString &button, const QString &mode = "success") {
        return std::vector<std::pair<Match, std::function<void(QWidget &)>>>{
            {named("renderSetup"),
             [&ctx, mode](QWidget &setup) {
                 waitDialogFocus(setup);
                 verifiedBlender(ctx, mode)(setup);
                 setup.findChild<QSpinBox *>("renderWidth")->setValue(64);
                 setup.findChild<QSpinBox *>("renderHeight")->setValue(64);
                 setup.findChild<QSpinBox *>("renderSamples")->setValue(1);
                 press(tabTo(setup, "startRender"));
             }},
            {[&ctx](QWidget *w) {
                 auto *tabs = ctx.window.findChild<QTabWidget *>("modelTabs");
                 return w == &ctx.window && tabs && tabs->currentWidget() &&
                        tabs->currentWidget()->objectName() == "renderResult" &&
                        !ctx.window.renderPanel()->active();
             },
             [&ctx, button](QWidget &window) {
                 // The setup sheet hid itself; Weston leaves no window active.
                 ctx.phase["activationFallbackAfterRender"] = activate(window);
                 require(QTest::qWaitFor([&] { return window.isActiveWindow(); }, 5000),
                         "Window active after render (active " +
                             identity(QApplication::activeWindow()) + ")");
                 ctx.phase["focusAfterRender"] = identity(focused());
                 auto *page = window.findChild<QTabWidget *>("modelTabs")->currentWidget();
                 auto *target = tabTo(window, button);
                 require(page->isAncestorOf(target), "Tab reaches the render result page");
                 ctx.opener = target;
                 ctx.openerId = identity(target);
                 press(target);
             }}};
    };
    add({"render-save-image",
         "blender",
         "Save render image (file chooser)",
         "Render (Tab, Space), then Save image as… on the result page (Tab, Space)",
         fresh,
         viaPalette("Render…"),
         fileDialog("Save render image"),
         Enter::DefaultOnly,
         "Save",
         Return::Opener,
         {},
         {},
         renderedResult("saveRender")});
    add({"blender-handoff",
         "blender",
         "Save and open Blender scene (file chooser)",
         "Render (Tab, Space), then Open scene in Blender… on the result page (Tab, Space)",
         fresh,
         viaPalette("Render…"),
         fileDialog("Save and open Blender scene — one-way handoff"),
         Enter::DefaultOnly,
         "Save and open",
         Return::Opener,
         {},
         {},
         renderedResult("openBlenderScene")});
    // UX_DESIGN §3.3: below 1100 px the Assistant opens in the narrow layout's overlay sheet,
    // not a dialog; narrow_layout_input_tests covers its keyboard path.
    add({"assistant-consent",
         "assistant",
         "What is sent (permission before OpenAI requests)",
         "Assistant composer: type a request, then Send (Tab, Space)",
         [](Context &context) {
             hideAssistant(context);
             QSettings settings("SketchyUp", "SketchyUp");
             settings.setValue("assistant/provider", "OpenAI");
             settings.setValue("assistant/openaiModel", "audit-model");
         },
         [](Context &context) {
             QTest::keyClick(focused(), Qt::Key_J, Qt::ControlModifier);
             auto *panel = context.window.assistantPanel();
             require(QTest::qWaitFor([&] {
                         return focused() && focused()->objectName() == "assistantComposer";
                     }),
                     "Ctrl+J focuses the assistant request");
             QTest::keyClicks(focused(), "Describe the model");
             // Tab leaves the composer for the Send button.
             auto *send = tabTo(*panel, "assistantSend");
             context.opener = send;
             context.openerId = identity(send);
             press(send);
         },
         namedTitle("assistantConsent", "What is sent"),
         Enter::DefaultOnly,
         "Allow OpenAI requests",
         Return::Opener,
         {},
         {},
         {},
         "Allow is not pressed: no provider request is made by the audit."});
    {
        auto hops = renderedResult("openBlenderScene", "hold-handoff");
        hops.push_back(chooseFile("Save and open Blender scene — one-way handoff",
                                  newFile(ctx, "handoff", ".blend")));
        add({"blender-handoff-progress",
             "animation",
             "One-way Blender handoff (progress)",
             "Open scene in Blender… > file name",
             fresh,
             viaPalette("Render…"),
             named("blenderHandoffProgress"),
             Enter::Closes,
             "Cancel",
             Return::Opener,
             {},
             {},
             hops,
             "The fake worker keeps the scene preparation running so Cancel is live."});
    }
    add({"chatgpt-welcome",
         "assistant",
         "You're using your ChatGPT plan (message)",
         "Opens itself in Assistant preferences when a ChatGPT sign-in completes",
         [](Context &context) {
             hideAssistant(context);
             QSettings("SketchyUp", "SketchyUp").remove("assistant/chatgptWelcomed");
         },
         [](Context &context) {
             QTest::keyClick(focused(), Qt::Key_J, Qt::ControlModifier);
             auto *panel = context.window.assistantPanel();
             require(QTest::qWaitFor([&] { return inside(*panel, focused()); }),
                     "Ctrl+J focuses the assistant");
             press(tabTo(*panel, "assistantPreferences"));
         },
         kind<QMessageBox>("You're using your ChatGPT plan"),
         Enter::Closes,
         "Got it",
         Return::Opener,
         {},
         {},
         {{named("assistantPreferencesDialog"),
           [&ctx](QWidget &dialog) {
               waitDialogFocus(dialog);
               auto *opener = focused();
               ctx.opener = opener;
               ctx.openerId = identity(opener);
               // Simulates the completed browser sign-in; no account or network is used.
               auto *auth = ctx.window.assistantPanel()->findChild<ChatGptAuth *>();
               require(auth, "Assistant owns its ChatGPT sign-in");
               emit auth->connected("audit-client");
               const auto welcome = kind<QMessageBox>("You're using your ChatGPT plan");
               (void)QTest::qWaitFor([&] { return shown(welcome) != nullptr; }, 3000);
               (void)QTest::qWaitFor([&] { return shown(welcome) == nullptr; }, 20000);
               (void)QTest::qWaitFor([&] { return returnFocus(&dialog) == opener; }, 2000);
               ctx.afterChildId = identity(returnFocus(&dialog));
               ctx.afterChildIsOpener = returnFocus(&dialog) == opener;
               dialog.activateWindow();
               (void)QTest::qWaitForWindowActive(&dialog);
               QTest::keyClick(focused(), Qt::Key_Escape);
               (void)QTest::qWaitFor([&] { return !dialog.isVisible(); }, 3000);
           }}},
         "The sign-in is simulated by emitting ChatGptAuth::connected; nothing is sent."});
    // ---------------------------------------------------------- assistant/render ----
    add({"render-setup", "render", "Render with Blender", "Camera > Render… (palette)", fresh,
         viaPalette("Render…"), named("renderSetup"), Enter::DefaultOnly, "Render"});
    add({"render-jobs",
         "render",
         "Render jobs",
         "Render with Blender > Jobs… (Tab, Space)",
         fresh,
         viaPalette("Render…"),
         named("renderJobs"),
         Enter::DefaultOnly,
         "Open image",
         Return::Opener,
         {},
         {},
         {childOf(ctx, named("renderSetup"), "showRenderJobs")}});
    add({"blender-executable",
         "render",
         "Choose Blender (file chooser)",
         "Render with Blender > Choose executable… (Tab, Space)",
         fresh,
         viaPalette("Render…"),
         fileDialog("Choose Blender 5.2 LTS"),
         Enter::DefaultOnly,
         "Open",
         Return::Opener,
         {},
         {},
         {childOf(ctx, named("renderSetup"), "chooseBlender")}});
    add({"hdr-environment",
         "render",
         "Choose HDR environment (file chooser)",
         "Render with Blender > Choose HDR panorama… (Tab, Space)",
         fresh,
         viaPalette("Render…"),
         fileDialog("Choose HDR environment"),
         Enter::DefaultOnly,
         "Open",
         Return::Opener,
         {},
         {},
         {childOf(ctx, named("renderSetup"), "chooseRenderEnvironment")}});
    add({"assistant-preferences",
         "assistant",
         "Assistant preferences",
         "View > Assistant (Ctrl+J), then Preferences… (Tab, Space)",
         hideAssistant,
         [](Context &context) {
             context.opener = focused();
             context.openerId = identity(focused());
             QTest::keyClick(focused(), Qt::Key_J, Qt::ControlModifier);
             auto *panel = context.window.assistantPanel();
             require(QTest::qWaitFor([&] { return inside(*panel, focused()); }),
                     "Ctrl+J focuses the assistant");
             auto *button = tabTo(*panel, "assistantPreferences");
             context.opener = button;
             context.openerId = identity(button);
             press(button);
         },
         named("assistantPreferencesDialog"),
         Enter::Accept,
         "Save",
         Return::Opener,
         {},
         [](QWidget &dialog) {
             // A provider needs a model before Save can apply; "None" saves as is.
             dialog.findChild<QComboBox *>("assistantProviderChoice")->setCurrentText("None");
         }});
    // The preference form changes with the provider and OpenAI sign-in method.
    for (const auto &[id, provider, auth] :
         {std::tuple{"assistant-preferences-ollama", "Ollama", -1},
          std::tuple{"assistant-preferences-chatgpt", "OpenAI", 0},
          std::tuple{"assistant-preferences-api-key", "OpenAI", 1}})
        add({id,
             "assistant",
             QString("Assistant preferences (%1%2)")
                 .arg(provider)
                 .arg(auth < 0 ? ""
                      : auth   ? ", API key"
                               : ", ChatGPT plan"),
             "View > Assistant (Ctrl+J), then Preferences… (Tab, Space)",
             hideAssistant,
             [](Context &context) {
                 QTest::keyClick(focused(), Qt::Key_J, Qt::ControlModifier);
                 auto *panel = context.window.assistantPanel();
                 require(QTest::qWaitFor([&] { return inside(*panel, focused()); }),
                         "Ctrl+J focuses the assistant");
                 auto *button = tabTo(*panel, "assistantPreferences");
                 context.opener = button;
                 context.openerId = identity(button);
                 press(button);
             },
             named("assistantPreferencesDialog"),
             Enter::DefaultOnly,
             "Save",
             Return::Opener,
             {},
             [provider, auth](QWidget &dialog) {
                 dialog.findChild<QComboBox *>("assistantProviderChoice")->setCurrentText(provider);
                 if (auth >= 0)
                     dialog.findChild<QComboBox *>("assistantOpenAIAuth")->setCurrentIndex(auth);
                 QTest::qWait(25);
             }});
    add({"assistant-disclosure", "assistant", "What is sent (assistant disclosure)",
         "View > Assistant (Ctrl+J), then What is sent (Shift+Tab, Space)", hideAssistant,
         [](Context &context) {
             context.opener = focused();
             context.openerId = identity(focused());
             QTest::keyClick(focused(), Qt::Key_J, Qt::ControlModifier);
             auto *panel = context.window.assistantPanel();
             require(QTest::qWaitFor([&] { return inside(*panel, focused()); }),
                     "Ctrl+J focuses the assistant");
             auto *button = tabTo(*panel, "assistantDisclosure", true);
             context.opener = button;
             context.openerId = identity(button);
             press(button);
         },
         named("assistantConsent"), Enter::Closes, "Cancel", Return::Opener});
    // ----------------------------------------------------------- exchange ----------
    // R097.e1, R098.a3, R098.c2, R102.c: the IFC, STEP and SketchUp exchange commands. The
    // offline fixture has no helper, so each command opens its "not installed" availability
    // sheet directly, with no file chooser first. Each sheet's OK is its only visible button,
    // and the audit finds it the default. The SketchUp helper is built under src/skp, not beside
    // this binary, so findSkpWorker does not find it.
    add({"ifc-import-unavailable", "exchange", "IFC import (helper not installed)",
         "File > Import IFC… (palette)", fresh, viaPalette("Import IFC…"),
         namedTitle("ifcUnavailable", "IFC import"), Enter::Closes, "OK"});
    add({"ifc-export-unavailable", "exchange", "IFC export (helper not installed)",
         "File > Export IFC… (palette)", fresh, viaPalette("Export IFC…"),
         namedTitle("ifcUnavailable", "IFC export"), Enter::Closes, "OK"});
    add({"step-import-unavailable", "exchange", "STEP import (helper not installed)",
         "File > Import STEP… (palette)", fresh, viaPalette("Import STEP…"),
         namedTitle("stepUnavailable", "STEP import"), Enter::Closes, "OK"});
    add({"step-export-unavailable", "exchange", "STEP export (helper not installed)",
         "File > Export STEP… (palette)", fresh, viaPalette("Export STEP…"),
         namedTitle("stepExportUnavailable", "STEP export"), Enter::Closes, "OK"});
    add({"skp-import-unavailable", "exchange", "SketchUp import (helper not installed)",
         "File > Import SketchUp (.skp)… (palette)", fresh,
         viaPalette("Import SketchUp (.skp)…"),
         namedTitle("skpUnavailable", "SketchUp import"), Enter::Closes, "OK"});
    // -------------------------------------------------------------- sheets ---------
    // R091.f: opened from the page canvas through the palette; focus returns to the canvas.
    add({"sheet-choose-scene", "sheets", "Choose scene",
         "Sheets: Tab selects a view, then Page > Choose scene… (palette)", sheetView,
         [](Context &context) {
             selectSheetView(context, false);
             palette(context, "Choose scene…");
         },
         named("chooseSceneDialog"), Enter::Accept, "Choose", Return::Opener});
    add({"sheet-save-scene", "sheets", "Save as new scene",
         "Sheets: Tab selects a view, Enter opens edit-view, then Page > Save as new scene… "
         "(palette)",
         sheetView,
         [](Context &context) {
             selectSheetView(context, true);
             palette(context, "Save as new scene…");
         },
         named("saveViewSceneDialog"), Enter::Accept, "Save scene", Return::Opener});
    // R092.b: the preflight dialogs, audited once their findings (and the findings' controls)
    // are listed. Their default button exports a file or prints, so only the default is
    // asserted; Choose… opens the platform's file or print dialog.
    const auto preflightSettles = [](QWidget &dialog) {
        auto *sheet = dynamic_cast<SheetExportDialog *>(&dialog);
        require(sheet && QTest::qWaitFor([&] { return !sheet->checking() && sheet->preflight(); },
                                         20000),
                "The export preflight settles");
    };
    add({"sheet-export",
         "sheets",
         "Export sheets as PDF",
         "Sheets: File > Export sheets… (palette)",
         sheetView,
         [](Context &context) {
             selectSheetView(context, false);
             palette(context, "Export sheets…");
         },
         named("sheetExportDialog"),
         Enter::DefaultOnly,
         "Export PDF",
         Return::Opener,
         {},
         preflightSettles});
    add({"sheet-print",
         "sheets",
         "Print sheets",
         "Sheets: File > Print… (palette)",
         sheetView,
         [](Context &context) {
             selectSheetView(context, false);
             palette(context, "Print…");
         },
         named("sheetPrintDialog"),
         Enter::DefaultOnly,
         "Print",
         Return::Opener,
         {},
         preflightSettles});
    add({"sheet-export-model",
         "sheets",
         "Export sheets as PDF (from Model)",
         "Model: File > Export sheets… (palette)",
         sheetView,
         [](Context &context) { palette(context, "Export sheets…"); },
         named("sheetExportDialog"),
         Enter::DefaultOnly,
         "Export PDF",
         Return::Viewport,
         {},
         preflightSettles});
    // ------------------------------------------------------------ quantities ---------
    // R101.e2 (UX_DESIGN §19): the Quantities section's dialogs. Save needs a saved model, so the
    // case loads one; Delete needs a schedule, which the case saves in the isolated store. Rename
    // and the export file choosers are not audited yet: their keyboard checks fail here (see the
    // notes).
    add({"quantities-save-schedule", "quantities", "Save as schedule",
         "View > Quantities (palette), then Save as schedule… (Tab, Space)",
         [](Context &context) {
             fresh(context);
             load(context, source("tests/fixtures/container-tags-v9.sketchyup"));
         },
         viaPanel("Quantities", "quantitiesSaveSchedule"), named("scheduleNameDialog"),
         Enter::Disabled, "Save", Return::OpenerPanel});
    add({"quantities-delete-schedule", "quantities", "Delete schedule (confirmation)",
         "View > Quantities (palette), then Delete on the schedule row", quantitiesSchedule,
         keyOnSchedule(Qt::Key_Delete), named("scheduleDeleteDialog"), Enter::Closes, "Cancel",
         Return::OpenerPanel});

    return all;
}
// Dialogs that exist in the product but cannot be opened in the isolated, offline fixture.
// Public "…" commands that edit in place rather than open a dialog.
inline QJsonArray inPlaceEditors() {
    return QJsonArray{QJsonObject{
        {"id", "sheet.page.rename"},
        {"reason", "Renames the page in place in the Sheets Tray's Pages tree (F2); no dialog "
                   "opens (ADR 0164 §4)."}}};
}
// Public "…" commands that are disabled until an import has run. An import needs its helper,
// which the offline fixture does not have, so no case can make these reports appear. Each
// reason says what the named suite covers. The suite re-shows the report from the command, but
// closes it by clicking OK, so the report's own Enter and Escape are not audited anywhere.
inline QJsonArray needsPriorImport() {
    return QJsonArray{
        QJsonObject{
            {"id", "file.ifcImportReport"},
            {"reason", "Disabled until an IFC import completes; the import needs the IFC helper, "
                       "which this fixture lacks. ifc_import_dialog_tests re-shows the report and "
                       "checks its contents, but closes it by clicking OK: the report's Enter and "
                       "Escape are not audited."}},
        QJsonObject{
            {"id", "file.stepImportReport"},
            {"reason", "Disabled until a STEP import completes; the import needs the IFC helper, "
                       "which this fixture lacks. step_import_dialog_tests re-shows the report and "
                       "checks its contents, but closes it by clicking OK: the report's Enter and "
                       "Escape are not audited."}},
        QJsonObject{
            {"id", "file.skpImportReport"},
            {"reason", "Disabled until a SketchUp import completes; the import needs the SketchUp "
                       "helper, which this fixture lacks. skp_import_dialog_tests re-shows the "
                       "report and checks its contents, but closes it by clicking OK: the report's "
                       "Enter and Escape are not audited."}},
    };
}
inline QJsonArray unexercised() {
    return QJsonArray{
        QJsonObject{{"id", "file-operation-progress"},
                    {"title", "Saving/Opening model… (progress)"},
                    {"reason", "Non-activating tooltip surface with no controls (R082.jj); it "
                               "never takes keyboard focus by design."}},
    };
}
} // namespace dialog_audit
