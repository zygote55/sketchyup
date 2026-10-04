#include "app/inference_worker.hpp"
#include <QRunnable>
#include <QThreadPool>
namespace sketchy {
InferenceWorker::InferenceWorker() : state_(std::make_shared<State>()) {}
InferenceWorker::~InferenceWorker() {
    state_->canceled = true;
    std::lock_guard lock(state_->mutex);
    state_->pending.reset();
}
void InferenceWorker::request(const Document &document) {
    const auto state = state_;
    std::lock_guard lock(state->mutex);
    if (state->requestedIdentity == document.identity() &&
        state->requestedRevision == document.revision() &&
        document.isCurrentSnapshot(state->requestedSnapshot))
        return;
    state->requestedSnapshot = document.saveStamp();
    state->pending = document;
    state->requestedIdentity = document.identity();
    state->requestedRevision = document.revision();
    if (state->running)
        return;
    state->running = true;
    QThreadPool::globalInstance()->start(QRunnable::create([state] {
        for (;;) {
            std::optional<Document> document;
            std::shared_ptr<const InferenceIndex> previous;
            {
                std::lock_guard lock(state->mutex);
                if (state->canceled || !state->pending) {
                    state->running = false;
                    return;
                }
                document = std::move(state->pending);
                state->pending.reset();
                previous = state->index;
            }
            std::shared_ptr<InferenceIndex> built;
            std::string error;
            try {
                built = previous ? std::make_shared<InferenceIndex>(*previous)
                                 : std::make_shared<InferenceIndex>();
                built->sync(*document, [&] { return state->canceled.load(); });
            } catch (const std::exception &e) {
                error = e.what();
                built.reset();
            }
            {
                std::lock_guard lock(state->mutex);
                if (state->canceled) {
                    state->running = false;
                    return;
                }
                if (built) {
                    state->index = std::move(built);
                    state->readyIdentity = document->identity();
                    state->readyRevision = document->revision();
                    state->readySnapshot = document->saveStamp();
                    state->failure.clear();
                    state->failedIdentity.clear();
                } else {
                    state->failure = std::move(error);
                    state->failedIdentity = document->identity();
                    state->failedRevision = document->revision();
                    state->failedSnapshot = document->saveStamp();
                }
            }
        }
    }));
}
std::shared_ptr<const InferenceIndex> InferenceWorker::ready(const Document &document) const {
    std::lock_guard lock(state_->mutex);
    if (state_->readyIdentity == document.identity() &&
        state_->readyRevision == document.revision() &&
        document.isCurrentSnapshot(state_->readySnapshot))
        return state_->index;
    return {};
}
std::string InferenceWorker::error(const Document &document) const {
    std::lock_guard lock(state_->mutex);
    if (state_->failedIdentity == document.identity() &&
        state_->failedRevision == document.revision() &&
        document.isCurrentSnapshot(state_->failedSnapshot))
        return state_->failure;
    return {};
}
} // namespace sketchy
