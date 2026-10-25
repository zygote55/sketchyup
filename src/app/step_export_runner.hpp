#pragma once
// R098.c2 (ADR 0172 section 8): the STEP export's runner — a thin wrapper around the shared
// ExchangeRunner, as IfcExportRunner is: the whole export (the capture, the helper, the checks,
// the publication) runs on one worker thread over a copy of the document, the phases come back
// as queued signals, Cancel is checked between phases, and the result is read back after the
// thread has finished. The open document is read, never changed: the export asks no
// unsaved-changes question.
#include "app/exchange_runner.hpp"
#include "io/step_export.hpp"
namespace sketchy {
class StepExportRunner : public ExchangeRunner {
  public:
    // A copy of the document, not the window's, so nothing the UI does later can change what is
    // written. The caller keeps the runner until wait() has returned (the window deletes it after).
    void begin(const Document &document, const QString &path, StepExportOptions options, IfcWorkerOptions worker);
};
} // namespace sketchy
