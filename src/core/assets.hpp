#pragma once
#include "core/model.hpp"
namespace sketchy {
Id createAsset(Document &doc, std::string name, std::string mediaType,
               AssetPayloadPtr payload = {});
void replaceAsset(Document &doc, Id asset, AssetPayloadPtr payload,
                  std::optional<std::string> mediaType = {});
void eraseAsset(Document &doc, Id asset);
} // namespace sketchy
