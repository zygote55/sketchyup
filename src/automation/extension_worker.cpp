#include "automation/extension_worker.hpp"
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QJsonDocument>
#include <QProcess>
#include <QProcessEnvironment>
#include <QThread>
namespace sketchy {
namespace {
void require(bool value, const char *message) {
    if (!value)
        throw std::runtime_error(message);
}
void fields(const QJsonObject &object, const QStringList &names) {
    require(object.size() == names.size(), "Extension worker has missing or extra fields");
    for (const auto &name : names)
        require(object.contains(name), "Extension worker field missing");
}
QString hash(const QByteArray &bytes) {
    return QString::fromLatin1(QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex());
}
QJsonObject object(const QByteArray &bytes) {
    QJsonParseError error;
    const auto json = QJsonDocument::fromJson(bytes, &error);
    require(error.error == QJsonParseError::NoError && json.isObject(),
            "Extension worker requires one JSON object");
    return json.object();
}
QString executable() {
    const auto root = QCoreApplication::applicationDirPath();
    for (const auto &path : {root + "/sketchyup-extension-worker",
                             root + "/../lib/sketchyup/sketchyup-extension-worker"}) {
        const QFileInfo info(path);
        if (info.isFile() && info.isExecutable())
            return info.canonicalFilePath();
    }
    throw std::runtime_error("Extension worker is missing from this installation");
}
} // namespace
QByteArray resolveExtensionWorkerRequest(const QByteArray &bytes) {
    require(bytes.size() <= extensionWorkerRequestLimit,
            "Extension worker request exceeds 384 KiB");
    const auto request = object(bytes);
    fields(request, {"protocol", "manifest", "action", "parameters"});
    require(request["protocol"] == 1 && request["manifest"].isString() &&
                request["action"].isString() && request["action"].toString().size() <= 64 &&
                request["parameters"].isObject(),
            "Invalid extension worker request");
    const auto encoded = request["manifest"].toString().toLatin1();
    const auto source = QByteArray::fromBase64(encoded, QByteArray::AbortOnBase64DecodingErrors);
    require(!source.isEmpty() && source.size() <= extensionManifestLimit &&
                source.toBase64() == encoded,
            "Invalid extension worker manifest encoding");
    const auto parameters = request["parameters"].toObject();
    require(QJsonDocument(parameters).toJson(QJsonDocument::Compact).size() <= 16384,
            "Extension worker parameters exceed 16 KiB");
    const auto manifest = parseExtensionManifest(source);
    const auto commands =
        resolveExtensionAction(manifest, request["action"].toString(), parameters);
    const auto output = QJsonDocument(QJsonObject{{"protocol", 1},
                                                  {"ok", true},
                                                  {"manifestSha256", hash(source)},
                                                  {"action", request["action"]},
                                                  {"commands", commands}})
                            .toJson(QJsonDocument::Compact);
    require(output.size() <= extensionWorkerResponseLimit,
            "Extension worker response exceeds 96 KiB");
    return output;
}
QJsonArray runExtensionWorker(const ExtensionManifest &manifest, const QString &action,
                              const QJsonObject &parameters,
                              const ExtensionWorkerOptions &options) {
    require(options.timeoutMs >= 10 && options.timeoutMs <= 30000,
            "Invalid extension worker timeout");
    require(action.size() <= 64 &&
                QJsonDocument(parameters).toJson(QJsonDocument::Compact).size() <= 16384,
            "Extension action or parameters exceed limits");
    const auto verified = parseExtensionManifest(manifest.source);
    const auto input =
        QJsonDocument(QJsonObject{{"protocol", 1},
                                  {"manifest", QString::fromLatin1(verified.source.toBase64())},
                                  {"action", action},
                                  {"parameters", parameters}})
            .toJson(QJsonDocument::Compact);
    require(input.size() <= extensionWorkerRequestLimit,
            "Extension worker request exceeds 384 KiB");
    require(!QThread::currentThread()->isInterruptionRequested(), "Extension action canceled");
    QProcess process;
    process.setProgram(options.executable.isEmpty() ? executable() : options.executable);
    process.setProcessChannelMode(QProcess::SeparateChannels);
    QProcessEnvironment environment;
    environment.insert("PATH", "/usr/bin:/bin");
    environment.insert("LANG", "C.UTF-8");
    for (const auto *name : {"ASAN_OPTIONS", "UBSAN_OPTIONS"})
        if (qEnvironmentVariableIsSet(name))
            environment.insert(name, qEnvironmentVariable(name));
    process.setProcessEnvironment(environment);
    QElapsedTimer timer;
    timer.start();
    auto stop = [&] {
        if (process.state() != QProcess::NotRunning) {
            process.kill();
            process.waitForFinished(1000);
        }
    };
    try {
        process.start();
        require(process.waitForStarted(std::min(options.timeoutMs, 1000)),
                "Extension worker failed to start");
        require(process.write(input) == input.size(), "Extension worker input write failed");
        process.closeWriteChannel();
        QByteArray output;
        qsizetype errors{};
        while (true) {
            require(!QThread::currentThread()->isInterruptionRequested(),
                    "Extension action canceled");
            process.waitForReadyRead(10);
            process.setReadChannel(QProcess::StandardOutput);
            output += process.read(extensionWorkerResponseLimit - output.size() + 1);
            process.setReadChannel(QProcess::StandardError);
            errors += process.read(4096 - errors + 1).size();
            process.setReadChannel(QProcess::StandardOutput);
            require(output.size() <= extensionWorkerResponseLimit && errors <= 4096,
                    "Extension worker output exceeds limits");
            require(timer.elapsed() <= options.timeoutMs, "Extension worker timed out");
            if (process.state() == QProcess::NotRunning)
                break;
        }
        require(process.exitStatus() == QProcess::NormalExit, "Extension worker crashed");
        const auto reply = object(output);
        require(reply["protocol"] == 1 && reply["ok"].isBool(),
                "Unsupported extension worker reply");
        if (!reply["ok"].toBool()) {
            fields(reply, {"protocol", "ok", "error"});
            require(reply["error"].isString() && reply["error"].toString().size() <= 256,
                    "Invalid extension worker error");
            throw std::runtime_error(reply["error"].toString().toStdString());
        }
        fields(reply, {"protocol", "ok", "manifestSha256", "action", "commands"});
        require(process.exitCode() == 0 && reply["manifestSha256"] == hash(verified.source) &&
                    reply["action"] == action && reply["commands"].isArray(),
                "Extension worker response identity mismatch");
        const auto commands = reply["commands"].toArray();
        require(commands == resolveExtensionAction(verified, action, parameters),
                "Extension worker response differs from the declared action");
        return commands;
    } catch (...) {
        stop();
        throw;
    }
}
} // namespace sketchy
