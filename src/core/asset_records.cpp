#include "core/asset_records.hpp"
namespace sketchy {
namespace {
const std::vector<std::uint8_t> &bounded(const std::vector<std::uint8_t> &bytes) {
    if (bytes.empty() || bytes.size() > AssetPayload::limit)
        throw std::runtime_error("Asset payload must contain 1 byte to 16 MiB");
    return bytes;
}
} // namespace
AssetPayload::AssetPayload(const std::vector<std::uint8_t> &bytes) : bytes_(bounded(bytes)) {}
void validateAssetRecords(const AssetRecords &assets, Id next) {
    if (!next || assets.size() > 1024)
        throw std::runtime_error("Invalid asset allocator or count");
    size_t total = 0;
    for (const auto &[id, asset] : assets) {
        if (!asset || !id || id >= next || asset->id != id || asset->name.empty() ||
            asset->name.size() > 1024 || asset->name.find('\0') != std::string::npos ||
            asset->mediaType.empty() || asset->mediaType.size() > 128)
            throw std::runtime_error("Invalid asset identity or metadata");
        const auto slash = asset->mediaType.find('/');
        if (!slash || slash == std::string::npos || slash + 1 == asset->mediaType.size() ||
            asset->mediaType.find('/', slash + 1) != std::string::npos)
            throw std::runtime_error("Invalid asset media type");
        for (char c : asset->mediaType)
            if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '/' || c == '.' ||
                  c == '+' || c == '-'))
                throw std::runtime_error("Invalid asset media type");
        if (asset->payload)
            total += bounded(asset->payload->bytes()).size();
    }
    if (total > assetTotalLimit)
        throw std::runtime_error("Assets exceed the 64 MiB document budget");
}
void validateMaterialAssets(const MaterialRecords &materials, const AssetRecords &assets) {
    for (const auto &[id, material] : materials)
        if (material->asset && !assets.contains(material->asset))
            throw std::runtime_error(
                "Material references an unknown asset; retain an explicit missing record");
}
size_t assetBytes(const AssetPtr &asset) {
    return asset ? sizeof(AssetRecord) + asset->name.size() + asset->mediaType.size() + 64 +
                       (asset->payload ? asset->payload->bytes().size() : 0)
                 : 0;
}
} // namespace sketchy
