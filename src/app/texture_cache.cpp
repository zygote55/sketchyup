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
namespace {
// Half of a size, rounded up, and never below one pixel.
int halve(int size) { return (size + 1) / 2; }
size_t bytesOf(int width, int height) { return size_t(width) * size_t(height) * 4; }

// One requested image with a valid header, and its place in the admission plan.
struct Plan {
    Id id{};
    AssetPtr asset;
    int storedWidth{}, storedHeight{};
    int width{}, height{};
    // The image published for this asset, which is reduced instead of decoded again when it is large enough.
    std::shared_ptr<const TextureImage> previous;
    bool admitted{};
};
bool reducible(const Plan &plan, int floorSide) {
    return halve(std::max(plan.width, plan.height)) >= floorSide;
}
// Sizes every image of `plans`, which are in asset-ID order (R118).
void admit(std::vector<Plan> &plans, const TextureCache::Limits &limits) {
    size_t total{}, count{};
    for (auto &plan : plans)
        if (count < limits.images) {
            plan.admitted = true;
            ++count;
            total += bytesOf(plan.width, plan.height);
        }
    // Halve the largest reducible image until the total fits or nothing can shrink. The strict
    // comparison keeps the lowest asset ID among images of equal size.
    while (total > limits.bytes) {
        Plan *pick{};
        for (auto &plan : plans)
            if (plan.admitted && reducible(plan, limits.floorSide) &&
                (!pick || bytesOf(plan.width, plan.height) > bytesOf(pick->width, pick->height)))
                pick = &plan;
        if (!pick)
            break;
        total -= bytesOf(pick->width, pick->height);
        pick->width = halve(pick->width);
        pick->height = halve(pick->height);
        total += bytesOf(pick->width, pick->height);
    }
    // Every reducible image has reached the floor. Admit the rest in asset-ID order while they fit.
    total = 0;
    for (auto &plan : plans) {
        if (!plan.admitted)
            continue;
        const auto bytes = bytesOf(plan.width, plan.height);
        if (bytes > limits.bytes - total)
            plan.admitted = false;
        else
            total += bytes;
    }
}
} // namespace
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
        // Whether `generation` is still the newest request of an open cache.
        auto current = [&state](std::uint64_t generation) {
            std::lock_guard lock(state->mutex);
            return !state->closed && state->generation == generation;
        };
        // Builds the snapshot of one request. Returns false when it was superseded or the cache closed.
        auto build = [&](const AssetRecords &requested, const Snapshot &previous, Snapshot &next) {
            // Headers first: every size is known before anything is decoded.
            std::vector<Plan> plans;
            for (const auto &[id, asset] : requested) {
                if (!current(next.generation))
                    return false;
                const auto *old = find(previous, asset);
                // A refusal already decoded stays refused until the bytes change.
                if (old && old->status != "budget" && !old->image) {
                    next.entries.emplace(id, *old);
                    continue;
                }
                const auto probe = probeTextureImage(*asset);
                if (probe.status != TextureImageStatus::Ready) {
                    next.entries.emplace(
                        id, Entry{asset, {}, std::string(textureImageStatusName(probe.status))});
                    continue;
                }
                plans.push_back({id, asset, probe.width, probe.height, probe.width, probe.height,
                                 old ? old->image : nullptr, false});
            }
            admit(plans, state->limits);
            // Then one image at a time, so that at most one full-size decode is held beside the admitted set.
            for (auto &plan : plans) {
                if (!current(next.generation))
                    return false;
                Entry entry{plan.asset, {}, "budget", plan.storedWidth, plan.storedHeight, false};
                if (plan.admitted) {
                    try {
                        std::shared_ptr<const TextureImage> decoded;
                        if (plan.previous && plan.previous->width() >= plan.width &&
                            plan.previous->height() >= plan.height) {
                            decoded = plan.previous;
                            entry.status = "ready";
                        } else {
                            const auto result = state->decoder(*plan.asset);
                            entry.status = std::string(textureImageStatusName(result.status));
                            decoded = result.image;
                        }
                        if (decoded && (decoded->width() > plan.width ||
                                        decoded->height() > plan.height))
                            decoded = reduceTextureImage(*decoded, plan.width, plan.height);
                        if (decoded && decoded->rgba().size() > state->limits.bytes - next.bytes)
                            entry.status = "budget";
                        else if (decoded) {
                            next.bytes += decoded->rgba().size();
                            entry.image = std::move(decoded);
                        }
                    } catch (const std::exception &) {
                        entry.image.reset();
                        entry.status = "invalid";
                    }
                }
                entry.reduced = entry.image && (entry.image->width() < plan.storedWidth ||
                                                entry.image->height() < plan.storedHeight);
                next.entries.emplace(plan.id, std::move(entry));
            }
            return current(next.generation);
        };
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
            const bool complete = build(requested, *previous, *next);
            {
                std::lock_guard lock(state->mutex);
                if (state->closed) {
                    state->running = false;
                    return;
                }
                if (!complete || next->generation != state->generation)
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
