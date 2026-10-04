#pragma once
#include "core/material_records.hpp"
namespace sketchy {
// Construction copies input bytes; neither retained input aliases nor callers
// holding a mutable shared_ptr can change a published payload.
class AssetPayload {
  public:
    static constexpr size_t limit = 16 * 1024 * 1024;
    explicit AssetPayload(const std::vector<std::uint8_t> &bytes);
    const std::vector<std::uint8_t> &bytes() const { return bytes_; }

  private:
    const std::vector<std::uint8_t> bytes_;
};
using AssetPayloadPtr = std::shared_ptr<const AssetPayload>;
using AssetPayloads = std::map<Id, AssetPayloadPtr>;
struct AssetRecord {
    Id id{};
    std::string name, mediaType;
    AssetPayloadPtr payload; // Null explicitly means missing, never an external path.
    bool operator==(const AssetRecord &) const = default;
};
using AssetPtr = std::shared_ptr<const AssetRecord>;
using AssetRecords = std::map<Id, AssetPtr>;
inline constexpr size_t assetTotalLimit = 64 * 1024 * 1024;
void validateAssetRecords(const AssetRecords &assets, Id next);
void validateMaterialAssets(const MaterialRecords &materials, const AssetRecords &assets);
size_t assetBytes(const AssetPtr &asset);
} // namespace sketchy
