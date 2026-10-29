#pragma once
// R098.a3 (ADR 0172 section 8): running the STEP import through sketchyup-ifc-worker
// (--import-step). The application checks the cheap things in its own bounded code, copies the file
// into a private directory and hashes the copy, runs the helper with a fixed argv, no shell, a
// scrubbed environment, resource limits and a deadline, and decodes what it wrote with the checked
// package decoder. R098.i (ADR 0172 section 5, owner decision 7): a stop in the tessellation of a
// base (its `P at` line) that is over a limit, the deadline or a signal, is restarted with that base
// left out, at most maxRestarts times, and the import is partial. A stop in the read or write phase
// has no base to leave out and fails the whole file. A refusal is the helper's words, and the
// conversion is the application's, as in the IFC import.
#include "io/ifc_import.hpp"
#include <QString>
#include <functional>
namespace sketchy {
struct StepWorkerOptions {
    // Empty: <application directory>/../lib/sketchyup/sketchyup-ifc-worker, then next to the application
    // (the STEP import runs in the IFC helper, ADR 0172 section 8: one component, one helper).
    QString workerPath;
    // The tessellation policy of ADR 0172 section 3: the maximum chord deviation of curved
    // geometry in millimetres (default 1) and the crease angle in degrees (default 30).
    double deflectionMm{1.0};
    double angleDeg{30.0};
    // mm, cm, m, in or ft: used only when the file declares no length unit (ADR 0172 section 2);
    // a file's declared unit is authoritative.
    QString lengthUnitOverride;
    // The triangle caps of ADR 0172 section 6 (100,000 per element, 1,000,000 in total).
    qint64 maxTrianglesPerElement{100000};
    qint64 maxTotalTriangles{1000000};
    int deadlineSeconds{120};
    // RLIMIT_AS of the helper; 0 for none (sanitizer builds map huge shadow memory).
    qint64 addressSpaceBytes{qint64(4) << 30};
    // The restarts a stop in the tessellation may cost, each with its base left out (ADR 0172 section 5).
    int maxRestarts{3};
    // Where the private directory is made (empty: the system temporary directory).
    QString scratchRoot;
    // Checked while the helper runs; true cancels (terminate, then kill after a second).
    std::function<bool()> cancelled;
    // Phases and counts from the helper: ("read", 0), ("tessellate", 0), ("tasks", n), ("at", i), ("done", n) ...
    std::function<void(const QString &phase, qint64 value)> progress;
};
// The cheap checks of the application itself (size, regular file, the ISO-10303-21 header, one
// accepted AP schema, no zip or XML) and the file's declared length unit (the raw text's own,
// the helper's own scan's display: the helper stays the authority, the factor is the helper's).
// Throws IfcImportError naming what is wrong.
struct StepScan {
    QString schema; // ap203, ap214 or ap242, as the helper classifies it
    bool unitDeclared = false; // the raw text's own fact (a LENGTH_UNIT entity)
    QString unit; // the declared unit's name (empty when none is declared)
};
StepScan checkStepSource(const QString &path);
// Runs the whole import of one file into a new unsaved document. A restarted stop gives a partial
// import that names the bases left out (its report's excluded list and the restarted notice). Throws
// IfcImportError (with the report so far when there is one) for a refusal, a stop that is not
// restarted or that used up the restarts, a sandbox violation, or a cancel.
IfcImport importStepFile(const QString &path, const StepWorkerOptions &worker);
} // namespace sketchy
