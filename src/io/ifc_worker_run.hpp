#pragma once
// Running sketchyup-ifc-worker once (ADR 0171 section 1.6, ADR 0172 section 8): the IFC export (R097.d) and the STEP
// export (R098.c) share this runner. A fixed argv, a scrubbed environment, the helper's own session, a deadline, a
// cancel check, and the "R " result line decoded into the run. Nothing here is shell-expanded.
#include "io/ifc_worker.hpp"

#include <QJsonObject>
#include <QString>
#include <QStringList>

namespace sketchy {

struct IfcWorkerRun {
    enum Kind { Finished, Refused, Stopped, Cancelled } kind{Stopped};
    QString reason, logTail;
    QJsonObject result;
    int exitCode{-1};
};

// Runs the helper once with a fixed argv and a scrubbed environment, in its own session, with a deadline; the parent ends
// it (terminate, then kill after a second) on cancel, on the deadline or when it floods its output.
IfcWorkerRun runIfcWorker(const QString &worker, const QString &directory, const QStringList &args, const IfcWorkerOptions &options);

} // namespace sketchy
