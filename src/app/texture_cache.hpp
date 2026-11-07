#pragma once
#include "core/document_limits.hpp"
#include "io/texture_image.hpp"
#include <functional>
#include <mutex>

namespace sketchy {
// Decoding owns immutable asset bytes and never touches a widget or GL context.
// A changed request supersedes pending work; destruction never waits for a codec.
class TextureCache {
  public:
    struct Entry {
        AssetPtr asset;
        std::shared_ptr<const TextureImage> image;
        std::string status;
    };
    struct Snapshot {
        std::map<Id, Entry> entries;
        size_t bytes{};
        std::uint64_t generation{};
    };
    struct Limits {
        // Decoded bytes and images held at once. Scaled only in benchmark builds (R114): the
        // decoded budget is 128 MiB at every product build, and scale squared at scale 8 so that
        // a 512 MiB stored texture set (about 2.4 GB decoded) is held, as the benchmark needs.
        size_t bytes{128 * 1024 * 1024 * DocumentLimits::scale * DocumentLimits::scale},
            images{128 * DocumentLimits::scale};
    };
    using Decoder = std::function<TextureImageResult(const AssetRecord &)>;
    TextureCache();
    explicit TextureCache(Limits limits, Decoder decoder = decodeTextureImage);
    ~TextureCache();
    TextureCache(const TextureCache &) = delete;
    TextureCache &operator=(const TextureCache &) = delete;
    bool request(const AssetRecords &assets);
    std::shared_ptr<const Snapshot> snapshot() const;
    bool pending() const;
    static bool sameImage(const AssetPtr &a, const AssetPtr &b);
    static const Entry *find(const Snapshot &snapshot, const AssetPtr &asset);

  private:
    struct State;
    std::shared_ptr<State> state_;
};
} // namespace sketchy
