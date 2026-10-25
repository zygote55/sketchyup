#include "app/step_import_runner.hpp"
#include "io/step_worker.hpp"
namespace sketchy {
void StepImportRunner::begin(const QString &path, StepWorkerOptions worker) {
    ExchangeRunner::begin(
        [path, worker](const ExchangeWorkerHooks &hooks) {
            StepWorkerOptions options = worker;
            options.cancelled = hooks.cancelled;
            options.progress = hooks.progress;
            const auto imported = importStepFile(path, options);
            ExchangeResult result;
            result.done = true;
            result.report = imported.report;
            result.document = std::move(imported.document);
            return result;
        },
        "The STEP import failed.");
}
} // namespace sketchy
