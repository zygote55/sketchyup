#include "app/ifc_import_runner.hpp"
#include "io/ifc_import.hpp"
namespace sketchy {
void IfcImportRunner::begin(const QString &path, IfcImportOptions import, IfcWorkerOptions worker) {
    ExchangeRunner::begin(
        [path, import, worker](const ExchangeWorkerHooks &hooks) {
            IfcWorkerOptions options = worker;
            options.cancelled = hooks.cancelled;
            options.progress = hooks.progress;
            const auto imported = importIfcFile(path, import, options);
            ExchangeResult result;
            result.done = true;
            result.report = imported.report;
            result.document = std::move(imported.document);
            return result;
        },
        "The IFC import failed.");
}
} // namespace sketchy
