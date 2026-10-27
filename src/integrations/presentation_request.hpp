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
// runs; when it turns true the batch cancels at the next frame boundary. `stopped` is polled the
// same way; when it turns true the batch stops now and is recorded interrupted (a SIGTERM).
// Refusals throw before any folder or job is created.
PresentationRun runPresentationRequest(const Document &doc, const PresentationRequest &request,
                                       PresentationJobStore &store,
                                       const std::function<bool()> &interrupted,
                                       const std::function<bool()> &stopped,
                                       BlenderJob::Options options = {});
// Installs the Blender video check behind presentation.describe for the desktop. It never blocks:
// it reports the cached check, or {checked: false, pending: true} while a background probe fills
// the cache. `checks` must outlive the process's describe queries.
void installPresentationVideoProbe(PresentationVideoChecks &checks);
// The CLI's form: it checks synchronously, since no interactive event loop can freeze. `checks`
// must outlive the process's describe queries. Without an installed probe the video is reported as
// not checked.
void installBlockingPresentationVideoProbe(PresentationVideoChecks &checks);
} // namespace sketchy
