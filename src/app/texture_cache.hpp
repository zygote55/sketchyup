#pragma once
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
        size_t bytes{128 * 1024 * 1024}, images{128};
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
