// Explicit opt-in live acceptance; synthetic documents only, never a user's open model.
#include "app/render_panel.hpp"
#include "app/surface_format.hpp"
#include "app/window.hpp"
#include "automation/commands.hpp"
#include "core/entity_measure.hpp"
#include "io/document_io.hpp"
#include <QAction>
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDateTime>
#include <QDialog>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QJsonDocument>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QRegularExpression>
#include <QSettings>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTest>
#include <iostream>
using namespace sketchy;
void check(bool ok, const char *message) {
    if (!ok)
        throw std::runtime_error(message);
}
void write(const QString &path, QJsonObject data) {
    QFile f(path);
    check(f.open(QIODevice::WriteOnly | QIODevice::NewOnly), "Fresh evidence file");
    f.setPermissions(QFile::ReadOwner | QFile::WriteOwner);
    const auto bytes = QJsonDocument(data).toJson();
    check(f.write(bytes) == bytes.size(), "Write evidence");
}
QJsonObject content(const Document &doc) {
    auto obj = QJsonDocument::fromJson(encodeDocument(doc)).object();
    // Undo preserves allocator high-water marks to avoid reusing stable IDs.
    for (auto key :
         {"revision", "nextId", "nextDefinitionId", "nextTagId", "nextMaterialId", "nextAssetId"})
        obj.remove(key);
    return obj;
}
Vec3 dimensions(const Document &doc, Id body) {
    const auto bounds = measureEntity(doc, {body, SelectionKind::Body, 0}).world.bounds;
    check(bool(bounds), "Measured bounds");
    return bounds->dimensions();
}
int main(int argc, char **argv) {
    QSurfaceFormat format;
    format.setVersion(3, 3);
    format.setProfile(QSurfaceFormat::CoreProfile);
    format.setDepthBufferSize(24);
    sketchy::setDefaultViewportFormat(format);
    QApplication app(argc, argv);
    QCoreApplication::setApplicationName("SketchyUp");
    QCoreApplication::setOrganizationName("SketchyUp");
    const auto args = app.arguments();
    if (args.size() != 6 || args[1] != "--chatgpt" ||
        !QStringList{"preview", "direct", "manual", "room"}.contains(args[3])) {
        std::cerr << "Usage: native_provider_trial --chatgpt MODEL preview|direct|manual|room "
                     "INPUT.sketchyup "
                     "NEW_EVIDENCE_DIR\n";
        return 2;
    }
    const bool room = args[3] == "room";
    QJsonObject report{{"mode", args[3]},
                       {"model", args[2]},
                       {"liveProvider", true},
                       {"recordedAt", QDateTime::currentDateTimeUtc().toString(Qt::ISODate)}};
    const auto root = QDir(args[5]).absolutePath();
    if (QFile::exists(root) || !QDir().mkdir(root))
        return 2;
    QFile::setPermissions(root, QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner);
    QTemporaryDir settings;
    try {
        // Copy public account metadata only. Native preferences and the user's model are untouched.
        QVariantMap publicSettings;
        {
            QSettings original("SketchyUp", "SketchyUp");
            for (auto key : {"assistant/chatgptAccount", "assistant/chatgptAccounts",
                             "assistant/chatgptHostId"})
                publicSettings[key] = original.value(key);
        }
        check(!publicSettings["assistant/chatgptAccount"].toString().isEmpty(),
              "Configure ChatGPT in native Preferences first");
        qputenv("XDG_CONFIG_HOME", settings.path().toUtf8());
        QSettings::setPath(QSettings::NativeFormat, QSettings::UserScope, settings.path());
        QSettings prefs("SketchyUp", "SketchyUp");
        check(prefs.fileName().startsWith(settings.path()), "Isolated native trial preferences");
        for (auto it = publicSettings.cbegin(); it != publicSettings.cend(); ++it)
            prefs.setValue(it.key(), it.value());
        prefs.setValue("assistant/provider", "OpenAI");
        prefs.setValue("assistant/openaiAuth", "chatgpt");
        check(QRegularExpression("^[A-Za-z0-9][A-Za-z0-9_.:-]{0,127}$").match(args[2]).hasMatch(),
              "Explicit valid model ID");
        prefs.setValue("assistant/chatgptModel", args[2]);
        prefs.setValue("assistant/consentOpenAI", false);
        prefs.setValue("recoverySeconds", 0);
        prefs.sync();
        Window window(nullptr, {.outcomeRoot = root + "/outcomes"});
        window.resize(1500, 900);
        window.show();
        auto &doc = window.document();
        auto &view = *window.viewport();
        auto &panel = *window.assistantPanel();
        check(QTest::qWaitFor([&] { return view.rendererReady(); }), "Native renderer ready");
        window.openPath(args[4]);
        Id first{}, second{}, wall{};
        auto findTargets = [&] {
            for (const auto &[id, body] : doc.bodies()) {
                const auto prop = body->properties.find("recipe.role");
                if (body->name == "Room walls" ||
                    (prop != body->properties.end() &&
                     std::holds_alternative<std::string>(prop->second) &&
                     std::get<std::string>(prop->second) == "host-wall"))
                    wall = id;
                if (body->name == "Window A")
                    first = id;
                if (body->name == "Window B")
                    second = id;
            }
            if (!first)
                for (const auto &[id, instance] : doc.instances()) {
                    if (!first)
                        first = id;
                    else if (!second)
                        second = id;
                }
        };
        if (!room)
            findTargets();
        check(room ? doc.bodies().empty() : bool(first && second && wall),
              "Empty room input or two windows and host wall in resize fixture");
        // An ordinary native gesture before the live assistant request.
        if (!room) {
            view.setDrawingPlane({});
            view.setTool(Viewport::Tool::Rectangle);
            check(view.measurements("[8m,0m,0m]") && view.measurements("1m,1m"),
                  "Manual native edit before AI");
            view.setTool(Viewport::Tool::Select);
        }
        saveDocument(doc, root + "/before.sketchyup");
        auto before = loadDocument(root + "/before.sketchyup");
        const auto history = doc.history().position;
        const auto firstBounds =
            room ? EntityBounds{{.9, .05, .9}, {2.1, .15, 1.9}}
                 : *measureEntity(doc, {first, SelectionKind::Body, 0}).world.bounds;
        if (!room) {
            std::vector<Id> contextPath;
            for (auto parent = doc.bodies().at(first)->parent; parent;
                 parent = doc.bodies().at(parent)->parent)
                contextPath.push_back(parent);
            for (auto i = contextPath.rbegin(); i != contextPath.rend(); ++i)
                view.enterContext(*i);
            view.setSelection(first);
            view.fit();
            view.refresh();
            check(view.selectedBody() == first, "Target selected in its actual editing context");
        }
        window.findChild<QAction *>("view.assistant")->trigger();
        panel.findChild<QComboBox *>("assistantMode")->setCurrentIndex(args[3] == "direct" ? 1 : 0);
        const QString prompt = room ? "Create a room with 6 m by 4 m outside dimensions, 0.2 m "
                                      "thick walls and 2.7 m height. "
                                      "Put two reusable window instances in the front wall, "
                                      "centered at x=1.5 m and x=4.5 m. "
                                      "Each outer frame is 1.2 m wide by 1.0 m high, with a 0.9 m "
                                      "sill, 80 mm frame members, "
                                      "100 mm depth, glass and real through-wall openings. Inspect "
                                      "and measure the private "
                                      "result, then present a preview."
                                    : "Widen only the selected window (body " +
                                          QString::number(first) +
                                          ") by 0.2 m in outer-frame width (1.2 m to 1.4 m). Keep "
                                          "its center, sill, height, "
                                          "depth and 80 mm frame members unchanged. Preserve "
                                          "Window B and all unrelated "
                                          "entities. Update the real opening in the host wall, and "
                                          "make this window instance "
                                          "unique if necessary. Inspect and measure the private "
                                          "result, then present a preview.";
        report["prompt"] = prompt;
        panel.submit(prompt);
        auto *consent = window.findChild<QDialog *>("assistantConsent");
        check(consent && consent->isVisible(), "Actual native disclosure presented");
        consent->findChild<QPushButton *>("assistantConsentAllow")->click();
        QElapsedTimer timer;
        timer.start();
        QString last;
        QJsonArray phases;
        int answers{};
        while (timer.elapsed() < 315000) {
            QCoreApplication::processEvents();
            QThread::msleep(5);
            const auto result = panel.result();
            const auto phase = result.value("phase").toString();
            if (phase != last && !phase.isEmpty()) {
                phases.append(QJsonObject{{"phase", phase}, {"elapsedMs", timer.elapsed()}});
                std::cerr << phase.toStdString() << " " << timer.elapsed() << " ms\n";
                last = phase;
            }
            if (phase == "awaiting-clarification" && room)
                break;
            if (phase == "awaiting-clarification" && answers++ < 2) {
                (void)QTest::qWaitFor(
                    [&] { return panel.findChild<QLineEdit *>("assistantAnswerText") != nullptr; },
                    1000);
                auto *answer = panel.findChild<QLineEdit *>("assistantAnswerText");
                if (answer) {
                    answer->setText(
                        "Only the selected Window A, body " + QString::number(first) +
                        ". Widen its outer frame and matching wall opening to 1.4 m, preserving 80 "
                        "mm members. Make this instance unique. Keep Window B unchanged.");
                    panel.findChild<QPushButton *>("assistantAnswerTextSend")->click();
                } else
                    break;
            }
            if ((phase == "preview-ready" && args[3] != "direct") || phase == "completed" ||
                phase == "failed" || phase == "stale" || phase == "canceled" ||
                phase == "outcome-unknown")
                break;
        }
        report["elapsedMs"] = timer.elapsed();
        report["phases"] = phases;
        panel.refresh();
        QTest::qWait(100);
        auto proposal = panel.result();
        report["proposal"] = proposal;
        view.grabFramebuffer().save(root + "/preview-viewport.png");
        window.grab().save(root + "/preview-ui.png");
        if (proposal.value("phase") == "preview-ready") {
            check(content(doc) == content(before), "Preview has not changed the live document");
            panel.findChild<QPushButton *>("assistantApply")->click();
        }
        report["result"] = panel.result();
        report["activity"] =
            QJsonDocument::fromJson(
                panel.findChild<QPlainTextEdit *>("assistantRawActivity")->toPlainText().toUtf8())
                .object();
        check(panel.result().value("applied") == true, "Live assistant did not apply a proposal");
        saveDocument(doc, root + "/applied.sketchyup");
        check(doc.history().position == history + 1, "AI change occupies exactly one undo entry");
        if (room)
            findTargets();
        check(first && second && wall, "Result includes two window instances and a host wall");
        const auto bounds = *measureEntity(doc, {first, SelectionKind::Body, 0}).world.bounds;
        const auto volume = measureEntity(doc, {wall, SelectionKind::Body, 0}).world.volume;
        bool preserved = room || doc.bodies().size() == before.bodies().size();
        for (const auto &[id, body] : before.bodies()) {
            auto ancestor = id;
            while (ancestor && ancestor != first)
                ancestor = before.bodies().at(ancestor)->parent;
            if (ancestor != first && id != wall)
                preserved &= doc.bodies().contains(id) && *doc.bodies().at(id) == *body;
        }
        auto near = [](double x, double y) { return std::abs(x - y) < 1e-6; };
        bool members = true, glass = true;
        int frameParts{}, glassParts{};
        const double center = (firstBounds.low.x + firstBounds.high.x) / 2;
        const double halfWidth = room ? .6 : .7;
        for (const auto &[id, body] : doc.bodies()) {
            auto ancestor = id;
            while (ancestor && ancestor != first)
                ancestor = doc.bodies().at(ancestor)->parent;
            if (ancestor != first || body->surface.vertices.empty())
                continue;
            // Verify the frame solid and glass independently of the whole-window bounds.
            const auto part = measureEntity(doc, {id, SelectionKind::Body, 0}).world;
            const auto transform = doc.worldTransform(id);
            if (!part.volume) {
                ++glassParts;
                glass &= body->surface.vertices.size() == 4 && body->surface.faces.size() == 1;
                for (const auto &[vertex, local] : body->surface.vertices) {
                    const auto p = transform.point(local);
                    glass &=
                        near(std::abs(p.x - center), halfWidth - .08) &&
                        near(p.y, (firstBounds.low.y + firstBounds.high.y) / 2) &&
                        (near(p.z, firstBounds.low.z + .08) || near(p.z, firstBounds.high.z - .08));
                }
                continue;
            }
            ++frameParts;
            members &= near(*part.volume, (2 * halfWidth - (2 * halfWidth - .16) * .84) * .1);
            for (const auto &[vertex, local] : body->surface.vertices) {
                const auto p = transform.point(local);
                members &= (near(std::abs(p.x - center), halfWidth) ||
                            near(std::abs(p.x - center), halfWidth - .08)) &&
                           (near(p.z, firstBounds.low.z) || near(p.z, firstBounds.low.z + .08) ||
                            near(p.z, firstBounds.high.z - .08) || near(p.z, firstBounds.high.z));
            }
        }
        bool geometry =
            near(bounds.dimensions().x, room ? 1.2 : 1.4) && near(dimensions(doc, second).x, 1.2) &&
            near(bounds.low.x + bounds.high.x, firstBounds.low.x + firstBounds.high.x) &&
            near(bounds.low.z, firstBounds.low.z) && near(bounds.dimensions().z, 1) &&
            near(bounds.dimensions().y, firstBounds.dimensions().y) && volume &&
            near(*volume, room ? 9.888 : 9.848) && members && frameParts == 1 && glass &&
            glassParts == 1 && preserved;
        if (room) {
            const auto wallDimensions = dimensions(doc, wall);
            const auto sibling = *measureEntity(doc, {second, SelectionKind::Body, 0}).world.bounds;
            geometry &= doc.bodies().size() == 9 && doc.instances().size() == 2 &&
                        doc.definitions().size() == 1 && near(wallDimensions.x, 6) &&
                        near(wallDimensions.y, 4) && near(wallDimensions.z, 2.7) &&
                        near(sibling.low.x + sibling.high.x, 9) && near(sibling.low.z, .9) &&
                        near(sibling.dimensions().z, 1) && near(sibling.dimensions().y, .1);
        }
        report["measurements"] = QJsonObject{{"width", bounds.dimensions().x},
                                             {"siblingWidth", dimensions(doc, second).x},
                                             {"wallVolume", volume ? *volume : -1},
                                             {"membersPreserved", members && frameParts == 1},
                                             {"glassVerified", glass && glassParts == 1},
                                             {"unrelatedPreserved", preserved}};
        report["geometryVerified"] = geometry;
        check(geometry, "Independent geometry preservation check failed");
        const auto changed = content(doc);
        window.findChild<QAction *>("edit.undo")->trigger();
        check(content(doc) == content(before),
              "One native Undo restores the entire pre-AI document");
        window.findChild<QAction *>("edit.redo")->trigger();
        check(content(doc) == changed, "One native Redo restores the entire AI change");
        report["undoRedoVerified"] = true;
        saveDocument(doc, root + "/after.sketchyup");
        window.openPath(root + "/after.sketchyup");
        check(content(doc) == changed, "Native reopen retains AI geometry");
        report["saveReopenVerified"] = true;
        view.fit();
        view.refresh();
        view.grabFramebuffer().save(root + "/after-viewport.png");
        window.grab().save(root + "/after-ui.png");
        RenderOptions render;
        render.settings = {512, 512, 32, 0};
        BlenderJob::Options worker;
        worker.executable = "/usr/bin/blender";
        const auto revision = doc.revision();
        window.renderPanel()->start(render, worker, true);
        doc.addFace({{{10, 0, 0}, {11, 0, 0}, {11, 1, 0}, {10, 1, 0}}});
        view.refresh();
        emit view.changed();
        check(QTest::qWaitFor([&] { return !window.renderPanel()->active(); }, 120000),
              "Live native render completed");
        const auto image = window.renderPanel()->latest();
        check(image && image->manifest.value("revision").toString() == QString::number(revision),
              "Verified render identifies pre-edit source revision");
        window.renderPanel()->saveLatest(root + "/render.png");
        write(root + "/render-manifest.json", image->manifest);
        window.grab().save(root + "/render-ui.png");
        report["renderVerified"] = true;
        report["passed"] = true;
        saveDocument(doc, root + "/manual-after-render.sketchyup");
    } catch (const std::exception &error) {
        report["passed"] = false;
        report["failure"] = QString::fromUtf8(error.what());
        std::cerr << error.what() << '\n';
    }
    try {
        write(root + "/report.json", report);
    } catch (...) {
        return 1;
    }
    return report.value("passed").toBool() ? 0 : 1;
}
