#pragma once
#include "core/model.hpp"
namespace sketchy {
TextureMappingSides faceTextureMappings(const Body &body, Id face);
// Implicit projections use a canonical face-plane basis at the body origin,
// with one metre per repeat on either side. Explicit side records take precedence.
TextureMapping effectiveFaceTextureMapping(const Body &body, Id face, bool back = false);
void setFaceTextureMappings(Body &body, Id face, const TextureMappingSides &mapping);
void validateFaceTextureMappings(const Body &body);
void reportFaceTextureMappingChanges(const Body &before, const Body &after, EntityChanges &changes);
TextureMappingSides transformTextureMappings(const TextureMappingSides &mapping,
                                             const Transform &oldToNew);
// A null mapping restores implicit mapping on the chosen side(s). Assignment is
// independent of material identity; changing a swatch retains its face placement.
ChangeReport assignTextureMapping(Document &doc, Id body, Id face,
                                  std::optional<TextureMapping> mapping, bool front = true,
                                  bool back = true);
} // namespace sketchy
