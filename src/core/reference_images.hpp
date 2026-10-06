#pragma once
#include "core/model.hpp"
namespace sketchy {
Id createReferenceImage(Document &doc, ReferenceImage image, Transform placement = {},
                        Id parent = 0, std::string name = "Reference image");
ChangeReport setReferenceImage(Document &doc, Id body, ReferenceImage image);
ChangeReport calibrateReferenceImage(Document &doc, Id body, ImagePoint first, ImagePoint second,
                                     double knownLength);
void validateReferenceImageAssets(const std::map<Id, BodyPtr> &bodies, const AssetRecords &assets);
} // namespace sketchy
