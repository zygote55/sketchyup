#pragma once
// R097.b: running sketchyup-ifc-worker (ADR 0171 section 1.6). The application checks the cheap things in
// its own bounded code, copies the file into a private directory and hashes the copy, runs the helper
// with a fixed argv, no shell, a scrubbed environment, resource limits and a deadline, restarts it with
// the element it stopped on left out, and decodes what it wrote with the checked package decoder.
#include "io/ifc_import.hpp"
#include <QString>
#include <functional>
namespace sketchy {
struct IfcWorkerOptions {
    // Empty: <application directory>/../lib/sketchyup/sketchyup-ifc-worker, then next to the application.
    QString workerPath;
    // The maximum chord deviation of curved geometry, in millimetres (default 1).
    double chordDeviationMm{1};
    // mm, cm, m, in or ft: used only when the file declares no length unit.
    QString lengthUnit;
    int threads{4};
    int deadlineSeconds{120};
    // RLIMIT_AS of the helper; 0 for none (sanitizer builds map huge shadow memory).
    qint64 addressSpaceBytes{qint64(4) << 30};
    int maxRestarts{3};
    int maxVoidsPerElement{100};
    qint64 maxEntities{2000000};
    // Tests only: "crash:N", "abort:N", "hang:N" or "alloc:N" makes the helper fail on product #N.
    QString testFault;
    // Where the private directory is made (empty: the system temporary directory).
    QString scratchRoot;
    // Checked while the helper runs; true cancels (terminate, then kill after a second).
    std::function<bool()> cancelled;
    // Phases and counts from the helper: ("geometry", tasks), ("done", n) ...
    std::function<void(const QString &phase, qint64 value)> progress;
};
// The cheap checks of the application itself (size, regular file, END-ISO-10303-21, one accepted
// FILE_SCHEMA, no zip or XML); throws IfcImportError naming what is wrong. Returns the schema.
QString checkIfcSource(const QString &path);
// Runs the whole import of one file into a new unsaved document. Throws IfcImportError (with the
// report so far when there is one) for a refusal, a failure after the restarts, or a cancel.
IfcImport importIfcFile(const QString &path, const IfcImportOptions &options, const IfcWorkerOptions &worker);
// R098.b (ADR 0172): the same for a STEP file: the helper's --import-step (OCCT only), the same
// conversion (the tag family STEP, the label Import STEP). The options' lengthUnit stands only when
// the file declares no length unit (a unit is never guessed).
IfcImport importStepFile(const QString &path, const IfcImportOptions &options, const IfcWorkerOptions &worker);
// Where the helper is expected, or empty when it is missing (for the "unavailable" message).
QString findIfcWorker(const QString &explicitPath = {});
} // namespace sketchy
