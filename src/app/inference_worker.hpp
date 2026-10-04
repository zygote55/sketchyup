#pragma once
#include "geometry/inference.hpp"
#include <atomic>
#include <mutex>
namespace sketchy {
// Snapshot preparation owns no widget and never publishes into the document.
// Latest-request wins; the UI only reads an index for its exact revision.
class InferenceWorker {
  public:
    InferenceWorker();
    ~InferenceWorker();
    void request(const Document &document);
    std::shared_ptr<const InferenceIndex> ready(const Document &document) const;
    std::string error(const Document &document) const;

  private:
    struct State {
        std::mutex mutex;
        std::optional<Document> pending;
        std::shared_ptr<const InferenceIndex> index;
        std::string requestedIdentity, readyIdentity, failedIdentity, failure;
        std::uint64_t requestedRevision{}, readyRevision{}, failedRevision{};
        Document::SaveStamp requestedSnapshot, readySnapshot, failedSnapshot;
        bool running{};
        std::atomic<bool> canceled{false};
    };
    std::shared_ptr<State> state_;
};
} // namespace sketchy
