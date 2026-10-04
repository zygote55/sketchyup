#include "core/assets.hpp"
namespace sketchy {
Id createAsset(Document &doc, std::string name, std::string mediaType, AssetPayloadPtr payload) {
    const auto id = doc.nextAssetId();
    auto record = std::make_shared<AssetRecord>(
        AssetRecord{id, std::move(name), std::move(mediaType), std::move(payload)});
    Edit edit{"Create managed asset", {}};
    edit.assets.push_back({id, nullptr, record});
    doc.apply(std::move(edit), doc.revision());
    return id;
}
void replaceAsset(Document &doc, Id id, AssetPayloadPtr payload,
                  std::optional<std::string> mediaType) {
    const auto old = doc.assets().at(id);
    auto record = std::make_shared<AssetRecord>(*old);
    record->payload = std::move(payload);
    if (mediaType)
        record->mediaType = *mediaType;
    if (*record == *old)
        return;
    Edit edit{record->payload ? "Resolve or replace asset" : "Mark asset missing", {}};
    edit.assets.push_back({id, old, record});
    doc.apply(std::move(edit), doc.revision());
}
void eraseAsset(Document &doc, Id id) {
    Edit edit{"Delete unused asset", {}};
    edit.assets.push_back({id, doc.assets().at(id), nullptr});
    doc.apply(std::move(edit), doc.revision());
}
} // namespace sketchy
