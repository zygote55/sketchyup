#include "app/step_export_runner.hpp"
#include "io/step_export.hpp"
namespace sketchy {
void StepExportRunner::begin(const Document &document, const QString &path, StepExportOptions options,
                             IfcWorkerOptions worker) {
    // The job captures the document by value: the export runs on the worker thread over this
    // copy, so nothing the UI does later can change what is being written.
    ExchangeRunner::begin(
        [document, path, options, worker](const ExchangeWorkerHooks &hooks) {
            IfcWorkerOptions w = worker;
            w.cancelled = hooks.cancelled;
            w.progress = hooks.progress;
            ExchangeResult result;
            // The export's refusals are StepExportError, which the shared runner does not know, so
            // they are read here: the code, the words and the report so far are kept as the
            // runner keeps the IFC's, and a cancel is the "cancelled" result.
            try {
                const auto exported = exportStepFile(document, path, options, w);
                result.done = true;
                result.bytes = exported.bytes;
                result.report = exported.report;
            } catch (const StepExportError &error) {
                result.code = error.code();
                result.message = QString::fromUtf8(error.what());
                result.report = error.report();
                result.cancelled = result.code == "cancelled";
            }
            return result;
        },
        "The STEP export failed.");
}
} // namespace sketchy
