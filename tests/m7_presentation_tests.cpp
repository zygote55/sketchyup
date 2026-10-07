#include "integrations/animation_export.hpp"
#include "m7_fixture.hpp"
#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QJsonDocument>
#include <QProcess>
#include <QTemporaryDir>
#include <QThread>
#include <iostream>
using namespace sketchy;
using m7::check;
void write(const QString &path, const QByteArray &data) {
    QFile file(path);
    check(file.open(QIODevice::WriteOnly) && file.write(data) == data.size(),
          "Retain study artifact");
}
QJsonObject json(const QString &path) {
    QFile file(path);
    check(file.open(QIODevice::ReadOnly), "Read study report");
    return QJsonDocument::fromJson(file.readAll()).object();
}
template <class T> void wait(T &job) {
    QElapsedTimer timer;
    timer.start();
    while (!job.done() && timer.elapsed() < 180000) {
        QCoreApplication::processEvents();
        QThread::msleep(2);
    }
    check(job.done(), "Study job terminates within deadline");
}
int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    const bool real = app.arguments().contains("--real");
    const auto executable = qEnvironmentVariable("SKETCHYUP_BLENDER_TEST");
    if (real && executable.isEmpty())
        return 77;
    try {
        auto study = m7::study();
        auto report = m7::verify(study);
        QTemporaryDir temporary;
        check(temporary.isValid(), "Study scratch");
        const auto retained = qEnvironmentVariable("SKETCHYUP_M7_EVIDENCE");
        const auto folder = retained.isEmpty() ? temporary.path() : retained;
        check(QDir().mkpath(folder), "Study output directory");
        saveDocument(study.document, folder + "/building-study.sketchyup");
        check(encodeDocument(loadDocument(folder + "/building-study.sketchyup")) ==
                  encodeDocument(study.document),
              "Study save/reopen is exact");
        const auto original = encodeDocument(study.document);
        const auto history = study.document.history().total;
        if (real) {
            RenderOptions options;
            options.settings = {320, 240, 4, 0};
            const auto capture = AnimationCapture::capture(
                study.document, {study.perspective, study.plan, study.section}, {1, 1, 0}, options);
            BlenderJob::Options worker;
            worker.executable = executable;
            worker.timeoutMs = 60000;
            AnimationExport animation;
            animation.start(capture, folder + "/animation", worker);
            wait(animation);
            if (animation.state() != AnimationExport::State::Completed)
                std::cerr << animation.message().toStdString() << '\n';
            check(animation.state() == AnimationExport::State::Completed &&
                      animation.completedFrames() == 3,
                  "Actual Blender renders all study views");
            report["animation"] = json(folder + "/animation/animation.json");
            auto prepared = PreparedRender::prepare(capture.frame(0));
            const auto blend = folder + "/building-study.blend";
            {
                BlenderJob handoff;
                handoff.handoff(prepared, worker);
                wait(handoff);
                check(bool(handoff.sceneResult()), "Actual Blender creates packed study handoff");
                saveBlenderScene(*handoff.sceneResult(), blend);
                report["handoff"] = handoff.sceneResult()->manifest;
            }
            const auto capturePath = prepared->directory();
            prepared.reset();
            check(!QFileInfo::exists(capturePath),
                  "Disposable study capture removed before reopening");
            const auto inspect = temporary.filePath("inspect.py");
            write(inspect,
                  QByteArrayLiteral(
                      "import bpy, json\ns=bpy.context.scene\nassert s.camera is not None\nassert "
                      "any(o.type == 'MESH' for o in s.objects)\nassert any(i.packed_file for i in "
                      "bpy.data.images)\nassert any(o.type == 'LIGHT' and o.data.type == 'SUN' for "
                      "o in s.objects)\nprint('SKETCHYUP_M7_REOPEN "
                      "'+json.dumps({'camera':s.camera.data.type,'packedImages':sum(bool(i.packed_"
                      "file) for i in bpy.data.images),'meshObjects':sum(o.type=='MESH' for o in "
                      "s.objects)}))\n"));
            QProcess reopen;
            reopen.start(executable, {"--background", "--factory-startup", "--disable-autoexec",
                                      blend, "--python-exit-code", "1", "--python", inspect});
            check(reopen.waitForStarted(10000) && reopen.waitForFinished(60000) &&
                      reopen.exitStatus() == QProcess::NormalExit && reopen.exitCode() == 0,
                  "Packed study reopens independently in Blender");
            const auto output = reopen.readAllStandardOutput();
            check(output.contains("SKETCHYUP_M7_REOPEN"), "Reopened study inspection ran");
            write(folder + "/blender-reopen.log", output + reopen.readAllStandardError());
            report["independentHandoffReopen"] = true;
        }
        check(encodeDocument(study.document) == original &&
                  study.document.history().total == history,
              "Study jobs preserve live model and history");
        write(folder + "/study.json", QJsonDocument(report).toJson());
        std::cout << "M7 building study dimensions, material, saved cameras, section state and "
                     "persistence pass"
                  << (real ? "; actual animation and packed Blender reopening pass\n" : "\n");
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
