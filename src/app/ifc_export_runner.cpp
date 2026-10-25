#include "app/ifc_export_runner.hpp"
#include "io/ifc_export.hpp"
namespace sketchy {
void IfcExportRunner::begin(const Document &document, const QString &path, IfcExportOptions options,
                            IfcWorkerOptions worker) {
    // The job captures the document by value: the export runs on the worker thread over this
    // copy, so nothing the UI does later can change what is being written.
    ExchangeRunner::begin(
        [document, path, options, worker](const ExchangeWorkerHooks &hooks) {
            IfcWorkerOptions w = worker;
            w.cancelled = hooks.cancelled;
            w.progress = hooks.progress;
            const auto exported = exportIfcFile(document, path, options, w);
            ExchangeResult result;
            result.done = true;
            result.bytes = exported.bytes;
            result.report = exported.report;
            return result;
        },
        "The IFC export failed.");
}
} // namespace sketchy
