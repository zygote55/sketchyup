#pragma once
// R097.e2: the IFC export's runner (ADR 0171 section 16.8) — a thin wrapper around the shared
// ExchangeRunner: the whole export (the capture, the helper, the checks, the publication) runs
// on one worker thread, the phases come back as queued signals, Cancel is checked between
// phases, and the result is read back after the thread has finished. The open document is
// read, never changed: the export does not replace the model, so it asks no dirty-document
// question.
#include "app/exchange_runner.hpp"
#include "io/ifc_export.hpp"
namespace sketchy {
class IfcExportRunner : public ExchangeRunner {
  public:
    // A copy of the document, not the window's: the export reads it on the worker thread. A
    // document is a value over shared immutable bodies, so the copy is cheap and nothing the
    // UI does later can change what is being written. The caller must keep the document
    // alive until the thread has finished (the window deletes the runner after wait()).
    void begin(const Document &document, const QString &path, IfcExportOptions options,
               IfcWorkerOptions worker);
};
} // namespace sketchy
