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

// The helper's RLIMIT_AS (ADR 0176 section 1.6). The sanitizer and fuzzer builds have none: ASan maps
// more address space than the 4 GiB ceiling, so the helper and this default agree on zero there.
#ifdef SKP_WORKER_NO_ADDRESS_LIMIT
inline constexpr qint64 kSkpAddressSpaceBytes = 0;
#else
inline constexpr qint64 kSkpAddressSpaceBytes = qint64(4) << 30;
#endif

struct SkpWorkerOptions {
    // Empty: <application directory>/../lib/sketchyup/sketchyup-skp-worker, then next to the application.
    QString workerPath;
    int deadlineSeconds{120};
    // RLIMIT_AS of the helper; 0 for none.
    qint64 addressSpaceBytes{kSkpAddressSpaceBytes};
    // Where the private directory is made (empty: the system temporary directory).
    QString scratchRoot;
    // Checked while the helper runs; true cancels (terminate, then kill after a second).
    std::function<bool()> cancelled;
    // Phases: the helper's ("read", 0), ("parse", 0), ("package", 0), then the application's own
    // ("build", 0) before the package is decoded and ("create", 0) before the document is made.
    std::function<void(const QString &phase, qint64 value)> progress;
};

// The helper, where it is expected, or empty when it is missing (for the "workerMissing" message).
QString findSkpWorker(const QString &explicitPath = {});

// Runs the whole import of one file into a new unsaved document. Throws IfcImportError (carrying the
// report so far when there is one) for a refusal, a stop, a bad package, or a cancel.
SkpImport importSkpFile(const QString &path, const SkpWorkerOptions &worker);

} // namespace sketchy
