#include "app/viewport.hpp"
#include "core/face_textures.hpp"

namespace sketchy {
void Viewport::syncTextures() {
    AssetRecords assets;
    for (const auto &[id, body] : doc_.bodies()) {
        if (opacity_.contains(id) && opacity_.at(id) == 0)
            continue;
        for (const auto &[face, record] : body->surface.faces) {
            if (!visible({id, SelectionKind::Face, face}))
                continue;
            const auto sides = faceMaterials(*body, face);
            for (auto material : {sides.front, sides.back})
                if (material)
                    if (const auto asset = doc_.materials().at(material)->asset)
                        assets.emplace(asset, doc_.assets().at(asset));
        }
    }
    textureCache_.request(assets);
    textureSnapshot_ = textureCache_.snapshot();
    std::map<Id, std::shared_ptr<const TextureImage>> images;
    for (const auto &[id, asset] : assets)
        if (const auto *entry = TextureCache::find(*textureSnapshot_, asset); entry && entry->image)
            images.emplace(id, entry->image);
    // Release obsolete GPU storage before allocating replacements. Published CPU
    // images already obey the same 128 MiB / 128 image budget.
    std::erase_if(textureGpu_, [&](const auto &item) {
        const auto [id, name] = item;
        if (images.contains(id) && textureImages_.contains(id) &&
            images.at(id) == textureImages_.at(id))
            return false;
        gl_->glDeleteTextures(1, &name);
        return true;
    });
    gl_->glActiveTexture(GL_TEXTURE0);
    gl_->glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    for (auto it = images.begin(); it != images.end();) {
        const auto &[id, image] = *it;
        if (!textureGpu_.contains(id)) {
            GLuint name{};
            gl_->glGenTextures(1, &name);
            gl_->glBindTexture(GL_TEXTURE_2D, name);
            gl_->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
            gl_->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
            gl_->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
            gl_->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
            gl_->glTexImage2D(GL_TEXTURE_2D, 0, GL_SRGB8_ALPHA8, image->width(), image->height(), 0,
                         GL_RGBA, GL_UNSIGNED_BYTE, image->rgba().data());
            if (!name || gl_->glGetError() != GL_NO_ERROR) {
                gl_->glDeleteTextures(1, &name);
                it = images.erase(it);
                continue;
            }
            textureGpu_.emplace(id, name);
        }
        ++it;
    }
    gl_->glBindTexture(GL_TEXTURE_2D, 0);
    gl_->glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
    textureAssets_ = std::move(assets);
    textureImages_ = std::move(images);
    textureFallbacks_ = textureAssets_.size() - textureImages_.size();
}
Viewport::TextureProjection Viewport::textureProjection(const Body &body, const Triangle &local,
                                                        bool back) const {
    const auto sides = faceMaterials(body, local.face);
    const auto material = back ? sides.back : sides.front;
    if (!material)
        return {};
    const auto asset = doc_.materials().at(material)->asset;
    if (!asset || !textureImages_.contains(asset) || !textureAssets_.contains(asset) ||
        !TextureCache::sameImage(textureAssets_.at(asset), doc_.assets().at(asset)))
        return {};
    const auto &image = *textureImages_.at(asset);
    try {
        const auto mapping = effectiveFaceTextureMapping(body, local.face, back);
        const auto uv =
            floatTextureCoordinates({mapping.coordinates(local.a), mapping.coordinates(local.b),
                                     mapping.coordinates(local.c)},
                                    {1. / (64 * image.width()), 1. / (64 * image.height())});
        return {asset, uv};
    } catch (const std::exception &) {
        // An extreme projection must not disable the viewport. Picking uses
        // this same precision guard and therefore the same swatch fallback.
        return {};
    }
}
void Viewport::textureVertices(const Body &body, const Triangle &local, bool reflected,
                               std::array<Vertex, 3> &vertices) const {
    const auto front = textureProjection(body, local, reflected);
    const auto back = textureProjection(body, local, !reflected);
    for (size_t i = 0; i < vertices.size(); ++i) {
        auto &v = vertices[i];
        v.image = front.image;
        v.backImage = back.image;
        v.u = front.uv[i][0];
        v.v = front.uv[i][1];
        v.bu = back.uv[i][0];
        v.bv = back.uv[i][1];
    }
}
QString Viewport::textureSummary() const {
    if (textureCache_.pending())
        return "Loading material images…";
    if (textureFallbacks_ || textureMappingFallbacks_)
        return textureMappingFallbacks_ ? "Some textures use a color preview: mapping too large"
                                        : "Some material images are unavailable in this preview";
    return {};
}
} // namespace sketchy
