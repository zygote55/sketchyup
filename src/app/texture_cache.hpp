#pragma once
#include "core/document_limits.hpp"
#include "io/texture_image.hpp"
#include <functional>
#include <mutex>

namespace sketchy {
// Decoding owns immutable asset bytes and never touches a widget or GL context.
// A changed request supersedes pending work; destruction never waits for a codec.
//
// Admission (R118). Every requested image is shown when it decodes. Each stored size is read from
// the header first. If the images at full size exceed the decoded budget, the largest is halved in
// width and height (a quarter of its bytes) until the total fits: largest first, ties by asset ID,
// and never below Limits::floorSide on the long side. Only when every image has reached the floor
// and the total still does not fit are the remaining images, in asset-ID order, left without
// pixels (status "budget").
class TextureCache {
  public:
    struct Entry {
        AssetPtr asset;
        std::shared_ptr<const TextureImage> image;
        std::string status;
        // The stored size, and whether `image` is a reduced copy of it (R118).
        int storedWidth{}, storedHeight{};
        bool reduced{};
    };
    struct Snapshot {
        std::map<Id, Entry> entries;
        size_t bytes{};
        std::uint64_t generation{};
    };
    struct Limits {
        // Decoded bytes and images held at once, and the smallest long side a reduced image may have.
        // R118 (2026-11-08): 1 GiB decoded and 1,024 images, the most assets a model holds
        // (validateAssetRecords). The viewport uploads no mipmaps (ADR 0065), so GPU storage equals
        // the decoded bytes. Scaled only in benchmark builds (R114), as before.
        size_t bytes{1024ull * 1024 * 1024 * DocumentLimits::scale * DocumentLimits::scale},
            images{1024 * DocumentLimits::scale};
        int floorSide{256};
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
