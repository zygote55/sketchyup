#include "app/texture_cache.hpp"
#include <QRunnable>
#include <QThreadPool>
#include <algorithm>

namespace sketchy {
struct TextureCache::State {
    std::mutex mutex;
    AssetRecords requested;
    std::shared_ptr<const Snapshot> published{std::make_shared<Snapshot>()};
    Limits limits;
    Decoder decoder;
    std::uint64_t generation{};
    bool running{}, closed{};
};
TextureCache::TextureCache() : TextureCache(Limits{}) {}
TextureCache::TextureCache(Limits limits, Decoder decoder) : state_(std::make_shared<State>()) {
    state_->limits = limits;
    state_->decoder = std::move(decoder);
}
TextureCache::~TextureCache() {
    std::lock_guard lock(state_->mutex);
    state_->closed = true;
    state_->requested.clear();
    state_->published.reset();
}
bool TextureCache::sameImage(const AssetPtr &a, const AssetPtr &b) {
    return a && b && a->id == b->id && a->payload == b->payload && a->mediaType == b->mediaType;
}
const TextureCache::Entry *TextureCache::find(const Snapshot &snapshot, const AssetPtr &asset) {
    if (!asset)
        return nullptr;
    const auto found = snapshot.entries.find(asset->id);
    return found != snapshot.entries.end() && sameImage(found->second.asset, asset) ? &found->second
                                                                                    : nullptr;
}
bool TextureCache::request(const AssetRecords &assets) {
    const auto state = state_;
    std::lock_guard lock(state->mutex);
    if (assets.size() == state->requested.size() &&
        std::equal(assets.begin(), assets.end(), state->requested.begin(), [](auto &a, auto &b) {
            return a.first == b.first && sameImage(a.second, b.second);
        }))
        return false;
    state->requested = assets;
    ++state->generation;
    if (state->running)
        return true;
    state->running = true;
    QThreadPool::globalInstance()->start(QRunnable::create([state] {
        for (;;) {
            AssetRecords requested;
            std::shared_ptr<const Snapshot> previous;
            auto next = std::make_shared<Snapshot>();
            {
                std::lock_guard lock(state->mutex);
                if (state->closed) {
                    state->running = false;
                    return;
                }
                requested = state->requested;
                previous = state->published;
                next->generation = state->generation;
            }
            size_t images = 0;
            for (const auto &[id, asset] : requested) {
                {
                    std::lock_guard lock(state->mutex);
                    if (state->closed || next->generation != state->generation)
                        break;
                }
                Entry entry{asset, {}, "budget"};
                // Admission is deterministic in asset-ID order. No repeated eviction
                // and decoding loop when the visible scene exceeds the budget.
                if (images < state->limits.images && next->bytes < state->limits.bytes) {
                    const auto *old = find(*previous, asset);
                    if (old && old->status != "budget")
                        entry = *old;
                    else {
                        try {
                            auto decoded = state->decoder(*asset);
                            entry.image = std::move(decoded.image);
                            entry.status = textureImageStatusName(decoded.status);
                        } catch (const std::exception &) {
                            entry.status = "invalid";
                        }
                    }
                    if (entry.image) {
                        const auto bytes = entry.image->rgba().size();
                        if (bytes > state->limits.bytes - next->bytes) {
                            entry.image.reset();
                            entry.status = "budget";
                        } else {
                            next->bytes += bytes;
                            ++images;
                        }
                    }
                }
                next->entries.emplace(id, std::move(entry));
            }
            {
                std::lock_guard lock(state->mutex);
                if (state->closed) {
                    state->running = false;
                    return;
                }
                if (next->generation != state->generation)
                    continue;
                state->published = std::move(next);
                state->running = false;
                return;
            }
        }
    }));
    return true;
}
std::shared_ptr<const TextureCache::Snapshot> TextureCache::snapshot() const {
    std::lock_guard lock(state_->mutex);
    return state_->published;
}
bool TextureCache::pending() const {
    std::lock_guard lock(state_->mutex);
    return state_->published->generation != state_->generation;
}
} // namespace sketchy
