#include "app/ifc_availability.hpp"
#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QJsonDocument>
#include <QProcess>
#include <algorithm>
#include <string>
namespace sketchy {
namespace {
constexpr int probeDeadlineMs = 15000;
constexpr int maxProbeBytes = 64 * 1024;
QString tailOf(const QByteArray &bytes, int maxBytes = 4096) {
    auto text = QString::fromUtf8(bytes).trimmed();
    if (text.size() > maxBytes)
        text = text.right(maxBytes);
    return text;
}
} // namespace
QString resolveIfcWorker(const QString &override) {
    const QString appDir = QCoreApplication::applicationDirPath();
    const QStringList candidates{
        appDir + "/../lib/sketchyup/sketchyup-ifc-worker",
        appDir + "/sketchyup-ifc-worker",
        override,
    };
    for (const auto &candidate : candidates) {
        if (candidate.isEmpty())
            continue;
        const QFileInfo info(candidate);
        if (info.isExecutable())
            return info.canonicalFilePath();
    }
    return {};
}
IfcProbe runIfcProbe(const QString &workerPath) {
    IfcProbe probe;
    if (workerPath.isEmpty())
        return probe; // Missing.
    probe.workerPath = workerPath;
    QProcess process;
    process.setProgram(workerPath);
    process.setArguments({"--probe"});
    process.setProcessChannelMode(QProcess::SeparateChannels);
    // A scrubbed environment and a short deadline: the probe is data, and a hung helper is a failure.
    QProcessEnvironment environment;
    environment.insert("LC_ALL", "C.UTF-8");
    process.setProcessEnvironment(environment);
    process.start();
    if (!process.waitForStarted(probeDeadlineMs)) {
        probe.state = IfcState::Broken;
        probe.logTail = tailOf(process.errorString().toUtf8());
        return probe;
    }
    if (!process.waitForFinished(probeDeadlineMs)) {
        process.kill();
        process.waitForFinished(probeDeadlineMs);
        probe.state = IfcState::Broken;
        probe.exitCode = -1;
        probe.logTail = tailOf(process.readAllStandardError());
        return probe;
    }
    probe.exitCode = process.exitCode();
    if (process.exitStatus() != QProcess::NormalExit || process.exitCode() != 0) {
        probe.state = IfcState::Broken;
        probe.logTail = tailOf(process.readAllStandardError());
        return probe;
    }
    auto output = process.readAllStandardOutput();
    if (output.size() > maxProbeBytes)
        output = output.left(maxProbeBytes);
    const QJsonObject json = QJsonDocument::fromJson(output).object();
    if (json.isEmpty()) {
        // A probe that prints nothing usable is broken, even on exit 0.
        probe.state = IfcState::Broken;
        probe.logTail = "The helper's probe produced no readable output.";
        return probe;
    }
    probe.workerVersion = json["worker"].toObject()["version"].toString();
    probe.ifcOpenShell = json["ifcopenshell"].toObject()["version"].toString();
    probe.kernelAvailable = json["kernelAvailable"].toBool(false);
    const auto kernels = json["kernels"].toArray();
    for (const auto &value : kernels) {
        const QJsonObject kernel = value.toObject();
        if (kernel["name"].toString() == "opencascade" && !kernel["available"].toBool(false)) {
            probe.kernelReason = kernel["reason"].toString();
            break;
        }
    }
    probe.state = probe.kernelAvailable ? IfcState::Available : IfcState::Limited;
    return probe;
}
} // namespace sketchy
