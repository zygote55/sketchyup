#include "core/presentation.hpp"
#include "core/scenes.hpp"
#include "io/document_io.hpp"
#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QProcess>
#include <QProcessEnvironment>
#include <QTemporaryDir>
#include <QThread>
#include <csignal>
#include <iostream>
#include <signal.h>
#include <sys/types.h>
using namespace sketchy;
namespace {
void check(bool ok, const char *message) {
    if (!ok)
        throw std::runtime_error(message);
}
QByteArray read(const QString &path) {
    QFile file(path);
    check(file.open(QIODevice::ReadOnly), "Read CLI fixture");
    return file.readAll();
}
void write(const QString &path, const QByteArray &bytes) {
    QFile file(path);
    check(file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size(),
          "Write CLI fixture");
}
QString quote(QString text) { return "'" + text.replace("'", "'\\''") + "'"; }
int launches(const QString &counter) {
    return QFileInfo::exists(counter) ? read(counter).trimmed().toInt() : 0;
}
// A fake `blender` on PATH. Launch number `hang` never returns; zero never hangs.
void blenderLauncher(const QString &path, const QString &binary, const QString &counter, int hang) {
    write(path, ("#!/bin/sh\nn=0\n[ -f " + quote(counter) + " ] && n=$(cat " + quote(counter) +
                 ")\nn=$((n+1))\necho $n > " + quote(counter) + "\nif [ \"$n\" = \"" +
                 QString::number(hang) + "\" ]; then exec " + quote(binary) +
                 " --fake hang \"$@\"; fi\nexec " + quote(binary) + " --fake success \"$@\"\n")
                    .toUtf8());
    check(QFile::setPermissions(path, QFileDevice::ReadOwner | QFileDevice::WriteOwner |
                                          QFileDevice::ExeOwner),
          "Executable fixture");
}
struct Outcome {
    int code{};
    QByteArray out, err;
};
Outcome runCli(const QStringList &args, const QProcessEnvironment &env, const QString &directory) {
    QProcess process;
    process.setProcessEnvironment(env);
    process.setWorkingDirectory(directory);
    process.start(QStringLiteral(CLI_PATH), args);
    check(process.waitForStarted(10000) && process.waitForFinished(180000), "CLI finishes");
    return {process.exitCode(), process.readAllStandardOutput(), process.readAllStandardError()};
}
} // namespace

int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    try {
        QTemporaryDir files;
        check(files.isValid(), "CLI fixture directory");
        const auto binary = app.applicationDirPath() + "/blender_job_tests";
        const auto bin = files.filePath("bin");
        check(QDir().mkpath(bin) && QDir().mkpath(files.filePath("empty")) &&
                  QDir().mkpath(files.filePath("export")),
              "CLI fixture folders");
        Document doc;
        doc.addFace({{{0, 0, 0}, {1, 0, 0}, {1, 1, 0}, {0, 1, 0}}});
        SceneSnapshot view;
        view.camera = SceneCamera{};
        const auto a = createScene(doc, "A", view);
        view.camera->yaw = 90;
        const auto b = createScene(doc, "B", view);
        view.camera->yaw = 180;
        const auto c = createScene(doc, "C", view);
        createCameraPath(doc, "Orbit", {a, b, c}, {24, 1, 1});
        RenderSettings draft;
        draft.width = 64;
        draft.height = 64;
        draft.samples = 1;
        createRenderPreset(doc, "Draft", draft);
        RenderSettings second;
        second.width = 80;
        second.height = 64;
        second.samples = 1;
        second.seed = 2;
        createRenderPreset(doc, "Check", second);
        const auto native = files.filePath("model.sketchyup");
        saveDocument(doc, native);
        const auto nativeBytes = read(native);

        const auto data = files.filePath("data");
        auto env = QProcessEnvironment::systemEnvironment();
        env.insert("XDG_DATA_HOME", data);
        env.insert("PATH", bin + ":/usr/bin:/bin");
        const auto counter = files.filePath("count-success");
        blenderLauncher(bin + "/blender", binary, counter, 0);
        const auto request = [&](const QString &name, const QByteArray &body) {
            write(files.filePath(name), body);
            return files.filePath(name);
        };

        // Success by camera path name, into an absolute new folder.
        const auto byPath =
            request("path.json",
                    R"({"apiVersion":1,"path":"Orbit","presets":["Draft","Check"],"output":")" +
                        files.filePath("export/orbit").toUtf8() + R"("})");
        auto done = runCli({"--input", native, "--export-presentation", byPath}, env, files.path());
        check(done.code == 0, "A batch from a camera path completes with exit 0");
        const auto manifest = QJsonDocument::fromJson(done.out).object();
        check(manifest.value("state") == "completed" && manifest.value("path") == "Orbit" &&
                  manifest.value("totalFrames") == 12 &&
                  manifest.value("jobId").toString().size() == 36,
              "The CLI prints the completed manifest on stdout");
        check(QFileInfo(files.filePath("export/orbit/manifest.json")).isFile() &&
                  QFileInfo(files.filePath("export/orbit/02-Check/frames/frame-0006.png")).isFile(),
              "The CLI writes the frame sequence and manifest");
        check(QDir(data + "/SketchyUp/SketchyUp/presentation-jobs").exists(),
              "The CLI uses the application's presentation job directory");
        check(read(native) == nativeBytes, "The CLI leaves the native model unchanged");

        // Success with an explicit keyframe list and timing, relative to the working folder.
        const auto explicitList = request(
            "explicit.json", R"({"apiVersion":1,"keyframes":["A","B"],"name":"Pair",)"
                             R"("timing":{"framesPerSecond":12,"transitionSteps":1,"holdSteps":0},)"
                             R"("presets":["Draft"],"output":"pair"})");
        done = runCli({"--input", native, "--export-presentation", explicitList}, env,
                      files.filePath("export"));
        check(done.code == 0 &&
                  QJsonDocument::fromJson(done.out).object().value("framesPerSecond") == 12 &&
                  QJsonDocument::fromJson(done.out).object().value("path") == "Pair",
              "An explicit keyframe list uses its own name and timing, and a relative folder");
        check(QFileInfo::exists(files.filePath("export/pair/manifest.json")),
              "A relative output folder is created in the working folder");

        // Interrupt: SIGINT stops at the next frame boundary, keeps the finished frame, exits 130.
        const auto interruptCounter = files.filePath("count-interrupt");
        blenderLauncher(bin + "/blender", binary, interruptCounter, 2);
        const auto interruptedOutput = files.filePath("export/interrupted");
        const auto interruptRequest = request(
            "interrupt.json", R"({"apiVersion":1,"path":"Orbit","presets":["Draft"],"output":")" +
                                  interruptedOutput.toUtf8() + R"("})");
        QProcess running;
        running.setProcessEnvironment(env);
        running.start(QStringLiteral(CLI_PATH),
                      {"--input", native, "--export-presentation", interruptRequest});
        check(running.waitForStarted(10000), "Interrupted CLI starts");
        QElapsedTimer timer;
        timer.start();
        while (!(launches(interruptCounter) == 2 &&
                 QFileInfo::exists(interruptedOutput + "/01-Draft/frames/frame-0001.png")) &&
               timer.elapsed() < 120000) {
            QCoreApplication::processEvents();
            QThread::msleep(5);
        }
        check(launches(interruptCounter) == 2, "The interrupted batch reaches its second frame");
        check(::kill(static_cast<pid_t>(running.processId()), SIGINT) == 0, "Send SIGINT");
        check(running.waitForFinished(120000), "Interrupted CLI finishes");
        check(running.exitCode() == 130, "A SIGINT cancel exits 130");
        const auto canceled = QJsonDocument::fromJson(running.readAllStandardOutput()).object();
        check(canceled.value("state") == "canceled" && canceled.value("completedFrames") == 1,
              "A SIGINT cancel prints the canceled manifest with the finished frame");
        check(QFileInfo::exists(interruptedOutput + "/01-Draft/frames/frame-0001.png") &&
                  !QFileInfo::exists(interruptedOutput + "/01-Draft/frames/frame-0002.png"),
              "A SIGINT cancel keeps finished frames and writes nothing past the boundary");
        check(read(native) == nativeBytes, "An interrupted run leaves the native model unchanged");

        // Refusals: exit 1, a structured reason on stderr, and nothing created.
        const auto refused = [&](const QString &name, const QByteArray &body,
                                 const QProcessEnvironment &refusedEnv, const char *message) {
            const auto path = request(name, body);
            const auto result = runCli({"--input", native, "--export-presentation", path},
                                       refusedEnv, files.path());
            check(result.code == 1 && result.err.contains("\"status\":\"failed\"") &&
                      result.out.isEmpty(),
                  message);
            return result;
        };
        refused("unknown-preset.json",
                R"({"apiVersion":1,"path":"Orbit","presets":["Missing"],"output":")" +
                    files.filePath("export/refused-1").toUtf8() + R"("})",
                env, "An unknown render preset is refused");
        check(!QFileInfo::exists(files.filePath("export/refused-1")),
              "A refused request makes no folder");
        refused("video.json",
                R"({"apiVersion":1,"path":"Orbit","presets":["Draft"],"video":true,"output":")" +
                    files.filePath("export/refused-2").toUtf8() + R"("})",
                env, "A video request is refused until R099.c");
        refused("extra.json",
                R"({"apiVersion":1,"path":"Orbit","presets":["Draft"],"preview":true,"output":")" +
                    files.filePath("export/refused-3").toUtf8() + R"("})",
                env, "An unknown request field is refused");
        const auto existing = files.filePath("export/existing");
        check(QDir().mkpath(existing), "Existing folder fixture");
        write(existing + "/keep.txt", "keep");
        refused("existing.json",
                R"({"apiVersion":1,"path":"Orbit","presets":["Draft"],"output":")" +
                    existing.toUtf8() + R"("})",
                env, "An existing destination is refused");
        check(read(existing + "/keep.txt") == "keep",
              "A refused existing folder is left as it was");
        auto noBlender = env;
        noBlender.insert("PATH", files.filePath("empty"));
        const auto blenderMissing =
            refused("blender.json",
                    R"({"apiVersion":1,"path":"Orbit","presets":["Draft"],"output":")" +
                        files.filePath("export/refused-4").toUtf8() + R"("})",
                    noBlender, "A missing Blender is refused");
        check(blenderMissing.err.contains("Blender"), "A missing Blender names the requirement");
        check(!QFileInfo::exists(files.filePath("export/refused-4")),
              "A missing Blender makes no folder");
        refused("oversized.json", QByteArray(17 * 1024, ' '), env,
                "A request over 16 KiB is refused");
        {
            const auto result = runCli({"--export-presentation", byPath}, env, files.path());
            check(result.code == 1, "A batch needs an explicit input model");
        }
        {
            const auto result =
                runCli({"--input", native, "--export-presentation", byPath, "--capabilities"}, env,
                       files.path());
            check(result.code == 1, "The batch is a standalone operation");
        }

        // Discovery: the request schema and the capability entry are printed without a model.
        {
            const auto result =
                runCli({"--describe-command", "export-presentation"}, env, files.path());
            const auto description = QJsonDocument::fromJson(result.out).object();
            check(result.code == 0 && description.value("name") == "export-presentation" &&
                      description.value("request").toObject().contains("presets"),
                  "The request schema is printed for --describe-command");
            const auto capabilities = runCli({"--capabilities"}, env, files.path());
            check(QJsonDocument::fromJson(capabilities.out)
                          .object()
                          .value("presentation")
                          .toObject()
                          .value("operation") == "export-presentation",
                  "The capabilities list the presentation operation");
        }
        std::cout << "Presentation CLI success, SIGINT cancel, refusals and discovery passed\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
