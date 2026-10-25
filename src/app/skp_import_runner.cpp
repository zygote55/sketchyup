#include "app/skp_import_runner.hpp"

#include "io/skp_worker.hpp"

namespace sketchy {

void SkpImportRunner::begin(const QString &path, SkpWorkerOptions worker) {
    ExchangeRunner::begin(
        [path, worker](const ExchangeWorkerHooks &hooks) {
            SkpWorkerOptions options = worker;
            options.cancelled = hooks.cancelled;
            options.progress = hooks.progress;
            auto imported = importSkpFile(path, options);
            ExchangeResult result;
            result.done = true;
            result.report = imported.report;
            result.document = std::move(imported.document);
            return result;
        },
        "The SketchUp import failed.");
}

} // namespace sketchy
