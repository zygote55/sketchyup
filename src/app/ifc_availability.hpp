#pragma once
// R097.e1: the availability of the IFC helper (ADR 0171 section 16.7). The application resolves the
// helper (its own directory, then an explicit override for tests), runs its --probe once when the
// import is opened, and reports whether import is full, limited (no kernel) or unavailable. The
// probe is data, never trusted.
#include <QJsonObject>
#include <QString>
namespace sketchy {
enum class IfcState { Missing, Broken, Limited, Available };
// The state and the facts a sheet or the dialog needs.
struct IfcProbe {
    IfcState state = IfcState::Missing;
    QString workerPath; // empty when missing
    QString workerVersion; // the worker's own version (from the probe)
    QString ifcOpenShell; // the IfcOpenShell version (from the probe)
    bool kernelAvailable = false; // the OpenCASCADE kernel the conversion needs
    QString kernelReason; // why the kernel is unavailable, when it is
    int exitCode = -1; // the probe's exit code, when it ran
    QString logTail; // the tail of the probe's stderr, when it could not be run
};
// <application directory>/../lib/sketchyup/sketchyup-ifc-worker, then next to the application, then
// the explicit override (tests, and a future "where the helper is").
QString resolveIfcWorker(const QString &override = {});
// Runs "worker --probe" with a short deadline and parses the result. Never throws.
IfcProbe runIfcProbe(const QString &workerPath);
} // namespace sketchy
