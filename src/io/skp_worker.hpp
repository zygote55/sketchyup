#pragma once
// R102.b (ADR 0176 sections 1.6 and 9): running sketchyup-skp-worker --import. The application checks
// the cheap things in its own bounded code (a regular file, at most 256 MiB, the SketchUp header),
// copies the file into a private directory and hashes the copy, runs the helper with a fixed argv,
// no shell, a scrubbed environment, resource limits and a deadline, and decodes what it wrote with
// the checked package decoder. The helper has no per-element restart protocol: a stop is a failure
// (reported with what the helper left behind), a refusal is the helper's words, and the conversion
// is the application's (importSkpPackage). The parser never runs in the application.
#include "io/skp_import.hpp"

#include <QString>
#include <functional>

namespace sketchy {

struct SkpWorkerOptions {
    // Empty: <application directory>/../lib/sketchyup/sketchyup-skp-worker, then next to the application.
    QString workerPath;
    int deadlineSeconds{120};
    // RLIMIT_AS of the helper; 0 for none (sanitizer builds map huge shadow memory).
    qint64 addressSpaceBytes{qint64(4) << 30};
    // Where the private directory is made (empty: the system temporary directory).
    QString scratchRoot;
    // Checked while the helper runs; true cancels (terminate, then kill after a second).
    std::function<bool()> cancelled;
    // Phases from the helper: ("parse", 0), ("package", 0), ("tasks", n), ("done", n) ...
    std::function<void(const QString &phase, qint64 value)> progress;
};

// The helper, where it is expected, or empty when it is missing (for the "workerMissing" message).
QString findSkpWorker(const QString &explicitPath = {});

// Runs the whole import of one file into a new unsaved document. Throws IfcImportError (carrying the
// report so far when there is one) for a refusal, a stop, a bad package, or a cancel.
SkpImport importSkpFile(const QString &path, const SkpWorkerOptions &worker);

} // namespace sketchy
