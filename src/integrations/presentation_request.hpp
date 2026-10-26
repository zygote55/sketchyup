#pragma once
#include "automation/presentation_export.hpp"
#include "integrations/presentation_batch.hpp"
#include <QJsonObject>
#include <functional>
namespace sketchy {
// Resolves a request's names against the document: a camera path (or an explicit scene list),
// and render presets by name. The batch engine checks the plan's limits when it captures.
PresentationPlan resolvePresentationRequest(const Document &doc,
                                            const PresentationRequest &request);
struct PresentationRun {
    PresentationJobState state{};
    QJsonObject manifest; // The batch manifest as it was published in the output folder.
};
// Runs a request to a terminal state on this thread. `interrupted` is polled while the batch
// runs; when it turns true the batch cancels at the next frame boundary. Refusals throw before
// any folder or job is created.
PresentationRun runPresentationRequest(const Document &doc, const PresentationRequest &request,
                                       PresentationJobStore &store,
                                       const std::function<bool()> &interrupted,
                                       BlenderJob::Options options = {});
} // namespace sketchy
